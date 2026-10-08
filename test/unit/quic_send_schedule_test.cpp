/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <httpserver/detail/quic_flow_control.hpp>
#include <httpserver/detail/quic_recovery.hpp>
#include "../littletest.hpp"
namespace hd = httpserver::detail;
using space = hd::quic_pn_space;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""ns;
using std::chrono_literals::operator""us;
using std::chrono_literals::operator""s;
using std::chrono_literals::operator""h;
namespace {
auto budget() { return httpserver::server::resource_budget::root({}); }
hd::quic_transport_parameters parameters(std::uint64_t limit = 100000) {
    hd::quic_transport_parameters p;
    p.initial_max_data = p.initial_max_stream_data_bidi_remote = limit;
    p.initial_max_streams_bidi = 16;
    return p;
}
hd::quic_ack_frame ack(std::uint64_t pn) { return {pn, 0, 0, 0, {}, {}}; }
void sent(hd::quic_recovery& r, space s, hd::quic_recovery::time_point now, std::size_t bytes = 1200) {
    auto p = r.reserve_packet(s);
    if (!p || !r.commit_sent(p.token, now, bytes, true, true)) throw std::runtime_error("send failed");
}
}  // namespace
LT_BEGIN_SUITE(schedule_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(schedule_suite)
LT_BEGIN_AUTO_TEST(schedule_suite, recovery_grows_once_reduces_once_and_discard_is_not_loss)
    hd::quic_recovery r({}, budget());
    for (unsigned i = 0; i < 4; ++i) sent(r, space::initial, {});
    LT_ASSERT(r.receive_ack(space::initial, ack(3), {}));
    LT_CHECK(r.congestion().window == 6600);
    LT_ASSERT(r.receive_ack(space::initial, ack(3), {}));
    LT_CHECK(r.congestion().window == 6600);
    LT_ASSERT(r.receive_ack(space::initial, ack(0), {}));
    LT_CHECK(r.congestion().window == 6600);
    LT_ASSERT(r.discard_space(space::initial));
    LT_CHECK(r.bytes_in_flight() == 0 && r.congestion().window == 6600);
LT_END_AUTO_TEST(recovery_grows_once_reduces_once_and_discard_is_not_loss)
LT_BEGIN_AUTO_TEST(schedule_suite, wire_admission_pacing_and_abandon_are_transactional)
    hd::quic_recovery r({}, budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 16, budget());
    LT_ASSERT(f.open_local(false));
    std::array<std::byte, 6000> data{};
    std::array<std::byte, 1200> out{};
    LT_ASSERT(r.retain_stream({1, 0, data}));
    auto prepare = [&] { return r.prepare_scheduled_packet(space::application, out, {}, {1200, 40}, f); };
    auto p = prepare();
    LT_ASSERT(p && p.stream);
    LT_CHECK(p.bytes <= 1160 && f.sent() == 0);
    LT_CHECK(r.check_scheduled_emission(p.token, {}, 1201, true, true).code == hd::quic_recovery_code::invalid);
    LT_ASSERT(r.abandon_packet(p.token));
    for (unsigned i = 0; i < 2; ++i) {
        p = prepare();
        LT_ASSERT(p);
        LT_ASSERT(r.check_scheduled_emission(p.token, {}, 1200, true, true));
        LT_ASSERT(r.commit_sent(p.token, {}, 1200, true, true));
    }
    p = prepare();
    LT_CHECK(p.code == hd::quic_recovery_code::pacing_blocked && p.deadline.has_value());
    auto due = r.next_send_deadline();
    LT_ASSERT(due);
    p = r.prepare_scheduled_packet(space::application, out, *due, {1200, 40}, f);
    LT_ASSERT(p);
    LT_ASSERT(r.abandon_packet(p.token));
LT_END_AUTO_TEST(wire_admission_pacing_and_abandon_are_transactional)
LT_BEGIN_AUTO_TEST(schedule_suite, genuine_pto_grants_bypass_exhausted_window_only_on_emission)
    hd::quic_recovery r({}, budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 16, budget());
    std::array<std::byte, 1200> out{};
    for (unsigned i = 0; i < 10; ++i) sent(r, space::initial, {});
    LT_ASSERT(r.retain_crypto(space::initial, 0, std::span(out).first(1)));
    LT_CHECK(r.prepare_scheduled_packet(space::initial, out, {}, {1200, 40}, f).code == hd::quic_recovery_code::congestion_blocked);
    auto timer = r.next_deadline();
    LT_ASSERT(timer);
    auto event = r.expire(timer->deadline);
    LT_CHECK(event.probes == 2 && event.probe_space == space::initial);
    auto request = hd::quic_send_request{1200, 40, true};
    auto p = r.prepare_scheduled_packet(space::initial, out, timer->deadline, request, f);
    LT_ASSERT(p);
    LT_ASSERT(r.abandon_packet(p.token));
    for (unsigned i = 0; i < 2; ++i) {
        p = r.prepare_scheduled_packet(space::initial, out, timer->deadline, request, f);
        LT_ASSERT(p);
        LT_ASSERT(r.commit_sent(p.token, timer->deadline, 1200, true, true));
    }
    LT_CHECK(r.bytes_in_flight() == 14400);
    LT_CHECK(r.prepare_scheduled_packet(space::initial, out, timer->deadline, request, f).code == hd::quic_recovery_code::unavailable);
    LT_CHECK(r.congestion().window == 12000);
LT_END_AUTO_TEST(genuine_pto_grants_bypass_exhausted_window_only_on_emission)
LT_BEGIN_AUTO_TEST(schedule_suite, multiple_segments_share_a_turn_and_abandon_preserves_service)
    hd::quic_recovery r({}, budget());
    std::array<std::byte, 2000> data{};
    std::array<std::byte, 1300> out{};
    for (unsigned i = 0; i < 6; ++i) LT_ASSERT(r.retain_stream({1, i * 200, std::span(data).first(200)}));
    LT_ASSERT(r.retain_stream({5, 0, data}));
    for (unsigned i = 0; i < 6; ++i) {
        auto p = r.prepare_packet(space::application, out, {});
        LT_ASSERT(p && p.stream);
        LT_CHECK(p.stream->stream == 1);
        LT_ASSERT(r.commit_sent(p.token, {}, 250, true, true));
    }
    auto p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p && p.stream);
    LT_CHECK(p.stream->stream == 5 && p.stream->data.size() == 1200);
    LT_ASSERT(r.abandon_packet(p.token));
    p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p && p.stream && p.stream->stream == 5);
