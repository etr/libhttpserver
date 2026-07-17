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

// Error-page helpers, split out of detail/webserver_dispatch.cpp to
// keep that TU under the per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh).
//
// Five small functions live here: the three synth-response helpers
// (not_found_page, method_not_allowed_page, internal_error_page), the
// guarded user-logger forwarder (log_dispatch_error), and the
// double-fault-safe wrapper around the user internal_error_handler
// (run_internal_error_handler_safely). They are pure const helpers off
// webserver_impl and share no state with the rest of the dispatch TU
// beyond `parent->*` user handlers and `mr->request`.

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
    if (parent->config.not_found_handler != nullptr) {
        return parent->config.not_found_handler(*mr->request);
    }
    return http_response::string(std::string{constants::NOT_FOUND_ERROR})
        .with_status(http_utils::http_not_found);
}

http_response webserver_impl::method_not_allowed_page(detail::modded_request* mr) const {
    if (parent->config.method_not_allowed_handler != nullptr) {
        return parent->config.method_not_allowed_handler(*mr->request);
    }
    return http_response::string(std::string{constants::METHOD_ERROR})
        .with_status(http_utils::http_method_not_allowed);
}

http_response webserver_impl::internal_error_page(
    detail::modded_request* mr,
    std::string_view msg,
    bool force_our) const {
    // The double-fault fallback. Used
    // when the user-supplied internal_error_handler itself threw or
    // when the belt-and-suspenders site after
    // get_raw_response_with_fallback fires. The body is intentionally
    // empty and the message is intentionally ignored.
    if (force_our) {
        return http_response::empty()
            .with_status(http_utils::http_internal_server_error);
    }
    // Invoke the user handler with the originating message.
    if (parent->config.internal_error_handler != nullptr) {
        return parent->config.internal_error_handler(*mr->request, msg);
    }
    // The default body is the fixed string
    // "Internal Server Error" to avoid CWE-209 information disclosure of
    // e.what() text (which routinely embeds file paths, SQL fragments,
    // internal identifiers, attacker-influenced input). The originating
    // message is still surfaced via the configured log_error callback
    // (see log_dispatch_error). Application code that needs the v1
    // verbose body (for development) must opt in via
    // create_webserver::expose_exception_messages(true).
    const auto status = http_utils::http_internal_server_error;
    if (parent->config.expose_exception_messages) {
        return http_response::string(std::string{msg}).with_status(status);
    }
    return http_response::string(
            std::string{constants::INTERNAL_SERVER_ERROR})
        .with_status(status);
}

void webserver_impl::log_dispatch_error(std::string_view msg) const noexcept {
    if (parent->config.log_error == nullptr) {
        return;
    }
    // msg is forwarded VERBATIM regardless
    // of create_webserver::expose_exception_messages. The error log is
    // the canonical destination for the verbatim exception text; only
    // the HTTP response body path is sanitized.
    //
    // Framework contract (CWE-532): msg may contain e.what() text from
    // a handler exception, which could include sensitive data (DB connection
    // strings, file paths, user-supplied input that triggered the exception).
    // Application code should sanitize or wrap exceptions that might expose
    // sensitive information before re-throwing them.
    //
    // A misbehaving user logger must not poison the catch from inside
    // the catch. Swallow any exception it throws; we have no recovery
    // beyond dropping the log line.
    try {
        parent->config.log_error(std::string(msg));
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
        // The user handler itself threw. Log generically
        // and return an empty-body 500. No exception escapes from here.
        log_dispatch_error("internal_error_handler threw; "
                           "sending hardcoded empty-body 500");
        return internal_error_page(mr, "", /*force_our=*/true);
    }
}

}  // namespace detail

}  // namespace httpserver
