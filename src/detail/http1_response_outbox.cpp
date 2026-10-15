/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/http1_response_outbox.hpp>

namespace httpserver::detail {

auto http1_response_sink::fail_locked(const http::outcome& reason) -> http::outcome {
    failed_ = true;
    failure_ = reason;
    return reason;
}

auto http1_outbox_budget::from_budget_limits(const server::budget_limits& limits) noexcept -> http1_outbox_budget {
    http1_outbox_budget budget;
    budget.max_queue_bytes = limits.get(server::resource::response_queue_bytes);
    budget.max_head_bytes = limits.get(server::resource::header_bytes);
    return budget;
}

http1_response_outbox::http1_response_outbox(http1_outbox_budget budget, server::resource_budget scope) : budget_(budget), storage_charge_(scope, server::resource::response_queue_bytes) {}

auto http1_response_outbox::open(std::uint64_t sequence, stop_token cancel) -> http1_response_sink& {
    std::lock_guard<std::mutex> lock(mu_);
    // The sink constructor is private to the outbox (engine-only
    // seam), so the unique_ptr adopts a direct new here.
    slots_.emplace_back(std::unique_ptr<http1_response_sink>(new http1_response_sink(*this, std::move(cancel))));
    http1_response_sink& sink = *slots_.back();
    sink.sequence_ = sequence;
    if (abandoned_) {
        static_cast<void>(sink.fail_locked(http::outcome(http::outcome_code::connection_closed, "http1_response_outbox: connection abandoned")));
    }
    return sink;
}

auto http1_response_outbox::copy_front(std::span<std::byte> dest) const -> std::size_t {
    std::lock_guard<std::mutex> lock(mu_);
    if (slots_.empty()) return 0;
    const http1_response_sink& front = *slots_.front();
    const std::size_t available = front.bytes_.size() - front.offset_;
    const std::size_t n = std::min(dest.size(), available);
    if (n > 0) {
        std::memcpy(dest.data(), front.bytes_.data() + front.offset_, n);
    }
    return n;
}

auto http1_response_outbox::consume_front(std::size_t n) -> std::size_t {
    std::vector<body_write_wait*> woken;
    std::size_t consumed = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (slots_.empty()) return 0;
        http1_response_sink& front = *slots_.front();
        consumed = std::min(n, front.bytes_.size() - front.offset_);
        front.offset_ += consumed;
        queued_bytes_ -= consumed;
        if (front.offset_ == front.bytes_.size()) {
            release_buffer_locked(front);
            front.offset_ = 0;
        }
        if (front.ended_ && front.offset_ == front.bytes_.size()) {
            // Popped even for a zero consume: the call doubles as
            // the "advance past the completed front" step.
            slots_.pop_front();
        }
        if (consumed > 0) take_all_waiters_locked(woken);
    }
    wake_all(woken, body_room::freed);
    return consumed;
}

auto http1_response_outbox::front_complete() const -> bool {
    std::lock_guard<std::mutex> lock(mu_);
    if (slots_.empty()) return false;
    const http1_response_sink& front = *slots_.front();
    return front.ended_ && front.offset_ == front.bytes_.size();
}

auto http1_response_outbox::front_failed() const -> bool {
    std::lock_guard<std::mutex> lock(mu_);
    if (slots_.empty()) return false;
    return slots_.front()->failed_;
}

auto http1_response_outbox::empty() const -> bool {
    std::lock_guard<std::mutex> lock(mu_);
    return slots_.empty();
}

auto http1_response_outbox::queued_bytes() const -> std::size_t {
    std::lock_guard<std::mutex> lock(mu_);
    return queued_bytes_;
}

auto http1_response_outbox::retire() -> void {
    std::lock_guard<std::mutex> lock(mu_);
    slots_.clear();
    queued_bytes_ = 0;
    storage_charge_.shrink(storage_charge_.units());
    abandoned_ = true;
}

auto http1_response_outbox::abandon() -> void {
    std::vector<body_write_wait*> woken;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (abandoned_) return;
        abandoned_ = true;
        for (const std::unique_ptr<http1_response_sink>& slot : slots_) {
            static_cast<void>(slot->fail_locked(http::outcome(http::outcome_code::connection_closed, "http1_response_outbox: connection abandoned")));
            if (slot->waiter_ != nullptr) {
                woken.push_back(slot->waiter_);
                slot->waiter_ = nullptr;
            }
        }
    }
    wake_all(woken, body_room::failed);
}

