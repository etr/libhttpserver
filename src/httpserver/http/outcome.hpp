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

#ifndef SRC_HTTPSERVER_HTTP_OUTCOME_HPP_
#define SRC_HTTPSERVER_HTTP_OUTCOME_HPP_

#include <cstdint>
#include <string>
#include <utility>

namespace httpserver {

namespace http {

// Typed outcome codes for libhttpserver v3 operations. The taxonomy is
// entirely libhttpserver-owned (PRD-V3N-REQ-037): no backend, OS, or
// third-party numeric identifiers appear here or ever will. The list
// is extensible; values are ABI before the v3.0 freeze.
enum class outcome_code : std::uint8_t {
    ok = 0,               // operation completed successfully
    invalid_argument,     // caller passed a bad value
    invalid_state,        // e.g. terminal action already taken
    limit_exceeded,       // documented budget exceeded (bodies, fields, queues)
    protocol_error,       // peer sent something the engine must reject
    not_supported,        // protocol/feature not enabled in this build or config
    connection_closed,
    cancelled,            // cancellation completed the operation
    timeout,
    would_deadlock,       // e.g. drain-ticket wait from counted work (DR-V3-008)
    peer_refused,         // the peer policy refused admission (TASK-119)
};

// Operation result for API surface that can fail. Default-constructed
// to ok; error outcomes carry a human-readable diagnostic. The generic
// result<T> template that transports an outcome lands with the task
// plumbing; outcome is the error vocabulary it will carry.
class outcome {
 public:
    constexpr outcome() noexcept = default;

    outcome(outcome_code code, std::string message) noexcept
        : code_(code), message_(std::move(message)) { }

    constexpr bool ok() const noexcept {
        return code_ == outcome_code::ok;
    }

    constexpr outcome_code code() const noexcept {
        return code_;
    }

    // Diagnostic text; empty for ok outcomes.
    const std::string& message() const noexcept {
        return message_;
    }

    // Shared ok instance for `return outcome::okay();` at call sites
    // that cannot fail on the path in question.
    static const outcome& okay() noexcept {
        static const outcome ok_instance;
        return ok_instance;
    }

 private:
    outcome_code code_ = outcome_code::ok;
    std::string message_;
};

}  // namespace http

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_HTTP_OUTCOME_HPP_
