/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <algorithm>
#include <httpserver/detail/quic_flow_control.hpp>
#include <httpserver/detail/quic_recovery.hpp>
#include <httpserver/detail/quic_pacing.hpp>
#include <httpserver/detail/quic_datagram_sizing.hpp>
#include "../littletest.hpp"
#include "../support/quic_network_harness.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;
using space = hd::quic_pn_space;
namespace {
hs::resource_budget budget() { return hs::resource_budget::root({}); }
hd::quic_recovery_config config() {
    hd::quic_recovery_config c;
    c.max_datagram_size = 1472;
    return c;
}
hd::quic_transport_parameters parameters() {
    hd::quic_transport_parameters p;
    p.initial_max_data = 100000;
    p.initial_max_stream_data_bidi_remote = 100000;
    p.initial_max_streams_bidi = 4;
    return p;
}
hd::quic_ack_frame ack(std::uint64_t number) { return {number, 0, 0, 0, {}, {}}; }
hd::quic_recovery_environment confirmed() {
    hd::quic_recovery_environment e;
    e.handshake_confirmed = true;
    return e;
}
}  // namespace
LT_BEGIN_SUITE(path_mtu_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(path_mtu_suite)
LT_BEGIN_AUTO_TEST(path_mtu_suite, configured_ceiling_does_not_authorize_unconfirmed_large_sends)
    hd::quic_recovery r(config(), budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 8, budget());
    std::array<std::byte, 1600> output{};
    LT_ASSERT(f.open_local(false));
    LT_ASSERT(r.retain_stream({1, 0, output}));
    auto p = r.prepare_scheduled_packet(space::application, output, {}, {1472, 40}, f);
    LT_ASSERT(p);
    LT_CHECK(p.bytes + 40 <= 1200);
    LT_CHECK(r.check_scheduled_emission(p.token, {}, 1201, true, true).code == hd::quic_recovery_code::invalid);
    LT_ASSERT(r.check_scheduled_emission(p.token, {}, 1200, true, true));
    LT_ASSERT(r.abandon_packet(p.token));
    LT_CHECK(f.sent() == 0 && r.bytes_in_flight() == 0);
LT_END_AUTO_TEST(configured_ceiling_does_not_authorize_unconfirmed_large_sends)
LT_BEGIN_AUTO_TEST(path_mtu_suite, probe_ack_confirms_only_actual_emitted_size_with_peer_ceiling)
    hd::quic_recovery r(config(), budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 8, budget());
    std::array<std::byte, 1600> output{};
    LT_CHECK(r.set_peer_max_datagram_size(1300));
    r.set_environment(confirmed(), {});
    auto p = r.prepare_pmtu_probe(output, {}, 40);
    LT_CHECK(p && p.ack_eliciting && p.bytes + 40 == 1300);
    if (!p) return;
    LT_CHECK(r.commit_sent(p.token, {}, 1301, true, true).code == hd::quic_recovery_code::invalid);
    LT_CHECK(r.path_datagram_size() == 1200 && r.bytes_in_flight() == 0);
    LT_ASSERT(r.commit_sent(p.token, {}, 1300, true, true));
    LT_CHECK(r.prepare_pmtu_probe(output, {}, 40).code == hd::quic_recovery_code::unavailable);
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), hd::quic_recovery::time_point {} + 10ms));
    LT_CHECK(r.path_datagram_size() == 1300 && r.bytes_in_flight() == 0);
    LT_CHECK(r.prepare_pmtu_probe(output, hd::quic_recovery::time_point {} + 20ms, 40).code == hd::quic_recovery_code::unavailable);
    LT_ASSERT(r.activate_path());
    LT_CHECK(r.path_datagram_size() == 1200);
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), hd::quic_recovery::time_point {} + 30ms));
    LT_CHECK(r.path_datagram_size() == 1200);
