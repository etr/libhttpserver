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

// Immutable reusable response definitions and per-send overlays
// (architecture §3.2, DR-V3-005, PRD-V3N-REQ-026/028/029/030).
//
// A response_definition is a replayable response value: one status,
// one base field set, and one body source, frozen at factory time and
// never mutated afterwards. Any number of sends — sequential or
// concurrent — share the same immutable state; every send allocates
// its own cursor (serialization offset, file handle, producer) inside
// its own coroutine frame, so sends cannot interfere (DR-V3-005:
// "each send owns serialization state, body cursor, cancellation,
// overlay").
//
// Body sources (REQ-026):
//   owned_bytes  — the library holds a deep copy of the body;
//   reopen_file  — the library stores a path and opens one fresh
//                  handle per send;
//   factory      — the application supplies a callable returning one
//                  fresh producer per send.
//
// One send = send_definition(exchange, definition, overlay): it
// validates the overlay and the merged framing BEFORE committing the
// head (a conflicting or malformed input fails typed with the exchange
// untouched), merges the overlay's headers after the definition's base
// fields in order, streams the body through exchange::writer() with
// backpressure, and finishes with the overlay's trailers. The send
// never mutates the definition or the overlay; both are borrowed for
// the duration of the task only.
//
// Ownership / lifetime matrix (REQ-028):
//
//   Source          | Library owns              | Application guarantees
//   ----------------+---------------------------+--------------------------------
//   owned_bytes     | deep copy of the bytes    | nothing after the factory
//                   | (moved in at factory time)| returns
//   reopen_file     | the path string; one      | the path resolves at each
//                   | fresh handle per send     | send's prepare; a file that
//                   |                           | shrinks below the pinned
//                   |                           | size fails that send typed
//   factory         | the callable (move-only)  | the callable is safe to
//                   |                           | invoke concurrently (once
//                   |                           | per in-flight send); each
//                   |                           | producer serves one send;
//                   |                           | producer data spans stay
//                   |                           | valid until the next pull
//   response_overlay| its own field sets (deep  | borrowed for the duration
//                   | value type)               | of one send task only
//
// TASK-113 extends source_kind with owned_file / owned_pipe transfers
// and borrowed leases: one-shot sources will fail a second send before
// writing anything, and borrowed memory will require a lease spanning
// send completion. The cursor's kind dispatch is the extension point.

#ifndef SRC_HTTPSERVER_RESPONSE_DEFINITION_HPP_
#define SRC_HTTPSERVER_RESPONSE_DEFINITION_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/status.hpp>

namespace httpserver {

namespace detail {

// True iff any byte of v is a control character forbidden in a field
// value: every CTL (0x00-0x1F, 0x7F) except HTAB, which is legal
// interior OWS. Mirrors the engine-side rule
// (detail/http1_head_parser.hpp contains_field_value_ctl), re-expressed
// here because a public header may not include a private one.
inline bool field_value_has_ctl(std::string_view v) noexcept {
    for (const char c : v) {
        const auto u = static_cast<unsigned char>(c);
        if ((u < 0x20 && u != '\t') || u == 0x7F) return true;
    }
    return false;
}

// Wire-safety of one field set: every name a token (RFC 9110 tchar),
// every value free of forbidden control bytes. Shared by the
// definition factories (base fields) and the send path (overlay
// fields).
inline http::outcome check_fields_wire_safe(const http::fields& f) {
    for (const http::fields::entry e : f.entries()) {
        if (!http::detail::is_token(e.name)) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response fields: name '" + std::string(e.name)
                    + "' is not a token");
        }
        if (field_value_has_ctl(e.value)) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response fields: value of '" + std::string(e.name)
                    + "' carries a control byte");
        }
    }
    return http::outcome::okay();
}

// Framing self-consistency of one field set: at most one
// Content-Length occurrence, and never a Transfer-Encoding beside a
// Content-Length (the same conflicts the engine's response mode
// rejects).
inline http::outcome check_fields_framing(const http::fields& f) {
    if (f.count("content-length") > 1) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "response fields: repeated Content-Length");
    }
    if (f.count("content-length") > 0
            && f.count("transfer-encoding") > 0) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "response fields: Transfer-Encoding with Content-Length");
    }
    return http::outcome::okay();
}

// Pins Content-Length to `size` when the field set carries neither
// framing field (an explicit Content-Length or Transfer-Encoding passes
// through verbatim; "0" is pinned like any other size).
inline void pin_content_length(http::fields& f, std::uint64_t size) {
    if (f.count("content-length") == 0
            && f.count("transfer-encoding") == 0) {
        f.append("Content-Length", std::to_string(size));
    }
}

}  // namespace detail