LT_END_AUTO_TEST(multiple_segments_share_a_turn_and_abandon_preserves_service)
LT_BEGIN_AUTO_TEST(schedule_suite, data_reserve_preserves_control_and_ack_only_progress)
    hd::quic_recovery r({}, budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 16, budget());
    LT_ASSERT(f.open_local(false));
    std::array<std::byte, 20000> data{};
    std::array<std::byte, 1200> out{};
    LT_ASSERT(r.retain_stream({1, 0, data}));
    for (unsigned i = 0; i < 9; ++i) {
        auto now = hd::quic_recovery::time_point {} + i * 1s;
        auto p = r.prepare_scheduled_packet(space::application, out, now, {1200, 40}, f);
        LT_ASSERT(p);
        LT_ASSERT(r.commit_sent(p.token, now, 1200, true, true));
    }
    LT_CHECK(r.prepare_scheduled_packet(space::application, out, hd::quic_recovery::time_point {} + 10s, {1200, 40}, f).code == hd::quic_recovery_code::congestion_blocked);
    LT_ASSERT(r.retain_crypto(space::handshake, 0, std::span(data).first(1)));
    auto p = r.prepare_scheduled_packet(space::handshake, out, hd::quic_recovery::time_point {} + 10s, {1200, 40}, f);
    LT_ASSERT(p);
    LT_ASSERT(r.commit_sent(p.token, hd::quic_recovery::time_point {} + 10s, 1200, true, true));
    LT_ASSERT(r.receive_packet(space::application, 0, true, {}));
    p = r.prepare_scheduled_packet(space::application, out, hd::quic_recovery::time_point {} + 10s, {1200, 40}, f);
    LT_ASSERT(p && !p.ack_eliciting);
    LT_ASSERT(r.commit_sent(p.token, hd::quic_recovery::time_point {} + 10s, 1200, false, false));
    LT_CHECK(r.bytes_in_flight() == 12000);
