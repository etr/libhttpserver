/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <chrono>
#include <httpserver/detail/quic_recovery.hpp>
#include "../littletest.hpp"
#include "../support/quic_network_harness.hpp"
namespace hd = httpserver::detail;
using space = hd::quic_pn_space;
using namespace std::chrono_literals;  // NOLINT(build/namespaces)
namespace {
using time_point = hd::quic_recovery::time_point;
auto budget() { return httpserver::server::resource_budget::root({}); }
hd::quic_ack_frame ack(std::uint64_t largest, std::uint64_t length = 0, std::uint64_t delay = 0) {
    return {largest, delay, length, 0, {}, {}};
}
std::uint64_t send(hd::quic_recovery& r, space s, time_point now = {}, std::size_t bytes = 100, bool eliciting = true) {
    auto p = r.reserve_packet(s);
    if (!p || !r.commit_sent(p.token, now, bytes, eliciting, true)) throw std::runtime_error("Send admission failed");
    return p.packet_number;
}
}  // namespace
LT_BEGIN_SUITE(recovery_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(recovery_suite)
LT_BEGIN_AUTO_TEST(recovery_suite, independent_spaces_range_selection_and_duplicate_accounting)
    hd::quic_recovery r({}, budget());
    for (auto s : {space::initial, space::handshake, space::application}) {
        send(r, s); send(r, s); send(r, s);
    }
    LT_CHECK(r.bytes_in_flight() == 900);
    auto event = r.receive_ack(space::handshake, ack(2, 1), time_point() + 1ms);
    LT_ASSERT(event);
    LT_CHECK(event.acknowledged_bytes == 200 && r.bytes_in_flight() == 700);
    event = r.receive_ack(space::handshake, ack(2, 1), time_point() + 2ms);
    LT_CHECK(event && event.acknowledged_bytes == 0 && r.bytes_in_flight() == 700);
    event = r.receive_ack(space::initial, ack(0), time_point() + 2ms);
    LT_CHECK(event.acknowledged_bytes == 100 && r.bytes_in_flight() == 600);
LT_END_AUTO_TEST(independent_spaces_range_selection_and_duplicate_accounting)
LT_BEGIN_AUTO_TEST(recovery_suite, malformed_ranges_and_unsent_numbers_roll_back)
    hd::quic_recovery r({}, budget());
    send(r, space::initial); send(r, space::initial);
    std::array bad{std::byte{10}, std::byte{0}};
    hd::quic_ack_frame malformed{1, 0, 0, 1, bad, {}};
    LT_CHECK(r.receive_ack(space::initial, malformed, time_point() + 1ms).code == hd::quic_recovery_code::invalid);
    LT_CHECK(r.bytes_in_flight() == 200);
    LT_CHECK(r.receive_ack(space::initial, ack(2), time_point() + 1ms).code == hd::quic_recovery_code::invalid);
    auto burned = r.reserve_packet(space::initial);
    LT_ASSERT(burned);
    LT_ASSERT(r.abandon_packet(burned.token));
    auto next = send(r, space::initial);
    LT_CHECK(next == burned.packet_number + 1);
    LT_CHECK(r.receive_ack(space::initial, ack(next, 1), time_point() + 1ms).code == hd::quic_recovery_code::invalid);
    LT_CHECK(r.bytes_in_flight() == 300);
    LT_CHECK(r.receive_ack(space::initial, ack(1, 1), time_point() + 1ms).acknowledged_bytes == 200);
LT_END_AUTO_TEST(malformed_ranges_and_unsent_numbers_roll_back)
LT_BEGIN_AUTO_TEST(recovery_suite, capacity_reservation_is_transactional)
    hd::quic_recovery_config config;
    config.max_sent_packets = 1;
    hd::quic_recovery r(config, budget());
    auto p = r.reserve_packet(space::initial);
    LT_ASSERT(p);
    LT_CHECK(r.reserve_packet(space::handshake).code == hd::quic_recovery_code::busy);
    LT_CHECK(r.bytes_in_flight() == 0);
    LT_CHECK(!r.commit_sent(p.token + 1, {}, 100, true, true));
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_CHECK(r.reserve_packet(space::initial).code == hd::quic_recovery_code::capacity);
    LT_ASSERT(r.receive_ack(space::initial, ack(0), time_point() + 1ms));
    auto next = r.reserve_packet(space::initial);
    LT_ASSERT(next);
    LT_CHECK(next.packet_number == 1);
    LT_ASSERT(r.abandon_packet(next.token));
    LT_CHECK(r.reserve_packet(static_cast<space>(3)).code == hd::quic_recovery_code::invalid);
    httpserver::server::budget_limits limits;
    limits.set(httpserver::server::resource::quic_reassembly_bytes, 1);
    hd::quic_recovery refused(config, httpserver::server::resource_budget::root(limits));
    LT_CHECK(refused.reserve_packet(space::initial).code == hd::quic_recovery_code::no_memory);
LT_END_AUTO_TEST(capacity_reservation_is_transactional)
LT_BEGIN_AUTO_TEST(recovery_suite, rtt_first_sample_minimum_delay_cap_and_ewma)
    hd::quic_recovery_config config;
    config.peer_ack_delay_exponent = 0;
    hd::quic_recovery r(config, budget());
    hd::quic_recovery_environment env;
    env.handshake_confirmed = true;
    r.set_environment(env, {});
    send(r, space::application);
    LT_ASSERT(r.receive_ack(space::application, ack(0, 0, 50000), time_point() + 100ms));
    LT_CHECK(r.rtt().sampled && r.rtt().minimum == 100ms && r.rtt().smoothed == 100ms && r.rtt().variation == 50ms);
    send(r, space::application, time_point() + 100ms);
    LT_ASSERT(r.receive_ack(space::application, ack(1, 0, 30000), time_point() + 240ms));
    LT_CHECK(r.rtt().latest == 140ms && r.rtt().minimum == 100ms);
    LT_CHECK(r.rtt().smoothed == 101875us && r.rtt().variation == 41250us);
    auto before = r.rtt();
    LT_ASSERT(r.receive_ack(space::application, ack(1), time_point() + 300ms));
    LT_CHECK(r.rtt().latest == before.latest && r.rtt().smoothed == before.smoothed);
LT_END_AUTO_TEST(rtt_first_sample_minimum_delay_cap_and_ewma)
LT_BEGIN_AUTO_TEST(recovery_suite, rtt_requires_new_largest_and_a_new_eliciting_packet)
    hd::quic_recovery r({}, budget());
    send(r, space::initial, {}, 100, false);
    LT_ASSERT(r.receive_ack(space::initial, ack(0), time_point() + 1ms));
    LT_CHECK(!r.rtt().sampled);
    send(r, space::initial, time_point() + 1ms);
    send(r, space::initial, time_point() + 1ms, 100, false);
    LT_ASSERT(r.receive_ack(space::initial, ack(2, 1, hd::k_quic_max_integer), time_point() + 11ms));
    LT_CHECK(r.rtt().sampled && r.rtt().latest == 10ms);
    send(r, space::handshake, time_point() + 11ms);
    LT_ASSERT(r.receive_ack(space::handshake, ack(0, 0, hd::k_quic_max_integer), time_point() + 31ms));
    LT_CHECK(r.rtt().latest == 20ms && r.rtt().minimum == 10ms && r.rtt().smoothed == 11250us);
LT_END_AUTO_TEST(rtt_requires_new_largest_and_a_new_eliciting_packet)
LT_BEGIN_AUTO_TEST(recovery_suite, packet_threshold_three_is_space_local)
    hd::quic_recovery r({}, budget());
    for (unsigned n = 0; n < 4; ++n) {
        send(r, space::initial);
        send(r, space::handshake);
    }
    auto event = r.receive_ack(space::initial, ack(3), {});
    LT_ASSERT(event);
    LT_CHECK(event.acknowledged_bytes == 100 && event.lost_bytes == 100);
    LT_CHECK(r.bytes_in_flight() == 600);
    LT_CHECK(r.receive_ack(space::initial, ack(0), time_point() + 1ms).acknowledged_bytes == 0);
    LT_CHECK(r.bytes_in_flight() == 600);
LT_END_AUTO_TEST(packet_threshold_three_is_space_local)
LT_BEGIN_AUTO_TEST(recovery_suite, time_threshold_boundary_and_one_ms_granularity)
    for (auto sample : {0ms, 8ms}) {
        hd::quic_recovery r({}, budget());
        send(r, space::initial); send(r, space::initial); send(r, space::application);
        auto event = r.receive_ack(space::initial, ack(1), time_point() + sample);
        LT_ASSERT(event);
        LT_CHECK(event.lost_bytes == 0 && r.bytes_in_flight() == 200);
        auto boundary = sample == 0ms ? 1ms : 9ms;
        LT_CHECK(r.expire(time_point() + boundary - 1ns).lost_bytes == 0);
        LT_CHECK(r.expire(time_point() + boundary).lost_bytes == 100);
        LT_CHECK(r.bytes_in_flight() == 100 && r.expire(time_point() + boundary).lost_bytes == 0);
    }
LT_END_AUTO_TEST(time_threshold_boundary_and_one_ms_granularity)
LT_BEGIN_AUTO_TEST(recovery_suite, initial_pto_backoff_probe_emission_and_ack_reset)
    hd::quic_recovery r({}, budget());
    send(r, space::initial);
    auto timer = r.next_deadline();
    LT_ASSERT(timer);
    LT_CHECK(timer->action == hd::quic_recovery_timer::kind::probe && timer->deadline == time_point() + 999ms);
    LT_CHECK(r.expire(time_point() + 999ms - 1ns).probes == 0);
    auto event = r.expire(time_point() + 999ms);
    LT_CHECK(event.probes == 2 && event.probe_space == space::initial && event.lost_bytes == 0 && r.bytes_in_flight() == 100);
    LT_CHECK(r.pto_count() == 1 && r.expire(time_point() + 999ms).probes == 0);
    send(r, space::initial, time_point() + 999ms);
    timer = r.next_deadline();
    LT_ASSERT(timer);
    LT_CHECK(timer->deadline == time_point() + 2997ms);
    event = r.expire(time_point() + 2997ms);
    LT_CHECK(event.probes == 2 && r.pto_count() == 2 && r.bytes_in_flight() == 200);
    send(r, space::initial, time_point() + 2997ms);
    LT_ASSERT(r.receive_ack(space::initial, ack(2), time_point() + 3000ms));
    LT_CHECK(r.pto_count() == 0);
LT_END_AUTO_TEST(initial_pto_backoff_probe_emission_and_ack_reset)
LT_BEGIN_AUTO_TEST(recovery_suite, non_eliciting_ack_progress_resets_pto_without_sampling_rtt)
    // ACK-only packets are not in flight; PADDING packets are in flight.
    for (bool in_flight : {false, true}) {
        hd::quic_recovery r({}, budget());
        send(r, space::initial);
        LT_CHECK(r.expire(time_point() + 999ms).probes == 2 && r.pto_count() == 1);
        auto packet = r.reserve_packet(space::handshake);
        LT_ASSERT(packet);
        LT_ASSERT(r.commit_sent(packet.token, time_point() + 1000ms, 100, false, in_flight));
        auto before = r.rtt();
        auto event = r.receive_ack(space::handshake, ack(packet.packet_number), time_point() + 1001ms);
        LT_ASSERT(event);
        LT_CHECK(event.acknowledged_packets == 1 && event.acknowledged_bytes == (in_flight ? 100 : 0));
        LT_CHECK(r.pto_count() == 0);
        auto after = r.rtt();
        LT_CHECK(after.sampled == before.sampled && after.latest == before.latest && after.minimum == before.minimum);
        LT_CHECK(after.smoothed == before.smoothed && after.variation == before.variation);
        LT_ASSERT(r.next_deadline());
        LT_CHECK(r.next_deadline()->deadline == time_point() + 999ms);
        LT_CHECK(r.expire(time_point() + 1001ms).probes == 2 && r.pto_count() == 1);
        event = r.receive_ack(space::handshake, ack(packet.packet_number), time_point() + 1002ms);
        LT_CHECK(event && event.acknowledged_packets == 0 && r.pto_count() == 1);
        LT_ASSERT(r.next_deadline());
        LT_CHECK(r.next_deadline()->deadline == time_point() + 2999ms);
    }
LT_END_AUTO_TEST(non_eliciting_ack_progress_resets_pto_without_sampling_rtt)
LT_BEGIN_AUTO_TEST(recovery_suite, non_eliciting_initial_ack_preserves_unvalidated_client_backoff)
    hd::quic_recovery_config config;
    config.role = hd::quic_endpoint_role::client;
    hd::quic_recovery r(config, budget());
    hd::quic_recovery_environment env;
    env.peer_validated_endpoint = false;
    r.set_environment(env, {});
    send(r, space::initial);
    LT_CHECK(r.expire(time_point() + 999ms).probes == 2 && r.pto_count() == 1);
    send(r, space::initial, time_point() + 1000ms, 100, false);
    auto event = r.receive_ack(space::initial, ack(1), time_point() + 1001ms);
    LT_CHECK(event && event.acknowledged_packets == 1 && r.pto_count() == 1);
    LT_CHECK(!r.rtt().sampled);
    LT_ASSERT(r.next_deadline());
    LT_CHECK(r.next_deadline()->deadline == time_point() + 2997ms);
    env.peer_validated_endpoint = true;
    r.set_environment(env, time_point() + 1002ms);
    send(r, space::initial, time_point() + 1002ms, 100, false);
    event = r.receive_ack(space::initial, ack(2), time_point() + 1003ms);
    LT_CHECK(event && event.acknowledged_packets == 1 && r.pto_count() == 0);
    LT_CHECK(!r.rtt().sampled);
    LT_CHECK(!r.next_deadline());
LT_END_AUTO_TEST(non_eliciting_initial_ack_preserves_unvalidated_client_backoff)
LT_BEGIN_AUTO_TEST(recovery_suite, non_eliciting_handshake_ack_validates_client_and_resets_pto)
    hd::quic_recovery_config config;
    config.role = hd::quic_endpoint_role::client;
    hd::quic_recovery r(config, budget());
    hd::quic_recovery_environment env;
    env.peer_validated_endpoint = false;
    r.set_environment(env, {});
    LT_CHECK(r.expire(time_point() + 999ms).probes == 1 && r.pto_count() == 1);
    auto packet = r.reserve_packet(space::handshake);
    LT_ASSERT(packet);
    LT_ASSERT(r.commit_sent(packet.token, time_point() + 1000ms, 100, false, false));
    auto event = r.receive_ack(space::handshake, ack(packet.packet_number), time_point() + 1001ms);
    LT_CHECK(event && event.acknowledged_packets == 1 && r.pto_count() == 0);
    LT_CHECK(!r.rtt().sampled && !r.next_deadline());
LT_END_AUTO_TEST(non_eliciting_handshake_ack_validates_client_and_resets_pto)
LT_BEGIN_AUTO_TEST(recovery_suite, application_pto_confirmation_keys_and_amplification)
    hd::quic_recovery r({}, budget());
    send(r, space::application);
    LT_CHECK(!r.next_deadline());
    hd::quic_recovery_environment env;
    env.handshake_confirmed = true;
    r.set_environment(env, {});
    LT_ASSERT(r.next_deadline());
    LT_CHECK(r.next_deadline()->deadline == time_point() + 1024ms);
    env.send_permitted = false;
    r.set_environment(env, time_point() + 1s);
    LT_CHECK(!r.next_deadline() && r.expire(time_point() + 2s).probes == 0);
    env.send_permitted = true;
    env.write_keys[2] = false;
    r.set_environment(env, time_point() + 2s);
    LT_CHECK(!r.next_deadline());
    env.write_keys[2] = true;
    r.set_environment(env, time_point() + 2s);
    LT_CHECK(r.expire(time_point() + 2s).probes == 2);
    auto discarded = r.discard_space(space::application);
    LT_CHECK(discarded.discarded_bytes == 100 && discarded.lost_bytes == 0 && r.bytes_in_flight() == 0);
    LT_CHECK(!r.next_deadline() && r.pto_count() == 0);
    LT_CHECK(r.discard_space(space::application).discarded_bytes == 0);
    LT_CHECK(r.reserve_packet(space::application).code == hd::quic_recovery_code::discarded);
LT_END_AUTO_TEST(application_pto_confirmation_keys_and_amplification)
LT_BEGIN_AUTO_TEST(recovery_suite, loss_timer_precedes_pto_and_runs_while_send_blocked)
    hd::quic_recovery r({}, budget());
    send(r, space::initial); send(r, space::initial);
    LT_ASSERT(r.receive_ack(space::initial, ack(1), time_point() + 8ms));
    hd::quic_recovery_environment env;
    env.send_permitted = false;
    r.set_environment(env, time_point() + 8ms);
    auto timer = r.next_deadline();
    LT_ASSERT(timer);
    LT_CHECK(timer->action == hd::quic_recovery_timer::kind::detect_loss && timer->deadline == time_point() + 9ms);
    auto result = r.expire(time_point() + 10s);
    LT_CHECK(result.lost_bytes == 100 && result.probes == 0 && !r.next_deadline());
LT_END_AUTO_TEST(loss_timer_precedes_pto_and_runs_while_send_blocked)
LT_BEGIN_AUTO_TEST(recovery_suite, client_anti_deadlock_uses_distinct_peer_validation_fact)
    hd::quic_recovery_config config;
    config.role = hd::quic_endpoint_role::client;
    hd::quic_recovery r(config, budget());
    hd::quic_recovery_environment env;
    env.peer_validated_endpoint = false;
    r.set_environment(env, {});
    auto timer = r.next_deadline();
    LT_ASSERT(timer);
    LT_CHECK(timer->space == space::handshake && timer->deadline == time_point() + 999ms);
    auto event = r.expire(time_point() + 999ms);
    LT_CHECK(event.probes == 1 && r.pto_count() == 1);
    send(r, space::initial, time_point() + 999ms);
    LT_ASSERT(r.receive_ack(space::initial, ack(0), time_point() + 1000ms));
    LT_CHECK(r.pto_count() == 1);
    env.peer_validated_endpoint = true;
    r.set_environment(env, time_point() + 1000ms);
    LT_CHECK(!r.next_deadline());
    auto p = r.reserve_packet(space::initial);
    LT_ASSERT(p);
    LT_ASSERT(r.discard_space(space::initial));
    LT_CHECK(!r.commit_sent(p.token, time_point() + 1001ms, 100, true, true));
LT_END_AUTO_TEST(client_anti_deadlock_uses_distinct_peer_validation_fact)
LT_BEGIN_AUTO_TEST(recovery_suite, late_pto_calls_and_saturated_deadlines_are_bounded)
    hd::quic_recovery r({}, budget());
    send(r, space::initial);
    LT_CHECK(r.expire(time_point() + 100s).probes == 2);
    LT_CHECK(r.expire(time_point() + 100s).probes == 0);
    LT_ASSERT(r.next_deadline());
    LT_CHECK(r.next_deadline()->deadline == time_point() + 101998ms);
    hd::quic_recovery extreme({}, budget());
    send(extreme, space::initial, time_point::max() - 1ns);
    LT_ASSERT(extreme.next_deadline());
    LT_CHECK(extreme.next_deadline()->deadline == time_point::max());
LT_END_AUTO_TEST(late_pto_calls_and_saturated_deadlines_are_bounded)
LT_BEGIN_AUTO_TEST(recovery_suite, bounded_drop_reorder_duplicate_wire_ack_scenario)
    quic_test::network_harness rig;
    quic_test::logical_clock clock;
    LT_ASSERT(rig.register_endpoint(1));
    hd::quic_recovery sender({}, budget()), receiver({}, budget());
    std::array<std::uint64_t, 4> ids{};
    for (unsigned n = 0; n < 4; ++n) {
        auto number = send(sender, space::initial, clock.now(), 4);
        auto id = rig.send(quic_test::short_packet(1, static_cast<unsigned>(number)));
        LT_ASSERT(id);
        ids[n] = *id;
    }
    LT_ASSERT(rig.drop(ids[0]));
    auto duplicate = rig.duplicate(ids[3]);
    LT_ASSERT(duplicate);
    for (auto id : {ids[3], ids[1], *duplicate}) {
        LT_CHECK(rig.deliver(id) == hd::datagram_dispatch_code::routed);
        LT_ASSERT(rig.drain());
    }
    unsigned applied = 0;
    for (const auto& datagram : rig.delivered()) {
        auto number = std::to_integer<unsigned>(datagram.bytes.back());
        if (receiver.inspect_received(space::initial, number) == hd::quic_receipt::fresh) ++applied;
        LT_ASSERT(receiver.receive_packet(space::initial, number, true, clock.now()));
    }
    LT_CHECK(applied == 2);
    std::array<std::byte, 100> wire{};
    auto published = receiver.prepare_ack(space::initial, wire, clock.now());
    LT_ASSERT(published);
    hd::quic_frame_cursor cursor;
    auto f = hd::next_quic_frame(std::span(wire).first(published.bytes), cursor);
    LT_ASSERT(f.code == hd::quic_codec_code::ok);
    auto result = sender.receive_ack(space::initial, std::get<hd::quic_ack_frame>(f.value), clock.now());
    LT_CHECK(result.acknowledged_bytes == 8 && result.lost_bytes == 4 && sender.bytes_in_flight() == 4);
    LT_ASSERT(clock.advance(1000000));
    LT_CHECK(sender.expire(clock.now()).lost_bytes == 4);
    rig.teardown();
    LT_CHECK(rig.resources().packets == 0 && rig.resources().owner_pending == 0);
LT_END_AUTO_TEST(bounded_drop_reorder_duplicate_wire_ack_scenario)
LT_BEGIN_AUTO_TEST(recovery_suite, peer_maximum_ack_delay_accepts_transport_parameter_range)
    hd::quic_recovery_config config;
    config.peer_max_ack_delay = 2000ms;
    hd::quic_recovery r(config, budget());
    hd::quic_recovery_environment env;
    env.handshake_confirmed = true;
    r.set_environment(env, {});
    send(r, space::application);
    LT_ASSERT(r.next_deadline());
    LT_CHECK(r.next_deadline()->deadline == time_point() + 2999ms);
    config.peer_max_ack_delay = 16384ms;
    bool rejected = false;
    try {
        hd::quic_recovery invalid(config, budget());
    } catch (const std::invalid_argument&) { rejected = true; }
    LT_CHECK(rejected);
LT_END_AUTO_TEST(peer_maximum_ack_delay_accepts_transport_parameter_range)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
