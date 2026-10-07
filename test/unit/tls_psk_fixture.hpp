/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_TLS_PSK_FIXTURE_HPP_
#define TEST_UNIT_TLS_PSK_FIXTURE_HPP_
#include <chrono>
#include <cstring>
#include <memory>
#include <utility>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <httpserver/detail/tls_psk_runtime.hpp>
#include "./tls_mtls_fixture.hpp"
namespace psk_test {
inline std::vector<std::byte> bytes(const std::string& value) {
    return {reinterpret_cast<const std::byte*>(value.data()), reinterpret_cast<const std::byte*>(value.data() + value.size())};
}
inline hd::tls_credentials_config credentials(const std::shared_ptr<hd::tls_psk_runtime>& runtime, hd::psk_lookup lookup) {
    hd::tls_credentials_config config;
    hd::tls_host_credentials host;
    host.host = "psk.example";
    host.profile = httpserver::server::tls_profile::external_psk;
    host.alpn = {"http/1.1"};
    host.psk = hd::tls_psk_config{std::move(lookup), runtime};
    config.hosts.push_back(std::move(host));
    return config;
}
struct client_key {
    std::string identity;
    std::vector<std::byte> key;
    bool early = false;
    static unsigned legacy(SSL* ssl, const char*, char* identity, unsigned capacity, unsigned char* key, unsigned maximum) {
        const auto* self = static_cast<client_key*>(SSL_get_app_data(ssl));
        if (!self || self->identity.size() + 1 > capacity || self->key.size() > maximum) return 0;
        std::memcpy(identity, self->identity.c_str(), self->identity.size() + 1);
        std::memcpy(key, self->key.data(), self->key.size());
        return static_cast<unsigned>(self->key.size());
    }
    static int session(SSL* ssl, const EVP_MD*, const unsigned char** identity, std::size_t* size, SSL_SESSION** out) {
        auto* self = static_cast<client_key*>(SSL_get_app_data(ssl));
        if (!self) return 0;
        *out = SSL_SESSION_new();
        const unsigned char cipher_id[] = {0x13, 0x01};
        const auto* cipher = SSL_CIPHER_find(ssl, cipher_id);
        if (!*out || !cipher || SSL_SESSION_set1_master_key(*out, reinterpret_cast<const unsigned char*>(self->key.data()), self->key.size()) != 1 ||
            SSL_SESSION_set_cipher(*out, cipher) != 1 || SSL_SESSION_set_protocol_version(*out, TLS1_3_VERSION) != 1 ||
            SSL_SESSION_set_max_early_data(*out, self->early ? 16384 : 0) != 1) {
            SSL_SESSION_free(*out);
            *out = nullptr;
            return 0;
        }
        *identity = reinterpret_cast<const unsigned char*>(self->identity.data());
        *size = self->identity.size();
        return 1;
    }
};
inline mtls_test::client_context client(int version) {
    auto ctx = mtls_test::client(version);
    SSL_CTX_set_cipher_list(ctx.get(), "PSK-AES128-GCM-SHA256");
    SSL_CTX_set_ciphersuites(ctx.get(), "TLS_AES_128_GCM_SHA256");
    SSL_CTX_set_psk_client_callback(ctx.get(), client_key::legacy);
    SSL_CTX_set_psk_use_session_callback(ctx.get(), client_key::session);
    return ctx;
}
struct connection : mtls_test::adapter_connection {
    client_key material;
    explicit connection(hd::tls_credentials_selection selected, int version, std::string identity, std::vector<std::byte> key,
                        const char* host = "psk.example", std::chrono::milliseconds timeout = std::chrono::seconds(2))
        : adapter_connection(std::move(selected), client(version)), material{std::move(identity), std::move(key)} {
        SSL_set_app_data(peer.get(), &material);
        if (host) SSL_set_tlsext_host_name(peer.get(), host);
        handshake = hd::tls_handshake_operation(owner, 1, std::chrono::steady_clock::now() + timeout);
    }
    void tick() {
        ex.run_pending();
        for (const auto& op : raw.pending_ops()) transfer(op);
        advance_peer();
    }
    template<class Predicate> bool until(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            tick();
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    bool connect() {
        handshake.submit(*tls);
        until([&] {
            const bool terminal = peer_failed || SSL_is_init_finished(peer.get()) ||
                handshake.state()->stored_result().code != httpserver::http::outcome_code::ok;
            return handshake.state()->applied() && terminal;
        });
        return handshake.state()->applied() && handshake.state()->stored_result().code == httpserver::http::outcome_code::ok && SSL_is_init_finished(peer.get());
    }
    bool exchange() {
        const std::string text = "authenticated request";
        std::array<std::byte, 64> buffer{};
        hd::read_operation read(owner, 1, buffer);
        read.submit(*tls);
        std::size_t written = 0;
        if (SSL_write_ex(peer.get(), text.data(), text.size(), &written) != 1 || written != text.size()) return false;
        const bool applied = until([&] { return read.state()->applied(); });
        if (!applied) return false;
        const auto result = read.state()->stored_result();
        return result.code == httpserver::http::outcome_code::ok && result.transferred == text.size() &&
            std::memcmp(buffer.data(), text.data(), text.size()) == 0;
    }
};
}  // namespace psk_test
#endif  // TEST_UNIT_TLS_PSK_FIXTURE_HPP_
