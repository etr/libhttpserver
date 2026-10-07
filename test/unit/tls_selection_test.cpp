/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <httpserver/detail/tls_session.hpp>
#include "./tls_credentials_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace {
using client_context = std::shared_ptr<SSL_CTX>;
using session_ptr = std::unique_ptr<SSL_SESSION, decltype(&SSL_SESSION_free)>;
client_context client(int version) {
    client_context ctx(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
    const auto root = tls_test::pem("data/tls_credentials/root.pem");
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(root.data(), static_cast<int>(root.size())), BIO_free);
    std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
    if (!cert || X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx.get()), cert.get()) != 1) throw std::runtime_error("test root unavailable");
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_min_proto_version(ctx.get(), version);
    SSL_CTX_set_max_proto_version(ctx.get(), version);
    SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_CLIENT);
    return ctx;
}
hd::tls_credentials_config profiles() {
    auto result = tls_test::credentials();
    result.hosts.push_back(tls_test::credentials("b").hosts[0]);
    result.hosts[1].alpn = {"http/1.1"};
    result.default_host = 1;
    return result;
}
unsigned u16(const unsigned char* p) { return (static_cast<unsigned>(p[0]) << 8) | p[1]; }
// Inspect received ServerHello (1.2) or EncryptedExtensions (1.3), rather
// than the peer's cached server name, to prove wire acknowledgement.
void received(int writing, int, int type, const void* data, std::size_t size, SSL*, void* arg) {
    if (writing || type != SSL3_RT_HANDSHAKE || size < 6) return;
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t pos = 4;
    if (bytes[0] == SSL3_MT_SERVER_HELLO) {
        if (size < 39) return;
        pos = 39 + bytes[38] + 3;
    } else if (bytes[0] != SSL3_MT_ENCRYPTED_EXTENSIONS) {
        return;
    }
    if (pos + 2 > size) return;
    pos += 2;
    while (pos + 4 <= size) {
        const auto extension = u16(bytes + pos);
        const auto length = u16(bytes + pos + 2);
        pos += 4;
        if (pos + length > size) return;
        if (extension == TLSEXT_TYPE_server_name) *static_cast<bool*>(arg) = true;
        pos += length;
    }
}
struct connection {
    hd::tls_session server;
    client_context ctx;
    std::unique_ptr<SSL, decltype(&SSL_free)> peer{nullptr, SSL_free};
    bool acknowledged = false;
    bool server_done = false;
    bool failed = false;
    connection(hd::tls_credentials_selection selected, client_context context, const char* name,
               const std::vector<std::string>& offers = {}, SSL_SESSION* ticket = nullptr)
        : server(std::move(selected), true), ctx(std::move(context)), peer(SSL_new(ctx.get()), SSL_free) {
        SSL_set_bio(peer.get(), BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
        SSL_set_connect_state(peer.get());
        SSL_set_msg_callback(peer.get(), received);
        SSL_set_msg_callback_arg(peer.get(), &acknowledged);
        if (name) SSL_set_tlsext_host_name(peer.get(), name);
        std::vector<unsigned char> wire;
        for (const auto& offer : offers) {
            wire.push_back(static_cast<unsigned char>(offer.size()));
            wire.insert(wire.end(), offer.begin(), offer.end());
        }
        if (!wire.empty()) SSL_set_alpn_protos(peer.get(), wire.data(), static_cast<unsigned>(wire.size()));
        if (ticket && SSL_set_session(peer.get(), ticket) != 1) throw std::runtime_error("test session unavailable");
    }
    ~connection() { SSL_set_shutdown(peer.get(), SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN); }
    bool transfer() {
        std::array<std::byte, 73> bytes{};
        bool moved = false;
        const auto output = server.drain(bytes);
        if (output) {
            BIO_write(SSL_get_rbio(peer.get()), bytes.data(), static_cast<int>(output));
            moved = true;
        }
        const auto capacity = std::min(server.input_capacity(), std::size_t{41});
        if (capacity && BIO_ctrl_pending(SSL_get_wbio(peer.get()))) {
            const auto count = BIO_read(SSL_get_wbio(peer.get()), bytes.data(), static_cast<int>(capacity));
            if (!server.feed(std::span(bytes).first(count))) throw std::runtime_error("test feed failed");
            moved = true;
        }
        return moved;
    }
    bool connect() {
        for (unsigned i = 0; i < 10000; ++i) {
            if (!server_done && !failed) {
                const auto result = server.handshake();
                server_done = result.state == hd::tls_session::progress::complete;
                failed = result.state == hd::tls_session::progress::failed;
            }
            const bool moved = transfer();
            ERR_clear_error();
            int rc = 0;
            if (!SSL_is_init_finished(peer.get())) {
                rc = SSL_do_handshake(peer.get());
            } else {
                std::array<char, 16> bytes{};
                std::size_t count = 0;
                rc = SSL_read_ex(peer.get(), bytes.data(), bytes.size(), &count);
            }
            if (rc != 1) {
                const auto error = SSL_get_error(peer.get(), rc);
                if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) return false;
            }
            if (!moved && !BIO_ctrl_pending(SSL_get_wbio(peer.get())) && !server.output_pending()) {
                return !failed && server_done && SSL_is_init_finished(peer.get());
            }
        }
        throw std::runtime_error("selection transport did not park");
    }
    void corrupt_name(unsigned mode) {
        SSL_do_handshake(peer.get());
        std::vector<unsigned char> hello(BIO_ctrl_pending(SSL_get_wbio(peer.get())));
        BIO_read(SSL_get_wbio(peer.get()), hello.data(), static_cast<int>(hello.size()));
        std::size_t pos = 43;
        pos += 1 + hello.at(pos);
        pos += 2 + u16(hello.data() + pos);
        pos += 1 + hello.at(pos);
        pos += 2;
        bool found = false;
        while (pos + 4 <= hello.size()) {
            const auto type = u16(hello.data() + pos);
            const auto size = u16(hello.data() + pos + 2);
            pos += 4;
            if (pos + size > hello.size()) throw std::runtime_error("test hello invalid");
            if (type == TLSEXT_TYPE_server_name) {
                if (mode == 0) hello.at(pos + 5) = 0;
                if (mode == 1) hello.at(pos + 1) ^= 1;
                if (mode == 2) hello.at(pos + 4) ^= 1;
                if (mode == 3) hello.at(pos + 2) = 1;
                if (mode == 4) {
                    const std::array<unsigned char, 12> duplicate{0, 0, 3, 'a', 'b', 'c', 0, 0, 3, 'd', 'e', 'f'};
                    std::copy(duplicate.begin(), duplicate.end(), hello.begin() + pos + 2);
                }
                found = true;
                break;
            }
            pos += size;
        }
        if (!found) throw std::runtime_error("test SNI missing");
        BIO_write(SSL_get_wbio(peer.get()), hello.data(), static_cast<int>(hello.size()));
    }
    std::int64_t serial() const { return ASN1_INTEGER_get(X509_get_serialNumber(SSL_get0_peer_certificate(peer.get()))); }
    std::string alpn() const {
        const unsigned char* value = nullptr;
        unsigned size = 0;
        SSL_get0_alpn_selected(peer.get(), &value, &size);
        return size ? std::string(reinterpret_cast<const char*>(value), size) : std::string{};
    }
    session_ptr ticket() { return {SSL_get1_session(peer.get()), SSL_SESSION_free}; }
    bool reused() const { return SSL_session_reused(peer.get()) == 1; }
};
}  // namespace
LT_BEGIN_SUITE(tls_selection_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_selection_suite)
LT_BEGIN_AUTO_TEST(tls_selection_suite, current_hello_selects_known_or_explicit_default_and_acknowledges_only_known)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(profiles()).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (const char* name : {"a.example", "A.ExAmPlE.", "b.example", "unknown.example", "sub.a.example", "example", "a.example.other", static_cast<const char*>(nullptr)}) {
            connection peer(registry.acquire()->select_default(), client(version), name);
            LT_ASSERT(peer.connect());
            const bool a = name && (std::string(name) == "a.example" || std::string(name) == "A.ExAmPlE.");
            LT_CHECK_EQ(peer.serial(), a ? 101L : 202L);
            LT_CHECK_EQ(peer.acknowledged, a || (name && std::string(name) == "b.example"));
        }
    }
