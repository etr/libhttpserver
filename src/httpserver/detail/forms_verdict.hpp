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

// The rejection-verdict shaping the two form surfaces share (TASK-117
// factored out of form_read so multipart_read is not a copy: the
// duplication gate requires one helper, not one per form). Both
// verdict types answer a typed limit_exceeded with 413 and a typed
// invalid_argument with 400, frame the empty rejection body with an
// explicit Content-Length: 0 (the v2 parity framing), and carry no
// committable status for any other failure (transport, cancellation,
// state -- the runner synthesizes what those need).

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/forms_verdict.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_FORMS_VERDICT_HPP_
#define SRC_HTTPSERVER_DETAIL_FORMS_VERDICT_HPP_

#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/status.hpp>

namespace httpserver {

namespace detail {

namespace forms_verdict {

// limit_exceeded -> 413, invalid_argument -> 400, anything else -> an
// invalid status (nothing to commit).
inline http::status reject_status(const http::outcome& status) noexcept {
    if (status.code() == http::outcome_code::limit_exceeded) {
        return http::status::from_code(413);
    }
    if (status.code() == http::outcome_code::invalid_argument) {
        return http::status::from_code(400);
    }
    return http::status::from_code(0);
}

// The length-framed empty rejection body.
inline http::fields reject_fields() {
    http::fields fields;
    fields.append("Content-Length", "0");
    return fields;
}

}  // namespace forms_verdict

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_FORMS_VERDICT_HPP_
