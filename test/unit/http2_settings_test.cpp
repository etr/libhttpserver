/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <vector>
#include <httpserver/detail/http2_connection.hpp>
#include "./http2_fixture.hpp"
#include "./littletest.hpp"
using namespace h2test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(http2_settings_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_settings_suite)
LT_BEGIN_AUTO_TEST(http2_settings_suite, initial_settings_precedes_peer_ack_and_directions_are_independent)
    hd::http2_connection c(budget());
    LT_CHECK_EQ(c.peer_settings().header_table_size, 4096U);
    LT_CHECK(!c.peer_settings().max_concurrent_streams);
    LT_CHECK(!c.peer_settings().max_header_list_size);
    const auto first = c.output();
    LT_ASSERT(first.size() >= 9);
    LT_CHECK_EQ(first[3], 4);
    LT_CHECK_EQ(first[4], 0);
    LT_ASSERT(c.advance_output(first.size()));
    LT_CHECK(c.feed(preface()).progress == hd::http2_progress::control_ready);
    const auto ack = c.output();
    LT_ASSERT(ack.size() == 9);
    LT_CHECK_EQ(ack[4], 1);
    c.advance_output(9);
    LT_CHECK(c.feed(frame(4, 1)).progress == hd::http2_progress::control_ready);
    LT_CHECK(c.output().empty());
    auto values = setting(1, 0);
    append(values, setting(65000, 17));
    append(values, setting(1, UINT32_MAX));
    append(values, setting(2, 0));
    append(values, setting(3, 0));
    append(values, setting(4, 0x7fffffff));
    append(values, setting(5, 0xffffff));
    append(values, setting(6, 0));
    LT_CHECK(c.feed(frame(4, 0, 0, values)).progress == hd::http2_progress::control_ready);
    LT_CHECK_EQ(c.peer_settings().header_table_size, UINT32_MAX);
    LT_CHECK_EQ(c.peer_settings().max_frame_size, 0xffffffU);
    LT_CHECK_EQ(c.peer_settings().initial_window_size, 0x7fffffffU);
    LT_CHECK_EQ(c.peer_settings().max_concurrent_streams.value(), 0U);
    LT_CHECK_EQ(c.compression().encoder().peer_maximum(), UINT32_MAX);
    LT_CHECK_EQ(c.compression().encoder().table().capacity(), 0U);
    LT_CHECK_EQ(c.compression().decoder().acknowledged_maximum(), 4096U);
    hd::http2_connection other(budget());
    LT_CHECK_EQ(other.peer_settings().max_frame_size, 16384U);
    LT_CHECK_EQ(other.compression().encoder().table().capacity(), 4096U);
    LT_CHECK(c.feed(frame(99, 0, 0, std::vector<std::uint8_t>(16385))).error.has_value());
LT_END_AUTO_TEST(initial_settings_precedes_peer_ack_and_directions_are_independent)
LT_BEGIN_AUTO_TEST(http2_settings_suite, every_invalid_occurrence_aborts_without_successful_ack)
    for (auto [id, value, code] : std::vector<std::tuple<unsigned, std::uint32_t, hd::http2_error_code>>{
        {2, 2, hd::http2_error_code::protocol_error}, {4, 0x80000000, hd::http2_error_code::flow_control_error},
        {5, 16383, hd::http2_error_code::protocol_error}, {5, 0x1000000, hd::http2_error_code::protocol_error}}) {
        hd::http2_connection c(budget());
        auto first = c.output();
        c.advance_output(first.size());
        auto wire = std::vector<std::uint8_t>(hd::http2_magic.begin(), hd::http2_magic.end());
        auto values = setting(id, value);
        append(values, setting(id, id == 5 ? 16384 : 0));
        append(wire, frame(4, 0, 0, values));
        auto result = c.feed(wire);
        LT_ASSERT(result.error.has_value());
        LT_CHECK(result.error->wire_code == code);
        LT_CHECK_EQ(c.peer_settings().header_table_size, 4096U);
        LT_CHECK_EQ(c.output()[3], 7);
        LT_CHECK_EQ(c.feed(preface()).consumed, 0U);
    }
