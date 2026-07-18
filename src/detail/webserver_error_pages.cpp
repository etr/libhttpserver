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

// Thin webserver_impl forwarders into the error_pages behavior service
// and the log_dispatch_error free function (DR-014 §4.11). The real logic
// moved to detail/error_pages.cpp and detail/dispatch_util.cpp. These
// forwarders keep the existing in-class call sites
// (not_found_page(mr) / log_dispatch_error(msg) / ...) compiling
// unchanged during the migration; they are removed once every caller is
// itself a service holding error_pages& / the config bag directly.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <string_view>

#include "httpserver/create_webserver.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/detail/dispatch_util.hpp"

namespace httpserver {
namespace detail {

http_response webserver_impl::not_found_page(detail::modded_request* mr) const {
    return errors_.not_found_page(mr);
}

http_response webserver_impl::method_not_allowed_page(
        detail::modded_request* mr) const {
    return errors_.method_not_allowed_page(mr);
}

http_response webserver_impl::internal_error_page(detail::modded_request* mr,
                                                  std::string_view msg,
                                                  bool force_our) const {
    return errors_.internal_error_page(mr, msg, force_our);
}

http_response webserver_impl::run_internal_error_handler_safely(
        detail::modded_request* mr,
        std::string_view msg) const {
    return errors_.run_internal_error_handler_safely(mr, msg);
}

void webserver_impl::log_dispatch_error(std::string_view msg) const noexcept {
    detail::log_dispatch_error(parent->config, msg);
}

}  // namespace detail
}  // namespace httpserver
