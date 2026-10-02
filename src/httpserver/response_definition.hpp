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
// Body sources (REQ-026/028) and their vocabulary — pull chunks,
// producers, factories, the source-kind taxonomy, and the per-send
// cursor — live in response_sources.hpp (TASK-113 split):
//   owned_bytes  — the library holds a deep copy of the body;
//   reopen_file  — the library stores a path and opens one fresh
//                  handle per send;
//   factory      — the application supplies a callable returning one
//                  fresh producer per send;
//   borrowed     — the application keeps the memory and hands over a
//                  span plus the body_lease pinning it alive;
//   owned_file   — an open std::FILE* transferred to the library and
//                  streamed once from its position at transfer;
//   owned_pipe   — a pipe endpoint transferred to the library and
//                  streamed once to its own EOF (unknown length).
//
// One send = send_definition(exchange, definition, overlay): it
// validates the overlay and the merged framing BEFORE committing the
// head (a conflicting or malformed input fails typed with the exchange
// untouched), merges the overlay's headers after the definition's base
// fields in order, streams the body through exchange::writer() with
// backpressure, and finishes with the overlay's trailers. The send
// never mutates the definition (borrowed) or the caller's overlay
// (taken by value into the send's own frame).
//
// Ownership / lifetime matrix (REQ-028). The Replay column is
// REQ-029's concurrent-reuse contract, deliberately narrowed by the
// one-shot kinds (architecture §3.2, DR-V3-005):
//
//   Source     | Library owns          | Replay     | Application guarantees
//   -----------+-----------------------+------------+-----------------------
//   owned_bytes| deep copy of the      | any number | nothing after the
//              | body, moved in at     | of sends   | factory returns
//              | factory time          |            |
//   reopen_file| the path; one fresh   | any number | the path resolves at
//              | handle per send       | of sends   | each send's prepare; a
//              |                       |            | file that shrinks below
//              |                       |            | the declared or pinned
//              |                       |            | size fails that send
//              |                       |            | typed
//   factory    | the callable          | any number | the callable is safe to
//              | (move-only)           | of sends   | invoke concurrently
//              |                       |            | (once per in-flight
//              |                       |            | send); each producer
//              |                       |            | serves one send; a
//              |                       |            | producer's data spans
//              |                       |            | stay valid until its
//              |                       |            | next pull
//   borrowed   | the span and the      | any number | the keeper keeps
//              | lease (nothing is     | of sends   | [data, data + size)
//              | copied)               |            | unchanged and alive
//              |                       |            | while the definition
//              |                       |            | lives; the lease spans
//              |                       |            | every send
//   owned_file | the handle until the  | ONE send   | the handle is seekable
//              | exactly-once close    |            | with a remaining size a
//              | (the claiming send's  |            | long can express; an
//              | cursor, or the unsent |            | application keeping the
//              | holder, releases it)  |            | handle open wraps the
//              |                       |            | readable side in a
//              |                       |            | factory source instead
//   owned_pipe | the endpoint until    | ONE send   | the stream reaches EOF
//              | the exactly-once      |            | by itself, or supplies
//              | close                 |            | a Content-Length that
//              |                       |            | bounds the read
//   response_overlay| its own field sets (deep  | a plain value; the send
//                    | value type)               | frame holds its own copy
//   send_definition | nothing of either input   | the definition outlives
//                    |                           | the task (borrowed by
//                    |                           | reference)
//
// The one-shot narrowing: a one-shot definition admits exactly ONE
// send attempt past validation. The claim is taken before the probe
// and is not refundable, so a second send — or a racing loser —
// fails invalid_state before writing anything, and a failed probe
// spends the definition. A leased borrowed body keeps REQ-029's full
// contract: reading a span is non-destructive.

#ifndef SRC_HTTPSERVER_RESPONSE_DEFINITION_HPP_
#define SRC_HTTPSERVER_RESPONSE_DEFINITION_HPP_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/response_sources.hpp>

namespace httpserver {

class response_definition;
struct response_overlay;

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

// Everything a send does before committing the head (defined after
// send_report below): the one function reaching the definition's
// frozen impl, so the cursor stays decoupled from the class.
http::outcome prepare_send(const response_definition& def,
                           const response_overlay& overlay,
                           http::fields& merged,
                           send_cursor& cursor);

}  // namespace detail

// A replayable immutable response value (DR-V3-005). Constructed once
// through the validating factories; afterwards no API mutates it —
// copies share the frozen state (a shared immutable block), moves hand
// it over. Sends read it concurrently and never write it.
class response_definition {
 public:
    // The source taxonomy lives in response_sources.hpp; the alias
    // keeps the TASK-112 spelling (response_definition::source_kind).
    using source_kind = response_source_kind;

