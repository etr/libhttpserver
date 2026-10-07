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
     License along with this library; if not, see the file LICENSE in
     the distribution; if not, write to the Free Software Foundation,
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef SRC_HTTPSERVER_DETAIL_TLS_BUILD_PROBE_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_BUILD_PROBE_HPP_

#ifndef HTTPSERVER_COMPILATION
#error "httpserver/detail/tls_build_probe.hpp is internal; only include it when compiling libhttpserver"
#endif

#include <httpserver/features.hpp>

namespace httpserver::detail {
build_features tls_build_probe() noexcept;
}

#endif  // SRC_HTTPSERVER_DETAIL_TLS_BUILD_PROBE_HPP_