LT_END_AUTO_TEST(probe_ack_confirms_only_actual_emitted_size_with_peer_ceiling)
LT_BEGIN_AUTO_TEST(path_mtu_suite, abandoned_probe_burns_number_without_starting_search_timer)
    hd::quic_recovery r(config(), budget());
    std::array<std::byte, 1600> output{};
    LT_CHECK(r.prepare_pmtu_probe(output, {}, 40).code == hd::quic_recovery_code::unavailable);
    r.set_environment(confirmed(), {});
    auto p = r.prepare_pmtu_probe(output, {}, 40);
    LT_CHECK(p);
    if (!p) return;
    LT_ASSERT(r.abandon_packet(p.token));
    LT_CHECK(!r.next_deadline() && r.bytes_in_flight() == 0);
    auto next = r.prepare_pmtu_probe(output, {}, 40);
    LT_CHECK(next && next.packet_number > p.packet_number && next.bytes == p.bytes);
    if (next) LT_ASSERT(r.abandon_packet(next.token));
    LT_CHECK(r.receive_ack(space::application, ack(p.packet_number), {}).code == hd::quic_recovery_code::invalid);
LT_END_AUTO_TEST(abandoned_probe_burns_number_without_starting_search_timer)
LT_BEGIN_AUTO_TEST(path_mtu_suite, probe_timeout_releases_flight_once_without_congestion_loss)
    hd::quic_recovery r(config(), budget());
    std::array<std::byte, 1600> output{};
    r.set_environment(confirmed(), {});
    const auto window = r.congestion().window;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const auto now = hd::quic_recovery::time_point {} + attempt * 2s;
        auto p = r.prepare_pmtu_probe(output, now, 40);
        LT_CHECK(p && p.bytes + 40 == 1328);
        if (!p) return;
        LT_ASSERT(r.commit_sent(p.token, now, 1328, true, true));
        auto due = r.next_deadline();
        LT_ASSERT(due);
        auto expired = r.expire(due->deadline);
        LT_CHECK(expired.lost_bytes == 1328);
        LT_CHECK(r.expire(due->deadline).lost_bytes == 0 && r.bytes_in_flight() == 0);
        LT_CHECK(r.congestion().window == window && r.path_datagram_size() == 1200);
        LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), due->deadline + 1ms));
        LT_CHECK(r.path_datagram_size() == 1200);
    }
    LT_CHECK(r.prepare_pmtu_probe(output, hd::quic_recovery::time_point {} + 6s, 40).code == hd::quic_recovery_code::unavailable);
LT_END_AUTO_TEST(probe_timeout_releases_flight_once_without_congestion_loss)
LT_BEGIN_AUTO_TEST(path_mtu_suite, invalid_peer_limits_do_not_mutate_or_abandon_preparation)
    hd::quic_recovery r(config(), budget());
    std::array<std::byte, 1600> output{};
    LT_CHECK(r.set_peer_max_datagram_size(1199).code == hd::quic_recovery_code::invalid);
    LT_CHECK(r.set_peer_max_datagram_size(hd::k_quic_max_integer + 1).code == hd::quic_recovery_code::invalid);
    r.set_environment(confirmed(), {});
    auto p = r.prepare_pmtu_probe(output, {}, 40);
    LT_CHECK(p);
    if (!p) return;
    LT_CHECK(r.set_peer_max_datagram_size(1200).code == hd::quic_recovery_code::busy);
    LT_CHECK(r.activate_path().code == hd::quic_recovery_code::busy);
    LT_ASSERT(r.abandon_packet(p.token));
    LT_CHECK(r.set_peer_max_datagram_size(1200));
    LT_CHECK(r.prepare_pmtu_probe(output, {}, 40).code == hd::quic_recovery_code::unavailable);