auto http1_response_outbox::field_bound(const http::fields& fields, std::size_t fixed, std::size_t& bound) const -> bool {
    std::size_t bytes = 0;
    for (const auto& field : fields.entries()) {
        if (field.name.size() > budget_.max_head_bytes || field.value.size() > budget_.max_head_bytes ||
            field.name.size() + field.value.size() + 4 > budget_.max_head_bytes - std::min(bytes, budget_.max_head_bytes))
            return false;
        bytes += field.name.size() + field.value.size() + 4;
    }
    bound = bytes + fixed;
    return true;
}

auto http1_response_outbox::reserve_buffer_locked(http1_response_sink& sink, std::size_t size) -> bool {
    if (size == 0) return true;
    if (size <= sink.bytes_.capacity()) {
        if (sink.storage_units_ != 0) return true;
        if (!storage_charge_.grow(sink.bytes_.capacity())) return false;
        sink.storage_units_ = sink.bytes_.capacity();
        return true;
    }
    const auto bound = std::max(size, 2 * sink.bytes_.capacity());
    if (!storage_charge_.grow(bound)) return false;
    std::string replacement;
    replacement.reserve(bound);
    if (replacement.capacity() > bound) {
        std::string().swap(replacement);
        storage_charge_.shrink(bound);
        return false;
    }
    replacement.append(sink.bytes_);
    sink.bytes_.swap(replacement);
    std::string().swap(replacement);
    storage_charge_.shrink(std::exchange(sink.storage_units_, bound));
    return true;
}

auto http1_response_outbox::release_buffer_locked(http1_response_sink& sink) -> void {
    std::string().swap(sink.bytes_);
    storage_charge_.shrink(std::exchange(sink.storage_units_, 0));
}

auto http1_response_outbox::push_into(http1_response_sink& sink, std::span<const std::byte> from) -> body_push_result {
    std::lock_guard<std::mutex> lock(mu_);
    if (sink.failed_) return failed_result();
    if (sink.framer_ == nullptr) {
        static_cast<void>(sink.fail_locked(http::outcome(http::outcome_code::invalid_state,
                                                         "http1_response_sink: body push before the response"
                                                         " head")));
        return failed_result();
    }
    // Floored: the end marker may have pushed the queue past the
    // budget (documented overshoot), and a plain subtraction would
    // wrap into a phantom huge room.
    const std::size_t room = budget_.max_queue_bytes - std::min(queued_bytes_, budget_.max_queue_bytes);
    if (room == 0) return body_push_result{body_push::full, 0};
    if (sink.offset_) {
        // Reuse the retained allocation. Compaction changes logical
        // queue room but cannot return its shared allocation charge.
        sink.bytes_.erase(0, sink.offset_);
        sink.offset_ = 0;
    }
    if (!reserve_buffer_locked(sink, sink.bytes_.size() + std::min(room, from.size() + 24))) {
        static_cast<void>(sink.fail_locked({http::outcome_code::limit_exceeded, "shared response storage exhausted"}));
        return failed_result();
    }
    const std::size_t before = sink.bytes_.size();
    const http::outcome framed = sink.framer_->push_body(sink.bytes_, from, room);
    const std::size_t copied = sink.bytes_.size() - before;
    if (!framed.ok()) {
        static_cast<void>(sink.fail_locked(framed));
        return body_push_result{body_push::failed, 0};
    }
    if (copied == 0) return body_push_result{body_push::full, 0};
    queued_bytes_ += copied;
    return body_push_result{body_push::accepted, copied};
}

