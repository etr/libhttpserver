/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include <memory>
#include <httpserver/detail/io_udp_backend.hpp>
#include <httpserver/detail/io_operation.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include "./littletest.hpp"
namespace hd = httpserver::detail;
LT_BEGIN_SUITE(datagram_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(datagram_suite)
LT_BEGIN_AUTO_TEST(datagram_suite, send_snapshot_and_receive_lifetime)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::vector<std::byte> source(3, std::byte{42});
    hd::udp_send_operation send(owner, 7, source, {});
    source.clear();
    const auto& packet = *std::get<hd::udp_payload>(send.state()->payload()).packet;
    LT_CHECK_EQ(packet.bytes.size(), std::size_t{3});
    LT_CHECK(packet.bytes[0] == std::byte{42});
    std::shared_ptr<hd::io_datagram> received;
    {
        hd::udp_receive_operation receive(owner, 7, 3);
        received = std::get<hd::udp_payload>(receive.state()->payload()).packet;
    }
    LT_CHECK_EQ(received->bytes.size(), std::size_t{3});
LT_END_AUTO_TEST(send_snapshot_and_receive_lifetime)
LT_BEGIN_AUTO_TEST(datagram_suite, bounds_and_one_shot)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::udp_receive_operation huge(owner, 1, hd::k_max_datagram_bytes + 1);
    LT_CHECK(!std::get<hd::udp_payload>(huge.state()->payload()).valid);
    LT_CHECK(std::get<hd::udp_payload>(huge.state()->payload()).packet->bytes.empty());
    hd::udp_receive_operation empty(owner, 1, 0);
    LT_CHECK(std::get<hd::udp_payload>(empty.state()->payload()).valid);
    LT_CHECK(empty.state()->mark_submitted());
    LT_CHECK(!empty.state()->mark_submitted());
    LT_CHECK(empty.state()->mark_awaited());
    LT_CHECK(!empty.state()->mark_awaited());
    LT_CHECK(empty.state()->claim_terminal());
    LT_CHECK(!empty.state()->claim_terminal());
LT_END_AUTO_TEST(bounds_and_one_shot)
LT_BEGIN_AUTO_TEST(datagram_suite, admission_keeps_capacity_until_terminal_even_after_receive_resize)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::udp_admission budget;
    std::vector<std::shared_ptr<hd::op_state>> pending;
    const auto count = hd::k_udp_pending_bytes / hd::k_max_datagram_bytes;
    for (std::size_t i = 0; i < count; ++i) {
        hd::udp_receive_operation receive(owner, 1);
        pending.push_back(receive.state());
        LT_CHECK(budget.admit(receive.state()) == httpserver::http::outcome_code::ok);
    }
    // A detached syscall can resize the packet before its terminal claim.
    std::get<hd::udp_payload>(pending.front()->payload()).packet->bytes.clear();
    hd::udp_receive_operation excess(owner, 1, hd::k_udp_pending_bytes - count * hd::k_max_datagram_bytes + 1);
    LT_CHECK(budget.admit(excess.state()) == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK(pending.front()->claim_terminal());
    LT_CHECK(budget.admit(excess.state()) == httpserver::http::outcome_code::ok);
LT_END_AUTO_TEST(admission_keeps_capacity_until_terminal_even_after_receive_resize)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
