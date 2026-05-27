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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// TASK-048: error-page helpers carved out of detail/webserver_dispatch.cpp
// to keep that TU under the per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh) once the route_resolved and before_handler
// firing sites landed.
//
// Five small functions live here: the three synth-response helpers
// (not_found_page, method_not_allowed_page, internal_error_page), the
// guarded user-logger forwarder (log_dispatch_error), and the
// double-fault-safe wrapper around the user internal_error_handler
// (run_internal_error_handler_safely). They are pure const helpers off
// webserver_impl and share no state with the rest of the dispatch TU
// beyond `parent->*` user handlers and `mr->dhr`. Splitting them out is
// a mechanical refactor with no behaviour change.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <string>
#include <string_view>

#include "httpserver/constants.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"

namespace httpserver {

using httpserver::http::http_utils;

namespace detail {

http_response webserver_impl::not_found_page(detail::modded_request* mr) const {
    if (parent->not_found_handler != nullptr) {
        return parent->not_found_handler(*mr->dhr);
    }
    return http_response::string(std::string{constants::NOT_FOUND_ERROR})
        .with_status(http_utils::http_not_found);
}

http_response webserver_impl::method_not_allowed_page(detail::modded_request* mr) const {
    if (parent->method_not_allowed_handler != nullptr) {
        return parent->method_not_allowed_handler(*mr->dhr);
    }
    return http_response::string(std::string{constants::METHOD_ERROR})
        .with_status(http_utils::http_method_not_allowed);
}

http_response webserver_impl::internal_error_page(
    detail::modded_request* mr,
    std::string_view msg,
    bool force_our) const {
    // TASK-031 / DR-009 §5.2 point 4: the double-fault fallback. Used
    // when the user-supplied internal_error_handler itself threw or
    // when the belt-and-suspenders site after
    // get_raw_response_with_fallback fires. The body is intentionally
    // empty and the message is intentionally ignored.
    if (force_our) {
        return http_response::empty()
            .with_status(http_utils::http_internal_server_error);
    }
    // §5.2 point 2/3: invoke the user handler with the originating message.
    if (parent->internal_error_handler != nullptr) {
        return parent->internal_error_handler(*mr->dhr, msg);
    }
    // No handler configured: surface the message in the default body so
    // the unset-handler path is informative for debugging.
    // WARNING (CWE-209): in production, always wire a constant-returning
    // internal_error_handler to avoid leaking exception details (stack
    // paths, database connection strings, etc.) to HTTP clients. The
    // informative default is intentional for development only.
    return http_response::string(std::string{msg})
        .with_status(http_utils::http_internal_server_error);
}

void webserver_impl::log_dispatch_error(std::string_view msg) const {
    if (parent->log_error == nullptr) {
        return;
    }
    // A misbehaving user logger must not poison the catch from inside
    // the catch. Swallow any exception it throws; we have no recovery
    // beyond dropping the log line.
    try {
        parent->log_error(std::string(msg));
    } catch (...) {
        // Intentionally suppressed.
    }
}

http_response
webserver_impl::run_internal_error_handler_safely(
    detail::modded_request* mr,
    std::string_view msg) const {
    try {
        return internal_error_page(mr, msg, /*force_our=*/false);
    } catch (...) {
        // §5.2 point 4: the user handler itself threw. Log generically
        // and return an empty-body 500. No exception escapes from here.
        log_dispatch_error("internal_error_handler threw; "
                           "sending hardcoded empty-body 500");
        return internal_error_page(mr, "", /*force_our=*/true);
    }
}

}  // namespace detail

}  // namespace httpserver
