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

#ifndef SRC_HTTPSERVER_FEATURES_HPP_
#define SRC_HTTPSERVER_FEATURES_HPP_

#include <string_view>

namespace httpserver {

// Provider availability is separate from operational protocol transports.
struct build_features {
    bool tls_provider = false;
    bool tcp_tls = false;
    bool http2 = false;
    bool http3 = false;
    // Process-lifetime storage; empty when the provider is unavailable.
    std::string_view provider_version;
};

build_features query_features() noexcept;

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_FEATURES_HPP_