LT_END_AUTO_TEST(every_invalid_occurrence_aborts_without_successful_ack)
LT_BEGIN_AUTO_TEST(http2_settings_suite, local_updates_ack_fifo_and_deadline_starts_on_output)
    hd::http2_limits limits;
    limits.settings_timeout = std::chrono::seconds(3);
    hd::http2_connection c(budget(), limits);
    auto now = hd::http2_connection::time_point {} + std::chrono::seconds(100);
    LT_CHECK(!c.check_timeout(now));
    c.advance_output(c.output(now).size());
    c.feed(preface(), now);
    c.advance_output(c.output(now).size());
    c.feed(frame(4, 1), now);
    auto local = c.local_settings();
    local.header_table_size = 0;
    LT_CHECK(c.queue_settings(local) == httpserver::http::outcome_code::ok);
    c.advance_output(c.output(now).size());
    local.header_table_size = 8192;
    LT_CHECK(c.queue_settings(local) == httpserver::http::outcome_code::ok);
    c.advance_output(c.output(now + std::chrono::seconds(1)).size());
    LT_CHECK_EQ(c.pending_settings(), 2U);
    LT_CHECK(c.feed(frame(4, 1), now).progress == hd::http2_progress::control_ready);
    LT_CHECK_EQ(c.compression().decoder().acknowledged_maximum(), 0U);
    LT_CHECK(!c.check_timeout(now + std::chrono::seconds(3)));
    LT_ASSERT(c.check_timeout(now + std::chrono::seconds(4)).has_value());
    LT_CHECK(c.failure()->wire_code == hd::http2_error_code::settings_timeout);
    LT_CHECK_EQ(c.pending_settings(), 0U);
LT_END_AUTO_TEST(local_updates_ack_fifo_and_deadline_starts_on_output)
LT_BEGIN_AUTO_TEST(http2_settings_suite, unexpected_ack_and_wrong_first_frame)
    for (const auto& invalid : {frame(4, 1), frame(6, 0, 0, std::vector<std::uint8_t>(8)), frame(4, 0, 1), frame(4, 1, 0, {0})}) {
        hd::http2_connection c(budget());
        std::vector<std::uint8_t> wire(hd::http2_magic.begin(), hd::http2_magic.end());
        append(wire, invalid);
        LT_CHECK(c.feed(wire).error.has_value());
    }
    hd::http2_connection c(budget());
    c.feed(preface());
    LT_CHECK(c.feed(frame(4, 1)).error.has_value());
LT_END_AUTO_TEST(unexpected_ack_and_wrong_first_frame)
LT_BEGIN_AUTO_TEST(http2_settings_suite, legal_endpoints_and_fifo_acknowledges_both_snapshots)
    for (const auto& values : {std::vector<std::uint8_t>{}, setting(2, 1), setting(4, 0), setting(5, 16384), setting(6, UINT32_MAX), setting(3, UINT32_MAX)}) {
        hd::http2_connection c(budget());
        c.advance_output(c.output().size());
        std::vector<std::uint8_t> wire(hd::http2_magic.begin(), hd::http2_magic.end());
        append(wire, frame(4, 0, 0, values));
        LT_CHECK(!c.feed(wire).error);
        LT_CHECK_EQ(c.output().size(), 9U);
    }
    hd::http2_connection c(budget());
    c.advance_output(c.output().size());
    c.feed(preface());
    c.advance_output(c.output().size());
    c.feed(frame(4, 1));
    hd::http2_settings local;
    local.header_table_size = 0;
    c.queue_settings(local);
    c.advance_output(c.output().size());
    local.header_table_size = 8192;
    c.queue_settings(local);
    c.advance_output(c.output().size());
    LT_CHECK(!c.feed(frame(4, 1)).error);
    LT_CHECK_EQ(c.compression().decoder().acknowledged_maximum(), 0U);
    LT_CHECK(!c.feed(frame(4, 1)).error);
    LT_CHECK_EQ(c.compression().decoder().acknowledged_maximum(), 8192U);
    LT_CHECK_EQ(c.pending_settings(), 0U);
    LT_CHECK(c.output().empty());
    LT_CHECK(c.feed(frame(4, 1)).error.has_value());
