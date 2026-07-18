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

// error_pages behavior service (DR-014 §4.11). Logic moved verbatim out
// of the former detail/webserver_error_pages.cpp; that TU now holds only
// the thin webserver_impl forwarders during the migration.

#include "httpserver/detail/error_pages.hpp"

#include <string>
#include <string_view>

#include "httpserver/constants.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/detail/dispatch_util.hpp"
#include "httpserver/detail/modded_request.hpp"

namespace httpserver {

using httpserver::http::http_utils;

namespace detail {

http_response error_pages::not_found_page(modded_request* mr) const {
    if (config_.not_found_handler != nullptr) {
        return config_.not_found_handler(*mr->request);
    }
    return http_response::string(std::string{constants::NOT_FOUND_ERROR})
        .with_status(http_utils::http_not_found);
}

http_response error_pages::method_not_allowed_page(modded_request* mr) const {
    if (config_.method_not_allowed_handler != nullptr) {
        return config_.method_not_allowed_handler(*mr->request);
    }
    return http_response::string(std::string{constants::METHOD_ERROR})
        .with_status(http_utils::http_method_not_allowed);
}

http_response error_pages::internal_error_page(modded_request* mr,
                                               std::string_view msg,
                                               bool force_our) const {
    // The double-fault fallback. Used when the user-supplied
    // internal_error_handler itself threw or when the belt-and-suspenders
    // site after get_raw_response_with_fallback fires. The body is
    // intentionally empty and the message is intentionally ignored.
    if (force_our) {
        return http_response::empty()
            .with_status(http_utils::http_internal_server_error);
    }
    // Invoke the user handler with the originating message.
    if (config_.internal_error_handler != nullptr) {
        return config_.internal_error_handler(*mr->request, msg);
    }
    // The default body is the fixed string "Internal Server Error" to
    // avoid CWE-209 information disclosure of e.what() text (which
    // routinely embeds file paths, SQL fragments, internal identifiers,
    // attacker-influenced input). The originating message is still
    // surfaced via the configured log_error callback (see
    // log_dispatch_error). Application code that needs the v1 verbose body
    // (for development) must opt in via
    // create_webserver::expose_exception_messages(true).
    const auto status = http_utils::http_internal_server_error;
    if (config_.expose_exception_messages) {
        return http_response::string(std::string{msg}).with_status(status);
    }
    return http_response::string(
            std::string{constants::INTERNAL_SERVER_ERROR})
        .with_status(status);
}

http_response error_pages::run_internal_error_handler_safely(
    modded_request* mr,
    std::string_view msg) const {
    try {
        return internal_error_page(mr, msg, /*force_our=*/false);
    } catch (...) {
        // The user handler itself threw. Log generically and return an
        // empty-body 500. No exception escapes from here.
        log_dispatch_error(config_,
                           "internal_error_handler threw; "
                           "sending hardcoded empty-body 500");
        return internal_error_page(mr, "", /*force_our=*/true);
    }
}

}  // namespace detail
}  // namespace httpserver
