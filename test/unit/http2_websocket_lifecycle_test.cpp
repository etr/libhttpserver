/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/concurrency/resume_signal.hpp>
#include "./http2_websocket_fixture.hpp"
#include "./littletest.hpp"
using namespace h2ws;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(h2_ws_lifecycle_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(h2_ws_lifecycle_suite)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, peer_close_flushes_close_before_end_stream_and_notifies_once)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    unsigned calls = 0; bool clean = false;
    LT_ASSERT(f.sessions[0].on_close([&](auto info) { ++calls; clean = info.clean; }).ok());
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(0, 1, 1, masked(8, std::string("\3\350", 2)))));
    auto frames = h2test::frames(f.output()); std::vector<h2test::wire_frame> tunnel;
    for (const auto& frame : frames) if (frame.type == 0 && frame.stream == 1) tunnel.push_back(frame);
    LT_ASSERT_EQ(tunnel.size(), 2u); LT_CHECK(tunnel[0].payload == std::vector<std::uint8_t>({136, 2, 3, 232}));
    LT_CHECK_EQ(tunnel[0].flags, 0u); LT_CHECK_EQ(tunnel[1].flags, 1u); LT_CHECK(tunnel[1].payload.empty());
    LT_CHECK_EQ(calls, 1u); LT_CHECK(clean); f.output(); LT_CHECK_EQ(calls, 1u);
    LT_CHECK_EQ(f.budget.in_use(h::server::resource::streams), 0u);
LT_END_AUTO_TEST(peer_close_flushes_close_before_end_stream_and_notifies_once)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, reset_cancels_parked_operations_without_echo_and_releases_stream)
    fixture f; f.options.limits.outgoing_messages = 1; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    const std::string text = "full"; f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(text)));
    unsigned notifications = 0; std::optional<http::outcome> writable, received;
    f.sessions[0].on_close([&](auto info) { ++notifications; LT_CHECK(!info.clean); });
    h::spawn(f.executor, f.sessions[0].writable(), [&](auto result) { writable.emplace(std::move(result.value())); });
    h::spawn(f.executor, f.sessions[0].receive(), [&](auto result) { received.emplace(result.value().status); });
    f.executor.run_pending(); LT_CHECK(!writable); LT_CHECK(!received);
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(3, 0, 1, h2test::increment(8))));
    auto wire = f.output(); LT_CHECK(h2test::resets(wire).empty());
    LT_ASSERT(writable); LT_ASSERT(received); LT_CHECK(!writable->ok()); LT_CHECK(!received->ok()); LT_CHECK_EQ(notifications, 1u);
    LT_CHECK_EQ(f.budget.in_use(h::server::resource::streams), 0u);
LT_END_AUTO_TEST(reset_cancels_parked_operations_without_echo_and_releases_stream)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, abnormal_eof_and_codec_errors_reset_only_the_tunnel)
    for (bool codec_error : {true, false}) {
        fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
        unsigned calls = 0; http::outcome_code reason = http::outcome_code::ok;
        f.sessions[0].on_close([&](auto info) { ++calls; reason = info.status.code(); LT_CHECK(!info.clean); });
        auto wire = codec_error ? h2test::frame(0, 0, 1, {129, 1, 'x'}) : h2test::frame(0, 1, 1);
        h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(f.encoder, h2test::get())));
        LT_ASSERT(h2test::feed(*f.engine, wire)); auto out = f.output(); LT_CHECK(!f.engine->failure());
        auto resets = h2test::resets(out); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(resets[0].stream, 1u); LT_CHECK_EQ(resets[0].code, 8u);
        LT_CHECK_EQ(h2test::responses(out).size(), 1u); LT_CHECK_EQ(calls, 1u);
        LT_CHECK(reason == (codec_error ? http::outcome_code::protocol_error : http::outcome_code::connection_closed));
    }
LT_END_AUTO_TEST(abnormal_eof_and_codec_errors_reset_only_the_tunnel)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, connection_eof_and_destruction_publish_transport_reason)
    for (bool explicit_eof : {true, false}) {
        fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
        unsigned calls = 0; http::outcome_code reason = http::outcome_code::ok;
        f.sessions[0].on_close([&](auto info) { ++calls; reason = info.status.code(); });
        if (explicit_eof) {
            f.engine->eof();
        } else {
            f.engine.reset();
        }
        f.executor.run_pending(); LT_CHECK_EQ(calls, 1u); LT_CHECK(reason == http::outcome_code::connection_closed);
    }
LT_END_AUTO_TEST(connection_eof_and_destruction_publish_transport_reason)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, drain_sends_going_away_close_and_waits_for_wire_retirement)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    h::server::drain_ticket ticket;
    LT_ASSERT(f.engine->begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    auto wire = f.output(); LT_CHECK(data(wire, 1) == std::vector<std::uint8_t>({136, 2, 3, 233}));
    std::vector<std::uint8_t> ping;
    for (const auto& frame : h2test::frames(wire)) if (frame.type == 6 && !frame.flags) ping = frame.payload;
    LT_ASSERT_EQ(ping.size(), 8u); LT_ASSERT(h2test::feed(*f.engine, h2test::frame(6, 1, 0, ping)));
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(0, 1, 1, masked(8, std::string("\3\351", 2)))));
    f.output(); f.engine->check_drain(std::chrono::steady_clock::now()); h::server::drain_result result;
    LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == h::server::drain_status::completed); LT_CHECK_EQ(result.remaining, 0u);