LT_END_AUTO_TEST(data_reserve_preserves_control_and_ack_only_progress)
LT_BEGIN_AUTO_TEST(schedule_suite, persistent_duration_requires_sampled_lost_endpoints_and_no_ack_separator)
    hd::quic_recovery r({}, budget());
    sent(r, space::initial, {});
    LT_ASSERT(r.receive_ack(space::initial, ack(0), hd::quic_recovery::time_point {} + 10ms));
    auto before = r.congestion().window;
    sent(r, space::initial, hd::quic_recovery::time_point {} + 20ms);
    sent(r, space::initial, hd::quic_recovery::time_point {} + 210ms);
    sent(r, space::initial, hd::quic_recovery::time_point {} + 221ms);
    LT_ASSERT(r.receive_ack(space::initial, ack(3), hd::quic_recovery::time_point {} + 231ms));
    LT_CHECK(before > 2400 && r.congestion().window == 2400);
    auto collapsed = r.congestion().window;
    r.expire(hd::quic_recovery::time_point {} + 1s);
    LT_CHECK(r.congestion().window == collapsed);
    hd::quic_recovery separated({}, budget());
    sent(separated, space::initial, {});
    LT_ASSERT(separated.receive_ack(space::initial, ack(0), hd::quic_recovery::time_point {} + 10ms));
    sent(separated, space::initial, hd::quic_recovery::time_point {} + 20ms);
    auto middle = separated.reserve_packet(space::handshake);
    LT_ASSERT(middle);
    LT_ASSERT(separated.commit_sent(middle.token, hd::quic_recovery::time_point {} + 50ms, 100, false, false));
    LT_ASSERT(separated.receive_ack(space::handshake, ack(0), hd::quic_recovery::time_point {} + 60ms));
    // Force collection of the acknowledged separator before the loss is known.
    auto collected = separated.reserve_packet(space::handshake);
    LT_ASSERT(collected);
    LT_ASSERT(separated.abandon_packet(collected.token));
    sent(separated, space::initial, hd::quic_recovery::time_point {} + 210ms);
    sent(separated, space::initial, hd::quic_recovery::time_point {} + 221ms);
    LT_ASSERT(separated.receive_ack(space::initial, ack(3), hd::quic_recovery::time_point {} + 231ms));
    LT_CHECK(separated.congestion().window > 2400);
LT_END_AUTO_TEST(persistent_duration_requires_sampled_lost_endpoints_and_no_ack_separator)
LT_BEGIN_AUTO_TEST(schedule_suite, application_limited_and_flow_limited_sends_do_not_grow_window)
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(100), parameters(100), 16, budget());
    LT_ASSERT(f.open_local(false));
    std::array<std::byte, 1200> out{};
    hd::quic_recovery r({}, budget());
    LT_ASSERT(r.retain_stream({1, 0, std::span(out).first(1)}));
    auto p = r.prepare_scheduled_packet(space::application, out, {}, {1200, 40}, f);
    LT_ASSERT(p);
    LT_ASSERT(r.commit_sent(p.token, {}, p.bytes + 40, true, true));
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), hd::quic_recovery::time_point {} + 10ms));
    LT_CHECK(r.congestion().window == 12000);
    LT_ASSERT(r.retain_stream({1, 1, std::span(out).first(500)}));
    p = r.prepare_scheduled_packet(space::application, out, hd::quic_recovery::time_point {} + 20ms, {1200, 40}, f);
    LT_ASSERT(p && p.stream && p.stream->data.size() == 99);
    LT_ASSERT(r.commit_sent(p.token, hd::quic_recovery::time_point {} + 20ms, p.bytes + 40, true, true));
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), hd::quic_recovery::time_point {} + 30ms));
    LT_CHECK(r.congestion().window == 12000);