LT_END_AUTO_TEST(invalid_peer_limits_do_not_mutate_or_abandon_preparation)
LT_BEGIN_AUTO_TEST(path_mtu_suite, old_path_probe_ack_cannot_confirm_new_path)
    auto run = [&]() {
        quic_test::network_harness net;
        quic_test::logical_clock clock;
        LT_ASSERT(net.register_endpoint(1));
        hd::quic_recovery r(config(), budget());
        std::array<std::byte, 1600> output{};
        r.set_environment(confirmed(), clock.now());
        auto old = r.prepare_pmtu_probe(output, clock.now(), 40);
        LT_CHECK(old);
        if (!old) return net.trace();
        LT_ASSERT(r.commit_sent(old.token, clock.now(), 1328, true, true));
        auto delayed = quic_test::short_packet(1, 0);
        delayed.bytes.resize(1328);
        auto id = net.send(delayed);
        LT_ASSERT(id && r.activate_path());
        auto current = r.prepare_pmtu_probe(output, clock.now(), 40);
        LT_ASSERT(current && r.commit_sent(current.token, clock.now(), 1328, true, true));
        LT_ASSERT(net.deliver(*id) && net.drain());
        LT_ASSERT(clock.advance(10000000) && net.advance(10000000));
        LT_ASSERT(r.receive_ack(space::application, ack(old.packet_number), clock.now()));
        LT_CHECK(r.path_datagram_size() == 1200 && r.bytes_in_flight() == 1328);
        auto due = r.next_deadline();
        LT_ASSERT(due && due->action == hd::quic_recovery_timer::kind::path_mtu);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(due->deadline - clock.now()).count();
        LT_ASSERT(clock.advance(elapsed) && net.advance(elapsed));
        LT_CHECK(r.expire(clock.now()).lost_bytes == 1328 && r.bytes_in_flight() == 0);
        net.teardown();
        auto resources = net.resources();
        LT_CHECK(resources.packets == 0 && resources.bytes == 0 && resources.timers == 0 && resources.owner_pending == 0);
        return net.trace();
    };
    LT_CHECK(run() == run());
LT_END_AUTO_TEST(old_path_probe_ack_cannot_confirm_new_path)
LT_BEGIN_AUTO_TEST(path_mtu_suite, deterministic_black_hole_fallback_reencodes_retained_stream_and_fin)
    auto run = [&]() {
        quic_test::network_harness net;
        quic_test::logical_clock clock;
        LT_ASSERT(net.register_endpoint(1));
        hd::quic_recovery r(config(), budget());
        hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 8, budget());
        std::array<std::byte, 1600> output{};
        LT_ASSERT(f.open_local(false));
        LT_ASSERT(r.set_peer_max_datagram_size(1328));
        r.set_environment(confirmed(), clock.now());
        auto probe = r.prepare_pmtu_probe(output, clock.now(), 40);
        LT_ASSERT(probe && r.commit_sent(probe.token, clock.now(), 1328, true, true));
        auto success = quic_test::short_packet(1, 0);
        success.bytes.resize(1328);
        auto delivered_probe = net.send(success);
        LT_ASSERT(delivered_probe && net.deliver(*delivered_probe) && net.drain());
        LT_ASSERT(clock.advance(10000000) && net.advance(10000000));
        LT_ASSERT(r.receive_ack(space::application, ack(probe.packet_number), clock.now()));
        LT_CHECK(r.path_datagram_size() == 1328);
        LT_ASSERT(r.retain_stream({1, 0, std::span(output).first(1280), true}, hd::quic_stream_retention::critical));
        LT_ASSERT(clock.advance(100000000) && net.advance(100000000));
        auto p = r.prepare_scheduled_packet(space::application, output, clock.now(), {1472, 40}, f);
        LT_ASSERT(p && p.stream);
        const auto original = p.packet_number;
        const auto original_prefix = p.stream->data.size();
        auto drop = [&](const hd::quic_send_plan& plan) {
            auto packet = quic_test::short_packet(1, static_cast<unsigned>(plan.packet_number));
            packet.bytes.resize(plan.bytes + 40);
            LT_CHECK(packet.bytes.size() > 1200 && packet.bytes.size() <= 1328);
            LT_ASSERT(r.commit_sent(plan.token, clock.now(), packet.bytes.size(), true, true));
            auto id = net.send(packet);
            LT_ASSERT(id && net.drop(*id));
        };
        drop(p);
        for (unsigned opportunity = 0; opportunity < 3; ++opportunity) {
            auto due = r.next_deadline();
            LT_ASSERT(due && due->action == hd::quic_recovery_timer::kind::probe);
            const auto elapsed = std::max(std::chrono::nanoseconds::rep{0}, std::chrono::duration_cast<std::chrono::nanoseconds>(due->deadline - clock.now()).count());
            LT_ASSERT(clock.advance(elapsed) && net.advance(elapsed));
            LT_CHECK(r.expire(clock.now()).probes == 2);
            LT_CHECK(r.path_datagram_size() == (opportunity == 2 ? 1200 : 1328));
            p = r.prepare_scheduled_packet(space::application, output, clock.now(), {1472, 40, true}, f);
            LT_ASSERT(p && p.stream && p.stream->offset == 0 && p.packet_number > original);
            if (opportunity != 2) drop(p);
        }
        LT_CHECK(p.bytes + 40 <= 1200 && p.stream->data.size() < original_prefix);
        auto small = quic_test::short_packet(1, static_cast<unsigned>(p.packet_number));
        small.bytes.resize(p.bytes + 40);
        LT_ASSERT(r.commit_sent(p.token, clock.now(), small.bytes.size(), true, true));
        auto id = net.send(small);
        LT_ASSERT(id && net.deliver(*id) && net.drain());
        LT_ASSERT(clock.advance(10000000) && net.advance(10000000));
        LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), clock.now()));
        const auto small_prefix = p.stream->data.size();
        LT_ASSERT(clock.advance(100000000) && net.advance(100000000));
        p = r.prepare_scheduled_packet(space::application, output, clock.now(), {1472, 40}, f);
        LT_ASSERT(p && p.stream && p.stream->offset == small_prefix && p.stream->fin);
        LT_ASSERT(r.commit_sent(p.token, clock.now(), p.bytes + 40, true, true));
        LT_ASSERT(clock.advance(10000000) && net.advance(10000000));
        LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), clock.now()));
        auto done = r.take_completion();
        LT_CHECK(done && done->through_offset == 1280 && done->fin);
        LT_ASSERT(r.receive_ack(space::application, ack(original), clock.now()));
        LT_CHECK(!r.take_completion() && r.bytes_in_flight() == 0 && f.sent() == 1280);
        LT_CHECK(r.prepare_pmtu_probe(output, clock.now(), 40).code == hd::quic_recovery_code::unavailable);
        net.teardown();
        auto resources = net.resources();
        LT_CHECK(resources.packets == 0 && resources.bytes == 0 && resources.timers == 0 && resources.owner_pending == 0);
        return net.trace();
    };
    LT_CHECK(run() == run());
