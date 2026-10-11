/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <memory>
#include <algorithm>
#include <array>
#include <utility>
#include <string>
#include <vector>
#include <httpserver/detail/http2_body_stream.hpp>
#include <httpserver/detail/http2_request_head.hpp>
namespace httpserver::detail {
const http::outcome& http2_body_stream::failure() const noexcept {
    // Each published outcome is immutable: callers may retain the API's
    // reference while the owner concurrently publishes terminal failure.
    static const auto outcomes = [] {
        std::array<http::outcome, 256> values;
        for (std::size_t i = 1; i < values.size(); ++i)
            values[i] = {static_cast<http::outcome_code>(i), "HTTP/2 body stream failed"};
        return values;
    }();
    return outcomes[static_cast<unsigned>(failure_code_.load())];
}
bool http2_body_stream::ring::prepare(server::resource_budget budget, server::resource kind, std::size_t capacity) {
    if (!capacity || !budget.reserve(kind, capacity + 128, charge).ok()) return false;
    bytes.resize(capacity);
    return true;
}
void http2_body_stream::ring::append(std::span<const std::byte> from) {
    for (auto byte : from) bytes[(head + size++) % bytes.size()] = byte;
}
void http2_body_stream::ring::consume(std::size_t n) {
    head = (head + n) % bytes.size(); size -= n;
}
bool http2_body_stream::prepare_receive(std::size_t capacity, bool ended, std::optional<std::uint64_t> length) {
    mutation lock(*this);
    receive_length_ = length; receive_ended_ = ended;
    if (ended) return !length || *length == 0;
    return received_.prepare(budget_, server::resource::body_buffer_bytes, capacity);
}
bool http2_body_stream::admit(std::size_t capacity) {
    mutation lock(*this);
    policy_capacity_ = std::min(capacity, received_.bytes.size()); admitted_ = true;
    if (received_.size > policy_capacity_ || (!receive_ended_ && !policy_capacity_)) {
        fail_locked(http::outcome_code::limit_exceeded); return false;
    }
    return true;
}
bool http2_body_stream::receive(std::span<const std::uint8_t> bytes, bool end) {
    mutation lock(*this);
    if (receive_ended_ || failed_locked() || bytes.size() > receive_capacity_locked() - received_.size) return false;
    if (receive_length_ && bytes.size() > *receive_length_ - received_total_) return false;
    received_.append(std::as_bytes(bytes)); received_total_ += bytes.size();
    if (end && !end_receive_locked()) return false;
    return true;
}
bool http2_body_stream::end_receive_locked(http::fields trailers) {
    if (receive_ended_ || (receive_length_ && *receive_length_ != received_total_)) return false;
    received_trailers_ = std::move(trailers); receive_ended_ = true;  return true;
}
std::span<const std::byte> http2_body_stream::receive_prefix() const {
    std::lock_guard lock(mutex_);
    if (!received_.size) return {};
    return std::span(received_.bytes).subspan(received_.head, std::min(received_.size, received_.bytes.size() - received_.head));
}
void http2_body_stream::consume_received(std::size_t n) {
    mutation lock(*this);
    // Reentrant cancellation can discard the borrowed prefix during feed.
    n = std::min(n, received_.size);
    if (n) {
        received_.consume(n); consumed += n;
    }
}
body_pull_result http2_body_stream::pull(std::span<std::byte> into) {
    mutation lock(*this);
    if (failed_locked()) return {body_pull::failed, 0};
    const auto n = std::min(into.size(), received_.size);
    if (n) {
        for (std::size_t i = 0; i < n; ++i) into[i] = received_.bytes[(received_.head + i) % received_.bytes.size()];
        received_.consume(n); consumed += n; return {body_pull::data, n};
    }
    if (receive_ended_) {
        eof_seen_ = true; return {body_pull::end, 0};
    }
    return {body_pull::empty, 0};
}
bool http2_body_stream::prepare_send(std::size_t capacity, std::optional<std::uint64_t> length, bool forbidden) {
    mutation lock(*this);
    send_length_ = length; forbidden_ = forbidden;
    if (!sent_.prepare(budget_, server::resource::response_queue_bytes, capacity)) {
        fail_locked(http::outcome_code::limit_exceeded); return false;
    }
    return true;
}
body_push_result http2_body_stream::push(std::span<const std::byte> from) {
    mutation lock(*this);
    if (failed_locked()) return {body_push::failed, 0};
    if (finished_ || forbidden_ || (send_length_ && from.size() > *send_length_ - sent_total_)) {
        fail_locked(); return {body_push::failed, 0};
    }
    const auto n = std::min(from.size(), sent_.bytes.size() - sent_.size);
    if (!n) return {body_push::full, 0};
    sent_.append(from.first(n)); sent_total_ += n;
    return {body_push::accepted, n};
}
body_push_result http2_body_stream::push_end(const http::fields& trailers) {
    mutation lock(*this);
    if (failed_locked()) return {body_push::failed, 0};
    if (finished_ || (send_length_ && !forbidden_ && *send_length_ != sent_total_)) {
        fail_locked(); return {body_push::failed, 0};
    }
    if (!admit_trailers(trailers)) return {body_push::failed, 0};
    finished_ = true; return {body_push::accepted, 0};
}
std::optional<std::size_t> http2_body_stream::trailer_size(const http::fields& trailers) const {
    std::size_t size = 0;
    if (trailers.size() > 256) {
        return {};
    }
    for (auto field : trailers.entries()) {
        const auto n = field.name.size() + field.value.size() + 32;
        if (n > 65536 || size > 65536 - n) {
            return {};
        }
        size += n;
    }
    return size;
}
bool http2_body_stream::admit_trailers(const http::fields& trailers) {
    const auto size = trailer_size(trailers);
    if (!size) {
        fail_locked(http::outcome_code::limit_exceeded); return false;
    }
    if (*size && !budget_.reserve(server::resource::response_queue_bytes, 4 * *size + 128, trailers_charge_).ok()) {
        fail_locked(http::outcome_code::limit_exceeded); return false;
    }
    for (auto field : trailers.entries()) {
        std::string name(field.name);
        for (char& c : name) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (!http2_trailer_field(name, field.value)) {
            fail_locked();
            return false;
        }
        sent_trailers_.append(name, field.value);
    }
    return true;
}
std::vector<std::uint8_t> http2_body_stream::send_prefix(std::size_t n) const {
    std::lock_guard lock(mutex_);
    std::vector<std::uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = std::to_integer<std::uint8_t>(sent_.bytes[(sent_.head + i) % sent_.bytes.size()]);
    return out;
}
void http2_body_stream::retire(std::size_t n) {
    mutation lock(*this);
    if (n) sent_.consume(n);
}
void http2_body_stream::fail_locked(http::outcome_code code) {
    if (failed_locked()) return;
    failure_code_.store(code);
}
void http2_body_stream::publish() {
    std::shared_ptr<wait_node> reader;
    std::shared_ptr<write_wait_node> writer;
    bool failed;
    {
        std::lock_guard lock(mutex_);
        failed = failed_locked();
        if (failed || received_.size || receive_ended_) reader = std::exchange(reader_, {});
        if (failed || sent_.size < sent_.bytes.size()) writer = std::exchange(writer_, {});
    }
    if (reader && reader->claim()) {
        reader->result = failed ? body_wake::failed : body_wake::data;
        reader->resume_posted();
    }
    if (writer && writer->claim()) {
        writer->result = failed ? body_room::failed : body_room::freed;
        writer->resume_posted();
    }
    if (progress_) progress_();
}
void http2_body_stream::park(body_wait& wait) { mutation lock(*this); reader_ = wait.node_; }
void http2_body_stream::unpark(body_wait& wait) {
    std::lock_guard lock(mutex_);
    if (reader_ == wait.node_) reader_.reset();
}
void http2_body_stream::park(body_write_wait& wait) { mutation lock(*this); writer_ = wait.node_; }
void http2_body_stream::unpark(body_write_wait& wait) {
    std::lock_guard lock(mutex_);
    if (writer_ == wait.node_) writer_.reset();
}
bool http2_body_stream::end_receive(http::fields trailers) {
    mutation lock(*this);
    return end_receive_locked(std::move(trailers));
}
void http2_body_stream::fail(http::outcome_code code) { mutation lock(*this); fail_locked(code); }
void http2_body_stream::discard_received() {
    mutation lock(*this);
    if (received_.size) received_.consume(received_.size);
}
}  // namespace httpserver::detail