auto http1_response_outbox::end_slot(http1_response_sink& sink, const http::fields& trailers) -> body_push_result {
    std::lock_guard<std::mutex> lock(mu_);
    if (sink.failed_) return failed_result();
    if (sink.framer_ == nullptr) {
        static_cast<void>(sink.fail_locked(http::outcome(http::outcome_code::invalid_state,
                                                         "http1_response_sink: body end before the response"
                                                         " head")));
        return failed_result();
    }
    const std::size_t room = budget_.max_queue_bytes - std::min(queued_bytes_, budget_.max_queue_bytes);
    if (room == 0) return body_push_result{body_push::full, 0};
    std::size_t bound = 0;
    if (!field_bound(trailers, 5, bound) || !reserve_buffer_locked(sink, sink.bytes_.size() + bound)) {
        static_cast<void>(sink.fail_locked({http::outcome_code::limit_exceeded, "shared response trailers exhausted"}));
        return failed_result();
    }
    const std::size_t before = sink.bytes_.size();
    const http::outcome framed = sink.framer_->finish_body(sink.bytes_, trailers);
    if (!framed.ok()) {
        static_cast<void>(sink.fail_locked(framed));
        return body_push_result{body_push::failed, 0};
    }
    queued_bytes_ += sink.bytes_.size() - before;
    sink.ended_ = true;
    return body_push_result{body_push::accepted, 0};
}

auto http1_response_outbox::park_sink(http1_response_sink& sink, body_write_wait& wait) -> void {
    std::lock_guard<std::mutex> lock(mu_);
    if (sink.failed_) {
        wait.complete(body_room::failed);
        return;
    }
    if (queued_bytes_ < budget_.max_queue_bytes) {
        wait.complete(body_room::freed);
        return;
    }
    if (sink.cancel_.stop_requested()) {
        wait.complete(body_room::cancelled);
        return;
    }
    sink.waiter_ = &wait;
}

auto http1_response_outbox::unpark_sink(http1_response_sink& sink, body_write_wait& wait) -> void {
    std::lock_guard<std::mutex> lock(mu_);
    if (sink.waiter_ == &wait) sink.waiter_ = nullptr;
}

auto http1_response_outbox::take_all_waiters_locked(std::vector<body_write_wait*>& out) -> void {
    for (const std::unique_ptr<http1_response_sink>& slot : slots_) {
        if (slot->waiter_ != nullptr) {
            out.push_back(slot->waiter_);
            slot->waiter_ = nullptr;
        }
    }
}

auto http1_response_outbox::wake_all(const std::vector<body_write_wait*>& waiters, body_room room) -> void {
    for (body_write_wait* wait : waiters) {
        wait->complete(room);
    }
}

bool valid_upgrade_fields(std::string_view accept, std::string_view protocol) {
    if (accept.size() != 28 || accept.back() != '=' || std::string_view("AEIMQUYcgkosw048").find(accept[26]) == std::string_view::npos ||
        accept.substr(0, 27).find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") != std::string_view::npos ||
        (!protocol.empty() && !http::detail::is_token(protocol))) {
        return false;
    }
    return true;
}

http::outcome http1_response_sink::commit_upgrade(std::string_view accept, std::string_view protocol) {
    if (!valid_upgrade_fields(accept, protocol)) return {http::outcome_code::invalid_argument, "invalid trusted upgrade fields"};
    constexpr std::string_view prefix = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
    constexpr std::string_view protocol_prefix = "Sec-WebSocket-Protocol: ";
    std::lock_guard<std::mutex> lock(owner_->mu_);
    if (failed_ || started_) return {http::outcome_code::invalid_state, "upgrade slot unavailable"};
    const auto available = owner_->budget_.max_queue_bytes - std::min(owner_->queued_bytes_, owner_->budget_.max_queue_bytes);
    const auto cap = std::min(available, owner_->budget_.max_head_bytes);
    const auto fixed = prefix.size() + accept.size() + 4;
    const auto protocol_head = protocol.empty() ? 0 : protocol_prefix.size() + 2;
    if (fixed + protocol_head > cap || protocol.size() > cap - fixed - protocol_head) return {http::outcome_code::limit_exceeded, "upgrade head exceeds available output capacity"};
    const auto before_bytes = bytes_.size();
    if (!owner_->reserve_buffer_locked(*this, before_bytes + fixed + protocol_head + protocol.size())) return {http::outcome_code::limit_exceeded, "shared upgrade head capacity exhausted"};
    bytes_.append(prefix);
    bytes_.append(accept);
    bytes_ += "\r\n";
    if (!protocol.empty()) {
        bytes_.append(protocol_prefix);
        bytes_.append(protocol);
        bytes_ += "\r\n";
    }
    bytes_ += "\r\n";
    owner_->queued_bytes_ += bytes_.size() - before_bytes;
    started_ = true;
    ended_ = true;
    return http::outcome::okay();
}