LT_END_AUTO_TEST(deterministic_black_hole_fallback_reencodes_retained_stream_and_fin)
LT_BEGIN_AUTO_TEST(path_mtu_suite, failed_larger_discovery_keeps_the_confirmed_working_size)
    hd::quic_recovery r(config(), budget());
    std::array<std::byte, 1600> output{};
    r.set_environment(confirmed(), {});
    auto p = r.prepare_pmtu_probe(output, {}, 40);
    LT_ASSERT(p && r.commit_sent(p.token, {}, 1328, true, true));
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), hd::quic_recovery::time_point {} + 10ms));
    const auto window = r.congestion().window;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const auto now = hd::quic_recovery::time_point {} + (attempt + 1) * 1s;
        p = r.prepare_pmtu_probe(output, now, 40);
        LT_ASSERT(p && p.bytes + 40 == 1456);
        LT_ASSERT(r.commit_sent(p.token, now, 1456, true, true));
        auto due = r.next_deadline();
        LT_ASSERT(due);
        LT_CHECK(r.expire(due->deadline).lost_bytes == 1456);
        LT_CHECK(r.path_datagram_size() == 1328 && r.congestion().window == window && r.bytes_in_flight() == 0);
    }
LT_END_AUTO_TEST(failed_larger_discovery_keeps_the_confirmed_working_size)
LT_BEGIN_AUTO_TEST(path_mtu_suite, sizing_updates_preserve_congestion_history_and_pacing_debt)
    hd::quic_congestion congestion;
    congestion.lose({}, {}, false);
    const auto before = congestion.snapshot();
    congestion.set_datagram_size(1328);
    congestion.acknowledge(3000, hd::quic_recovery::time_point {} + 1ms, false);
    congestion.set_datagram_size(1200);
    congestion.acknowledge(3000, hd::quic_recovery::time_point {} + 2ms, false);
    LT_CHECK(congestion.snapshot().window == before.window + 1200);
    LT_CHECK(congestion.snapshot().slow_start_threshold == before.slow_start_threshold && congestion.snapshot().recovery_started == before.recovery_started);
    hd::quic_pacing pacing;
    pacing.emitted({}, 2400, 12000, 100ms);
    pacing.set_datagram_size(1328, {}, 12000, 100ms);
    LT_CHECK(pacing.deadline({}, 1, 12000, 100ms).has_value());
    pacing.set_datagram_size(1200, {}, 12000, 100ms);
    LT_CHECK(pacing.deadline({}, 1, 12000, 100ms).has_value());
