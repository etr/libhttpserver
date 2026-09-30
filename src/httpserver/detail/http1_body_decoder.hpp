/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-106: incremental HTTP/1 body framing decoder (PRD-V3N-REQ-004/
// 017/021, DR-V3-006). NOT part of the installed surface.
//
// Turns the wire bytes that follow a parsed request head into a staged
// body stream plus final trailers, per the authoritative
// http1_body_mode decided once by the engine. Wire ownership stays with
// the caller: decode() consumes the maximal prefix it can frame AND
// stage and reports the count; octets past the message boundary (and
// everything a full staging queue refuses) are never consumed and are
// re-fed by the engine. The staging queue is the backpressure
// boundary — pull() is the consumption event that releases room, so an
// engine that reads from the socket only while staged_bytes() is below
// the cap never buffers an unbounded body.
//
// Failures are typed, sticky, and carry a close posture; truncation is
// NOT a decoder failure — an incomplete body simply never completes,
// and message_complete()/length_remaining() let the engine map a
// premature EOF to its typed failure.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_body_decoder.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_BODY_DECODER_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_BODY_DECODER_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/detail/http1_body_mode.hpp>
#include <httpserver/detail/http1_head_parser.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/budgets.hpp>

namespace httpserver {

namespace detail {

// Body-framing budgets, projected from the hierarchical server budget
// surface: the staging queue tracks body_buffer_bytes, the trailer
// field count tracks header_fields, and the trailer byte cap is an
// engine constant.
struct http1_body_budget {
    std::size_t max_staged_bytes = 8388608;   // body_buffer_bytes default
    std::size_t max_trailer_fields = 256;     // header_fields default
    std::size_t max_trailer_bytes = 65536;    // engine constant

    static http1_body_budget from_budget_limits(
        const server::budget_limits& limits) noexcept {
        http1_body_budget budget;
        budget.max_staged_bytes =
            limits.get(server::resource::body_buffer_bytes);
        budget.max_trailer_fields =
            limits.get(server::resource::header_fields);
        return budget;
    }
};

// Verdict of one decode() call. progressed: `consumed` wire bytes were
// framed (the caller re-feeds the unconsumed remainder); need_more:
// nothing was consumable — feed more wire; complete: the message
// boundary was reached (or had been reached before); failed: sticky
// rejection recorded.
enum class http1_body_decode : std::uint8_t {
    progressed, need_more, complete, failed,
};

struct http1_body_progress {
    http1_body_decode kind = http1_body_decode::need_more;
    std::size_t consumed = 0;
};

// Incremental framing state machine for one request body. Not
// thread-safe: the engine feeds and pulls it on the connection thread.
class http1_body_decoder {
 public:
    http1_body_decoder(const http1_body_mode& mode, http1_body_budget budget)
        : budget_(budget), kind_(mode.kind), failure_(mode.failure) {
        switch (kind_) {
            case http1_body_kind::none:
                phase_ = phase::message_end;
                break;
            case http1_body_kind::length:
                length_remaining_ = mode.content_length;
                phase_ = mode.content_length == 0 ? phase::message_end
                                                  : phase::length;
                break;
            case http1_body_kind::chunked:
                // The chunked phases land with their split matrices;
                // until then a chunked body cannot be framed.
                fail(http::outcome_code::protocol_error,
                     "chunked framing is not implemented yet");
                break;
            case http1_body_kind::rejected:
                // failure_ already carries the mode's rejection.
                close_policy_ = mode.close_policy;
                failed_ = true;
                break;
        }
    }

    // Frames a prefix of `wire` into the staging queue and returns how
    // much was consumed. Idempotent after complete/failed: complete
    // reports {complete, 0}, failure reports {failed, 0}.
    http1_body_progress decode(std::string_view wire) {
        if (failed_) return {http1_body_decode::failed, 0};
        if (phase_ == phase::message_end) {
            return {http1_body_decode::complete, 0};
        }
        std::size_t consumed = 0;
        bool blocked = false;
        while (!wire.empty()) {
            const absorbed step = absorb_phase(wire);
            consumed += step.consumed;
            wire.remove_prefix(step.consumed);
            if (step.complete) {
                return {http1_body_decode::complete, consumed};
            }
            if (step.consumed == 0) {
                blocked = step.blocked;
                break;
            }
        }
        if (failed_) return {http1_body_decode::failed, consumed};
        // A full staging queue blocks with zero consumed — progressed
        // (pull to release room), never need_more (more wire will not
        // help).
        if (blocked) return {http1_body_decode::progressed, consumed};
        return {consumed > 0 ? http1_body_decode::progressed
                             : http1_body_decode::need_more, consumed};
    }

    // THE consumption event: copies staged bytes into `into` and
    // releases staging room for exactly the bytes copied. Verdict
    // order: failed, then data (an empty destination never receives a
    // zero-byte data pull), then end (message end with the queue
    // drained), then empty.
    detail::body_pull_result pull(std::span<std::byte> into) {
        if (failed_) return {detail::body_pull::failed, 0};
        if (!into.empty() && !staging_.empty()) {
            std::vector<std::byte>& front = staging_.front();
            const std::size_t n = std::min(into.size(), front.size());
            std::copy_n(front.begin(), n, into.begin());
            staged_total_ -= n;
            if (n == front.size()) {
                staging_.pop_front();
            } else {
                front.erase(front.begin(),
                            front.begin()
                                + static_cast<std::ptrdiff_t>(n));
            }
            return {detail::body_pull::data, n};
        }
        if (staging_.empty() && phase_ == phase::message_end) {
            return {detail::body_pull::end, 0};
        }
        return {detail::body_pull::empty, 0};
    }

    // Received trailers; final and stable once a pull returned end.
    const http::fields& trailers() const noexcept { return trailers_; }

    // The sticky rejection; ok() until a decode/pull reported failed.
    const http::outcome& failure() const noexcept { return failure_; }

    // Close posture of the sticky rejection.
    http1_close_policy close_policy() const noexcept {
        return close_policy_;
    }

    http1_body_kind kind() const noexcept { return kind_; }

    bool message_complete() const noexcept {
        return !failed_ && phase_ == phase::message_end;
    }

    // Body octets still expected under the current framing expectation
    // (the fixed length, or the current chunk); 0 once complete.
    std::uint64_t length_remaining() const noexcept {
        return length_remaining_;
    }

    std::size_t staged_bytes() const noexcept { return staged_total_; }

 private:
    // Framing phases; none and length(0) construct at message_end.
    enum class phase : std::uint8_t { length, message_end };

    // Bytes one phase step consumed from the front of the wire, plus
    // whether the message boundary was reached or the staging queue is
    // full (blocked: pulling releases room, more wire will not help).
    struct absorbed {
        std::size_t consumed = 0;
        bool complete = false;
        bool blocked = false;
    };

    absorbed absorb_phase(std::string_view wire) {
        return phase_ == phase::length ? decode_length(wire) : absorbed{};
    }

    // Copies the body octets that both fit the staging budget and the
    // remaining count; the message ends exactly at the CL boundary.
    absorbed decode_length(std::string_view wire) {
        const std::size_t room = budget_.max_staged_bytes - staged_total_;
        if (room == 0) return {0, false, true};
        const std::size_t n = std::min(
            {wire.size(), room,
             static_cast<std::size_t>(length_remaining_)});
        if (n > 0) {
            stage(wire.substr(0, n));
            length_remaining_ -= n;
        }
        if (length_remaining_ == 0) {
            phase_ = phase::message_end;
            return {n, true};
        }
        return {n, false};
    }

    // Copies `chunk` into the staging queue; the caller has verified
    // it fits the budget.
    void stage(std::string_view chunk) {
        std::vector<std::byte> segment;
        segment.reserve(chunk.size());
        const auto* first = reinterpret_cast<const std::byte*>(
            chunk.data());
        segment.assign(first, first + chunk.size());
        staged_total_ += chunk.size();
        staging_.push_back(std::move(segment));
    }

    // Records a typed, sticky rejection. Returns false so callers can
    // write `return fail(...)`.
    bool fail(http::outcome_code code, std::string message) {
        failure_ = http::outcome(code,
                                 "http1_body_decoder: "
                                     + std::move(message));
        close_policy_ = code == http::outcome_code::limit_exceeded
                            ? http1_close_policy::respond_then_close
                            : http1_close_policy::close_now;
        failed_ = true;
        return false;
    }

    http1_body_budget budget_;
    http1_body_kind kind_ = http1_body_kind::none;
    http::outcome failure_;
    http1_close_policy close_policy_ = http1_close_policy::none;
    phase phase_ = phase::length;
    std::uint64_t length_remaining_ = 0;
    std::deque<std::vector<std::byte>> staging_;
    std::size_t staged_total_ = 0;
    http::fields trailers_;
    bool failed_ = false;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_BODY_DECODER_HPP_