LT_END_AUTO_TEST(application_limited_and_flow_limited_sends_do_not_grow_window)
LT_BEGIN_AUTO_TEST(schedule_suite, padded_ack_packets_charge_pacing_but_ack_only_bypasses_it)
    hd::quic_recovery r({}, budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 16, budget());
    LT_ASSERT(f.open_local(false));
    std::array<std::byte, 1200> out{};
    LT_ASSERT(r.receive_packet(space::application, 0, true, {}));
    for (unsigned i = 0; i < 2; ++i) {
        LT_ASSERT(r.receive_packet(space::application, i + 1, true, {}));
        auto p = r.prepare_scheduled_packet(space::application, out, {}, {1200, 40}, f);
        LT_ASSERT(p && !p.ack_eliciting);
        LT_ASSERT(r.commit_sent(p.token, {}, 1200, false, true));
    }
    LT_ASSERT(r.retain_stream({1, 0, out}));
    LT_CHECK(r.prepare_scheduled_packet(space::application, out, {}, {1200, 40}, f).code == hd::quic_recovery_code::pacing_blocked);
    LT_ASSERT(r.receive_packet(space::application, 4, true, {}));
    auto p = r.prepare_scheduled_packet(space::application, out, {}, {1200, 40}, f);
    LT_ASSERT(p && !p.ack_eliciting);
    LT_ASSERT(r.commit_sent(p.token, {}, 1200, false, false));
LT_END_AUTO_TEST(padded_ack_packets_charge_pacing_but_ack_only_bypasses_it)
LT_BEGIN_AUTO_TEST(schedule_suite, fair_three_streams_flow_skip_control_preference_and_metadata_release)
    auto b = budget();
    const auto baseline = b.in_use(httpserver::server::resource::quic_reassembly_bytes);
    {
        hd::quic_recovery r({}, b);
        std::array<std::byte, 10000> data{};
        std::array<std::byte, 1300> out{};
        for (auto stream : {1, 5, 61}) LT_ASSERT(r.retain_stream({static_cast<std::uint64_t>(stream), 0, data}));
        for (unsigned i = 0; i < 18; ++i) {
            LT_ASSERT(r.retain_flow({hd::quic_flow_kind::max_data, i + 1}));
            auto p = r.prepare_packet(space::application, out, {});
            LT_ASSERT(p);
            if (i % 3 == 2) {
                LT_ASSERT(p.stream);
                constexpr std::array<std::uint64_t, 3> expected{1, 5, 61};
                LT_CHECK(p.stream->stream == expected[(i / 3) % 3] && p.stream->data.size() == 1200);
            } else {
                LT_CHECK(p.flow.has_value());
            }
            LT_ASSERT(r.commit_sent(p.token, {}, 1250, true, true));
        }
    }
    LT_CHECK(b.in_use(httpserver::server::resource::quic_reassembly_bytes) == baseline);
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 16, b);
    LT_ASSERT(f.open_local(false));
    LT_ASSERT(f.open_local(false));
    hd::quic_recovery r({}, b);
    std::array<std::byte, 1200> out{};
    auto cancelled = r.retain_stream({1, 0, std::span(out).first(1)});
    LT_ASSERT(cancelled);
    LT_ASSERT(r.retain_stream({5, 0, {}, true}));
    LT_ASSERT(r.cancel_information(cancelled.id));
    LT_CHECK(r.prepare_packet(space::application, {}, {}, f).code == hd::quic_recovery_code::no_space);
    auto p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p && p.stream && p.stream->stream == 5 && p.stream->fin);
    LT_CHECK(r.commit_sent(p.token, {}, 0, true, true).code == hd::quic_recovery_code::invalid);
    LT_ASSERT(r.abandon_packet(p.token));
    p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p && p.stream && p.stream->stream == 5);
