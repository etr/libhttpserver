/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <limits>
#include <httpserver/detail/quic_amplification.hpp>
#include "./quic_codec_test_support.hpp"
LT_BEGIN_SUITE(amplification_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(amplification_suite)
LT_BEGIN_AUTO_TEST(amplification_suite, three_times_includes_all_outstanding_sends)
    hd::quic_amplification_budget budget({});
    budget.receive_datagram(1200);
    auto first = budget.reserve_send(2000), second = budget.reserve_send(1600);
    LT_ASSERT(first && second);
    LT_CHECK(!budget.reserve_send(1));
    LT_CHECK(budget.complete_send(*first));
    LT_CHECK(!budget.cancel_unsent(*first));
    LT_CHECK(budget.cancel_unsent(*second));
    LT_CHECK(!budget.cancel_unsent(*second));
    auto replacement = budget.reserve_send(1600);
    LT_CHECK(replacement.has_value());
    LT_CHECK(!budget.cancel_unsent(*second));
    LT_CHECK(!budget.reserve_send(1));
LT_END_AUTO_TEST(three_times_includes_all_outstanding_sends)
LT_BEGIN_AUTO_TEST(amplification_suite, stale_cross_budget_handles_and_overflow_are_safe)
    hd::quic_amplification_budget budget({}), other({});
    budget.receive_datagram(std::numeric_limits<std::size_t>::max());
    auto send = budget.reserve_send(std::numeric_limits<std::uint64_t>::max() - 2);
    LT_ASSERT(send);
    LT_CHECK(!other.complete_send(*send));
    LT_CHECK(!budget.reserve_send(3));
    budget.receive_datagram(std::numeric_limits<std::size_t>::max());
    LT_CHECK(!budget.reserve_send(3));
    LT_CHECK(budget.cancel_unsent(*send));
    budget.mark_address_validated();
    LT_CHECK(budget.validated());
    LT_CHECK(budget.reserve_send(std::numeric_limits<std::uint64_t>::max()).has_value());
    LT_CHECK(!budget.reserve_send(1));
LT_END_AUTO_TEST(stale_cross_budget_handles_and_overflow_are_safe)
LT_BEGIN_AUTO_TEST(amplification_suite, validation_and_credit_are_path_local_with_bounded_reservations)
    hd::quic_amplification_budget a({}), b({});
    a.mark_address_validated();
    LT_CHECK(!b.validated());
    LT_CHECK(!b.reserve_send(1));
    for (int i = 0; i < 64; ++i) LT_CHECK(a.reserve_send(1).has_value());
    LT_CHECK(!a.reserve_send(1));
LT_END_AUTO_TEST(validation_and_credit_are_path_local_with_bounded_reservations)
LT_BEGIN_AUTO_TEST(amplification_suite, canonical_path_preserves_ports_scope_and_local_socket)
    hd::io_datagram packet;
    packet.peer.peer.address = *httpserver::net::parse_address("192.0.2.1");
    packet.peer.peer.port = 4000;
    packet.socket_id = 7;
    auto original = hd::quic_datagram_path(packet);
    LT_ASSERT(original);
    packet.peer.peer.address.bytes[0] = std::byte{1};
    packet.peer.scope = 99;
    LT_CHECK(hd::quic_datagram_path(packet) == original);
    packet.peer.peer.port++;
    LT_CHECK(hd::quic_datagram_path(packet) != original);
    packet.peer.peer.address = *httpserver::net::parse_address("fe80::1");
    auto scoped = hd::quic_datagram_path(packet);
    packet.peer.scope++;
    LT_CHECK(hd::quic_datagram_path(packet) != scoped);
    packet.peer.peer.address.family = httpserver::net::address_family::unspec;
    LT_CHECK(!hd::quic_datagram_path(packet));
LT_END_AUTO_TEST(canonical_path_preserves_ports_scope_and_local_socket)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
