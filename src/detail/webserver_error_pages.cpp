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

// The 404/405/500 synthesis moved to the error_pages behavior service
// (detail/error_pages.cpp, DR-014 §4.11) and their webserver_impl forwarders
// were removed once every caller became a service holding error_pages&
// directly. Only the log_dispatch_error forwarder remains: it is still called
// via impl_ptr->log_dispatch_error(...) from the v1 alias hooks in
// webserver_aliases.cpp, so the thin member survives (delegating to the
// detail::log_dispatch_error free function over the config bag).

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <string_view>

#include "httpserver/create_webserver.hpp"
#include "httpserver/detail/dispatch_util.hpp"

namespace httpserver {
namespace detail {

void webserver_impl::log_dispatch_error(std::string_view msg) const noexcept {
    detail::log_dispatch_error(parent->config, msg);
}

}  // namespace detail
}  // namespace httpserver
