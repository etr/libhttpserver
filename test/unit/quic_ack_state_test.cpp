/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <chrono>
#include <httpserver/detail/quic_recovery.hpp>
#include "../littletest.hpp"
namespace hd = httpserver::detail;
using space = hd::quic_pn_space;
using namespace std::chrono_literals;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(ack_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(ack_suite)
LT_BEGIN_AUTO_TEST(ack_suite, spaces_duplicates_and_time_zero)
    hd::quic_recovery r({}, httpserver::server::resource_budget::root({}));
    for (auto s : {space::initial, space::handshake, space::application}) {
        LT_CHECK(r.inspect_received(s, 0) == hd::quic_receipt::fresh);
        LT_ASSERT(r.receive_packet(s, 0, true, {}));
        LT_CHECK(r.inspect_received(s, 0) == hd::quic_receipt::duplicate);
        auto due = r.ack_deadline(s);
        LT_ASSERT(due);
        LT_CHECK(*due == hd::quic_recovery::time_point() + (s == space::application ? 25ms : 0ms));
    }
LT_END_AUTO_TEST(spaces_duplicates_and_time_zero)
LT_BEGIN_AUTO_TEST(ack_suite, gaps_roundtrip_and_failed_preparation)
    hd::quic_recovery r({}, httpserver::server::resource_budget::root({}));
    LT_ASSERT(r.receive_packet(space::application, 0, true, {}));
    LT_ASSERT(r.receive_packet(space::application, 3, true, hd::quic_recovery::time_point() + 2ms));
    LT_ASSERT(r.receive_packet(space::application, 2, false, hd::quic_recovery::time_point() + 3ms));
    std::array<std::byte, 100> out{};
    auto short_ack = r.prepare_ack(space::application, {}, hd::quic_recovery::time_point() + 10ms);
    LT_CHECK(short_ack.code == hd::quic_recovery_code::no_space);
    LT_CHECK(r.ack_deadline(space::application).has_value());
    auto ack = r.prepare_ack(space::application, out, hd::quic_recovery::time_point() + 10ms);
    LT_ASSERT(ack);
    hd::quic_frame_cursor cursor;
    auto decoded = hd::next_quic_frame(std::span(out).first(ack.bytes), cursor);
    LT_CHECK(decoded.code == hd::quic_codec_code::ok);
    auto f = std::get<hd::quic_ack_frame>(decoded.value);
    LT_CHECK(f.largest == 3 && f.first_range == 1 && f.range_count == 1 && f.delay == 1000);
    hd::quic_ack_cursor ranges;
    auto first = hd::next_quic_ack_range(f, ranges), second = hd::next_quic_ack_range(f, ranges);
    LT_CHECK(first.value.smallest == 2 && second.value.smallest == 0);
    LT_ASSERT(r.receive_packet(space::application, 4, true, hd::quic_recovery::time_point() + 11ms));
    r.publish_ack(space::application, ack.generation);
    LT_CHECK(r.ack_deadline(space::application).has_value());
    ack = r.prepare_ack(space::application, out, hd::quic_recovery::time_point() + 12ms);
    LT_ASSERT(ack);
    r.publish_ack(space::application, ack.generation);
    LT_CHECK(!r.ack_deadline(space::application));
LT_END_AUTO_TEST(gaps_roundtrip_and_failed_preparation)
LT_BEGIN_AUTO_TEST(ack_suite, bounded_floor_prevents_reprocessing_and_noneliciting_loops)
    hd::quic_recovery_config config;
    config.max_receive_ranges = 2;
    hd::quic_recovery r(config, httpserver::server::resource_budget::root({}));
    for (auto pn : {0, 2, 4}) LT_ASSERT(r.receive_packet(space::initial, pn, false, {}));
    LT_CHECK(!r.ack_deadline(space::initial));
    LT_CHECK(r.inspect_received(space::initial, 0) == hd::quic_receipt::retired);
    LT_CHECK(r.inspect_received(space::initial, 1) == hd::quic_receipt::retired);
    LT_CHECK(r.inspect_received(space::initial, 3) == hd::quic_receipt::fresh);
    LT_CHECK(r.receive_packet(space::initial, hd::k_quic_max_integer + 1, true, {}).code == hd::quic_recovery_code::invalid);
    config.max_receive_ranges = 0;
    hd::quic_recovery empty(config, httpserver::server::resource_budget::root({}));
    LT_CHECK(empty.receive_packet(space::initial, 0, true, {}).code == hd::quic_recovery_code::capacity);
    LT_CHECK(empty.inspect_received(space::initial, 0) == hd::quic_receipt::fresh);
LT_END_AUTO_TEST(bounded_floor_prevents_reprocessing_and_noneliciting_loops)
LT_BEGIN_AUTO_TEST(ack_suite, duplicate_eliciting_packet_rearms_ack_without_reapplying_frames)
    hd::quic_recovery r({}, httpserver::server::resource_budget::root({}));
    std::array<std::byte, 100> out{};
    LT_ASSERT(r.receive_packet(space::application, 0, true, {}));
    auto prepared = r.prepare_ack(space::application, out, hd::quic_recovery::time_point() + 1ms);
    LT_ASSERT(prepared);
    r.publish_ack(space::application, prepared.generation);
    LT_CHECK(!r.ack_deadline(space::application));
    LT_CHECK(r.inspect_received(space::application, 0) == hd::quic_receipt::duplicate);
    LT_ASSERT(r.receive_packet(space::application, 0, true, hd::quic_recovery::time_point() + 10ms));
    LT_CHECK(r.ack_deadline(space::application) == hd::quic_recovery::time_point() + 10ms);
    prepared = r.prepare_ack(space::application, out, hd::quic_recovery::time_point() + 10ms);
    r.publish_ack(space::application, prepared.generation);
    LT_ASSERT(r.receive_packet(space::application, 0, false, hd::quic_recovery::time_point() + 11ms));
    LT_CHECK(!r.ack_deadline(space::application));
LT_END_AUTO_TEST(duplicate_eliciting_packet_rearms_ack_without_reapplying_frames)
LT_BEGIN_AUTO_TEST(ack_suite, zero_exponent_and_extreme_clock_do_not_overflow)
    hd::quic_recovery_config config;
    config.local_ack_delay_exponent = 0;
    hd::quic_recovery r(config, httpserver::server::resource_budget::root({}));
    LT_ASSERT(r.receive_packet(space::application, 0, true, hd::quic_recovery::time_point::max() - 1ns));
    LT_CHECK(r.ack_deadline(space::application) == hd::quic_recovery::time_point::max());
    hd::quic_recovery extreme(config, httpserver::server::resource_budget::root({}));
    LT_ASSERT(extreme.receive_packet(space::application, 0, true, hd::quic_recovery::time_point::min()));
    std::array<std::byte, 100> out{};
    auto prepared = extreme.prepare_ack(space::application, out, hd::quic_recovery::time_point::max());
    LT_CHECK(prepared);
LT_END_AUTO_TEST(zero_exponent_and_extreme_clock_do_not_overflow)
LT_BEGIN_AUTO_TEST(ack_suite, initial_application_gap_and_due_timer_report_ack_work)
    hd::quic_recovery r({}, httpserver::server::resource_budget::root({}));
    LT_ASSERT(r.receive_packet(space::application, 3, true, {}));
    LT_CHECK(r.ack_deadline(space::application) == hd::quic_recovery::time_point());
    auto due = r.next_deadline();
    LT_ASSERT(due);
    LT_CHECK(due->action == hd::quic_recovery_timer::kind::acknowledge);
    LT_CHECK(r.expire({}).acknowledge_spaces[2]);
    hd::quic_recovery_environment env;
    env.send_permitted = false;
    r.set_environment(env, {});
    LT_CHECK(!r.expire({}).acknowledge_spaces[2]);
LT_END_AUTO_TEST(initial_application_gap_and_due_timer_report_ack_work)
LT_BEGIN_AUTO_TEST(ack_suite, noneliciting_receipt_between_preparation_and_send_does_not_create_ack_loop)
    hd::quic_recovery r({}, httpserver::server::resource_budget::root({}));
    LT_ASSERT(r.receive_packet(space::application, 0, true, {}));
    std::array<std::byte, 100> out{};
    auto prepared = r.prepare_ack(space::application, out, hd::quic_recovery::time_point() + 25ms);
    LT_ASSERT(prepared);
    LT_ASSERT(r.receive_packet(space::application, 1, false, hd::quic_recovery::time_point() + 25ms));
    r.publish_ack(space::application, prepared.generation);
    LT_CHECK(!r.ack_deadline(space::application));
    LT_ASSERT(r.receive_packet(space::application, 2, false, hd::quic_recovery::time_point() + 26ms));
    LT_CHECK(!r.ack_deadline(space::application));
LT_END_AUTO_TEST(noneliciting_receipt_between_preparation_and_send_does_not_create_ack_loop)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