LT_END_AUTO_TEST(sizing_updates_preserve_congestion_history_and_pacing_debt)
LT_BEGIN_AUTO_TEST(path_mtu_suite, blocked_window_still_allows_smaller_pto_reencoding_after_fallback)
    hd::quic_recovery r(config(), budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 8, budget());
    LT_ASSERT(f.open_local(false));
    std::array<std::byte, 1600> output{};
    r.set_environment(confirmed(), {});
    auto probe = r.prepare_pmtu_probe(output, {}, 40);
    LT_ASSERT(probe && r.commit_sent(probe.token, {}, 1328, true, true));
    auto now = hd::quic_recovery::time_point {} + 10ms;
    LT_ASSERT(r.receive_ack(space::application, ack(probe.packet_number), now));
    LT_ASSERT(r.retain_stream({1, 0, std::span(output).first(1280), true}, hd::quic_stream_retention::critical));
    now += 100ms;
    auto p = r.prepare_scheduled_packet(space::application, output, now, {1472, 40}, f);
    LT_ASSERT(p && r.commit_sent(p.token, now, 1328, true, true));
    auto fill = r.reserve_packet(space::application);
    LT_ASSERT(fill && r.commit_sent(fill.token, now, r.congestion().window - r.bytes_in_flight(), true, true));
    LT_CHECK(r.bytes_in_flight() == r.congestion().window);
    for (unsigned i = 0; i < 3; ++i) {
        auto due = r.next_deadline();
        LT_ASSERT(due);
        now = due->deadline;
        LT_CHECK(r.expire(now).probes == 2);
        p = r.prepare_scheduled_packet(space::application, output, now, {1472, 40, true}, f);
        LT_ASSERT(p && p.stream);
        LT_CHECK(r.path_datagram_size() == (i == 2 ? 1200 : 1328));
        LT_ASSERT(r.commit_sent(p.token, now, i == 2 ? 1200 : 1328, true, true));
    }
    LT_CHECK(p.bytes + 40 <= 1200 && !p.stream->fin && r.bytes_in_flight() > r.congestion().window);
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), now + 10ms));
    LT_CHECK(r.bytes_in_flight() == 0 && r.path_datagram_size() == 1200);
LT_END_AUTO_TEST(blocked_window_still_allows_smaller_pto_reencoding_after_fallback)
LT_BEGIN_AUTO_TEST(path_mtu_suite, path_controls_cannot_bypass_the_confirmed_datagram_limit)
    hd::quic_recovery r(config(), budget());
    std::array<std::byte, 1600> output{};
    std::array<std::byte, 8> data{};
    auto p = r.prepare_path_packet({false, data}, output, {}, {1472, 40}, true);
    LT_ASSERT(p);
    LT_CHECK(p.bytes + 40 == 1200);
    LT_ASSERT(r.abandon_packet(p.token));
    p = r.prepare_path_packet({false, data}, output, {}, {1472, 40}, false);
    LT_ASSERT(p);
    LT_CHECK(p.bytes + 40 == 1200);
    LT_ASSERT(r.abandon_packet(p.token));
