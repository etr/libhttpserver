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
// one-shot decode_urlencoded, and the form_read rejection verdict.
// The two deltas v3 adds over v2 are encoded here as typed outcomes
// (never exceptions): a malformed %HH answers invalid_argument (v2
// passed it through literally) and a cap overrun answers
// limit_exceeded before any unbounded storage (v2 truncated
// silently).

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <httpserver/detail/forms_urlencoded.hpp>
#include <httpserver/forms/urlencoded.hpp>

namespace httpserver {

namespace forms {

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

http::status form_read::reject_status() const noexcept {
    if (status.code() == http::outcome_code::limit_exceeded) {
        return http::status::from_code(413);
    }
    if (status.code() == http::outcome_code::invalid_argument) {
        return http::status::from_code(400);
    }
    // Transport, cancellation, and state failures commit nothing.
    return http::status::from_code(0);
}

http::fields form_read::reject_fields() const {
    http::fields fields;
    fields.append("Content-Length", "0");
    return fields;
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

}  // namespace forms

}  // namespace httpserver