LT_END_AUTO_TEST(drain_sends_going_away_close_and_waits_for_wire_retirement)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, blocked_close_times_out_and_retains_exposed_data)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(h2test::feed(*f.engine, h2test::frame(4, 1)));
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(4, 0, 0, h2test::setting(4, 0))));
    LT_ASSERT(f.open()); f.output(); unsigned calls = 0; http::outcome_code reason = http::outcome_code::ok;
    f.sessions[0].on_close([&](auto info) { ++calls; reason = info.status.code(); });
    LT_CHECK(f.sessions[0].close().ok()); f.output();
    f.engine->check_drain(std::chrono::steady_clock::now() + std::chrono::seconds(11));
    auto resets = h2test::resets(f.output()); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(calls, 1u); LT_CHECK(reason == http::outcome_code::timeout);
LT_END_AUTO_TEST(blocked_close_times_out_and_retains_exposed_data)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, tunnel_trailers_are_refused_and_hpack_state_remains_usable)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    auto fields = std::vector<h::detail::hpack_field>{{"x-context", "retained"}};
    auto wire = h2test::frame(1, 5, 1, h2test::encode(f.encoder, fields));
    auto sibling = h2test::get(); sibling.push_back(fields[0]);
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(f.encoder, sibling)));
    LT_ASSERT(h2test::feed(*f.engine, wire)); auto out = f.output();
    auto resets = h2test::resets(out); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(resets[0].code, 1u);
    LT_CHECK_EQ(h2test::responses(out).size(), 1u); LT_CHECK(!f.engine->failure());
LT_END_AUTO_TEST(tunnel_trailers_are_refused_and_hpack_state_remains_usable)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, reentrant_close_notification_preserves_partially_retired_frame)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    unsigned notifications = 0;
    f.sessions[0].on_close([&](auto info) {
        ++notifications; LT_CHECK(info.clean);
        auto bytes = f.engine->output(); LT_CHECK(bytes.empty());
        LT_CHECK(h2test::feed(*f.engine, h2test::frame(3, 0, 1, h2test::increment(8))));
    });
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(0, 1, 1, masked(8, std::string("\3\350", 2)))));
    auto wire = f.output(); LT_CHECK_EQ(notifications, 1u); LT_CHECK(h2test::resets(wire).empty());
    LT_CHECK(data(wire, 1) == std::vector<std::uint8_t>({136, 2, 3, 232})); LT_CHECK_EQ(f.budget.in_use(h::server::resource::streams), 0u);
LT_END_AUTO_TEST(reentrant_close_notification_preserves_partially_retired_frame)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, admitted_handler_cannot_upgrade_after_drain_begins)
    fixture f; LT_ASSERT(f.start());
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(1, 4, 1, h2test::encode(f.encoder, connect()))));
    h::server::drain_ticket ticket; auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    LT_ASSERT(f.engine->begin_drain(deadline, ticket).ok()); f.executor.run_pending();
    LT_CHECK(f.sessions.empty()); LT_CHECK_EQ(f.refusals, 1u);
    f.engine->check_drain(deadline); f.output();
LT_END_AUTO_TEST(admitted_handler_cannot_upgrade_after_drain_begins)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, stalled_output_uses_write_idle_and_drain_deadline_reports_timeout)
    for (bool drain : {false, true}) {
        fixture f; LT_ASSERT(f.start()); LT_ASSERT(h2test::feed(*f.engine, h2test::frame(4, 1)));
        LT_ASSERT(h2test::feed(*f.engine, h2test::frame(4, 0, 0, h2test::setting(4, 0)))); LT_ASSERT(f.open()); f.output();
        unsigned calls = 0; http::outcome_code reason = http::outcome_code::ok;
        f.sessions[0].on_close([&](auto info) { ++calls; reason = info.status.code(); });
        h::server::drain_ticket ticket; auto now = std::chrono::steady_clock::now();
        if (drain) {
            LT_ASSERT(f.engine->begin_drain(now + std::chrono::seconds(2), ticket).ok());
        } else {
            const std::string text = "wait"; f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(text)));
        }
        f.engine->check_drain(now + std::chrono::seconds(drain ? 3 : 31)); f.output();
        LT_CHECK_EQ(calls, 1u); LT_CHECK(reason == http::outcome_code::timeout);
        if (drain) {
            h::server::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == h::server::drain_status::deadline_expired);
        }
    }
LT_END_AUTO_TEST(stalled_output_uses_write_idle_and_drain_deadline_reports_timeout)
LT_BEGIN_AUTO_TEST(h2_ws_lifecycle_suite, reentrant_drain_expiry_defers_stream_destruction_until_notification_returns)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    unsigned calls = 0;
    f.sessions[0].on_close([&](auto) {
        ++calls; h::server::drain_ticket ticket;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        LT_CHECK(f.engine->begin_drain(deadline, ticket).ok());
        f.engine->check_drain(deadline);
        LT_CHECK_EQ(f.budget.in_use(h::server::resource::streams), 1u);
    });
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(0, 1, 1, masked(8, std::string("\3\350", 2)))));
    f.output(); LT_CHECK_EQ(calls, 1u); LT_CHECK(f.engine->failure().has_value());
LT_END_AUTO_TEST(reentrant_drain_expiry_defers_stream_destruction_until_notification_returns)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
