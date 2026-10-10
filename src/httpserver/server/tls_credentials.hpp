/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef SRC_HTTPSERVER_SERVER_TLS_CREDENTIALS_HPP_
#define SRC_HTTPSERVER_SERVER_TLS_CREDENTIALS_HPP_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <httpserver/http/outcome.hpp>

namespace httpserver::detail { class tls_credentials_access; }
namespace httpserver::server {
// Certificate-only TCP HTTP/1 credentials. Exact canonical DNS names; unknown
// or absent SNI selects default_host. ALPN may contain only http/1.1 (or be empty).
struct tls_host_credentials {
    std::string host;
    std::string certificate_chain_pem;
    std::string private_key_pem;
    std::vector<std::string> alpn{"http/1.1"};
};
struct tls_credentials_config {
    std::vector<tls_host_credentials> hosts;
    std::size_t default_host = 0;
};
// Copies share one store, never process-global state. replace builds and
// validates a complete candidate synchronously, then publishes atomically.
// Failure preserves the active generation. PEM input is not retained; caller
// buffers may die on return. Listeners retain the implementation; accepted
// sessions pin one complete generation through handshake and I/O retirement.
// TLS-off has identical declarations and returns not_supported from replace.
class tls_credentials final {
 public:
    tls_credentials();
    tls_credentials(const tls_credentials&) = default;
    tls_credentials& operator=(const tls_credentials&) = default;
    http::outcome replace(const tls_credentials_config& config);

 private:
    friend class httpserver::detail::tls_credentials_access;
    class impl;
    std::shared_ptr<impl> impl_;
};
}  // namespace httpserver::server
#endif  // SRC_HTTPSERVER_SERVER_TLS_CREDENTIALS_HPP_