LT_END_AUTO_TEST(path_controls_cannot_bypass_the_confirmed_datagram_limit)
LT_BEGIN_AUTO_TEST(path_mtu_suite, acked_coalesced_datagram_stays_delivered_after_constituent_collection)
    hd::quic_recovery r(config(), budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 8, budget());
    std::array<std::byte, 1600> one{}, two{};
    r.set_environment(confirmed(), {});
    auto probe = r.prepare_pmtu_probe(one, {}, 40);
    LT_ASSERT(probe && r.commit_sent(probe.token, {}, 1328, true, true));
    auto now = hd::quic_recovery::time_point {} + 10ms;
    LT_ASSERT(r.receive_ack(space::application, ack(probe.packet_number), now));
    now += 100ms;
    LT_ASSERT(r.retain_crypto(space::initial, 0, std::span(one).first(1)));
    LT_ASSERT(r.retain_crypto(space::handshake, 0, std::span(two).first(1)));
    LT_ASSERT(r.begin_datagram());
    auto a = r.prepare_datagram_packet(space::initial, one, now, {1328, 40}, f);
    auto b = r.prepare_datagram_packet(space::handshake, two, now, {928, 40}, f);
    LT_ASSERT(a && b);
    std::array<hd::quic_datagram_emission, 2> actual{{{a.token, 400, true, true}, {b.token, 928, true, true}}};
    LT_ASSERT(r.commit_datagram(actual, now));
    LT_ASSERT(r.receive_ack(space::initial, ack(a.packet_number), now + 10ms));
    LT_ASSERT(r.discard_space(space::initial));
    for (unsigned opportunity = 0; opportunity < 3; ++opportunity) {
        auto due = r.next_deadline();
        LT_ASSERT(due && due->space == space::handshake);
        now = due->deadline;
        LT_CHECK(r.expire(now).probes == 2);
        LT_CHECK(r.path_datagram_size() == 1328);
        if (opportunity == 2) break;
        auto p = r.prepare_scheduled_packet(space::handshake, two, now, {1328, 40, true}, f);
        LT_ASSERT(p && r.commit_sent(p.token, now, 1328, true, true));
    }
LT_END_AUTO_TEST(acked_coalesced_datagram_stays_delivered_after_constituent_collection)
LT_BEGIN_AUTO_TEST(path_mtu_suite, deterministic_lost_discovery_probes_preserve_reliable_1200_byte_traffic)
    auto run = [&]() {
        quic_test::network_harness net;
        quic_test::logical_clock clock;
        LT_ASSERT(net.register_endpoint(1));
        hd::quic_recovery r(config(), budget());
        hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 8, budget());
        std::array<std::byte, 1600> output{};
        r.set_environment(confirmed(), clock.now());
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            auto probe = r.prepare_pmtu_probe(output, clock.now(), 40);
            LT_ASSERT(probe && probe.bytes + 40 == 1328);
            LT_ASSERT(r.commit_sent(probe.token, clock.now(), 1328, true, true));
            auto large = quic_test::short_packet(1, static_cast<unsigned>(probe.packet_number));
            large.bytes.resize(1328);
            auto lost = net.send(large);
            LT_ASSERT(lost && large.bytes.size() > 1200 && net.drop(*lost));
            LT_ASSERT(clock.advance(100000000) && net.advance(100000000));
            const auto offset = attempt * 128;
            LT_ASSERT(r.retain_crypto(space::application, offset, std::span(output).first(128)));
            auto p = r.prepare_scheduled_packet(space::application, output, clock.now(), {1472, 40}, f);
            LT_ASSERT(p && p.bytes + 40 <= 1200);
            LT_ASSERT(r.commit_sent(p.token, clock.now(), 1200, true, true));
            auto base = quic_test::short_packet(1, static_cast<unsigned>(p.packet_number));
            base.bytes.resize(1200);
            auto delivered = net.send(base);
            LT_ASSERT(delivered && net.deliver(*delivered) && net.drain());
            LT_ASSERT(clock.advance(10000000) && net.advance(10000000));
            LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), clock.now()));
            auto done = r.take_completion();
            LT_CHECK(done && done->kind == hd::quic_information_kind::crypto && done->through_offset == offset + 128);
            auto due = r.next_deadline();
            LT_ASSERT(due);
            const auto elapsed = std::max(std::chrono::nanoseconds::rep{0}, std::chrono::duration_cast<std::chrono::nanoseconds>(due->deadline - clock.now()).count());
            LT_ASSERT(clock.advance(elapsed) && net.advance(elapsed));
            r.expire(clock.now());
            LT_CHECK(r.path_datagram_size() == 1200 && r.bytes_in_flight() == 0 && !r.take_completion());
        }
        net.teardown();
        auto resources = net.resources();
        LT_CHECK(resources.packets == 0 && resources.bytes == 0 && resources.timers == 0 && resources.owner_pending == 0);
        return net.trace();
    };
    LT_CHECK(run() == run());