LT_END_AUTO_TEST(fair_three_streams_flow_skip_control_preference_and_metadata_release)
LT_BEGIN_AUTO_TEST(schedule_suite, permission_rechecks_environment_flow_and_actual_emission_time)
    hd::quic_recovery r({}, budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 16, budget());
    LT_ASSERT(f.open_local(false));
    std::array<std::byte, 1200> out{};
    LT_ASSERT(r.retain_stream({1, 0, out}));
    auto p = r.prepare_scheduled_packet(space::application, out, {}, {1200, 40}, f);
    LT_ASSERT(p);
    auto environment = hd::quic_recovery_environment{};
    environment.send_permitted = false;
    r.set_environment(environment, {});
    LT_CHECK(r.check_scheduled_emission(p.token, {}, 1200, true, true).code == hd::quic_recovery_code::unavailable);
    LT_CHECK(r.commit_sent(p.token, {}, 1200, true, true).code == hd::quic_recovery_code::unavailable && f.sent() == 0);
    environment.send_permitted = true;
    r.set_environment(environment, {});
    LT_ASSERT(r.check_scheduled_emission(p.token, hd::quic_recovery::time_point {} + 1s, 1200, true, true));
    LT_ASSERT(r.commit_sent(p.token, hd::quic_recovery::time_point {} + 1s, 1200, true, true));
    LT_CHECK(r.bytes_in_flight() == 1200);
LT_END_AUTO_TEST(permission_rechecks_environment_flow_and_actual_emission_time)
LT_BEGIN_AUTO_TEST(schedule_suite, final_size_rechecked_before_emission_without_charging_credit)
    hd::quic_recovery r({}, budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 16, budget());
    LT_ASSERT(f.open_local(false));
    std::array<std::byte, 1200> out{};
    LT_ASSERT(r.retain_stream({1, 0, std::span(out).first(8), true}));
    auto p = r.prepare_scheduled_packet(space::application, out, {}, {1200, 40}, f);
    LT_ASSERT(p);
    LT_ASSERT(f.record_stream_sent(1, 0, 4, true));
    LT_CHECK(r.check_scheduled_emission(p.token, {}, 1200, true, true).code == hd::quic_recovery_code::invalid);
    LT_CHECK(r.commit_sent(p.token, {}, 1200, true, true).code == hd::quic_recovery_code::invalid);
    LT_CHECK(r.bytes_in_flight() == 0 && f.sent() == 4);
    LT_ASSERT(r.abandon_packet(p.token));
LT_END_AUTO_TEST(final_size_rechecked_before_emission_without_charging_credit)
LT_BEGIN_AUTO_TEST(schedule_suite, explicit_time_is_monotonic_across_packet_spaces)
    hd::quic_recovery r({}, budget());
    sent(r, space::initial, hd::quic_recovery::time_point {} + 1ms);
    auto p = r.reserve_packet(space::handshake);
    LT_ASSERT(p);
    LT_CHECK(r.commit_sent(p.token, {}, 1200, true, true).code == hd::quic_recovery_code::invalid);
    LT_ASSERT(r.abandon_packet(p.token));
