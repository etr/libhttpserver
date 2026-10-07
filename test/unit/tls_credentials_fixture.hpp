/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_TLS_CREDENTIALS_FIXTURE_HPP_
#define TEST_UNIT_TLS_CREDENTIALS_FIXTURE_HPP_
#include <string>
#include <httpserver/detail/tls_credentials.hpp>
#include "./tls_io_fixture.hpp"
namespace tls_test {
inline hd::tls_credentials_config credentials(const char* name = "a") {
    const std::string base = std::string("data/tls_credentials/") + name;
    hd::tls_host_credentials host;
    host.host = std::string(name) + ".example";
    host.certificate_chain_pem = pem((base + ".pem").c_str()) + pem("data/tls_credentials/intermediate.pem");
    host.private_key_pem = pem((base + "-key.pem").c_str());
    host.trust_roots_pem = pem("data/tls_credentials/root.pem");
    host.alpn = {"h2", "http/1.1"};
    return {{host}, 0};
}
}  // namespace tls_test
#endif  // TEST_UNIT_TLS_CREDENTIALS_FIXTURE_HPP_
