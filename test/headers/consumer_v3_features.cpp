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

#include <cstdlib>
#include <iostream>
#include <string_view>

#include <httpserver/features.hpp>

int main(int argc, char**) {
    const auto features = httpserver::query_features();
    const char* mode = std::getenv("V3_TLS_MODE");
    const bool expected = argc > 1 || (mode && std::string_view(mode) == "yes");
    if (features.tls_provider != expected) return 1;
    if (features.tcp_tls || features.http2 || features.http3) return 2;
    if (features.provider_version.empty() == expected) return 3;
    std::cout << "Native provider: " << features.provider_version << '\n';
    return 0;
}