LT_END_AUTO_TEST(legal_endpoints_and_fifo_acknowledges_both_snapshots)
LT_BEGIN_AUTO_TEST(http2_settings_suite, advisory_local_limits_are_preserved_when_update_omits_them)
    hd::http2_connection c(budget());
    c.advance_output(c.output().size());
    c.feed(preface());
    c.advance_output(c.output().size());
    c.feed(frame(4, 1));
    hd::http2_settings local;
    local.max_concurrent_streams = 7;
    local.max_header_list_size = 4096;
    LT_CHECK(c.queue_settings(local) == httpserver::http::outcome_code::ok);
    local.max_concurrent_streams.reset();
    local.max_header_list_size.reset();
    LT_CHECK(c.queue_settings(local) == httpserver::http::outcome_code::ok);
    c.advance_output(c.output().size());
    c.advance_output(c.output().size());
    LT_CHECK_EQ(c.local_settings().max_concurrent_streams.value_or(0), 7U);
    LT_CHECK_EQ(c.local_settings().max_header_list_size.value_or(0), 4096U);
LT_END_AUTO_TEST(advisory_local_limits_are_preserved_when_update_omits_them)
LT_BEGIN_AUTO_TEST(http2_settings_suite, connect_setting_rejects_invalid_values_and_revocation)
    for (auto values : {setting(8, 2), setting(8, UINT32_MAX)}) {
        hd::http2_connection c(budget()); c.feed(preface());
        auto result = c.feed(frame(4, 0, 0, values));
        LT_ASSERT(result.error); LT_CHECK(result.error->wire_code == hd::http2_error_code::protocol_error);
    }
    for (bool same_frame : {true, false}) {
        hd::http2_connection c(budget()); c.feed(preface());
        auto values = setting(8, 1);
        if (same_frame) append(values, setting(8, 0));
        auto result = c.feed(frame(4, 0, 0, values));
        if (!same_frame) {
            LT_CHECK(!result.error); result = c.feed(frame(4, 0, 0, setting(8, 0)));
        }
        LT_ASSERT(result.error); LT_CHECK(result.error->wire_code == hd::http2_error_code::protocol_error);
    }
LT_END_AUTO_TEST(connect_setting_rejects_invalid_values_and_revocation)
LT_BEGIN_AUTO_TEST(http2_settings_suite, local_connect_capability_is_advertised_and_cannot_be_revoked)
    hd::http2_settings settings;
    // The standalone connection may advertise the capability explicitly.
    settings.enable_connect_protocol = 1;
    settings.max_concurrent_streams = 10; settings.max_header_list_size = 4096;
    hd::http2_connection c(budget(), {}, settings);
    auto wire = c.output(); LT_ASSERT_EQ(wire.size(), 45u);
    LT_CHECK_EQ(wire[wire.size() - 5], 8); LT_CHECK_EQ(wire.back(), 1);
    LT_CHECK_EQ(c.local_settings().enable_connect_protocol, 1u);
    settings.enable_connect_protocol = 0;
    LT_CHECK(c.queue_settings(settings) == httpserver::http::outcome_code::invalid_argument);
    LT_CHECK_EQ(c.peer_settings().enable_connect_protocol, 0u);
LT_END_AUTO_TEST(local_connect_capability_is_advertised_and_cannot_be_revoked)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
