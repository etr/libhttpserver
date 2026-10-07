/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <httpserver/detail/http2_connection.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/exchange.hpp>
#include "./http2_request_fixture.hpp"
#include "./http2_websocket_fixture.hpp"
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
LT_BEGIN_AUTO_TEST(http2_tls_boundary_suite, negotiated_h2_routes_segmented_heads_and_independent_responses)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    acme_test::adapter_connection peer(registry.acquire()->select_default(), false);
    LT_ASSERT(peer.connect());
    LT_ASSERT(peer.tls->negotiated_protocol() == hd::tls_negotiated_protocol::h2);
    auto budget = h2test::budget();
    httpserver::server::route_registry routes;
    LT_ASSERT(httpserver::server::route_registry::create(budget, routes).ok());
    httpserver::resume_signal parked;
    unsigned calls = 0;
    LT_ASSERT(routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/first",
        [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++calls; co_await parked.wait();
            x.respond(httpserver::http::status::from_code(201), {}); co_return;
        }).ok());
    LT_ASSERT(routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/second",
        [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++calls; x.respond(httpserver::http::status::from_code(204), {}); co_return;
        }).ok());
    httpserver::manual_executor executor;
    hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget);
    auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get("/first"))));
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get("/second"))));
    std::size_t written = 0;
    LT_ASSERT(SSL_write_ex(peer.peer.get(), wire.data(), wire.size(), &written) == 1);
    LT_ASSERT_EQ(written, wire.size());
    std::size_t total = 0;
    while (total < wire.size()) {
        std::array<std::byte, 5> bytes{};
        hd::read_operation read(peer.owner, 1, bytes);
        read.submit(*peer.tls); peer.drive();
        LT_ASSERT(read.state()->applied());
        auto n = read.state()->stored_result().transferred;
        LT_ASSERT(n > 0); total += n;
        LT_ASSERT(h2test::feed(engine, {reinterpret_cast<const std::uint8_t*>(bytes.data()), n}));
    }
    executor.run_pending(); LT_CHECK_EQ(calls, 2u);
    std::vector<std::uint8_t> received;
    for (unsigned turn = 0; turn < 2; ++turn) {
        if (turn) {
            parked.signal(); executor.run_pending();
        }
        auto plain = h2test::output(engine, 3);
        std::size_t transferred = 0;
        while (transferred < plain.size()) {
            const auto n = std::min<std::size_t>(7, plain.size() - transferred);
            auto out = std::span(plain).subspan(transferred, n);
            hd::write_operation write(peer.owner, 1, {reinterpret_cast<const std::byte*>(out.data()), out.size()});
            write.submit(*peer.tls); peer.drive();
            LT_ASSERT(write.state()->applied());
            LT_ASSERT_EQ(write.state()->stored_result().transferred, n);
            transferred += n;
        }
        const auto expected = received.size() + plain.size();
        for (unsigned round = 0; round < 1000 && received.size() < expected; ++round) {
            std::array<std::uint8_t, 13> bytes{};
            std::size_t n = 0;
            int rc = SSL_read_ex(peer.peer.get(), bytes.data(), bytes.size(), &n);
            if (rc != 1) LT_ASSERT(SSL_get_error(peer.peer.get(), rc) == SSL_ERROR_WANT_READ);
            received.insert(received.end(), bytes.begin(), bytes.begin() + n); peer.drive();
        }
        LT_ASSERT_EQ(received.size(), expected);
        auto replies = h2test::responses(received);
        LT_ASSERT_EQ(replies.size(), turn + 1);
        LT_CHECK_EQ(replies[0].stream, 3u); LT_CHECK_EQ(replies[0].fields[0].value, "204");
        if (turn) {
            LT_CHECK_EQ(replies[1].stream, 1u); LT_CHECK_EQ(replies[1].fields[0].value, "201");
        }
    }
