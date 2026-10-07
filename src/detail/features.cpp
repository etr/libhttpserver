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

#include "config.h"
#include <httpserver/features.hpp>

#ifdef NATIVE_V3_TLS
#include <httpserver/detail/tls_build_probe.hpp>
#endif

namespace httpserver {

build_features query_features() noexcept {
#ifdef NATIVE_V3_TLS
    static const auto features = detail::tls_build_probe();
    return features;
#else
    return {};
#endif
}

}  // namespace httpserver
