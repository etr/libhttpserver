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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/status.hpp>

namespace httpserver {

namespace detail {

// One send's private body state; defined after response_overlay below
// (it reaches the definition's frozen block through friendship).
class send_cursor;

// The per-pull read size for reopen_file sources: bounds the send's
// own memory while keeping transport-sized reads.
inline constexpr std::size_t k_file_chunk_bytes = 16u * 1024u;

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

// Parses the field set's Content-Length into `out`; absent is ok with
// out = 0. A present but non-numeric or overflowing value fails typed
// (the framing must be a length, not a claim).
inline http::outcome parse_content_length(const http::fields& f,
                                          std::uint64_t& out) {
    out = 0;
    const std::optional<std::string_view> value =
        f.first("content-length");
    if (!value.has_value()) return http::outcome::okay();
    if (value->empty()) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "response fields: Content-Length is not a number");
    }
    for (const char c : *value) {
        if (c < '0' || c > '9') {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response fields: Content-Length is not a number");
        }
        if (out > (std::numeric_limits<std::uint64_t>::max()
                   - static_cast<std::uint64_t>(c - '0')) / 10) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response fields: Content-Length is out of range");
        }
        out = out * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return http::outcome::okay();
}

// Framing self-consistency of one field set: at most one
// Content-Length occurrence, never a Transfer-Encoding beside a
// Content-Length (the same conflicts the engine's response mode
// rejects), and a numeric Content-Length when one is present (a
// framing lie fails validation, not the wire).
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
    std::uint64_t declared = 0;
    return parse_content_length(f, declared);
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
    // Reaches the frozen block (bytes, path, factory) to snapshot one
    // send's body source; consumers never see the impl.
    friend class detail::send_cursor;

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

// Request-specific additions for ONE send (DR-V3-005, REQ-030). Owns
// its ordered header and trailer occurrences as a plain deep value;
// the definition it decorates is never mutated. Headers append AFTER
// the definition's base fields, in entries() order (repeated names
// included); trailers ride the final framing of that send only. The
// engine's finish-time rules still apply: a trailer on a body the
// protocol frames without a trailer section fails there, typed.
struct response_overlay {
    http::fields headers;
    http::fields trailers;
};

// Typed result of one send_definition() completion. ok: the whole body
// was streamed and the final framing (with the overlay's trailers)
// was accepted; body_bytes counts the payload bytes handed to the
// writer. Every failure carries its typed outcome.
struct send_report {
    http::outcome status;
    std::size_t body_bytes = 0;
};

namespace detail {

// Merged wire fields of one send: the definition's base fields with
// every overlay header occurrence appended in entries() order, so the
// committed sequence is base-then-overlay with the overlay order
// preserved (REQ-030). A local copy per send — the definition is
// never touched.
inline http::fields merge_send_fields(const response_definition& def,
                                      const response_overlay& overlay) {
    http::fields merged = def.fields();
    for (const http::fields::entry e : overlay.headers.entries()) {
        merged.append(e.name, e.value);
    }
    return merged;
}

// Trailer names that may never ride a trailer section: the framing
// fields and the connection's routing name belong to the head only.
inline http::outcome check_trailer_names(const http::fields& trailers) {
    for (const std::string_view name :
         {"Transfer-Encoding", "Content-Length", "Host"}) {
        if (trailers.count(name) > 0) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response fields: '" + std::string(name)
                    + "' cannot be a trailer");
        }
    }
    return http::outcome::okay();
}

// Pre-commit validation of one overlay: wire-safe headers, wire-safe
// trailers, and no framing or routing names among the trailers.
inline http::outcome check_overlay(const response_overlay& overlay) {
    const http::outcome headers =
        check_fields_wire_safe(overlay.headers);
    if (!headers.ok()) return headers;
    const http::outcome trailers =
        check_fields_wire_safe(overlay.trailers);
    if (!trailers.ok()) return trailers;
    return check_trailer_names(overlay.trailers);
}

// One send's private body state, allocated in send_definition's
// coroutine frame: independence across concurrent sends of one shared
// definition is structural — nothing here is reachable from the
// definition. Declared in this public header for the same reason as
// detail::body_sink: send_definition's inline body drives it, and a
// public header may not include a private one.
class send_cursor {
 public:
    // Binds to `def` (which must be valid) and takes this send's
    // snapshot of the body source against the merged framing
    // `framing`: the bytes aliased (the definition pins them alive),
    // the file freshly opened with its read bound taken from an
    // explicit Content-Length when the merged fields carry one (the
    // observed size otherwise), or one fresh producer invoked.
    http::outcome prepare(const response_definition& def,
                          const http::fields& framing) {
        owner_ = def.impl_;
        switch (owner_->kind) {
            case response_definition::source_kind::owned_bytes:
                offset_ = 0;
                total_ = owner_->bytes.size();
                return http::outcome::okay();
            case response_definition::source_kind::reopen_file:
                return prepare_file(framing);
            case response_definition::source_kind::factory:
                return prepare_factory();
        }
        return http::outcome::okay();
    }

