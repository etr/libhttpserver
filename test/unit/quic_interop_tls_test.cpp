/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <sys/stat.h>
#include <cstdlib>
#include <filesystem>  // NOLINT(build/c++17): C++20 tests.
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "./quic_tls_peer.hpp"
#include "test/support/http3_network_owner.hpp"
#include "./littletest.hpp"
LT_BEGIN_SUITE(interop_tls_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(interop_tls_suite)
LT_BEGIN_AUTO_TEST(interop_tls_suite, exact_hq_offer_and_explicit_secret_export)
    const auto path = std::filesystem::temp_directory_path() / "task163-tls.keys";
    std::filesystem::remove(path);
    setenv("SSLKEYLOGFILE", path.c_str(), 1);
    unsetenv("LIBHTTPSERVER_INTEROP_TEST_MODE");
    hd::tls_credentials_registry registry;
    auto config = quic_test::credentials();
    config.hosts[0].alpn = {"hq-interop"};
    LT_ASSERT(registry.replace(config).ok());
    quic_test::connection disabled(registry.acquire()->select_default(), quic_test::client_context(), "a.example", {"hq-interop"});
    LT_CHECK(!disabled.connect());
    LT_CHECK(!std::filesystem::exists(path));
    setenv("LIBHTTPSERVER_INTEROP_TEST_MODE", "1", 1);
    quic_test::connection enabled(registry.acquire()->select_default(), quic_test::client_context(), "a.example", {"hq-interop"});
    LT_ASSERT(enabled.connect());
    LT_CHECK(enabled.client.alpn() == "hq-interop");
    LT_CHECK(enabled.server.negotiated_protocol() == hd::tls_negotiated_protocol::other);
    LT_CHECK(enabled.client.early_secrets == 0);
    std::ifstream keys(path);
    const std::string material{std::istreambuf_iterator<char>(keys), {}};
    LT_CHECK(material.find("SERVER_TRAFFIC_SECRET_0 ") != std::string::npos);
    LT_CHECK(material.find("CLIENT_HANDSHAKE_TRAFFIC_SECRET ") != std::string::npos);
    struct stat mode{};
    LT_ASSERT(stat(path.c_str(), &mode) == 0);
    LT_CHECK((mode.st_mode & 0777) == 0600);
    for (const auto& offers : std::vector<std::vector<std::string>>{{}, {"h2"}, {"hq"}}) {
        quic_test::connection rejected(registry.acquire()->select_default(), quic_test::client_context(), "a.example", offers);
        LT_CHECK(!rejected.connect());
    }
    unsetenv("LIBHTTPSERVER_INTEROP_TEST_MODE");
    unsetenv("SSLKEYLOGFILE");
    std::filesystem::remove(path);
LT_END_AUTO_TEST(exact_hq_offer_and_explicit_secret_export)
LT_BEGIN_AUTO_TEST(interop_tls_suite, partial_initial_keeps_file_pump_parked_before_negotiated_flow)
    setenv("LIBHTTPSERVER_INTEROP_TEST_MODE", "1", 1);
    hd::tls_credentials_registry registry;
    auto config = quic_test::credentials();
    config.hosts[0].alpn = {"hq-interop"};
    LT_ASSERT(registry.replace(config).ok());
    quic_test::peer client(quic_test::client_context(), "a.example", {"hq-interop"}, nullptr, false);
    LT_ASSERT(client.step());
    const std::array<std::byte, 12> cid{std::byte{1}};
    std::vector<std::byte> payload(1200), scratch(1600);
    LT_ASSERT(hd::encode_quic_frame(hd::quic_crypto_frame{0, std::span(client.output[0]).first(100)}, payload,
                                 {hd::quic_packet_kind::initial, hd::quic_endpoint_role::client}).code == hd::quic_codec_code::ok);
    hd::quic_initial_keys keys;
    LT_ASSERT(hd::derive_quic_initial_keys(cid, keys) == hd::quic_crypto_code::ok);
    hd::quic_packet_write write;
    write.destination = cid;
    write.payload = payload;
    auto packet = std::make_shared<hd::io_datagram>();
    packet->bytes.resize(1600);
    auto encoded = hd::protect_quic_packet(keys.client, write, packet->bytes, scratch);
    LT_ASSERT(encoded.code == hd::quic_crypto_code::ok);
    packet->bytes.resize(encoded.consumed);
    packet->peer.peer.address = *httpserver::net::parse_address("127.0.0.1");
    packet->peer.peer.port = 4433;
    packet->socket_id = 1;
    hd::quic_admission_limits limits;
    limits.require_retry = false;
    limits.cid_length = 12;
    hd::quic_server_admission admission(limits);
    auto pending = admission.receive(packet, 0);
    auto facts = admission.inspect(pending.pending);
    LT_ASSERT(facts);
    httpserver::manual_executor executor;
    httpserver::server::route_registry routes;
    const auto root = std::filesystem::temp_directory_path();
    h3net::connection owner(registry.acquire()->select_default(), *facts, routes, executor, 1, root.string());
    owner.on_datagram(packet);
    owner.tick(h3net::clock_type::now());
    LT_CHECK(owner.failure().empty());
    LT_CHECK(!owner.connected());
    unsetenv("LIBHTTPSERVER_INTEROP_TEST_MODE");
LT_END_AUTO_TEST(partial_initial_keeps_file_pump_parked_before_negotiated_flow)
LT_BEGIN_AUTO_TEST(interop_tls_suite, secret_export_rejects_symlinks_and_fails_closed_at_size_bound)
    const auto root = std::filesystem::temp_directory_path() / "task163-keylog-bound";
    std::filesystem::remove_all(root);
    std::filesystem::create_directory(root);
    const auto destination = root / "destination", path = root / "keys";
    std::ofstream(destination).write("original", 8);
    std::filesystem::create_symlink(destination, path);
    setenv("LIBHTTPSERVER_INTEROP_TEST_MODE", "1", 1);
    setenv("SSLKEYLOGFILE", path.c_str(), 1);
    hd::tls_credentials_registry registry;
    auto config = quic_test::credentials();
    config.hosts[0].alpn = {"hq-interop"};
    LT_ASSERT(registry.replace(config).ok());
    bool rejected = false;
    try {
        quic_test::connection peer(registry.acquire()->select_default(), quic_test::client_context(), "a.example", {"hq-interop"});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    LT_CHECK(rejected);
    LT_CHECK(std::filesystem::file_size(destination) == 8);
    std::filesystem::remove(path);
    std::ofstream(path).write(std::string(1048576, 'x').data(), 1048576);
    quic_test::connection full(registry.acquire()->select_default(), quic_test::client_context(), "a.example", {"hq-interop"});
    LT_CHECK(!full.connect());
    LT_CHECK(std::filesystem::file_size(path) == 1048576);
    unsetenv("LIBHTTPSERVER_INTEROP_TEST_MODE");
    unsetenv("SSLKEYLOGFILE");
    std::filesystem::remove_all(root);
LT_END_AUTO_TEST(secret_export_rejects_symlinks_and_fails_closed_at_size_bound)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
