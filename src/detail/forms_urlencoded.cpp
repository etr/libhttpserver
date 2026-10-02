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

// TASK-116: the urlencoded form implementation (PRD-V3N-REQ-021/022,
// v2 within-cap parity per PRD-V3N-REQ-038, DR-V3-001). The strict
// incremental decoder lives in the private detail header; this TU
// owns the public vocabulary's members: the limits factory, the
// one-shot decode_urlencoded, the form_read rejection verdict, the
// streaming read_urlencoded, and the bounded route adapter. The two
// deltas v3 adds over v2 are encoded here as typed outcomes (never
// exceptions on the request path): a malformed %HH answers
// invalid_argument (v2 passed it through literally) and a cap overrun
// answers limit_exceeded before any unbounded storage (v2 truncated
// silently).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/detail/auth_text.hpp>
#include <httpserver/detail/forms_urlencoded.hpp>
#include <httpserver/detail/forms_verdict.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/server/route_sync.hpp>

namespace httpserver {

namespace forms {

namespace {

constexpr std::string_view k_content_type_field = "Content-Type";
constexpr std::string_view k_urlencoded_media_type = "application/x-www-form-urlencoded";
// The streaming read feeds the decoder bounded chunks; the raw body is
// never buffered whole.
constexpr std::size_t kReadChunkBytes = 512;

// The v2 activation gate (request_pipeline.cpp: a form is processed
// only when the media type case-insensitively matches, parameters
// after the type allowed): the match stops at value end or the first
// ';' / whitespace boundary, so "...urlencoded; charset=utf-8" matches
// and a hypothetical "...urlencoded2" does not.
bool is_urlencoded_content_type(
    const std::optional<std::string_view>& value) noexcept {
    if (!value.has_value()) return false;
    const std::string_view v = *value;
    if (v.size() < k_urlencoded_media_type.size()) return false;
    if (!detail::auth_text::ascii_iequal(
            v.substr(0, k_urlencoded_media_type.size()),
            k_urlencoded_media_type)) {
        return false;
    }
    if (v.size() == k_urlencoded_media_type.size()) return true;
    const char boundary = v[k_urlencoded_media_type.size()];
    return boundary == ';' || boundary == ' ' || boundary == '\t';
}

}  // namespace

http::outcome urlencoded_limits::create(std::uint64_t max_total_bytes,
                                        std::uint64_t max_fields,
                                        urlencoded_limits& out) {
    if (max_total_bytes < 1 || max_fields < 1) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "urlencoded_limits: both caps must be at least 1");
    }
    out = urlencoded_limits{max_total_bytes, max_fields};
    return http::outcome::okay();
}

bool form_read::ok() const noexcept {
    return status.ok();
}

// The shared forms rejection shaping (detail/forms_verdict.hpp): the
// multipart verdict type delegates to the same helper, so the two
// form surfaces cannot drift apart (the duplication gate requires
// one copy, not one per form).
http::status form_read::reject_status() const noexcept {
    return detail::forms_verdict::reject_status(status);
}

http::fields form_read::reject_fields() const {
    return detail::forms_verdict::reject_fields();
}

http::outcome decode_urlencoded(std::span<const std::byte> body,
                                const urlencoded_limits& limits,
                                form_fields& out) {
    detail::urlencoded_decoder decoder(limits.max_total_bytes,
                                       limits.max_fields);
    const http::outcome fed = decoder.feed(body);
    if (!fed.ok()) return fed;
    const http::outcome done = decoder.finish();
    if (!done.ok()) return done;
    out = form_fields(decoder.take_fields());
    return http::outcome::okay();
}

task<form_read> read_urlencoded(exchange& x,
                                const urlencoded_limits& limits) {
    // The read owns its admission, so the byte cap rides the engine
    // side too; a caller that admitted already gets invalid_state.
    const http::outcome admitted =
        x.admit_body(body_policy{limits.max_total_bytes});
    if (!admitted.ok()) co_return form_read{admitted, form_fields{}};

    detail::urlencoded_decoder decoder(limits.max_total_bytes,
                                       limits.max_fields);
    std::byte chunk[kReadChunkBytes];
    for (;;) {
        const body_read read = co_await x.body().read_some(
            std::span<std::byte>(chunk, kReadChunkBytes));
        if (!read.status.ok()) {
            co_return form_read{read.status, form_fields{}};
        }
        if (read.end_of_body) break;
        const http::outcome fed = decoder.feed(read.data);
        if (!fed.ok()) co_return form_read{fed, form_fields{}};
    }
    const http::outcome done = decoder.finish();
    if (!done.ok()) co_return form_read{done, form_fields{}};
    co_return form_read{http::outcome::okay(),
                        form_fields(decoder.take_fields())};
}

server::route_handler make_urlencoded_route(urlencoded_limits limits,
                                            form_route_handler handler) {
    if (limits.max_total_bytes < 1 || limits.max_fields < 1
            || !handler) {
        throw std::invalid_argument(
            "make_urlencoded_route: both caps must be at least 1 and "
            "the handler must not be empty");
    }
    return [call = std::move(handler), caps = limits](
               exchange& x) -> task<void> {
        // The admission carries the cap to the engine; a typed failure
        // here is a disconnect, so the route ends quietly.
        if (!x.admit_body(body_policy{caps.max_total_bytes}).ok()) {
            co_return;
        }
        const body_collect collected =
            co_await x.body().collect(caps.max_total_bytes);
        if (!collected.status.ok()) {
            // Over-cap: the handler never runs; the undrained remainder
            // is the engine's to settle (the sync-route posture).
            if (collected.status.code()
                    == http::outcome_code::limit_exceeded) {
                static_cast<void>(x.respond(http::status::from_code(413),
                                            http::fields()));
            }
            co_return;
        }
        const std::span<const std::byte> raw(collected.data.data(),
                                             collected.data.size());
        if (!is_urlencoded_content_type(
                x.head().head_fields.first(k_content_type_field))) {
            // v2 parity: no form processing at all, so nothing about
            // the drained body can reject; the handler sees no fields.
            co_await server::detail::commit_sync_value(
                x, call(x.head(), form_fields{}));
            co_return;
        }
        form_fields fields;
        const http::outcome decoded = decode_urlencoded(raw, caps, fields);
        if (!decoded.ok()) {
            // Malformed: the handler never runs; the length-framed
            // empty body is the v2 parity framing for the rejection.
            const form_read rejection{decoded, form_fields{}};
            static_cast<void>(x.respond(rejection.reject_status(),
                                        rejection.reject_fields()));
            co_return;
        }
        co_await server::detail::commit_sync_value(
            x, call(x.head(), std::move(fields)));
    };
}

}  // namespace forms

}  // namespace httpserver
