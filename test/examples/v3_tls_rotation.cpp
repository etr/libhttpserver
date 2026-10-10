/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
// Repository-internal demonstration, built only with NATIVE_V3_TLS.
// This private header requires HTTPSERVER_COMPILATION and is not installed.
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <httpserver/detail/tls_credentials.hpp>

namespace {
std::string pem(const std::string& name) {
    std::ifstream input(std::string(TLS_TEST_DIR) + "/data/tls_credentials/" + name);
    if (!input) throw std::runtime_error("certificate fixture unavailable");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

httpserver::detail::tls_credentials_config credentials(const std::string& name) {
    httpserver::detail::tls_host_credentials host;
    host.host = name + ".example";
    host.certificate_chain_pem = pem(name + ".pem") + pem("intermediate.pem");
    host.private_key_pem = pem(name + "-key.pem");
    host.trust_roots_pem = pem("root.pem");
    host.alpn = {"h2", "http/1.1"};
    return {{host}, 0};
}
}  // namespace

int main() {
    try {
        httpserver::detail::tls_credentials_registry registry;
        if (!registry.replace(credentials("a")).ok()) return 1;
        auto old_selection = registry.acquire()->select_default();
        if (!registry.replace(credentials("b")).ok()) return 1;
        auto replacement = registry.acquire()->select_default();
        auto invalid = credentials("a");
        invalid.hosts[0].private_key_pem = "invalid key";
        if (registry.replace(invalid).ok()) return 1;
        auto active = registry.acquire();
        if (old_selection.snapshot->generation() != 1 || replacement.snapshot->generation() != 2
                || active != replacement.snapshot || old_selection.context == replacement.context) return 1;
        std::cout << "private rotation: old=" << old_selection.snapshot->generation()
                  << " active=" << replacement.snapshot->generation()
                  << " failed replacement preserved=" << active->generation() << '\n';
        // Independent verified TLS handshakes and surviving established
        // connections are exercised in unit/tls_credentials_rotation_test.cpp.
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
