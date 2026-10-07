/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
#include <httpserver/detail/fake_io_backend.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/tls_io_backend.hpp>
#include "./littletest.hpp"
#include "./tls_io_fixture.hpp"
namespace hd = httpserver::detail;
namespace hh = httpserver::http;
using httpserver::manual_executor;
using std::chrono_literals::operator""s;
LT_BEGIN_SUITE(tls_io_suite)
    void set_up() {}
    void tear_down() {}

LT_END_SUITE(tls_io_suite)
LT_BEGIN_AUTO_TEST(tls_io_suite, handshake_parks_with_owned_ciphertext)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::fake_io_backend raw;
    hd::tls_io_backend tls(raw, ex, 1, hd::tls_context::client(), false);
    hd::tls_handshake_operation op(owner, 1);
    op.submit(tls);
    ex.run_pending();
    LT_CHECK(!op.is_terminal());
    LT_CHECK(raw.pending_count() > 0);
    LT_CHECK_EQ(ex.pending(), std::size_t{0});
    tls.close();
    ex.run_pending();
    LT_CHECK(op.is_terminal());
    LT_CHECK_EQ(raw.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(handshake_parks_with_owned_ciphertext)
LT_BEGIN_AUTO_TEST(tls_io_suite, cancellation_retires_all_children_once)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::fake_io_backend raw;
    hd::tls_io_backend tls(raw, ex, 1, hd::tls_context::client(), false);
    hd::tls_handshake_operation op(owner, 1);
    op.submit(tls);
    ex.run_pending();
    LT_CHECK(tls.request_cancel(*op.state()) == hh::outcome_code::ok);
    ex.run_pending();
    LT_CHECK(op.state()->applied());
    LT_CHECK(op.state()->stored_result().code == hh::outcome_code::cancelled);
    LT_CHECK_EQ(raw.pending_count(), std::size_t{0});
    LT_CHECK(!op.state()->claim_terminal());
LT_END_AUTO_TEST(cancellation_retires_all_children_once)
LT_BEGIN_AUTO_TEST(tls_io_suite, absolute_handshake_timeout)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::fake_io_backend raw;
    hd::tls_io_backend tls(raw, ex, 1, hd::tls_context::client(), false);
    hd::tls_handshake_operation op(owner, 1, std::chrono::steady_clock::now() + 1s);
    op.submit(tls);
    ex.run_pending();
    raw.expire_timers(std::chrono::steady_clock::now() + 2s);
    ex.run_pending();
    LT_CHECK(op.state()->applied());
    LT_CHECK(op.state()->stored_result().code == hh::outcome_code::timeout);
    LT_CHECK_EQ(raw.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(absolute_handshake_timeout)

LT_BEGIN_AUTO_TEST(tls_io_suite, fragmented_handshake_and_full_duplex_plaintext)
    tls_test::pair p;
    LT_CHECK(p.handshake());
    LT_CHECK(p.sends > 10);
    std::array<std::byte, 128> incoming{}, reply{};
    const std::string message = "encrypted application bytes";
    const auto bytes = std::as_bytes(std::span(message));
    hd::read_operation server_read(p.owner, 1, incoming), client_read(p.owner, 1, reply);
    hd::write_operation client_write(p.owner, 1, bytes), server_write(p.owner, 1, bytes);
    client_read.submit(p.client);
    server_read.submit(p.server);
    p.drive();
    LT_CHECK(!client_read.is_terminal());
    LT_CHECK(!server_read.is_terminal());
    client_write.submit(p.client);
    server_write.submit(p.server);
    p.drive();
    for (auto op : {client_read.state(), server_read.state(), client_write.state(), server_write.state()}) {
        LT_CHECK(op->applied());
        LT_CHECK(op->stored_result().code == hh::outcome_code::ok);
        LT_CHECK_EQ(op->stored_result().transferred, message.size());
        LT_CHECK(!op->claim_terminal());
    }
    LT_CHECK(std::equal(bytes.begin(), bytes.end(), incoming.begin()));
    LT_CHECK(std::equal(bytes.begin(), bytes.end(), reply.begin()));
LT_END_AUTO_TEST(fragmented_handshake_and_full_duplex_plaintext)
LT_BEGIN_AUTO_TEST(tls_io_suite, orderly_eof_and_two_sided_shutdown)
    tls_test::pair p;
    LT_CHECK(p.handshake());
    std::array<std::byte, 32> bytes{};
    hd::read_operation read(p.owner, 1, bytes);
    read.submit(p.server);
    hd::tls_shutdown_operation shutdown(p.owner, 1);
    shutdown.submit(p.client);
    p.drive();
    LT_CHECK(read.state()->applied());
    LT_CHECK(read.state()->stored_result().code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(read.state()->stored_result().transferred, std::size_t{0});
    LT_CHECK(!shutdown.is_terminal());
    hd::tls_shutdown_operation peer_shutdown(p.owner, 1);
    peer_shutdown.submit(p.server);
    p.drive();
    LT_CHECK(shutdown.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK(peer_shutdown.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK_EQ(p.client_raw.pending_count(), std::size_t{0});
    LT_CHECK_EQ(p.server_raw.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(orderly_eof_and_two_sided_shutdown)
LT_BEGIN_AUTO_TEST(tls_io_suite, abrupt_eof_and_malformed_record_are_protocol_errors)
    for (bool malformed : {false, true}) {
        tls_test::pair p;
        LT_CHECK(p.handshake());
        std::array<std::byte, 32> bytes{};
        hd::read_operation read(p.owner, 1, bytes);
        read.submit(p.client);
        p.drive();
        for (const auto& op : p.client_raw.pending_ops()) {
            if (op->kind() != hd::io_op_kind::read) {
                continue;
            }
            if (malformed) {
                auto buffer = std::get<hd::read_payload>(op->payload()).buffer;
                std::fill_n(buffer.begin(), 8, std::byte{0xff});
                p.client_raw.complete(*op, {hh::outcome_code::ok, 8});
            } else {
                p.client_raw.complete(*op, {hh::outcome_code::connection_closed});
            }
        }
        p.ex.run_pending();
        LT_CHECK(read.state()->applied());
        LT_CHECK(read.state()->stored_result().code == hh::outcome_code::protocol_error);
        LT_CHECK_EQ(p.client_raw.pending_count(), std::size_t{0});
    }
LT_END_AUTO_TEST(abrupt_eof_and_malformed_record_are_protocol_errors)
LT_BEGIN_AUTO_TEST(tls_io_suite, overlapping_reads_rejected_without_aborting_first)
    tls_test::pair p;
    LT_CHECK(p.handshake());
    std::array<std::byte, 32> a{}, b{};
    hd::read_operation first(p.owner, 1, a), second(p.owner, 1, b);
    first.submit(p.client);
    second.submit(p.client);
    p.drive();
    LT_CHECK(second.state()->stored_result().code == hh::outcome_code::invalid_state);
    LT_CHECK(!first.is_terminal());
    p.client.request_cancel(*first.state());
    p.ex.run_pending();
    LT_CHECK(first.state()->stored_result().code == hh::outcome_code::cancelled);
LT_END_AUTO_TEST(overlapping_reads_rejected_without_aborting_first)

LT_BEGIN_AUTO_TEST(tls_io_suite, write_retries_under_bounded_output_backpressure)
    tls_test::pair p;
    p.fragment = 4096;
    LT_CHECK(p.handshake());
    std::vector<std::byte> message(100000, std::byte{0x5a});
    hd::write_operation write(p.owner, 1, message);
    write.submit(p.client);
    p.hold_writes = true;
    p.drive();
    LT_CHECK(!write.is_terminal());
    LT_CHECK_EQ(p.ex.run_pending(), std::size_t{0});
    const auto pending = p.client_raw.pending_ops();
    p.hold_writes = false;
    std::size_t total = 0;
    for (unsigned i = 0; i < 100 && total < message.size(); ++i) {
        std::array<std::byte, 4096> bytes{};
        hd::read_operation read(p.owner, 1, bytes);
        read.submit(p.server);
        p.drive();
        LT_ASSERT(read.state()->applied());
        LT_CHECK(read.state()->stored_result().code == hh::outcome_code::ok);
        const auto count = read.state()->stored_result().transferred;
        LT_CHECK(std::all_of(bytes.begin(), bytes.begin() + count, [](std::byte b) { return b == std::byte{0x5a}; }));
        total += count;
    }
    LT_CHECK_EQ(total, message.size());
    LT_CHECK(write.state()->applied());
    LT_CHECK_EQ(write.state()->stored_result().transferred, message.size());
    LT_CHECK(write.state()->stored_result().code == hh::outcome_code::ok);
LT_END_AUTO_TEST(write_retries_under_bounded_output_backpressure)

LT_BEGIN_AUTO_TEST(tls_io_suite, oversized_credentials_are_rejected_before_provider_reads)
    const auto cert = tls_test::pem("cert.pem"), key = tls_test::pem("key.pem");
    const auto oversized = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1;
    LT_CHECK_THROW(hd::tls_context::server_pem(std::string_view(cert.data(), oversized), key));
    LT_CHECK_THROW(hd::tls_context::server_pem(cert, std::string_view(key.data(), oversized)));
    LT_CHECK_THROW(hd::tls_context::server_pem("malformed", key));
LT_END_AUTO_TEST(oversized_credentials_are_rejected_before_provider_reads)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