    // The next chunk of this send's body: exactly one meaning per
    // body_chunk, spans valid until the next pull on this cursor.
    body_chunk pull() {
        switch (owner_->kind) {
            case response_definition::source_kind::owned_bytes:
                return pull_bytes();
            case response_definition::source_kind::reopen_file:
                return pull_file();
            case response_definition::source_kind::factory:
                return pull_factory();
        }
        return body_chunk{http::outcome::okay(), {}, true};
    }

    // reopen_file only: the size observed at this send's prepare (the
    // pinned Content-Length); 0 for every other kind.
    std::uint64_t file_size() const noexcept { return file_size_; }

 private:
    // One fresh private handle per send: concurrent sends of one
    // reopen_file definition never share seek state. The size observed
    // here is this send's pinned Content-Length; an explicit
    // Content-Length in the merged framing becomes the read bound
    // instead, so a file short of its declared length fails that send
    // typed (the ownership matrix's shrink guarantee).
    http::outcome prepare_file(const http::fields& framing) {
        file_.open(owner_->path, std::ios::binary);
        if (!file_.is_open()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "send_definition: cannot reopen file '" + owner_->path
                    + "'");
        }
        file_.seekg(0, std::ios::end);
        const std::streamoff end = file_.tellg();
        file_.seekg(0, std::ios::beg);
        file_size_ = end < 0 ? 0 : static_cast<std::uint64_t>(end);
        remaining_ = file_size_;
        if (framing.count("content-length") > 0) {
            std::uint64_t declared = 0;
            const http::outcome length =
                parse_content_length(framing, declared);
            if (!length.ok()) return length;
            remaining_ = declared;
        }
        buffer_.assign(k_file_chunk_bytes, std::byte{0});
        return http::outcome::okay();
    }

    // One fresh producer per send; an empty one is a contract
    // violation failed before the head commits.
    http::outcome prepare_factory() {
        producer_ = owner_->make();
        if (!producer_) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "send_definition: factory returned an empty producer");
        }
        return http::outcome::okay();
    }

    // owned_bytes: the whole remainder in one chunk carrying end (the
    // writer's internal loop paces it against queue capacity, so
    // backpressure stays the writer's).
    body_chunk pull_bytes() {
        if (offset_ >= total_) {
            return body_chunk{http::outcome::okay(), {}, true};
        }
        const std::byte* const base = owner_->bytes.data() + offset_;
        const std::size_t rest =
            static_cast<std::size_t>(total_ - offset_);
        offset_ = total_;
        return body_chunk{http::outcome::okay(),
                          std::span<const std::byte>(base, rest), true};
    }

    // reopen_file: at most one buffer's worth of the pinned size. A
    // clean EOF with bytes still owed to the pinned Content-Length is
    // a short body (the file shrank between sends): typed
    // protocol_error, mirroring the engine's short-body diagnosis.
    body_chunk pull_file() {
        if (remaining_ == 0) {
            return body_chunk{http::outcome::okay(), {}, true};
        }
        file_.read(reinterpret_cast<char*>(buffer_.data()),
                   static_cast<std::streamsize>(std::min<std::uint64_t>(
                       remaining_, buffer_.size())));
        const std::streamsize got = file_.gcount();
        // eofbit is set only when fewer characters than requested were
        // available, and the request never exceeds the bound -- so eof
        // (or a zero count) means the file ran out before the pinned
        // length: a short body, typed like the engine's.
        if (got <= 0 || file_.eof()) {
            return body_chunk{
                http::outcome(
                    http::outcome_code::protocol_error,
                    "send_definition: body shorter than the declared "
                    "Content-Length"),
                {}, false};
        }
        remaining_ -= static_cast<std::uint64_t>(got);
        return body_chunk{
            http::outcome::okay(),
            std::span<const std::byte>(buffer_.data(),
                                       static_cast<std::size_t>(got)),
            remaining_ == 0};
    }

    // factory: forwards to this send's producer; an empty non-end
    // chunk is a producer contract violation failed typed (never
    // silently skipped); end and failure pass through verbatim.
    body_chunk pull_factory() {
        const body_chunk chunk = producer_();
        if (chunk.status.ok() && !chunk.end && chunk.data.empty()) {
            return body_chunk{
                http::outcome(
                    http::outcome_code::invalid_argument,
                    "send_definition: producer returned an empty "
                    "non-end chunk"),
                {}, false};
        }
        return chunk;
    }

    std::shared_ptr<const response_definition::impl> owner_;
    std::uint64_t offset_ = 0;     // owned_bytes cursor into shared bytes
    std::uint64_t total_ = 0;      // owned_bytes total
    std::uint64_t file_size_ = 0;  // reopen_file pinned size
    std::uint64_t remaining_ = 0;  // reopen_file bytes left to pinned size
    std::ifstream file_;           // reopen_file: this send's private handle
    std::vector<std::byte> buffer_;  // reopen_file read buffer
    body_producer producer_;       // factory: this send's producer
};

