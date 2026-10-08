/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <limits>
#include <stdexcept>
#include <vector>
#include "../support/quic_network_harness.hpp"
#include "./littletest.hpp"
namespace qt = quic_test;
namespace hd = httpserver::detail;
LT_BEGIN_SUITE(network_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(network_suite)
LT_BEGIN_AUTO_TEST(network_suite, monotonic_clock_rejects_invalid_movement)
    qt::logical_clock clock;
    LT_CHECK_EQ(clock.ticks(), std::int64_t{0});
    LT_CHECK(clock.advance(10));
    LT_CHECK(!clock.advance(-1));
    LT_CHECK_EQ(clock.ticks(), std::int64_t{10});
    LT_CHECK(!clock.advance(std::numeric_limits<std::int64_t>::max()));
    LT_CHECK_EQ(clock.ticks(), std::int64_t{10});
LT_END_AUTO_TEST(monotonic_clock_rejects_invalid_movement)
LT_BEGIN_AUTO_TEST(network_suite, deadline_order_cancel_and_teardown)
    qt::network_harness rig;
    auto first = rig.timer(10), second = rig.timer(10), canceled = rig.timer(5);
    LT_ASSERT(first && second && canceled);
    LT_CHECK(rig.cancel(*canceled));
    LT_CHECK(rig.advance(9));
    LT_CHECK_EQ(rig.completed_timers().size(), std::size_t{1});
    LT_CHECK(rig.completed_timers()[0].code == httpserver::http::outcome_code::cancelled);
    LT_CHECK(rig.advance(1));
    LT_CHECK_EQ(rig.completed_timers().size(), std::size_t{3});
    LT_CHECK_EQ(rig.completed_timers()[1].id, *first);
    LT_CHECK_EQ(rig.completed_timers()[2].id, *second);
    LT_CHECK(rig.completed_timers()[1].code == httpserver::http::outcome_code::ok);
    LT_CHECK(rig.completed_timers()[2].code == httpserver::http::outcome_code::ok);
    LT_CHECK(!rig.cancel(*first));
    LT_CHECK(rig.advance(10));
    LT_CHECK_EQ(rig.completed_timers().size(), std::size_t{3});
    LT_CHECK(rig.timer(100));
    rig.teardown();
    LT_CHECK_EQ(rig.resources().timers, std::size_t{0});
    LT_CHECK_EQ(rig.resources().owner_pending, std::size_t{0});
    LT_CHECK(rig.completed_timers().back().code == httpserver::http::outcome_code::connection_closed);
LT_END_AUTO_TEST(deadline_order_cancel_and_teardown)
LT_BEGIN_AUTO_TEST(network_suite, loss_reorder_duplicate_preserve_whole_datagram_and_metadata)
    qt::network_harness rig;
    LT_ASSERT(rig.register_endpoint(1));
    auto packet = qt::short_packet(1, 11);
    packet.socket_id = 77;
    packet.peer.peer.address.family = httpserver::net::address_family::ipv6;
    packet.peer.peer.address.bytes[15] = std::byte{9};
    packet.peer.peer.port = 1234; packet.peer.scope = 6;
    packet.local = hd::datagram_endpoint{}; packet.local->peer.port = 8000;
    packet.local->scope = 5; packet.interface_index = 4;
    auto one = rig.send(packet), two = rig.send(qt::short_packet(1, 22)), lost = rig.send(qt::short_packet(1, 33));
    LT_ASSERT(one && two && lost);
    auto copy = rig.duplicate(*one); LT_ASSERT(copy);
    auto lifetime = rig.packet_lifetime(*one), duplicate_lifetime = rig.packet_lifetime(*copy);
    LT_CHECK(lifetime.lock() != duplicate_lifetime.lock());
    LT_CHECK(rig.drop(*lost));
    LT_CHECK_EQ(rig.resources().bytes, std::size_t{12});
    LT_CHECK(rig.advance(7));
    LT_CHECK(rig.deliver(*two) == hd::datagram_dispatch_code::routed);
    LT_CHECK(rig.deliver(*copy) == hd::datagram_dispatch_code::routed);
    LT_CHECK(rig.drain());
    LT_CHECK(rig.deliver(*one) == hd::datagram_dispatch_code::routed);
    LT_CHECK(rig.drain());
    LT_ASSERT_EQ(rig.delivered().size(), std::size_t{3});
    LT_CHECK(rig.delivered()[0].bytes == qt::short_packet(1, 22).bytes);
    for (std::size_t i : {1U, 2U}) {
        const auto& got = rig.delivered()[i];
        LT_CHECK(got.bytes == packet.bytes); LT_CHECK(got.peer == packet.peer);
        LT_CHECK(got.local == packet.local); LT_CHECK(got.interface_index == packet.interface_index);
        LT_CHECK_EQ(got.socket_id, packet.socket_id);
        LT_CHECK(got.received_at == qt::logical_clock::time_point(std::chrono::nanoseconds(7)));
    }
    LT_CHECK(lifetime.expired()); LT_CHECK(duplicate_lifetime.expired());
    LT_CHECK_EQ(rig.resources().bytes, std::size_t{0});
LT_END_AUTO_TEST(loss_reorder_duplicate_preserve_whole_datagram_and_metadata)
LT_BEGIN_AUTO_TEST(network_suite, capacity_rejection_is_atomic_and_reclaims_storage)
    qt::harness_limits limits; limits.packets = 2; limits.bytes = 4;
    qt::network_harness rig(limits);
    auto one = rig.send(qt::short_packet(1, 1)); LT_ASSERT(one);
    auto weak = rig.packet_lifetime(*one);
    LT_CHECK(!rig.duplicate(*one)); LT_CHECK(!rig.send(qt::short_packet(1, 2)));
    LT_CHECK_EQ(rig.resources().bytes, std::size_t{4});
    LT_CHECK(rig.drop(*one)); LT_CHECK(weak.expired());
    hd::io_datagram empty;
    LT_CHECK(rig.send(empty)); LT_CHECK(rig.send(empty)); LT_CHECK(!rig.send(empty));
    LT_CHECK_EQ(rig.resources().bytes, std::size_t{0});
    LT_CHECK_EQ(rig.resources().packets, std::size_t{2});
    rig.teardown(); LT_CHECK_EQ(rig.resources().packets, std::size_t{0});
LT_END_AUTO_TEST(capacity_rejection_is_atomic_and_reclaims_storage)
LT_BEGIN_AUTO_TEST(network_suite, owner_admission_releases_after_drain_and_destruction)
    qt::harness_limits limits; limits.owner_packets = 1; limits.owner_bytes = 4;
    qt::network_harness rig(limits); LT_ASSERT(rig.register_endpoint(1));
    auto one = rig.send(qt::short_packet(1, 1)), two = rig.send(qt::short_packet(1, 2)); LT_ASSERT(one && two);
    auto weak = rig.packet_lifetime(*one), rejected = rig.packet_lifetime(*two);
    LT_CHECK(rig.deliver(*one) == hd::datagram_dispatch_code::routed);
    LT_CHECK(rig.deliver(*two) == hd::datagram_dispatch_code::queue_full);
    LT_CHECK(!weak.expired()); LT_CHECK(rejected.expired());
    LT_CHECK(rig.drain()); LT_CHECK(weak.expired());
    auto three = rig.send(qt::short_packet(1, 3)); LT_ASSERT(three);
    auto destroyed = rig.packet_lifetime(*three);
    LT_CHECK(rig.deliver(*three) == hd::datagram_dispatch_code::routed);
    LT_CHECK(rig.destroy_endpoint(1)); LT_CHECK(destroyed.expired());
    rig.teardown(); LT_CHECK_EQ(rig.resources().owner_pending, std::size_t{0});
LT_END_AUTO_TEST(owner_admission_releases_after_drain_and_destruction)
LT_BEGIN_AUTO_TEST(network_suite, canonical_empty_teardown_trace)
    qt::network_harness rig;
    rig.teardown(); rig.teardown();
    // Independently encoded event 14 (teardown) and event 15 (zero resources).
    std::vector<std::uint8_t> expected{'Q', 'N', 1, 14};
    expected.insert(expected.end(), 16, 0);
    expected.push_back(15); expected.insert(expected.end(), 48, 0);
    LT_CHECK(rig.trace() == expected);
LT_END_AUTO_TEST(canonical_empty_teardown_trace)
LT_BEGIN_AUTO_TEST(network_suite, two_cid_combined_script_replays_with_known_outcomes)
    auto replay = [] {
        qt::network_harness rig;
        rig.register_endpoint(1); rig.register_endpoint(2);
        auto one = rig.send(qt::short_packet(1, 11));
        auto two = rig.send(qt::short_packet(2, 22));
        auto lost = rig.send(qt::short_packet(1, 33));
        if (!one || !two || !lost) throw std::runtime_error("script admission");
        rig.drop(*lost); auto duplicate = rig.duplicate(*one);
        if (!duplicate || rig.resources().bytes != 12) throw std::runtime_error("duplicate accounting");
        auto a = rig.timer(10), b = rig.timer(10), c = rig.timer(5);
        if (!a || !b || !c) throw std::runtime_error("timer admission");
        rig.cancel(*c); rig.advance(10);
        if (rig.completed_timers().size() != 3 || rig.completed_timers()[0].id != *c ||
            rig.completed_timers()[1].id != *a || rig.completed_timers()[2].id != *b) throw std::runtime_error("timer order");
        rig.deliver(*two);
        if (rig.resources().bytes != 8) throw std::runtime_error("delivery accounting");
        rig.deliver(*duplicate); rig.deliver(*one);
        auto excess = rig.send(qt::short_packet(1, 44));
        if (!excess || rig.deliver(*excess) != hd::datagram_dispatch_code::queue_full) throw std::runtime_error("queue saturation");
        rig.drain();
        auto fresh = rig.send(qt::short_packet(1, 55));
        if (!fresh || rig.deliver(*fresh) != hd::datagram_dispatch_code::routed) throw std::runtime_error("queue reclamation");
        rig.drain();
        const std::vector<unsigned> markers{11, 11, 22, 55};
        if (rig.delivered().size() != markers.size()) throw std::runtime_error("delivery count");
        for (std::size_t i = 0; i < markers.size(); ++i) {
            if (std::to_integer<unsigned>(rig.delivered()[i].bytes.back()) != markers[i]) throw std::runtime_error("delivery order");
        }
        rig.teardown();
        auto state = rig.resources();
        if (state.packets || state.bytes || state.timers || state.owner_pending) throw std::runtime_error("teardown reclamation");
        return rig.trace();
    };
    LT_CHECK(replay() == replay());
LT_END_AUTO_TEST(two_cid_combined_script_replays_with_known_outcomes)
LT_BEGIN_AUTO_TEST(network_suite, malformed_unknown_and_retired_packets_release)
    qt::network_harness rig; LT_ASSERT(rig.register_endpoint(1));
    hd::io_datagram empty;
    auto malformed = rig.send(empty), unknown = rig.send(qt::short_packet(2, 1)); LT_ASSERT(malformed && unknown);
    LT_CHECK(rig.deliver(*malformed) == hd::datagram_dispatch_code::malformed);
    LT_CHECK(rig.deliver(*unknown) == hd::datagram_dispatch_code::unknown);
    LT_CHECK(rig.retire_endpoint(1));
    auto retired = rig.send(qt::short_packet(1, 1)); LT_ASSERT(retired);
    LT_CHECK(rig.deliver(*retired) == hd::datagram_dispatch_code::retired);
    LT_CHECK(rig.register_endpoint(1));
    auto fresh = rig.send(qt::short_packet(1, 1)); LT_ASSERT(fresh);
    LT_CHECK(rig.deliver(*fresh) == hd::datagram_dispatch_code::routed);
    rig.drain();
    LT_CHECK_EQ(rig.delivered().size(), std::size_t{1});
    LT_CHECK_EQ(rig.resources().bytes, std::size_t{0});
LT_END_AUTO_TEST(malformed_unknown_and_retired_packets_release)
LT_BEGIN_AUTO_TEST(network_suite, independent_count_byte_timer_action_and_trace_limits)
    qt::harness_limits limits; limits.packets = 1; limits.bytes = 16;
    qt::network_harness count(limits);
    auto one = count.send(qt::short_packet(1, 1)); LT_ASSERT(one);
    LT_CHECK(!count.duplicate(*one));
    count.drop(*one); LT_CHECK(count.send(qt::short_packet(1, 2)));
    limits.owner_packets = 8; limits.owner_bytes = 4;
    qt::network_harness owner(limits); LT_ASSERT(owner.register_endpoint(1));
    auto a = owner.send(qt::short_packet(1, 1)); LT_ASSERT(a);
    LT_CHECK(owner.deliver(*a) == hd::datagram_dispatch_code::routed);
    auto b = owner.send(qt::short_packet(1, 2)); LT_ASSERT(b);
    LT_CHECK(owner.deliver(*b) == hd::datagram_dispatch_code::queue_full);
    owner.drain();
    auto c = owner.send(qt::short_packet(1, 3)); LT_ASSERT(c);
    LT_CHECK(owner.deliver(*c) == hd::datagram_dispatch_code::routed);
    limits.timers = 1; limits.actions = 2;
    qt::network_harness timer(limits);
    auto id = timer.timer(1); LT_ASSERT(id);
    LT_CHECK(!timer.timer(1)); LT_CHECK(timer.cancel(*id));
    LT_CHECK(!timer.advance(1));
    timer.teardown(); LT_CHECK_EQ(timer.resources().timers, std::size_t{0});
    limits.trace_bytes = 69;
    qt::network_harness trace(limits);
    LT_CHECK(!trace.send(qt::short_packet(1, 1)));
    trace.teardown(); LT_CHECK_EQ(trace.trace().size(), std::size_t{69});
    limits.trace_bytes = 2097152; limits.packet_bytes = 3;
    qt::network_harness size(limits);
    LT_CHECK(!size.send(qt::short_packet(1, 1)));
LT_END_AUTO_TEST(independent_count_byte_timer_action_and_trace_limits)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
