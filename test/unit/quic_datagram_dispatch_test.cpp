/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <utility>
#include <memory>
#include <algorithm>
#include <vector>
#include <httpserver/detail/quic_datagram_dispatch.hpp>
#include "./littletest.hpp"
#include "./io_udp_backend_contract.hpp"
namespace hd = httpserver::detail;
struct collecting_sink : hd::datagram_sink {
    std::vector<std::shared_ptr<const hd::io_datagram>> packets;
    bool executor_affinity = true;
    httpserver::executor* expected = nullptr;
    void on_datagram(std::shared_ptr<const hd::io_datagram> packet) noexcept override {
        executor_affinity &= httpserver::current_executor() == expected;
        packets.push_back(std::move(packet));
    }
};
static hd::quic_cid cid(unsigned byte) {
    hd::quic_cid result;
    result.size = 2;
    result.bytes[0] = std::byte(byte);
    return result;
}
static std::shared_ptr<hd::io_datagram> packet(unsigned byte, unsigned marker = 9) {
    auto result = std::make_shared<hd::io_datagram>();
    result->bytes = {std::byte{0x40}, std::byte(byte), {}, std::byte(marker)};
    result->socket_id = 77;
    result->peer.peer.port = marker;
    result->local = hd::datagram_endpoint{};
    result->local->peer.port = 8000;
    result->interface_index = 4;
    result->received_at = std::chrono::steady_clock::now();
    return result;
}
LT_BEGIN_SUITE(dispatch_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(dispatch_suite)
LT_BEGIN_AUTO_TEST(dispatch_suite, two_owners_route_by_cid_and_preserve_packet)
    httpserver::manual_executor a, b;
    hd::io_connection_owner first(a), second(b);
    auto one = std::make_shared<collecting_sink>(), two = std::make_shared<collecting_sink>();
    one->expected = &a;
    two->expected = &b;
    hd::quic_datagram_dispatch dispatch(2);
    auto first_cid = dispatch.register_cid(cid(1), first.datagrams(), one);
    auto second_cid = dispatch.register_cid(cid(2), second.datagrams(), two);
    LT_CHECK(first_cid.has_value());
    LT_CHECK(second_cid.has_value());
    auto original = packet(1);
    LT_CHECK(dispatch.dispatch(original).code == hd::datagram_dispatch_code::routed);
    LT_CHECK(dispatch.dispatch(packet(2)).code == hd::datagram_dispatch_code::routed);
    LT_CHECK(dispatch.dispatch(packet(1, 11)).code == hd::datagram_dispatch_code::routed);
    LT_CHECK(one->packets.empty());
    a.run_pending();
    LT_CHECK_EQ(one->packets.size(), std::size_t{2});
    LT_CHECK(two->packets.empty());
    LT_CHECK(one->packets.front() == original);
    LT_CHECK_EQ(one->packets.back()->peer.peer.port, std::uint16_t{11});
    LT_CHECK_EQ(one->packets.front()->socket_id, std::uint64_t{77});
    LT_CHECK_EQ(one->packets.front()->local->peer.port, std::uint16_t{8000});
    LT_CHECK(one->executor_affinity);
    b.run_pending();
    LT_CHECK_EQ(two->packets.size(), std::size_t{1});
    LT_CHECK(two->executor_affinity);
LT_END_AUTO_TEST(two_owners_route_by_cid_and_preserve_packet)
LT_BEGIN_AUTO_TEST(dispatch_suite, bounded_queue_retirement_and_cid_reuse)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex, 2, 8);
    auto old_sink = std::make_shared<collecting_sink>(), fresh_sink = std::make_shared<collecting_sink>();
    hd::quic_datagram_dispatch dispatch(2);
    auto registered = dispatch.register_cid(cid(1), owner.datagrams(), old_sink);
    LT_CHECK(!dispatch.register_cid(cid(1), owner.datagrams(), fresh_sink));
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::routed);
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::routed);
    auto rejected = packet(1);
    auto full = dispatch.dispatch(rejected);
    LT_CHECK(full.code == hd::datagram_dispatch_code::queue_full);
    LT_CHECK(full.packet == rejected);
    LT_CHECK(dispatch.remove(*registered));
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::retired);
    auto replacement = dispatch.register_cid(cid(1), owner.datagrams(), fresh_sink);
    LT_CHECK(replacement.has_value());
    LT_CHECK(!dispatch.remove(*registered));
    ex.run_pending();
    LT_CHECK(old_sink->packets.empty());
    LT_CHECK(fresh_sink->packets.empty());
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::routed);
    ex.run_pending();
    LT_CHECK_EQ(fresh_sink->packets.size(), std::size_t{1});
    auto unknown = packet(2);
    const auto missing = dispatch.dispatch(unknown);
    LT_CHECK(missing.code == hd::datagram_dispatch_code::unknown);
    LT_CHECK(missing.packet == unknown);
    auto malformed = std::make_shared<hd::io_datagram>();
    LT_CHECK(dispatch.dispatch(malformed).code == hd::datagram_dispatch_code::malformed);