LT_END_AUTO_TEST(negotiated_h2_routes_segmented_heads_and_independent_responses)
LT_BEGIN_AUTO_TEST(http2_tls_boundary_suite, segmented_tls_body_waits_for_stream_credit_then_sends_exact_response)
    hd::tls_credentials_registry registry; LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    acme_test::adapter_connection peer(registry.acquire()->select_default(), false); LT_ASSERT(peer.connect());
    auto budget = h2test::budget(); httpserver::server::route_registry routes;
    LT_ASSERT(httpserver::server::route_registry::create(budget, routes).ok());
    std::string body;
    LT_ASSERT(routes.route(httpserver::http::method::known(httpserver::http::method_id::post), "/hello",
        [&](httpserver::exchange& x) -> httpserver::task<void> {
            x.admit_body({4}); auto received = co_await x.body().collect(4);
            body.assign(reinterpret_cast<const char*>(received.data.data()), received.data.size());
            x.start_response(httpserver::http::status::from_code(200), {});
            co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
        }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto input = [&](const std::vector<std::uint8_t>& wire) {
        std::size_t written = 0;
        if (SSL_write_ex(peer.peer.get(), wire.data(), wire.size(), &written) != 1 || written != wire.size()) return false;
        std::size_t total = 0;
        for (unsigned round = 0; round < 1000 && total < wire.size(); ++round) {
            std::array<std::byte, 5> bytes{}; hd::read_operation read(peer.owner, 1, bytes);
            read.submit(*peer.tls); peer.drive();
            if (!read.state()->applied()) return false;
            auto n = read.state()->stored_result().transferred;
            if (!n || !h2test::feed(engine, {reinterpret_cast<const std::uint8_t*>(bytes.data()), n})) return false;
            total += n; executor.run_pending();
        }
        return total == wire.size();
    };
    auto transmit = [&](const std::vector<std::uint8_t>& plain) {
        std::vector<std::uint8_t> received;
        for (std::size_t at = 0; at < plain.size();) {
            const auto n = std::min<std::size_t>(7, plain.size() - at);
            hd::write_operation write(peer.owner, 1, {reinterpret_cast<const std::byte*>(plain.data() + at), n});
            write.submit(*peer.tls); peer.drive();
            if (!write.state()->applied() || write.state()->stored_result().transferred != n) return received;
            at += n;
        }
        for (unsigned round = 0; round < 1000 && received.size() < plain.size(); ++round) {
            std::array<std::uint8_t, 13> bytes{}; std::size_t n = 0;
            SSL_read_ex(peer.peer.get(), bytes.data(), bytes.size(), &n);
            received.insert(received.end(), bytes.begin(), bytes.begin() + n); peer.drive();
        }
        return received;
    };
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(4, 0)));
    auto fields = h2test::get(); fields[0].value = "POST"; fields.push_back({"content-length", "3"});
    h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    h2test::append(wire, h2test::frame(0, 0, 1, {'a'})); h2test::append(wire, h2test::frame(0, 1, 1, {'b', 'c'}));
    LT_ASSERT(input(wire)); LT_CHECK_EQ(body, "abc");
    auto head = h2test::output(engine); LT_CHECK_EQ(h2test::count_type(head, 0), 0u); LT_CHECK(transmit(head) == head);
    auto heads = h2test::responses(head, true); LT_ASSERT_EQ(heads.size(), 1u); LT_CHECK(!heads[0].end_stream);
    LT_ASSERT(input(h2test::frame(8, 0, 1, h2test::increment(3))));
    auto data = h2test::output(engine, 1); auto decrypted = transmit(data); LT_CHECK(decrypted == data);
    auto frames = h2test::frames(decrypted); LT_ASSERT_EQ(frames.size(), 2u);
    LT_CHECK_EQ(std::string(frames[0].payload.begin(), frames[0].payload.end()), "abc");
    LT_CHECK_EQ(frames[1].type, 0u); LT_CHECK_EQ(frames[1].flags, 1u); LT_CHECK(frames[1].payload.empty());
LT_END_AUTO_TEST(segmented_tls_body_waits_for_stream_credit_then_sends_exact_response)
LT_BEGIN_AUTO_TEST(http2_tls_boundary_suite, tls_extended_connect_exchanges_websocket_data_with_a_sibling)
    hd::tls_credentials_registry registry; LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    acme_test::adapter_connection peer(registry.acquire()->select_default(), false); LT_ASSERT(peer.connect());
    LT_ASSERT(peer.tls->negotiated_protocol() == hd::tls_negotiated_protocol::h2);
    h2ws::fixture f;
    auto input = [&](const std::vector<std::uint8_t>& wire) {
        std::size_t written = 0;
        if (SSL_write_ex(peer.peer.get(), wire.data(), wire.size(), &written) != 1 || written != wire.size()) return false;
        for (std::size_t total = 0; total < wire.size();) {
            std::array<std::byte, 7> bytes{}; hd::read_operation read(peer.owner, 1, bytes);
            read.submit(*peer.tls); peer.drive();
            if (!read.state()->applied()) return false;
            const auto n = read.state()->stored_result().transferred;
            if (!n || !h2test::feed(*f.engine, {reinterpret_cast<const std::uint8_t*>(bytes.data()), n})) return false;
            total += n;
        }
        f.executor.run_pending(); return true;
    };
    auto transmit = [&]() {
        auto wire = f.output();
        for (std::size_t at = 0; at < wire.size();) {
            const auto n = std::min<std::size_t>(11, wire.size() - at);
            hd::write_operation write(peer.owner, 1, {reinterpret_cast<const std::byte*>(wire.data() + at), n});
            write.submit(*peer.tls); peer.drive();
            if (!write.state()->applied() || write.state()->stored_result().transferred != n) return std::vector<std::uint8_t>{};
            at += n;
        }
        std::vector<std::uint8_t> received;
        for (unsigned round = 0; round < 1000 && received.size() < wire.size(); ++round) {
            std::array<std::uint8_t, 17> bytes{}; std::size_t n = 0;
            SSL_read_ex(peer.peer.get(), bytes.data(), bytes.size(), &n);
            received.insert(received.end(), bytes.begin(), bytes.begin() + n); peer.drive();
        }
        if (received != wire) return std::vector<std::uint8_t>{};
        return received;
    };
    auto advertised = transmit(); LT_ASSERT_EQ(h2test::count_type(advertised, 4), 1u);
    bool capability = false;
    for (std::size_t at = 9; at + 6 <= advertised.size(); at += 6) {
        if (advertised[at + 1] == 8) capability = advertised[at + 5] == 1;
    }
    LT_CHECK(capability);
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(4, 1));
    h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(f.encoder, h2ws::connect())));
    LT_ASSERT(input(wire)); LT_ASSERT_EQ(f.sessions.size(), 1u);
    auto received = transmit();
    wire = h2test::frame(0, 0, 1, h2ws::masked(9, "tls"));
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(f.encoder, h2test::get())));
    LT_ASSERT(input(wire)); h2test::append(received, transmit()); auto replies = h2test::responses(received, true); LT_ASSERT_EQ(replies.size(), 2u);
    LT_CHECK_EQ(replies[0].fields[0].value, "200"); LT_CHECK_EQ(replies[1].fields[0].value, "204");
    LT_CHECK(h2ws::data(received, 1) == std::vector<std::uint8_t>({138, 3, 't', 'l', 's'}));
LT_END_AUTO_TEST(tls_extended_connect_exchanges_websocket_data_with_a_sibling)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