LT_END_AUTO_TEST(current_hello_selects_known_or_explicit_default_and_acknowledges_only_known)
LT_BEGIN_AUTO_TEST(tls_selection_suite, invalid_dns_names_reject_handshake)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(profiles()).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (const char* name : {"-a.example", "a..example", "a_example", "a.example..", "*", "a/example"}) {
            connection peer(registry.acquire()->select_default(), client(version), name);
            LT_CHECK(!peer.connect());
        }
    }
LT_END_AUTO_TEST(invalid_dns_names_reject_handshake)
LT_BEGIN_AUTO_TEST(tls_selection_suite, alpn_uses_selected_server_preference_and_normal_http_protocols)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto config = profiles();
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        for (const char* name : {"a.example", "b.example", "unknown.example", static_cast<const char*>(nullptr)}) {
            connection peer(registry.acquire()->select_default(), client(version), name, {"http/1.1", "h2"});
            LT_ASSERT(peer.connect());
            LT_CHECK_EQ(peer.alpn(), name && std::string(name) == "a.example" ? "h2" : "http/1.1");
        }
        config.hosts[0].alpn = {"http/1.1", "h2"};
        LT_ASSERT(registry.replace(config).ok());
        connection reversed(registry.acquire()->select_default(), client(version), "a.example", {"h2", "http/1.1"});
        LT_ASSERT(reversed.connect());
        LT_CHECK_EQ(reversed.alpn(), "http/1.1");
        for (const auto& policy : std::vector<std::vector<std::string>>{{"h2"}, {"http/1.1"}, {"h3", "acme-tls/1"}, {}}) {
            config.hosts[0].alpn = policy;
            LT_ASSERT(registry.replace(config).ok());
            connection overlap(registry.acquire()->select_default(), client(version), "a.example", {"http/1.1", "h3", "acme-tls/1"});
            const bool http1 = std::find(policy.begin(), policy.end(), "http/1.1") != policy.end();
            LT_CHECK_EQ(overlap.connect(), http1 || policy.empty());
            if (http1 || policy.empty()) LT_CHECK_EQ(overlap.alpn(), http1 ? "http/1.1" : "");
            if (policy == std::vector<std::string>{"h2"}) {
                connection h2(registry.acquire()->select_default(), client(version), "a.example", {"h2"});
                LT_ASSERT(h2.connect());
                LT_CHECK_EQ(h2.alpn(), "h2");
            }
            connection absent(registry.acquire()->select_default(), client(version), "a.example");
            LT_CHECK_EQ(absent.connect(), http1 || policy.empty());
            connection unsupported(registry.acquire()->select_default(), client(version), "a.example", {"h3", "acme-tls/1"});
            LT_CHECK_EQ(unsupported.connect(), policy.empty());
        }
    }
