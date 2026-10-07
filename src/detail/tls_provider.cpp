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

#include <httpserver/detail/tls_build_probe.hpp>

#include <openssl/crypto.h>
#include <openssl/ssl.h>

namespace httpserver::detail {

build_features tls_build_probe() noexcept {
    // Exact matching prevents a selected library silently replacing the build
    // provider. Configure checks locally; this also checks cross-built targets.
    if (OpenSSL_version_num() != OPENSSL_VERSION_NUMBER
        || OPENSSL_version_pre_release()[0] != 0) return {};
    SSL_CTX* context = SSL_CTX_new(TLS_method());
    if (context == nullptr) return {};
    SSL* session = SSL_new(context);
    const bool ready = session != nullptr;
    SSL_free(session);
    SSL_CTX_free(context);
    if (!ready) return {};
    return {true, false, false, false, OpenSSL_version(OPENSSL_VERSION)};
}

}  // namespace httpserver::detail