LT_END_AUTO_TEST(explicit_time_is_monotonic_across_packet_spaces)
LT_BEGIN_AUTO_TEST(schedule_suite, persistent_threshold_is_strict_and_pre_sample_losses_do_not_collapse)
    for (auto extra : {0ns, 1ns}) {
        hd::quic_recovery_config c;
        c.peer_max_ack_delay = 0us;
        hd::quic_recovery r(c, budget());
        sent(r, space::initial, {});
        LT_ASSERT(r.receive_ack(space::initial, ack(0), {}));
        sent(r, space::initial, hd::quic_recovery::time_point {} + 1ms);
        sent(r, space::initial, hd::quic_recovery::time_point {} + 4ms + extra);
        sent(r, space::initial, hd::quic_recovery::time_point {} + 5ms + extra);
        LT_ASSERT(r.receive_ack(space::initial, ack(3), hd::quic_recovery::time_point {} + 5ms + extra));
        LT_CHECK((r.congestion().window == 2400) == (extra == 1ns));
    }
    hd::quic_recovery r({}, budget());
    sent(r, space::initial, {});
    sent(r, space::initial, hd::quic_recovery::time_point {} + 1s);
    sent(r, space::initial, hd::quic_recovery::time_point {} + 2s);
    LT_ASSERT(r.receive_ack(space::initial, ack(2), hd::quic_recovery::time_point {} + 2s));
    LT_CHECK(r.congestion().window > 2400);
LT_END_AUTO_TEST(persistent_threshold_is_strict_and_pre_sample_losses_do_not_collapse)
LT_BEGIN_AUTO_TEST(schedule_suite, ack_after_ack_only_packet_collection_still_separates_persistent_loss)
    hd::quic_recovery r({}, budget());
    sent(r, space::initial, {});
    LT_ASSERT(r.receive_ack(space::initial, ack(0), hd::quic_recovery::time_point {} + 10ms));
    sent(r, space::initial, hd::quic_recovery::time_point {} + 20ms);
    auto p = r.reserve_packet(space::handshake);
    LT_ASSERT(p);
    LT_ASSERT(r.commit_sent(p.token, hd::quic_recovery::time_point {} + 50ms, 100, false, false));
    p = r.reserve_packet(space::handshake);
    LT_ASSERT(p);
    LT_ASSERT(r.abandon_packet(p.token));
    LT_ASSERT(r.receive_ack(space::handshake, ack(0), hd::quic_recovery::time_point {} + 60ms));
    sent(r, space::initial, hd::quic_recovery::time_point {} + 210ms);
    sent(r, space::initial, hd::quic_recovery::time_point {} + 221ms);
    LT_ASSERT(r.receive_ack(space::initial, ack(3), hd::quic_recovery::time_point {} + 231ms));
    LT_CHECK(r.congestion().window > 2400);
LT_END_AUTO_TEST(ack_after_ack_only_packet_collection_still_separates_persistent_loss)
LT_BEGIN_AUTO_TEST(schedule_suite, acknowledgments_retire_unused_probe_grants)
    hd::quic_recovery r({}, budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 16, budget());
    std::array<std::byte, 1200> out{};
    sent(r, space::initial, {});
    auto timer = r.next_deadline();
    LT_ASSERT(timer);
    LT_CHECK(r.expire(timer->deadline).probes == 2);
    LT_ASSERT(r.receive_ack(space::initial, ack(0), timer->deadline));
    LT_ASSERT(r.retain_crypto(space::initial, 0, std::span(out).first(1)));
    LT_CHECK(r.prepare_scheduled_packet(space::initial, out, timer->deadline, {1200, 40, true}, f).code == hd::quic_recovery_code::unavailable);