LT_END_AUTO_TEST(alpn_uses_selected_server_preference_and_normal_http_protocols)
LT_BEGIN_AUTO_TEST(tls_selection_suite, real_tickets_reuse_only_the_current_selected_host_and_generation)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(profiles()).ok());
        auto ctx = client(version);
        connection first(registry.acquire()->select_default(), ctx, "a.example", {"h2", "http/1.1"});
        LT_ASSERT(first.connect());
        auto ticket = first.ticket();
        LT_ASSERT(ticket && SSL_SESSION_is_resumable(ticket.get()));
        LT_CHECK(SSL_SESSION_has_ticket(ticket.get()));
        LT_CHECK_EQ(SSL_SESSION_get_max_early_data(ticket.get()), 0U);
        connection same(registry.acquire()->select_default(), ctx, "a.example", {"http/1.1", "h2"}, ticket.get());
        LT_ASSERT(same.connect());
        LT_CHECK(same.reused());
        LT_CHECK_EQ(same.alpn(), "h2");
        LT_CHECK_EQ(same.acknowledged, version == TLS1_3_VERSION);
        auto renewed = same.ticket();
        LT_ASSERT(renewed && SSL_SESSION_is_resumable(renewed.get()));
        connection new_offer(registry.acquire()->select_default(), ctx, "a.example", {"http/1.1"}, renewed.get());
        LT_ASSERT(new_offer.connect());
        LT_CHECK(new_offer.reused());
        LT_CHECK_EQ(new_offer.alpn(), "http/1.1");
        connection default_first(registry.acquire()->select_default(), ctx, "b.example", {"http/1.1"});
        LT_ASSERT(default_first.connect());
        auto default_ticket = default_first.ticket();
        LT_ASSERT(default_ticket && SSL_SESSION_is_resumable(default_ticket.get()));
        for (const char* name : {"unknown.example", static_cast<const char*>(nullptr)}) {
            connection fallback(registry.acquire()->select_default(), ctx, name, {"http/1.1"}, default_ticket.get());
            LT_ASSERT(fallback.connect());
            LT_CHECK(fallback.reused());
            LT_CHECK_EQ(fallback.alpn(), "http/1.1");
            LT_CHECK(!fallback.acknowledged);
            default_ticket = fallback.ticket();
        }
        connection nondefault(registry.acquire()->select_default(), ctx, "a.example", {"h2"}, default_ticket.get());
        LT_ASSERT(nondefault.connect());
        LT_CHECK(!nondefault.reused());
        LT_CHECK_EQ(nondefault.serial(), std::int64_t{101});
        LT_CHECK_EQ(nondefault.alpn(), "h2");
        for (const char* name : {"b.example", "unknown.example", static_cast<const char*>(nullptr)}) {
            connection source(registry.acquire()->select_default(), ctx, "a.example", {"h2", "http/1.1"});
            LT_ASSERT(source.connect());
            auto unused = source.ticket();
            LT_ASSERT(unused && SSL_SESSION_is_resumable(unused.get()));
            connection changed(registry.acquire()->select_default(), ctx, name, {"h2", "http/1.1"}, unused.get());
            LT_ASSERT(changed.connect());
            LT_CHECK(!changed.reused());
            LT_CHECK_EQ(changed.serial(), 202L);
            LT_CHECK_EQ(changed.alpn(), "http/1.1");
            LT_CHECK_EQ(changed.acknowledged, name && std::string(name) == "b.example");
        }
        connection old_source(registry.acquire()->select_default(), ctx, "a.example", {"h2", "http/1.1"});
        LT_ASSERT(old_source.connect());
        auto old_ticket = old_source.ticket();
        LT_ASSERT(old_ticket && SSL_SESSION_is_resumable(old_ticket.get()));
        auto replacement = profiles();
        replacement.hosts[0] = tls_test::credentials("b").hosts[0];
        replacement.hosts[0].host = "a.example";
        replacement.hosts[0].alpn = {"http/1.1"};
        LT_ASSERT(registry.replace(replacement).ok());
        connection replaced(registry.acquire()->select_default(), ctx, "a.example", {"h2", "http/1.1"}, old_ticket.get());
        LT_ASSERT(replaced.connect());
        LT_CHECK(!replaced.reused());
        LT_CHECK_EQ(replaced.serial(), 202L);
        LT_CHECK_EQ(replaced.alpn(), "http/1.1");
    }