LT_END_AUTO_TEST(bounded_queue_retirement_and_cid_reuse)
LT_BEGIN_AUTO_TEST(dispatch_suite, delayed_drain_after_owner_teardown_is_safe)
    httpserver::manual_executor ex;
    auto sink = std::make_shared<collecting_sink>();
    hd::quic_datagram_dispatch dispatch(2);
    {
        hd::io_connection_owner owner(ex);
        auto route = dispatch.register_cid(cid(1), owner.datagrams(), sink);
        LT_CHECK(route.has_value());
        LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::routed);
    }
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::retired);
    ex.run_pending();
    LT_CHECK(sink->packets.empty());
LT_END_AUTO_TEST(delayed_drain_after_owner_teardown_is_safe)
LT_BEGIN_AUTO_TEST(dispatch_suite, inline_executor_delivers_without_queue_lock)
    httpserver::inline_executor ex;
    hd::io_connection_owner owner(ex);
    auto sink = std::make_shared<collecting_sink>();
    sink->expected = &ex;
    hd::quic_datagram_dispatch dispatch(2);
    auto route = dispatch.register_cid(cid(1), owner.datagrams(), sink);
    LT_CHECK(route.has_value());
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::routed);
    LT_CHECK_EQ(sink->packets.size(), std::size_t{1});
    LT_CHECK(sink->executor_affinity);
LT_END_AUTO_TEST(inline_executor_delivers_without_queue_lock)
LT_BEGIN_AUTO_TEST(dispatch_suite, retired_cid_does_not_consume_active_registry_capacity)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    auto sink = std::make_shared<collecting_sink>();
    hd::quic_datagram_dispatch dispatch(2, 1);
    auto original = dispatch.register_cid(cid(1), owner.datagrams(), sink);
    LT_CHECK(original.has_value());
    LT_CHECK(!dispatch.register_cid(cid(2), owner.datagrams(), sink));
    LT_CHECK(dispatch.remove(*original));
    auto replacement = dispatch.register_cid(cid(2), owner.datagrams(), sink);
    LT_CHECK(replacement.has_value());
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::retired);
LT_END_AUTO_TEST(retired_cid_does_not_consume_active_registry_capacity)
struct reentrant_sink : hd::datagram_sink {
    hd::quic_datagram_dispatch* dispatch;
    std::vector<unsigned> markers;
    hd::datagram_dispatch_code blocked = hd::datagram_dispatch_code::routed;
    hd::datagram_dispatch_code admitted = hd::datagram_dispatch_code::queue_full;
    int depth = 0, maximum_depth = 0;
    void on_datagram(std::shared_ptr<const hd::io_datagram> received) noexcept override {
        ++depth;
        maximum_depth = std::max(maximum_depth, depth);
        auto marker = std::to_integer<unsigned>(received->bytes.back());
        markers.push_back(marker);
        if (marker == 9) blocked = dispatch->dispatch(packet(1, 11)).code;
        if (marker == 10) admitted = dispatch->dispatch(packet(1, 12)).code;
        --depth;
    }
};
LT_BEGIN_AUTO_TEST(dispatch_suite, detached_batch_reservations_and_completions_are_lossless)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex, 2, 8);
    hd::quic_datagram_dispatch dispatch(2);
    auto sink = std::make_shared<reentrant_sink>();
    sink->dispatch = &dispatch;
    auto route = dispatch.register_cid(cid(1), owner.datagrams(), sink);
    LT_CHECK(route.has_value());
    LT_CHECK(dispatch.dispatch(packet(1, 9)).code == hd::datagram_dispatch_code::routed);
    LT_CHECK(dispatch.dispatch(packet(1, 10)).code == hd::datagram_dispatch_code::routed);
    std::vector<std::shared_ptr<hd::op_state>> completions;
    for (int i = 0; i < 5; ++i) {
        hd::wake_operation completion(owner, 1);
        LT_CHECK(completion.state()->claim_terminal());
        completions.push_back(completion.state());
        owner.enqueue(completion.state(), {});
    }
    ex.run_pending();
    LT_CHECK(sink->blocked == hd::datagram_dispatch_code::queue_full);
    LT_CHECK(sink->admitted == hd::datagram_dispatch_code::routed);
    LT_CHECK_EQ(sink->maximum_depth, 1);
    LT_CHECK((sink->markers == std::vector<unsigned>{9, 10, 12}));
    for (const auto& state : completions) LT_CHECK(state->applied());
    LT_CHECK_EQ(owner.pending(), std::size_t{0});
