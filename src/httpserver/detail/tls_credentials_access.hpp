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
class tls_credentials_access final {
 public:
    static std::shared_ptr<const tls_credentials_snapshot> acquire(const server::tls_credentials& credentials);
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_ACCESS_HPP_