LT_END_AUTO_TEST(real_tickets_reuse_only_the_current_selected_host_and_generation)
LT_BEGIN_AUTO_TEST(tls_selection_suite, malformed_current_sni_and_embedded_nul_fail_before_completion)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(profiles()).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (unsigned mode = 0; mode < 5; ++mode) {
            connection peer(registry.acquire()->select_default(), client(version), "a.example");
            peer.corrupt_name(mode);
            LT_CHECK(!peer.connect());
            LT_CHECK(!peer.acknowledged);
        }
    }
LT_END_AUTO_TEST(malformed_current_sni_and_embedded_nul_fail_before_completion)
LT_BEGIN_AUTO_TEST(tls_selection_suite, pinned_and_concurrent_replacement_keep_certificate_and_alpn_together)
    hd::tls_credentials_registry registry;
    auto a = tls_test::credentials();
    auto b = tls_test::credentials("b");
    b.hosts[0].host = "a.example";
    b.hosts[0].alpn = {"http/1.1"};
    LT_ASSERT(registry.replace(a).ok());
    std::barrier ready(3), published(3), finished(3);
    std::atomic<unsigned> failures{0};
    auto observe = [&](hd::tls_credentials_selection selected, int version) {
        const bool old = selected.snapshot->hosts()[0].alpn[0] == "h2";
        try {
            connection peer(std::move(selected), client(version), "a.example", {"http/1.1", "h2"});
            if (!peer.connect() || peer.serial() != (old ? 101 : 202) || peer.alpn() != (old ? "h2" : "http/1.1") || !peer.acknowledged) ++failures;
        } catch (...) { ++failures; }
    };
    auto reader = [&](int version) {
        for (unsigned round = 0; round < 8; ++round) {
            auto delayed = registry.acquire()->select_default();
            ready.arrive_and_wait();
            published.arrive_and_wait();
            observe(std::move(delayed), version);
            observe(registry.acquire()->select_default(), version);
            finished.arrive_and_wait();
        }
    };
    std::thread tls12(reader, TLS1_2_VERSION), tls13(reader, TLS1_3_VERSION);
    for (unsigned round = 0; round < 8; ++round) {
        ready.arrive_and_wait();
        if (!registry.replace(round % 2 ? a : b).ok()) ++failures;
        const auto current = registry.acquire();
        auto invalid = a;
        invalid.hosts[0].private_key_pem = "invalid";
        if (registry.replace(invalid).ok() || registry.acquire() != current) ++failures;
        published.arrive_and_wait();
        if (!registry.replace(round % 2 ? b : a).ok()) ++failures;
        finished.arrive_and_wait();
    }
    tls12.join();
    tls13.join();
    LT_CHECK_EQ(failures.load(), 0U);
    LT_CHECK_EQ(registry.acquire()->generation(), std::uint64_t{17});
LT_END_AUTO_TEST(pinned_and_concurrent_replacement_keep_certificate_and_alpn_together)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