LT_END_AUTO_TEST(deterministic_lost_discovery_probes_preserve_reliable_1200_byte_traffic)
LT_BEGIN_AUTO_TEST(path_mtu_suite, initial_pto_does_not_count_application_datagrams_before_their_recovery_opportunity)
    hd::quic_recovery r(config(), budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 8, budget());
    std::array<std::byte, 1600> output{};
    r.set_environment(confirmed(), {});
    auto probe = r.prepare_pmtu_probe(output, {}, 40);
    LT_ASSERT(probe && r.commit_sent(probe.token, {}, 1328, true, true));
    auto now = hd::quic_recovery::time_point {} + 10ms;
    LT_ASSERT(r.receive_ack(space::application, ack(probe.packet_number), now));
    now += 100ms;
    auto initial = r.reserve_packet(space::initial);
    LT_ASSERT(initial && r.commit_sent(initial.token, now, 1200, true, true));
    for (unsigned opportunity = 0; opportunity < 3; ++opportunity) {
        LT_ASSERT(r.retain_flow({hd::quic_flow_kind::max_data, opportunity + 1}));
        auto application = r.prepare_scheduled_packet(space::application, output, now, {1472, 40}, f);
        LT_ASSERT(application && r.commit_sent(application.token, now, 1328, true, true));
        auto due = r.next_deadline();
        LT_ASSERT(due && due->space == space::initial);
        now = due->deadline;
        LT_CHECK(r.expire(now).probe_space == space::initial);
        LT_CHECK(r.path_datagram_size() == 1328);
    }
