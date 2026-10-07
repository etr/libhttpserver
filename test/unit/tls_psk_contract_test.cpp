/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include "./littletest.hpp"

namespace {
using context_ptr = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
using ssl_ptr = std::unique_ptr<SSL, decltype(&SSL_free)>;
constexpr std::array<unsigned char, 16> test_key{1, 2, 3, 4};

void require(bool condition) {
    if (!condition) throw std::runtime_error("provider fixture setup failed");
}

struct peers {
    context_ptr server_context{SSL_CTX_new(TLS_server_method()), SSL_CTX_free};
    context_ptr client_context{SSL_CTX_new(TLS_client_method()), SSL_CTX_free};
    ssl_ptr server{nullptr, SSL_free};
    ssl_ptr client{nullptr, SSL_free};

    explicit peers(int version) {
        require(server_context && client_context);
        for (auto* ctx : {server_context.get(), client_context.get()}) {
            require(SSL_CTX_set_min_proto_version(ctx, version) == 1);
            require(SSL_CTX_set_max_proto_version(ctx, version) == 1);
        }
    }

    void start() {
        server.reset(SSL_new(server_context.get()));
        client.reset(SSL_new(client_context.get()));
        require(server && client);
        for (auto* ssl : {server.get(), client.get()}) {
            BIO* input = BIO_new(BIO_s_mem());
            BIO* output = BIO_new(BIO_s_mem());
            if (!input || !output) {
                BIO_free(input);
                BIO_free(output);
                require(false);
            }
            SSL_set_bio(ssl, input, output);
        }
        SSL_set_accept_state(server.get());
        SSL_set_connect_state(client.get());
    }

    static void transfer(SSL* from, SSL* to) {
        std::array<unsigned char, 4096> bytes{};
        for (int count; (count = BIO_read(SSL_get_wbio(from), bytes.data(), bytes.size())) > 0;) {
            require(BIO_write(SSL_get_rbio(to), bytes.data(), count) == count);
        }
    }

    static int step(SSL* ssl) {
        ERR_clear_error();
        const int result = SSL_do_handshake(ssl);
        return result == 1 ? SSL_ERROR_NONE : SSL_get_error(ssl, result);
    }

    bool connect() {
        for (unsigned round = 0; round < 64; ++round) {
            const int client_error = step(client.get());
            transfer(client.get(), server.get());
            const int server_error = step(server.get());
            transfer(server.get(), client.get());
            if (SSL_is_init_finished(server.get()) && SSL_is_init_finished(client.get())) return true;
            if (client_error != SSL_ERROR_NONE && client_error != SSL_ERROR_WANT_READ) return false;
            if (server_error != SSL_ERROR_NONE && server_error != SSL_ERROR_WANT_READ) return false;
        }
        return false;
    }
};

struct hello_state {
    unsigned calls{0};
    bool resume{false};
};

int hello(SSL*, int*, void* arg) {
    auto& state = *static_cast<hello_state*>(arg);
    ++state.calls;
    return state.resume ? SSL_CLIENT_HELLO_SUCCESS : SSL_CLIENT_HELLO_RETRY;
}

struct psk_state {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{false};
    bool released{false};
    bool wait_expired{false};
    bool identity_matches{false};
    unsigned maximum_key_bytes{0};
    std::thread::id callback_thread;
};

unsigned client_psk(SSL*, const char*, char* identity, unsigned identity_capacity,
                    unsigned char* key, unsigned key_capacity) {
    if (identity_capacity < sizeof("test-identity") || key_capacity < test_key.size()) return 0;
    std::memcpy(identity, "test-identity", sizeof("test-identity"));
    std::memcpy(key, test_key.data(), test_key.size());
    return test_key.size();
}

unsigned server_psk(SSL* ssl, const char* identity, unsigned char* key, unsigned capacity) {
    auto& state = *static_cast<psk_state*>(SSL_get_app_data(ssl));
    std::unique_lock lock(state.mutex);
    state.callback_thread = std::this_thread::get_id();
    state.maximum_key_bytes = capacity;
    state.identity_matches = std::strcmp(identity, "test-identity") == 0;
    state.entered = true;
    state.changed.notify_all();
    state.wait_expired = !state.changed.wait_for(lock, std::chrono::seconds{3}, [&] { return state.released; });
    if (state.wait_expired || capacity < test_key.size()) return 0;
    std::memcpy(key, test_key.data(), test_key.size());
    return test_key.size();
}
}  // namespace

LT_BEGIN_SUITE(tls_psk_contract_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_psk_contract_suite)