// Everything a send does BEFORE committing the head: gates on the
// definition, validates the overlay and the merged framing, merges
// the wire fields, and takes the send's cursor snapshot. A failure
// here leaves the exchange untouched (DR-V3-005: conflicting
// singleton/framing fields fail validation before headers commit).
inline http::outcome prepare_send(const response_definition& def,
                                  const response_overlay& overlay,
                                  http::fields& merged,
                                  send_cursor& cursor) {
    if (!def.valid()) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "send_definition: definition is empty");
    }
    const http::outcome overlay_ok = check_overlay(overlay);
    if (!overlay_ok.ok()) return overlay_ok;
    merged = merge_send_fields(def, overlay);
    const http::outcome framing = check_fields_framing(merged);
    if (!framing.ok()) return framing;
    const http::outcome prepared = cursor.prepare(def, merged);
    if (!prepared.ok()) return prepared;
    if (def.kind() == response_definition::source_kind::reopen_file) {
        // The pinned size is per send: the file is re-observed every
        // time, never baked into the definition.
        pin_content_length(merged, cursor.file_size());
    }
    return http::outcome::okay();
}

// Streams the cursor's body into the exchange's writer, one pull at a
// time; backpressure is the writer's (a parked write IS it).
inline task<http::outcome> stream_body(exchange& x, send_cursor& cursor,
                                       std::size_t& body_bytes) {
    for (;;) {
        const body_chunk chunk = cursor.pull();
        if (!chunk.status.ok()) co_return chunk.status;
        if (!chunk.data.empty()) {
            const body_write written =
                co_await x.writer().write(chunk.data);
            if (!written.status.ok()) co_return written.status;
            body_bytes += chunk.data.size();
        }
        if (chunk.end) co_return http::outcome::okay();
    }
}

}  // namespace detail

// Sends one definition through an open exchange with one overlay
// (architecture §3.1's exchange::respond(const response_definition&,
// response_overlay), as a free function wrapping the exchange the
// same way make_sync_route does — the exchange itself stays
// engine-facing and unmodified).
//
// The send validates the overlay and the merged framing, commits the
// head with start_response (base fields then overlay headers, in
// order), streams the body through writer() with backpressure, and
// finishes with the overlay's trailers. A validation, prepare, or
// head-commit failure returns typed with the exchange untouched; a
// failure after the committed head returns typed with the exchange
// terminal (the engine owns the connection's fate; the send never
// calls abort() itself).
//
// The definition is borrowed — it must outlive the task; it is never
// mutated. The overlay is taken BY VALUE: the send frame owns its
// copy, so a temporary overlay is exactly as safe as a named one (a
// reference parameter bound to a default temporary would dangle
// across the send's suspensions).
//
// Failure vocabulary: invalid_argument — malformed fields, framing
// conflicts, an unopenable file, an empty producer or chunk;
// invalid_state — the exchange or writer gates (e.g. a second
// terminal decision); connection_closed / cancelled — a disconnect
// before or during the send; protocol_error — a file body short of
// its pinned Content-Length.
inline task<send_report> send_definition(exchange& x,
                                         const response_definition& def,
                                         response_overlay overlay = {}) {
    send_report report;
    if (x.disconnected()) {
        report.status = http::outcome(
            http::outcome_code::connection_closed,
            "send_definition: exchange is disconnected");
        co_return report;
    }
    http::fields merged;
    detail::send_cursor cursor;
    report.status = detail::prepare_send(def, overlay, merged, cursor);
    if (!report.status.ok()) co_return report;
    report.status = x.start_response(def.status(), merged);
    if (!report.status.ok()) co_return report;
    report.status =
        co_await detail::stream_body(x, cursor, report.body_bytes);
    if (!report.status.ok()) co_return report;
    const body_finish finished =
        co_await x.writer().finish(overlay.trailers);
    report.status = finished.status;
    co_return report;
}

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_RESPONSE_DEFINITION_HPP_