LT_END_AUTO_TEST(initial_pto_does_not_count_application_datagrams_before_their_recovery_opportunity)
LT_BEGIN_AUTO_TEST(path_mtu_suite, deterministic_initial_coalescing_loss_and_reordering_respects_limits)
    auto run = [&]() {
        quic_test::network_harness net;
        quic_test::logical_clock clock;
        LT_ASSERT(net.register_endpoint(1));
        hd::quic_recovery r({}, budget());
        hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 8, budget());
        std::array<std::byte, 2> cid{std::byte{1}}, content{std::byte{9}};
        std::array<std::byte, 16> tag{};
        std::array<hd::quic_send_plan, 3> plans{};
        LT_ASSERT(r.retain_crypto(space::initial, 0, std::span(content).first(1)));
        LT_ASSERT(r.retain_crypto(space::handshake, 0, std::span(content).first(1)));
        auto batch = [&](std::span<const space> spaces, bool probe_first) {
            std::array<std::array<std::byte, 1200>, 3> plain{};
            std::array<hd::quic_datagram_packet, 3> packets{};
            std::array<hd::quic_datagram_emission, 3> emitted{};
            std::size_t used = 0;
            LT_ASSERT(r.begin_datagram());
            for (std::size_t i = 0; i < spaces.size(); ++i) {
                auto& h = packets[i].header;
                h.kind = spaces[i] == space::initial ? hd::quic_packet_kind::initial : spaces[i] == space::handshake ?
                    hd::quic_packet_kind::handshake : hd::quic_packet_kind::one_rtt;
                h.destination = cid;
                if (spaces[i] != space::application) h.source = cid;
                h.packet_number_width = 4;
                h.length_width = 2;
                const std::array<hd::quic_datagram_packet, 1> measure{{{h, 4, false}}};
                const auto small = hd::size_quic_datagram(measure, hd::quic_endpoint_role::server, 1200);
                const auto overhead = small.wire[0] - 4;
                plans[i] = r.prepare_datagram_packet(spaces[i], plain[i], clock.now(), {1200 - used, overhead, i == 0 && probe_first}, f);
                LT_ASSERT(plans[i]);
                h.packet_number = plans[i].packet_number;
                packets[i].payload_bytes = plans[i].bytes;
                packets[i].ack_eliciting = true;
                used += overhead + plans[i].bytes;
            }
            auto layout = hd::size_quic_datagram(std::span(packets).first(spaces.size()), hd::quic_endpoint_role::server, 1200);
            LT_ASSERT(layout.code == hd::quic_codec_code::ok && layout.total <= 1200);
            hd::io_datagram packet;
            packet.bytes.resize(layout.total);
            std::size_t at = 0;
            for (std::size_t i = 0; i < spaces.size(); ++i) {
                auto& h = packets[i].header;
                h.payload = std::span(plain[i]).first(layout.payload[i]);
                h.tag = tag;
                auto encoded = hd::encode_quic_packet(h, std::span(packet.bytes).subspan(at, layout.wire[i]));
                LT_ASSERT(encoded.code == hd::quic_codec_code::ok && encoded.consumed == layout.wire[i]);
                emitted[i] = {plans[i].token, encoded.consumed, true, true};
                at += encoded.consumed;
            }
            LT_ASSERT(r.commit_datagram(std::span(emitted).first(spaces.size()), clock.now()));
            return packet;
        };
        const std::array<space, 2> handshake{space::initial, space::handshake};
        auto first = batch(handshake, false);
        LT_CHECK(first.bytes.size() == 1200);
        auto lost = net.send(first);
        LT_ASSERT(lost && net.drop(*lost));
        auto due = r.next_deadline();
        LT_ASSERT(due && due->space == space::initial);
        auto elapsed = std::max(std::chrono::nanoseconds::rep{0}, std::chrono::duration_cast<std::chrono::nanoseconds>(due->deadline - clock.now()).count());
        LT_ASSERT(clock.advance(elapsed) && net.advance(elapsed));
        LT_CHECK(r.expire(clock.now()).probes == 2);
        const std::array<space, 1> initial{space::initial};
        auto retried = batch(initial, true);
        LT_CHECK(retried.bytes.size() == 1200 && plans[0].packet_number == 1);
        auto delivered = net.send(retried);
        LT_ASSERT(delivered && net.deliver(*delivered) && net.drain());
        LT_ASSERT(clock.advance(10000000) && net.advance(10000000));
        LT_ASSERT(r.receive_ack(space::initial, ack(plans[0].packet_number), clock.now()));
        LT_ASSERT(r.retain_crypto(space::application, 0, std::span(content).first(1)));
        due = r.next_deadline();
        LT_ASSERT(due && due->space == space::handshake);
        elapsed = std::max(std::chrono::nanoseconds::rep{0}, std::chrono::duration_cast<std::chrono::nanoseconds>(due->deadline - clock.now()).count());
        LT_ASSERT(clock.advance(elapsed) && net.advance(elapsed));
        LT_CHECK(r.expire(clock.now()).probes == 2);
        const std::array<space, 2> final_spaces{space::handshake, space::application};
        auto final = batch(final_spaces, true);
        auto envelope = hd::parse_quic_envelope(final.bytes, cid.size());
        LT_ASSERT(envelope.code == hd::quic_codec_code::ok && envelope.consumed < final.bytes.size());
        auto short_header = hd::parse_quic_envelope(std::span(final.bytes).subspan(envelope.consumed), cid.size());
        LT_CHECK(short_header.code == hd::quic_codec_code::ok && short_header.value.kind == hd::quic_packet_kind::one_rtt);
        auto original = net.send(final);
        LT_ASSERT(original);
        auto reordered = net.duplicate(*original);
        LT_ASSERT(reordered && net.deliver(*reordered) && net.drain());
        LT_ASSERT(clock.advance(10000000) && net.advance(10000000));
        LT_ASSERT(r.receive_ack(space::handshake, ack(plans[0].packet_number), clock.now()));
        LT_ASSERT(r.receive_ack(space::application, ack(plans[1].packet_number), clock.now()));
        LT_ASSERT(net.deliver(*original) && net.drain());
        LT_ASSERT(r.receive_ack(space::handshake, ack(plans[0].packet_number), clock.now()));
        LT_ASSERT(r.take_completion() && r.take_completion() && r.take_completion());
        LT_CHECK(!r.take_completion() && r.bytes_in_flight() == 0);
        net.teardown();
        auto resources = net.resources();
        LT_CHECK(resources.packets == 0 && resources.bytes == 0 && resources.timers == 0 && resources.owner_pending == 0);
        return net.trace();
    };
    LT_CHECK(run() == run());
LT_END_AUTO_TEST(deterministic_initial_coalescing_loss_and_reordering_respects_limits)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
