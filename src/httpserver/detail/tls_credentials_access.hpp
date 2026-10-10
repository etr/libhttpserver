/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "tls_credentials_access.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_ACCESS_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_ACCESS_HPP_
#include <memory>
#include <httpserver/server/tls_credentials.hpp>
namespace httpserver::detail {
class tls_credentials_snapshot;
struct tls_psk_config;
struct tls_credentials_binding {
    server::tls_profile profile;
    server::tls_client_certificate_mode mode;
    bool http11;
};
class tls_credentials_access final {
 public:
    static tls_psk_config psk(const server::tls_psk_config& input);
    static http::outcome bind(const server::tls_credentials& credentials, tls_credentials_binding policy,
                              std::shared_ptr<const tls_credentials_binding>& lease);
    static std::shared_ptr<const tls_credentials_snapshot> acquire(const server::tls_credentials& credentials);
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_ACCESS_HPP_