// One pull from a response body producer (REQ-026). Exactly one of the
// three members carries meaning:
//   - !status.ok(): the body failed; the producer is finished;
//   - data (status ok, !end): valid until the NEXT pull on that
//     producer; MUST be non-empty (an empty non-end chunk is a
//     producer contract violation; the send fails it typed);
//   - end (status ok): the body is complete; data is empty.
struct body_chunk {
    http::outcome status;
    std::span<const std::byte> data;
    bool end = false;
};

// A fresh per-send producer returned by a definition's factory. Used
// by exactly one send; not thread-safe.
using body_producer = concurrency::unique_function<body_chunk()>;

// Creates one new producer per send. Must be safe to invoke
// concurrently (one invocation per in-flight send).
using body_factory = concurrency::unique_function<body_producer()>;

// A replayable immutable response value (DR-V3-005). Constructed once
// through the validating factories; afterwards no API mutates it —
// copies share the frozen state (a shared immutable block), moves hand
// it over. Sends read it concurrently and never write it.
class response_definition {
 public:
    enum class source_kind : std::uint8_t {
        owned_bytes,
        reopen_file,
        factory,
    };

    // Body held by value; Content-Length pinned to the body size when
    // the fields carry neither framing field (including "0").
    static http::outcome owned_bytes(const http::status& s, http::fields f,
                                     std::vector<std::byte> body,
                                     response_definition& out) {
        impl state;
        state.status = s;
        detail::pin_content_length(f, body.size());
        state.fields = std::move(f);
        state.kind = source_kind::owned_bytes;
        state.bytes = std::move(body);
        return commit(std::move(state), out);
    }

    // Body re-read from `path` on every send: nothing is opened at
    // construction, so a definition outlives any one file's lifetime;
    // each send observes the file at its own prepare and pins
    // Content-Length per send.
    static http::outcome reopen_file(const http::status& s, http::fields f,
                                     std::string path,
                                     response_definition& out) {
        impl state;
        state.status = s;
        state.fields = std::move(f);
        state.kind = source_kind::reopen_file;
        state.path = std::move(path);
        return commit(std::move(state), out);
    }

    // Body produced per send by a fresh producer from `make`.
    static http::outcome factory(const http::status& s, http::fields f,
                                 body_factory make,
                                 response_definition& out) {
        impl state;
        state.status = s;
        state.fields = std::move(f);
        state.kind = source_kind::factory;
        state.make = std::move(make);
        return commit(std::move(state), out);
    }

    // The empty, not-yet-valid definition; valid() reports false.
    response_definition() noexcept = default;
    response_definition(const response_definition&) noexcept = default;
    response_definition& operator=(const response_definition&) noexcept
        = default;
    response_definition(response_definition&&) noexcept = default;
    response_definition& operator=(response_definition&&) noexcept
        = default;

    // Meaningful only when valid().
    source_kind kind() const noexcept {
        return impl_ ? impl_->kind : source_kind::owned_bytes;
    }

    bool valid() const noexcept { return impl_ != nullptr; }

    // The frozen status and base fields. For an invalid definition
    // these return shared empty instances. The fields never include
    // overlay content: a send merges its overlay into a local copy.
    const http::status& status() const noexcept {
        static const http::status empty;
        return impl_ ? impl_->status : empty;
    }

    const http::fields& fields() const noexcept {
        static const http::fields empty;
        return impl_ ? impl_->fields : empty;
    }

 private:
    // The frozen state. Built locally by a factory, validated in
    // full, and only then shared immutably (`shared_ptr<const impl>`
    // makes the immutability structural: no API can reach a mutable
    // reference).
    struct impl {
        http::status status;
        http::fields fields;
        source_kind kind = source_kind::owned_bytes;
        std::vector<std::byte> bytes;   // owned_bytes
        std::string path;               // reopen_file
        body_factory make;              // factory
    };

    // Full validation of the assembled state; nothing is mutated on
    // failure (the caller never touches `out` before this returns ok).
    static http::outcome validate(const impl& state) {
        if (!state.status.valid()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response_definition: status code is invalid");
        }
        const http::outcome wire =
            detail::check_fields_wire_safe(state.fields);
        if (!wire.ok()) return wire;
        const http::outcome framing =
            detail::check_fields_framing(state.fields);
        if (!framing.ok()) return framing;
        if (state.kind == source_kind::reopen_file && state.path.empty()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response_definition: reopen_file requires a path");
        }
        if (state.kind == source_kind::factory && !state.make) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response_definition: factory requires a callable");
        }
        return http::outcome::okay();
    }

    // Validates `state` and, only on success, freezes it into `out`.
    static http::outcome commit(impl&& state, response_definition& out) {
        const http::outcome checked = validate(state);
        if (!checked.ok()) return checked;
        out.impl_ = std::make_shared<const impl>(std::move(state));
        return http::outcome::okay();
    }

    std::shared_ptr<const impl> impl_;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_RESPONSE_DEFINITION_HPP_