    // Body held by value; Content-Length pinned to the body size when
    // the fields carry neither framing field (including "0").
    static http::outcome owned_bytes(const http::status& s, http::fields f,
                                     std::vector<std::byte> body,
                                     response_definition& out) {
        build state;
        state.status = s;
        detail::pin_content_length(f, body.size());
        state.fields = std::move(f);
        state.body.kind = source_kind::owned_bytes;
        state.body.bytes = std::move(body);
        return commit(std::move(state), out);
    }

    // Body re-read from `path` on every send: nothing is opened at
    // construction, so a definition outlives any one file's lifetime;
    // each send observes the file at its own prepare and pins
    // Content-Length per send.
    static http::outcome reopen_file(const http::status& s, http::fields f,
                                     std::string path,
                                     response_definition& out) {
        build state;
        state.status = s;
        state.fields = std::move(f);
        state.body.kind = source_kind::reopen_file;
        state.body.path = std::move(path);
        return commit(std::move(state), out);
    }

    // Body produced per send by a fresh producer from `make`.
    static http::outcome factory(const http::status& s, http::fields f,
                                 body_factory make,
                                 response_definition& out) {
        build state;
        state.status = s;
        state.fields = std::move(f);
        state.body.kind = source_kind::factory;
        state.body.make = std::move(make);
        return commit(std::move(state), out);
    }

    // Body borrowed from application memory under an explicit lease:
    // the library stores only the view and the lease, never a copy.
    // The keeper keeps [body.data(), body.data() + body.size())
    // unchanged and alive while the definition lives (that
    // immutability is what makes a borrowed body replayable);
    // releasing the last definition releases the keeper. An invalid
    // lease is rejected before a definition exists — sharing a
    // borrowed body without a valid lease has no safe send (REQ-028).
    static http::outcome borrowed(const http::status& s, http::fields f,
                                  std::span<const std::byte> body,
                                  body_lease lease,
                                  response_definition& out) {
        build state;
        state.status = s;
        detail::pin_content_length(f, body.size());
        state.fields = std::move(f);
        state.body.kind = source_kind::borrowed;
        state.body.view = body;
        state.body.lease = std::move(lease);
        return commit(std::move(state), out);
    }

    // Body streamed from an open std::FILE* TRANSFERRED to the
    // library and closed with std::fclose (see the owned_close_fn
    // overload for a custom release).
    static http::outcome owned_file(const http::status& s, http::fields f,
                                    std::FILE* handle,
                                    response_definition& out) {
        return owned_file(s, std::move(f), handle,
                          owned_close_fn(&detail::fclose_owned), out);
    }