LT_BEGIN_AUTO_TEST(tls_psk_contract_suite, client_hello_retry_suspends_and_resumes)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        peers peer(version);
        const std::string base = std::string(TLS_TEST_DIR) + "/data/tls_credentials/a";
        LT_ASSERT(SSL_CTX_use_certificate_chain_file(peer.server_context.get(), (base + ".pem").c_str()) == 1);
        LT_ASSERT(SSL_CTX_use_PrivateKey_file(peer.server_context.get(), (base + "-key.pem").c_str(), SSL_FILETYPE_PEM) == 1);
        hello_state state;
        SSL_CTX_set_client_hello_cb(peer.server_context.get(), hello, &state);
        peer.start();
        LT_CHECK_EQ(peers::step(peer.client.get()), SSL_ERROR_WANT_READ);
        peers::transfer(peer.client.get(), peer.server.get());
        LT_CHECK_EQ(peers::step(peer.server.get()), SSL_ERROR_WANT_CLIENT_HELLO_CB);
        LT_CHECK_EQ(state.calls, 1u);
        LT_CHECK(!SSL_is_init_finished(peer.server.get()));
        state.resume = true;
        LT_CHECK(peer.connect());
        LT_CHECK_EQ(state.calls, 2u);
    }
LT_END_AUTO_TEST(client_hello_retry_suspends_and_resumes)

LT_BEGIN_AUTO_TEST(tls_psk_contract_suite, tls12_lookup_blocks_the_calling_thread_until_callback_returns)
    peers peer(TLS1_2_VERSION);
    for (auto* ctx : {peer.server_context.get(), peer.client_context.get()}) {
        LT_ASSERT(SSL_CTX_set_cipher_list(ctx, "PSK-AES128-GCM-SHA256") == 1);
    }
    SSL_CTX_set_psk_client_callback(peer.client_context.get(), client_psk);
    SSL_CTX_set_psk_server_callback(peer.server_context.get(), server_psk);
    peer.start();
    psk_state state;
    SSL_set_app_data(peer.server.get(), &state);
    LT_ASSERT_EQ(peers::step(peer.client.get()), SSL_ERROR_WANT_READ);
    peers::transfer(peer.client.get(), peer.server.get());
    LT_ASSERT_EQ(peers::step(peer.server.get()), SSL_ERROR_WANT_READ);
    peers::transfer(peer.server.get(), peer.client.get());
    LT_ASSERT_EQ(peers::step(peer.client.get()), SSL_ERROR_WANT_READ);
    peers::transfer(peer.client.get(), peer.server.get());
    std::atomic<bool> returned{false};
    int server_error = SSL_ERROR_SSL;
    std::thread worker([&] {
        server_error = peers::step(peer.server.get());
        returned = true;
        state.changed.notify_all();
    });
    const auto handshake_thread = worker.get_id();
    bool entered;
    bool outstanding;
    {
        std::unique_lock lock(state.mutex);
        state.changed.wait_for(lock, std::chrono::seconds{3}, [&] { return state.entered || returned.load(); });
        entered = state.entered;
        outstanding = !returned.load();
        state.released = true;
    }
    state.changed.notify_all();
    worker.join();
    LT_CHECK(entered);
    LT_CHECK(outstanding);
    LT_CHECK(!state.wait_expired);
    LT_CHECK(state.callback_thread == handshake_thread);
    LT_CHECK(state.callback_thread != std::this_thread::get_id());
    LT_CHECK(state.identity_matches);
    LT_CHECK_EQ(state.maximum_key_bytes, 512u);
    LT_CHECK_EQ(server_error, SSL_ERROR_NONE);
    peers::transfer(peer.server.get(), peer.client.get());
    LT_CHECK(peer.connect());
    LT_CHECK_EQ(SSL_version(peer.server.get()), TLS1_2_VERSION);
LT_END_AUTO_TEST(tls12_lookup_blocks_the_calling_thread_until_callback_returns)

LT_BEGIN_AUTO_TEST(tls_psk_contract_suite, tls13_session_key_capacity_is_512_bytes)
    std::unique_ptr<SSL_SESSION, decltype(&SSL_SESSION_free)> session(SSL_SESSION_new(), SSL_SESSION_free);
    LT_ASSERT(session);
    std::array<unsigned char, 513> key{};
    LT_CHECK_EQ(SSL_SESSION_set1_master_key(session.get(), key.data(), 512), 1);
    LT_CHECK_EQ(SSL_SESSION_get_master_key(session.get(), nullptr, 0), std::size_t{512});
    LT_CHECK_EQ(SSL_SESSION_set1_master_key(session.get(), key.data(), 513), 0);
    LT_CHECK_EQ(SSL_SESSION_get_master_key(session.get(), nullptr, 0), std::size_t{512});
LT_END_AUTO_TEST(tls13_session_key_capacity_is_512_bytes)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