http::outcome http1_response_sink::start(const http::request_head& request, const http::status& s, const http::fields& f, const http1_response_framer::clock_source& clock) {
    std::lock_guard<std::mutex> lock(owner_->mu_);
    if (failed_) return failure_;
    if (started_) {
        return http::outcome(http::outcome_code::invalid_state, "http1_response_sink: response head already started");
    }
    // start() owns the final head's Date policy: rebuild the framer
    // with the configured clock even when interim() already created a
    // clock-less one. The swap is invisible — interim bytes live in
    // bytes_, not the framer, whose stage is still head, and the sink
    // never uses a status token.
    framer_ = std::make_unique<http1_response_framer>(clock);
    started_ = true;
    const std::size_t before_head = bytes_.size();
    // Keep the already queued interim buffer and its reservation intact
    // until the candidate final head is accepted. Rejection destroys the
    // candidate allocation before returning any of its shared capacity.
    std::string previous = std::move(bytes_);
    const auto previous_units = std::exchange(storage_units_, 0);
    const auto rollback = [this, &previous, previous_units](http::outcome failure) {
        owner_->release_buffer_locked(*this);
        bytes_ = std::move(previous);
        storage_units_ = previous_units;
        return fail_locked(failure);
    };
    std::size_t bound = 0;
    if (!owner_->field_bound(f, 256, bound) || !owner_->reserve_buffer_locked(*this, before_head + bound)) {
        return rollback({http::outcome_code::limit_exceeded, "shared response head capacity exhausted"});
    }
    bytes_.append(previous);
    const http::outcome head = framer_->start_head(bytes_, request, s, f);
    if (!head.ok()) return rollback(head);
    if (bytes_.size() > owner_->budget_.max_head_bytes) {
        return rollback(http::outcome(http::outcome_code::limit_exceeded,
                                      "http1_response_sink: serialized response head exceeds the"
                                      " head budget"));
    }
    std::string().swap(previous);
    owner_->storage_charge_.shrink(previous_units);
    owner_->queued_bytes_ += bytes_.size() - before_head;
    return http::outcome::okay();
}

http::outcome http1_response_sink::interim(std::uint16_t code) {
    std::lock_guard<std::mutex> lock(owner_->mu_);
    if (failed_) return failure_;
    if (framer_ == nullptr) {
        // Interims carry no Date, so the default (clock-less) framer is
        // the right serializer here.
        framer_ = std::make_unique<http1_response_framer>();
    }
    if (!owner_->reserve_buffer_locked(*this, bytes_.size() + 64)) return fail_locked({http::outcome_code::limit_exceeded, "shared interim capacity exhausted"});
    const std::size_t before_bytes = bytes_.size();
    const http::outcome appended = framer_->interim_head(bytes_, code);
    if (!appended.ok()) return appended;
    owner_->queued_bytes_ += bytes_.size() - before_bytes;
    return http::outcome::okay();
}

bool http1_response_sink::parked() const {
    std::lock_guard<std::mutex> lock(owner_->mu_);
    return waiter_ != nullptr;
}

task<io_result> http1_response_outbox::write_front(io_backend& backend, io_connection_owner& owner, std::uint64_t connection) {
    std::array<std::byte, 4096> buffer;
    io_result total;
    for (;;) {
        std::size_t n = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (slots_.empty()) co_return total;
            const http1_response_sink& front = *slots_.front();
            const std::size_t available = front.bytes_.size() - front.offset_;
            if (available == 0) co_return total;
            n = std::min(buffer.size(), available);
            std::memcpy(buffer.data(), front.bytes_.data() + front.offset_, n);
        }
        write_operation op(owner, connection, std::span<const std::byte>(buffer.data(), n));
        op.submit(backend);
        const io_result piece = co_await std::move(op);
        if (piece.code != http::outcome_code::ok) co_return piece;
        consume_front(piece.transferred);
        total.transferred += piece.transferred;
    }
}

}  // namespace httpserver::detail