LT_END_AUTO_TEST(detached_batch_reservations_and_completions_are_lossless)
LT_BEGIN_AUTO_TEST(dispatch_suite, byte_limit_is_independent_of_packet_count)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex, 8, 4);
    hd::quic_datagram_dispatch dispatch(2);
    auto sink = std::make_shared<collecting_sink>();
    auto route = dispatch.register_cid(cid(1), owner.datagrams(), sink);
    LT_CHECK(route.has_value());
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::routed);
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::queue_full);
    ex.run_pending();
    LT_CHECK(dispatch.dispatch(packet(1)).code == hd::datagram_dispatch_code::routed);
    ex.run_pending();
    LT_CHECK_EQ(sink->packets.size(), std::size_t{2});
LT_END_AUTO_TEST(byte_limit_is_independent_of_packet_count)
LT_BEGIN_AUTO_TEST(dispatch_suite, loopback_udp_to_cid_to_two_owners)
    httpserver::manual_executor transport_executor, first_executor, second_executor;
    hd::io_connection_owner transport_owner(transport_executor), first(first_executor), second(second_executor);
    auto one = std::make_shared<collecting_sink>(), two = std::make_shared<collecting_sink>();
    one->expected = &first_executor;
    two->expected = &second_executor;
    hd::quic_datagram_dispatch dispatch(2);
    auto first_cid = dispatch.register_cid(cid(1), first.datagrams(), one);
    auto second_cid = dispatch.register_cid(cid(2), second.datagrams(), two);
    LT_CHECK(first_cid.has_value());
    LT_CHECK(second_cid.has_value());
    hd::io_poll_backend backend(httpserver::server::loop_mode::external);
    backend.activate_external();
    auto local = io_udp_contract::udp_socket(), peer = io_udp_contract::udp_socket();
    auto destination = io_udp_contract::endpoint(local);
    backend.adopt_datagram(1, local);
    for (unsigned identity : {1U, 2U}) {
        hd::udp_receive_operation receive(transport_owner, 1, 8);
        receive.submit(backend);
        hd::udp_send_operation send(transport_owner, 2, packet(identity)->bytes, destination);
        backend.adopt_datagram(2, peer);
        send.submit(backend);
        auto snapshot = backend.interests();
        for (const auto& interest : snapshot.sockets) {
            if (!interest.writable) continue;
            httpserver::server::readiness_event event{interest.key, interest.generation, false, true};
            LT_CHECK(backend.dispatch(std::span(&event, 1), std::chrono::steady_clock::now()).ok());
        }
        snapshot = backend.interests();
        for (const auto& interest : snapshot.sockets) {
            if (!interest.readable) continue;
            hd::pollsys::poll_slot fd{static_cast<hd::pollsys::native_socket_t>(interest.handle.value), hd::pollsys::k_readable, 0};
            LT_ASSERT_EQ(hd::pollsys::poll_call(&fd, 1, 1000), 1);
            httpserver::server::readiness_event event{interest.key, interest.generation, true};
            LT_CHECK(backend.dispatch(std::span(&event, 1), std::chrono::steady_clock::now()).ok());
        }
        transport_executor.run_pending();
        LT_CHECK(receive.state()->applied());
        auto result = receive.state()->stored_result();
        LT_CHECK(result.code == httpserver::http::outcome_code::ok);
        LT_CHECK(dispatch.dispatch(result.datagram).code == hd::datagram_dispatch_code::routed);
        // The next sender has a changed source port; routing remains by CID.
        backend.release_connection(2);
        if (identity == 1) peer = io_udp_contract::udp_socket();
    }
    first_executor.run_pending();
    second_executor.run_pending();
    LT_CHECK_EQ(one->packets.size(), std::size_t{1});
    LT_CHECK_EQ(two->packets.size(), std::size_t{1});
    LT_CHECK(one->executor_affinity && two->executor_affinity);
    backend.close();
    transport_executor.run_pending();
LT_END_AUTO_TEST(loopback_udp_to_cid_to_two_owners)
LT_BEGIN_AUTO_TEST(dispatch_suite, coalesced_long_header_datagram_is_delivered_whole)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::quic_datagram_dispatch dispatch(2);
    auto sink = std::make_shared<collecting_sink>();
    auto registered = dispatch.register_cid(cid(1), owner.datagrams(), sink);
    LT_CHECK(registered.has_value());
    auto original = packet(1);
    original->bytes = {std::byte{0xc0}, {}, {}, {}, std::byte{1}, std::byte{2}, std::byte{1}, {}, {},
        std::byte{0x40}, std::byte{2}, {}, std::byte{0x99}};
    auto expected = original->bytes;
    LT_CHECK(dispatch.dispatch(original).code == hd::datagram_dispatch_code::routed);
    ex.run_pending();
    LT_CHECK_EQ(sink->packets.size(), std::size_t{1});
    LT_CHECK(sink->packets.front()->bytes == expected);
LT_END_AUTO_TEST(coalesced_long_header_datagram_is_delivered_whole)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
