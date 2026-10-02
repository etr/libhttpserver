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

// URL-encoded form handling with bounded admission (TASK-116,
// application/x-www-form-urlencoded request bodies, PRD-V3N-REQ-021/
// 022, v2 within-cap parity per PRD-V3N-REQ-038, DR-V3-001). The
// surface is vocabulary only: the strict incremental decoder and the
// adapters live in the library's private implementation.
//
// Within cap, decode matches the v2 contract this library has always
// documented for repeated fields: '&' separates pairs, the first '='
// in a pair separates name from value, '+' is 0x20, %HH is the byte,
// every other raw byte passes through verbatim, and repeated names
// append in arrival order with first-value lookup.
//
// Two bounded deltas over v2 (migration-noted in the parity
// inventory): a malformed %HH is a typed rejection the adapter
// answers with 400 (v2 passed the bytes through literally), and a
// body or field count past its cap is rejected with 413 BEFORE any
// unbounded storage (v2 truncated silently). Decoded storage is
// bounded by construction: decoded bytes never exceed the raw byte
// cap, and the emitted pair count is capped.
//
// Two consumption forms share one decoder:
//   - read_urlencoded streams the admitted body through ~512-byte
//     reads, never buffering the raw body, and returns a self-contained
//     form_read verdict;
//   - make_urlencoded_route wraps a value-returning handler with the
//     whole bounded sequence (admit, buffer under the cap, decode,
//     413/400 rejections, value commit), so a route registered with it
//     simply receives decoded fields. The adapter applies v2's
//     content-type gate: a request whose Content-Type is not
//     application/x-www-form-urlencoded (case-insensitive media type;
//     parameters allowed) still drains its body under the cap and
//     reaches the handler with EMPTY fields -- v2 ran no form
//     processing there either.

#ifndef SRC_HTTPSERVER_FORMS_URLENCODED_HPP_
#define SRC_HTTPSERVER_FORMS_URLENCODED_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/routes.hpp>

namespace httpserver {

namespace forms {

// Bounded-admission budgets for one urlencoded body: max_total_bytes
// counts raw body bytes consumed and max_fields counts decoded pairs
// emitted. The defaults mirror v2's GET-argument budgets
// (DEFAULT_MAX_ARGS_BYTES / DEFAULT_MAX_ARGS_COUNT). Exactly-at-cap
// succeeds; a cap of zero would disable the bound, so create()
// refuses both.
struct urlencoded_limits {
    std::uint64_t max_total_bytes = 65536;
    std::uint64_t max_fields = 64;

    // Validates both caps (each at least 1) and lands them in @p out;
    // on rejection @p out is untouched.
    static http::outcome create(std::uint64_t max_total_bytes,
                                std::uint64_t max_fields,
                                urlencoded_limits& out);
};

// The decoded fields of one urlencoded body, in arrival order
// (repeated names appended -- the v2 documented contract). Names and
// values are length-carrying strings: a decoded NUL is storable.
class form_fields {
 public:
    form_fields() = default;

    // Adopts the decoder's emitted pairs (the library-side bridge;
    // an application-side form_fields starts empty).
    explicit form_fields(
        std::vector<std::pair<std::string, std::string>> adopted)
        : entries_(std::move(adopted)) { }

    // Every pair in arrival order.
    const std::vector<std::pair<std::string, std::string>>& entries()
        const noexcept {
        return entries_;
    }

    // The FIRST value @p name mapped to (the v2 flat-lookup rule);
    // a name that never appeared has no value.
    std::optional<std::string_view> value(
        std::string_view name) const noexcept {
        for (const std::pair<std::string, std::string>& entry :
             entries_) {
            if (std::string_view(entry.first) == name) {
                return std::string_view(entry.second);
            }
        }
        return std::nullopt;
    }

    // Every value @p name mapped to, in arrival order; empty for a
    // name that never appeared.
    std::vector<std::string_view> all(std::string_view name) const {
        std::vector<std::string_view> values;
        for (const std::pair<std::string, std::string>& entry :
             entries_) {
            if (std::string_view(entry.first) == name) {
                values.emplace_back(entry.second);
            }
        }
        return values;
    }

    std::size_t size() const noexcept {
        return entries_.size();
    }

 private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

// The verdict of one bounded urlencoded read or decode. status is ok
// exactly when fields carries the decoded set; a rejection never
// carries fields (rejection precedes storage).
struct form_read {
    http::outcome status;
    form_fields fields;

    bool ok() const noexcept;

    // The status the two form rejections answer with:
    // limit_exceeded -> 413, invalid_argument (a malformed escape) ->
    // 400. Any other failure (transport, cancellation) carries no
    // commit-able status: the result is invalid and valid() gates it.
    http::status reject_status() const noexcept;

    // The response fields for a rejection: an explicit Content-Length:
    // 0 (the v2 parity framing for the empty rejection body).
    http::fields reject_fields() const;
};

// Application-supplied handler of one decoded form (the form adapter's
// value-returning callback, the sync-route shape).
using form_route_handler = concurrency::unique_function<server::sync_response(const http::request_head&, const form_fields&)>;

// Decodes one complete raw urlencoded body under @p limits. On
// success @p out carries the fields; on a typed rejection @p out is
// untouched (rejection precedes storage).
http::outcome decode_urlencoded(std::span<const std::byte> body,
                                const urlencoded_limits& limits,
                                form_fields& out);

// Streaming bounded read of the exchange's body: admits it under
// limits.max_total_bytes, feeds the incremental decoder through
// ~512-byte reads (the raw body is never buffered whole), and returns
// the self-contained verdict. A caller that already admitted the body
// gets invalid_state (this form owns its admission).
task<form_read> read_urlencoded(exchange& x, const urlencoded_limits& limits);

// Wraps a value-returning handler with the whole bounded form
// sequence: admit under the byte cap, buffer via collect (a body past
// the cap answers 413 and the handler never runs), decode (a
// malformed escape answers 400 and the handler never runs), apply the
// v2 content-type gate (a non-matching type still drains under the
// cap and reaches the handler with empty fields), invoke, and commit
// the returned value (auto Content-Length when the handler framed
// nothing -- the sync-route rules). Throws std::invalid_argument at
// registration time for a zero cap or an empty handler.
server::route_handler make_urlencoded_route(urlencoded_limits limits,
                                            form_route_handler handler);

}  // namespace forms

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_FORMS_URLENCODED_HPP_
