/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <httpserver/server/tls_credentials.hpp>

namespace {
std::string pem(const std::string& name) {
    std::ifstream input(std::string(TLS_TEST_DIR) + "/data/tls_credentials/" + name);
    if (!input) throw std::runtime_error("certificate fixture unavailable");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

httpserver::server::tls_credentials_config credentials(const std::string& name) {
    httpserver::server::tls_host_credentials host;
    host.host = name + ".example";
    host.certificate_chain_pem = pem(name + ".pem") + pem("intermediate.pem");
    host.private_key_pem = pem(name + "-key.pem");
    return {{host}, 0};
}
}  // namespace

int main() {
    httpserver::server::tls_credentials store;
    if (!store.replace(credentials("a")).ok()) return 1;
    if (!store.replace(credentials("b")).ok()) return 1;
    auto invalid = credentials("a");
    invalid.hosts[0].private_key_pem = "invalid key";
    const auto rejected = store.replace(invalid);
    if (rejected.code() != httpserver::http::outcome_code::invalid_argument) return 1;
    std::cout << "public certificate publication: valid replacement accepted; invalid replacement rejected\n";
    // This demonstrates typed configuration outcomes. Installed wire lifetime
    // evidence belongs to check-v3-public-tls.py; TASK-194 expands the profiles.
    return 0;
}