LT_END_AUTO_TEST(acknowledgments_retire_unused_probe_grants)
LT_BEGIN_AUTO_TEST(schedule_suite, final_wire_check_exposes_pacing_deadline_and_invalid_requests_do_not_prepare)
    hd::quic_recovery r({}, budget());
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 16, budget());
    std::array<std::byte, 1200> out{};
    LT_CHECK(r.prepare_scheduled_packet(space::initial, out, {}, {1200, 1200}, f).code == hd::quic_recovery_code::invalid);
    sent(r, space::initial, {}, 2300);
    LT_ASSERT(r.retain_crypto(space::initial, 0, std::span(out).first(1)));
    auto p = r.prepare_scheduled_packet(space::initial, out, {}, {1200, 40}, f);
    LT_ASSERT(p);
    auto permission = r.check_scheduled_emission(p.token, {}, 1200, true, true);
    LT_ASSERT(permission.code == hd::quic_recovery_code::pacing_blocked && permission.deadline);
    LT_CHECK(r.next_send_deadline() == permission.deadline);
    LT_ASSERT(r.check_scheduled_emission(p.token, *permission.deadline, 1200, true, true));
    LT_CHECK(!r.next_send_deadline());
    LT_ASSERT(r.abandon_packet(p.token));
LT_END_AUTO_TEST(final_wire_check_exposes_pacing_deadline_and_invalid_requests_do_not_prepare)
LT_BEGIN_AUTO_TEST(schedule_suite, flow_blocked_stream_does_not_stall_another_stream)
    auto peer = parameters();
    peer.initial_max_stream_data_bidi_remote = 4;
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), peer, 16, budget());
    LT_ASSERT(f.open_local(false));
    LT_ASSERT(f.open_local(false));
    LT_ASSERT(f.record_stream_sent(1, 0, 4, false));
    hd::quic_recovery r({}, budget());
    std::array<std::byte, 100> out{};
    LT_ASSERT(r.retain_stream({1, 4, std::span(out).first(1)}));
    LT_ASSERT(r.retain_stream({5, 0, std::span(out).first(4), true}));
    auto p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p && p.stream && p.stream->stream == 5);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_CHECK(f.sent() == 8);
LT_END_AUTO_TEST(flow_blocked_stream_does_not_stall_another_stream)
LT_BEGIN_AUTO_TEST(schedule_suite, incremental_loss_survives_collection_and_collapses_only_once)
    hd::quic_recovery r({}, budget());
    sent(r, space::initial, {});
    LT_ASSERT(r.receive_ack(space::initial, ack(0), hd::quic_recovery::time_point {} + 10ms));
    sent(r, space::initial, hd::quic_recovery::time_point {} + 20ms);
    sent(r, space::initial, hd::quic_recovery::time_point {} + 210ms);
    sent(r, space::initial, hd::quic_recovery::time_point {} + 211ms);
    LT_CHECK(r.receive_ack(space::initial, ack(3), hd::quic_recovery::time_point {} + 221ms).lost_bytes == 1200);
    LT_CHECK(r.congestion().window > 2400);
    auto p = r.reserve_packet(space::initial);
    LT_ASSERT(p);
    LT_ASSERT(r.abandon_packet(p.token));
    auto timer = r.next_deadline();
    LT_ASSERT(timer && timer->action == hd::quic_recovery_timer::kind::detect_loss);
    LT_CHECK(r.expire(timer->deadline).lost_bytes == 1200);
    LT_CHECK(r.congestion().window == 2400);
    LT_CHECK(r.expire(timer->deadline).lost_bytes == 0 && r.congestion().window == 2400);
LT_END_AUTO_TEST(incremental_loss_survives_collection_and_collapses_only_once)
LT_BEGIN_AUTO_TEST(schedule_suite, continuously_replenished_ack_work_has_bounded_data_preference)
    hd::quic_recovery r({}, budget());
    std::array<std::byte, 1200> out{};
    LT_ASSERT(r.retain_stream({1, 0, std::span(out).first(8)}));
    for (unsigned i = 0; i < 3; ++i) {
        LT_ASSERT(r.receive_packet(space::application, i, true, {}));
        auto p = r.prepare_packet(space::application, out, {});
        LT_ASSERT(p);
        LT_CHECK(p.stream.has_value() == (i == 2));
        LT_ASSERT(r.commit_sent(p.token, {}, 100, p.ack_eliciting, p.ack_eliciting));
    }
LT_END_AUTO_TEST(continuously_replenished_ack_work_has_bounded_data_preference)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
