/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <utility>
#include <string>
#include <vector>
#include <httpserver/detail/http2_body_stream.hpp>
#include <httpserver/detail/http2_request_head.hpp>
namespace httpserver::detail {
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
    receive_length_ = length; receive_ended_ = ended;
    if (ended) return !length || *length == 0;
    return received_.prepare(budget_, server::resource::body_buffer_bytes, capacity);
}
bool http2_body_stream::admit(std::size_t capacity) {
    policy_capacity_ = std::min(capacity, received_.bytes.size()); admitted_ = true;
    if (received_.size > policy_capacity_ || (!receive_ended_ && !policy_capacity_)) {
        fail(http::outcome_code::limit_exceeded); return false;
    }
    return true;
}
bool http2_body_stream::receive(std::span<const std::uint8_t> bytes, bool end) {
    if (receive_ended_ || failed() || bytes.size() > receive_capacity() - received_.size) return false;
    if (receive_length_ && bytes.size() > *receive_length_ - received_total_) return false;
    received_.append(std::as_bytes(bytes)); received_total_ += bytes.size();
    if (end && !end_receive()) return false;
    wake_reader(); return true;
}
bool http2_body_stream::end_receive(http::fields trailers) {
    if (receive_ended_ || (receive_length_ && *receive_length_ != received_total_)) return false;
    received_trailers_ = std::move(trailers); receive_ended_ = true; wake_reader(); return true;
}
body_pull_result http2_body_stream::pull(std::span<std::byte> into) {
    if (failed()) return {body_pull::failed, 0};
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
    send_length_ = length; forbidden_ = forbidden;
    if (!sent_.prepare(budget_, server::resource::response_queue_bytes, capacity)) {
        fail(http::outcome_code::limit_exceeded); return false;
    }
    return true;
}
body_push_result http2_body_stream::push(std::span<const std::byte> from) {
    if (failed()) return {body_push::failed, 0};
    if (finished_ || forbidden_ || (send_length_ && from.size() > *send_length_ - sent_total_)) {
        fail(); return {body_push::failed, 0};
    }
    const auto n = std::min(from.size(), sent_.bytes.size() - sent_.size);
    if (!n) return {body_push::full, 0};
    sent_.append(from.first(n)); sent_total_ += n;
    return {body_push::accepted, n};
}
body_push_result http2_body_stream::push_end(const http::fields& trailers) {
    if (failed()) return {body_push::failed, 0};
    if (finished_ || (send_length_ && !forbidden_ && *send_length_ != sent_total_)) {
        fail(); return {body_push::failed, 0};
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
        fail(http::outcome_code::limit_exceeded); return false;
    }
    if (*size && !budget_.reserve(server::resource::response_queue_bytes, 4 * *size + 128, trailers_charge_).ok()) {
        fail(http::outcome_code::limit_exceeded); return false;
    }
    for (auto field : trailers.entries()) {
        std::string name(field.name);
        for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (!http2_trailer_field(name, field.value)) {
            fail();
            return false;
        }
        sent_trailers_.append(name, field.value);
    }
    return true;
}
std::vector<std::uint8_t> http2_body_stream::send_prefix(std::size_t n) const {
    std::vector<std::uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = std::to_integer<std::uint8_t>(sent_.bytes[(sent_.head + i) % sent_.bytes.size()]);
    return out;
}
void http2_body_stream::retire(std::size_t n) {
    if (n) sent_.consume(n);
    wake_writer();
}
void http2_body_stream::fail(http::outcome_code code) {
    if (failed()) return;
    failure_ = {code, "HTTP/2 body stream failed"}; wake_reader(); wake_writer();
}
void http2_body_stream::wake_reader() {
    if (!reader_) return;
    if (!failed() && !received_.size && !receive_ended_) return;
    auto* wait = std::exchange(reader_, nullptr);
    wait->complete(failed() ? body_wake::failed : body_wake::data);
}
void http2_body_stream::wake_writer() {
    if (!writer_) return;
    if (!failed() && sent_.size == sent_.bytes.size()) return;
    auto* wait = std::exchange(writer_, nullptr);
    wait->complete(failed() ? body_room::failed : body_room::freed);
}
void http2_body_stream::park(body_wait& wait) { reader_ = &wait; wake_reader(); }
void http2_body_stream::unpark(body_wait& wait) { if (reader_ == &wait) reader_ = nullptr; }
void http2_body_stream::park(body_write_wait& wait) { writer_ = &wait; wake_writer(); }
void http2_body_stream::unpark(body_write_wait& wait) { if (writer_ == &wait) writer_ = nullptr; }
}  // namespace httpserver::detail
