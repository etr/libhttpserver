/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <array>
#include <string>
#include <vector>
#include <httpserver/detail/http2_connection.hpp>
#include "./http2_fixture.hpp"
#include "./tls_acme_peer.hpp"
#include "./tls_acme_adapter.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
LT_BEGIN_SUITE(http2_tls_boundary_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_tls_boundary_suite)
LT_BEGIN_AUTO_TEST(http2_tls_boundary_suite, completed_handshake_publishes_actual_protocol_only)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (const auto& offers : std::vector<std::vector<std::string>>{{"h2"}, {"http/1.1"}, {}}) {
            hd::tls_credentials_registry registry;
            auto config = tls_test::credentials();
            config.hosts[0].alpn = offers;
            LT_ASSERT(registry.replace(config).ok());
            acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", offers);
            LT_CHECK(peer.server.negotiated_protocol() == hd::tls_negotiated_protocol::unknown);
            LT_ASSERT(peer.connect());
            auto expected = offers.empty() ? hd::tls_negotiated_protocol::none :
                (offers[0] == "h2" ? hd::tls_negotiated_protocol::h2 : hd::tls_negotiated_protocol::http1);
            LT_CHECK(peer.server.negotiated_protocol() == expected);
            LT_CHECK_EQ(peer.alpn(), offers.empty() ? "" : offers[0]);
        }
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(tls_test::credentials()).ok());
        acme_test::connection failed(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"h3"});
        LT_CHECK(!failed.connect());
        LT_CHECK(failed.server.negotiated_protocol() == hd::tls_negotiated_protocol::unknown);
    }
LT_END_AUTO_TEST(completed_handshake_publishes_actual_protocol_only)
LT_BEGIN_AUTO_TEST(http2_tls_boundary_suite, h2_adapter_decrypts_segmented_controls_and_encrypts_ack)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    acme_test::adapter_connection peer(registry.acquire()->select_default(), false);
    LT_CHECK(peer.tls->negotiated_protocol() == hd::tls_negotiated_protocol::unknown);
    LT_ASSERT(peer.connect());
    LT_ASSERT(peer.tls->negotiated_protocol() == hd::tls_negotiated_protocol::h2);
    hd::http2_connection connection(h2test::budget());
    auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(6, 0, 0, {1, 2, 0, 255, 4, 5, 6, 7}));
    std::size_t written = 0;
    LT_ASSERT(SSL_write_ex(peer.peer.get(), wire.data(), wire.size(), &written) == 1);
    LT_CHECK_EQ(written, wire.size());
    std::size_t total = 0;
    while (total < wire.size()) {
        std::array<std::byte, 5> bytes{};
        hd::read_operation read(peer.owner, 1, bytes);
        read.submit(*peer.tls);
        peer.drive();
        LT_ASSERT(read.state()->applied());
        auto n = read.state()->stored_result().transferred;
        LT_ASSERT(n > 0);
        total += n;
        auto input = std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), n);
        while (!input.empty()) {
            auto result = connection.feed(input);
            LT_ASSERT(!result.error);
            LT_ASSERT(result.consumed > 0);
            input = input.subspan(result.consumed);
        }
    }
    std::vector<std::uint8_t> expected, received;
    while (!connection.output().empty()) {
        auto out = connection.output();
        expected.insert(expected.end(), out.begin(), out.end());
        hd::write_operation write(peer.owner, 1, {reinterpret_cast<const std::byte*>(out.data()), out.size()});
        write.submit(*peer.tls);
        peer.drive();
        LT_ASSERT(write.state()->applied());
        LT_CHECK_EQ(write.state()->stored_result().transferred, out.size());
        connection.advance_output(out.size());
    }
    for (unsigned round = 0; round < 100 && received.size() < expected.size(); ++round) {
        std::array<std::uint8_t, 13> bytes{};
        std::size_t n = 0;
        int rc = SSL_read_ex(peer.peer.get(), bytes.data(), bytes.size(), &n);
        if (rc != 1) {
            LT_ASSERT(SSL_get_error(peer.peer.get(), rc) == SSL_ERROR_WANT_READ);
        }
        received.insert(received.end(), bytes.begin(), bytes.begin() + n);
        peer.drive();
    }
    LT_CHECK(received == expected);
    LT_CHECK_EQ(expected[3], 4);
    LT_CHECK_EQ(expected[expected.size() - 13], 1);
    LT_CHECK(std::equal(wire.end() - 8, wire.end(), received.end() - 8));
LT_END_AUTO_TEST(h2_adapter_decrypts_segmented_controls_and_encrypts_ack)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