    // Body streamed from an open std::FILE* transferred to the
    // library (REQ-028): ONE-SHOT — one handle holds one seek
    // position, so exactly one send may consume it; a second send
    // fails invalid_state before writing anything. The body is the
    // handle's remaining bytes at transfer (position→EOF), probed at
    // the send's prepare (an unseekable handle fails there, typed,
    // and spends the definition). Content-Length pins per send to the
    // probed size unless the fields declare one — a declared length
    // becomes the read bound, and a body short of it fails that send
    // typed. The library owns the handle from here on and closes it
    // exactly once: after the send completes, fails, or is cancelled,
    // or when an unsent definition is destroyed. An application that
    // must keep its handle open wraps the readable side in a factory
    // source instead. `long`-based offsets bound the probed size to
    // LONG_MAX on LLP64 platforms; larger transfers belong to
    // reopen_file (std::streamoff).
    static http::outcome owned_file(const http::status& s, http::fields f,
                                    std::FILE* handle,
                                    owned_close_fn close,
                                    response_definition& out) {
        if (handle == nullptr || !close) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response_definition: owned_file requires a handle and "
                "a close operation");
        }
        build state;
        state.status = s;
        state.fields = std::move(f);
        state.body.kind = source_kind::owned_file;
        state.body.handle =
            std::make_shared<detail::transferred_handle>(
                handle, std::move(close));
        return commit(std::move(state), out);
    }

    // Body streamed from an open pipe endpoint TRANSFERRED to the
    // library and closed with std::fclose (see the owned_close_fn
    // overload for a custom release).
    static http::outcome owned_pipe(const http::status& s, http::fields f,
                                    std::FILE* handle,
                                    response_definition& out) {
        return owned_pipe(s, std::move(f), handle,
                          owned_close_fn(&detail::fclose_owned), out);
    }

    // Body streamed from an open pipe endpoint (any std::FILE* with no
    // usable seek position) transferred to the library (REQ-028):
    // ONE-SHOT and NON-REPLAYABLE (architecture §3.2) — the bytes can
    // be read exactly once, so exactly one send may consume the
    // definition; a second send, however it races the winner, fails
    // invalid_state before writing anything. Nothing is probed at the
    // send's prepare (a pipe is never seeked). The length is unknown
    // unless the fields declare a Content-Length: without one no
    // length is pinned and the framing stays the transport's (chunked
    // on HTTP/1.1, close-delimited on HTTP/1.0); with one the declared
    // length bounds the read, and a body short of it fails that send
    // typed. The library owns the endpoint from here on and closes it
    // exactly once: after the send completes, fails, or is cancelled,
    // or when an unsent definition is destroyed.
    static http::outcome owned_pipe(const http::status& s, http::fields f,
                                    std::FILE* handle,
                                    owned_close_fn close,
                                    response_definition& out) {
        if (handle == nullptr || !close) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response_definition: owned_pipe requires a handle and "
                "a close operation");
        }
        build state;
        state.status = s;
        state.fields = std::move(f);
        state.body.kind = source_kind::owned_pipe;
        state.body.handle =
            std::make_shared<detail::transferred_handle>(
                handle, std::move(close));
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
        return impl_ ? impl_->body->kind : source_kind::owned_bytes;
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
    // The one send-side reach into the frozen state: prepare_send
    // hands the cursor the body block. Consumers never see the impl.
    friend http::outcome detail::prepare_send(const response_definition&,
                                              const response_overlay&,
                                              http::fields&,
                                              detail::send_cursor&);

    // The frozen state. Built locally by a factory as a `build`,
    // validated in full, and only then shared immutably
    // (`shared_ptr<const impl>` makes the immutability structural: no
    // API can reach a mutable reference). The body rides as its own
    // shared immutable block so a live send's cursor pins it
    // independently of this object.
    struct impl {
        http::status status;
        http::fields fields;
        std::shared_ptr<const detail::response_body_source> body;
    };

    // A factory's pending, still-private assembly: like impl but the
    // body is a plain value. validate/commit are its only consumers;
    // a failed build is discarded without ever being shared.
    struct build {
        http::status status;
        http::fields fields;
        detail::response_body_source body;
    };

    // Full validation of the assembled state; nothing is mutated on
    // failure (the caller never touches `out` before this returns ok).
    static http::outcome validate(const build& state) {
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
        if (state.body.kind == source_kind::reopen_file
                && state.body.path.empty()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response_definition: reopen_file requires a path");
        }
        if (state.body.kind == source_kind::factory && !state.body.make) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response_definition: factory requires a callable");
        }
        if (state.body.kind == source_kind::borrowed
                && !state.body.lease.valid()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response_definition: borrowed requires a lease");
        }
        return http::outcome::okay();
    }

    // Validates `state` and, only on success, freezes it into `out`:
    // the body block becomes its own shared immutable piece, so the
    // definition and any in-flight cursor alias one frozen source.
    static http::outcome commit(build&& state, response_definition& out) {
        const http::outcome checked = validate(state);
        if (!checked.ok()) return checked;
        impl frozen;
        frozen.status = std::move(state.status);
        frozen.fields = std::move(state.fields);
        frozen.body = std::make_shared<const detail::response_body_source>(
            std::move(state.body));
        out.impl_ = std::make_shared<const impl>(std::move(frozen));
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

// Everything a send does BEFORE committing the head: gates on the
// definition, validates the overlay and the merged framing, merges the
// wire fields, and takes the send's cursor snapshot. A failure
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
    const std::shared_ptr<const response_body_source>& body =
        def.impl_->body;
    const http::outcome prepared = cursor.prepare(body, merged);
    if (!prepared.ok()) return prepared;
    if (body->kind == response_source_kind::reopen_file
            || body->kind == response_source_kind::owned_file) {
        // The pinned size is per send: the file is re-observed (or the
        // transferred handle probed) every time, never baked into the
        // definition.
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
// conflicts, an unopenable file, an unseekable owned_file handle, an
// empty producer or chunk; invalid_state — the exchange or writer
// gates (e.g. a second terminal decision), a spent one-shot source,
// or a handle read failure mid-body; connection_closed / cancelled —
// a disconnect before or during the send; protocol_error — a file,
// transferred, or borrowed body short of its pinned or declared
// Content-Length.
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
