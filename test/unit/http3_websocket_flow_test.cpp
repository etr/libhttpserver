/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <string>
#include <utility>
#include "./http3_websocket_fixture.hpp"
#include "./littletest.hpp"
using namespace h3ws;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(h3_ws_flow_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(h3_ws_flow_suite)
LT_BEGIN_AUTO_TEST(h3_ws_flow_suite, blocked_suffix_resumes_without_new_network_input)
    for (std::string second : {std::string{"efgh"}, std::string{}}) {
        fixture f; f.options.limits.incoming_messages = 1; f.options.limits.incoming_bytes = 4; f.options.limits.max_message_bytes = 4;
        LT_ASSERT(f.settings()); LT_ASSERT(f.request());
        auto wire = masked(1, "abcd"); auto next = masked(1, second); wire.insert(wire.end(), next.begin(), next.end());
        auto begin = f.credit(0); LT_ASSERT(f.data(wire));
        LT_CHECK_EQ(f.credit(0), begin + 2 + 10 + 6);
        LT_ASSERT(f.request(4, h3test::get(), true)); LT_CHECK_EQ(f.reply(4).heads[0][0].value, "204");
        auto first = f.take(); LT_ASSERT(first->has_value()); LT_CHECK_EQ(text(**first), "abcd");
        auto received = f.take(); LT_ASSERT(received->has_value()); LT_CHECK((*received)->value.has_value());
        LT_CHECK_EQ(text(**received), second); LT_CHECK_EQ(f.credit(0), 65536 + f.incoming[0]);
    }
LT_END_AUTO_TEST(blocked_suffix_resumes_without_new_network_input)
LT_BEGIN_AUTO_TEST(h3_ws_flow_suite, accepted_send_frames_exact_text_and_wakes_writable)
    fixture f; f.options.limits.outgoing_messages = 1;
    LT_ASSERT(f.settings()); LT_ASSERT(f.request()); f.drain();
    const std::string message = "pending";
    LT_ASSERT(f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(message))).status.ok());
    auto writable = std::make_shared<std::optional<http::outcome>>();
    h::spawn(f.executor, f.sessions[0].writable(), [writable](auto r) { writable->emplace(std::move(r.value())); });
    f.executor.run_pending(); LT_CHECK(!writable->has_value());
    LT_CHECK(f.sessions[0].try_send(h::websocket::message_kind::text, {}).disposition == h::websocket::send_disposition::backpressured);
    f.drain();
    LT_CHECK_EQ(f.reply().body, std::string("\x81\x07pending", 9));
    LT_ASSERT(writable->has_value()); LT_CHECK((*writable)->ok());
LT_END_AUTO_TEST(accepted_send_frames_exact_text_and_wakes_writable)
LT_BEGIN_AUTO_TEST(h3_ws_flow_suite, tiny_stream_increments_retain_headers_without_freeing_codec_capacity)
    fixture f({}, 5); f.options.limits.outgoing_messages = 1;
    LT_ASSERT(f.settings()); LT_ASSERT(f.request()); f.drain();
    LT_ASSERT_EQ(f.output[0].size(), 5U);
    const std::string message = "hi";
    LT_ASSERT(f.sessions[0].try_send(h::websocket::message_kind::binary, std::as_bytes(std::span(message))).status.ok());
    auto writable = std::make_shared<std::optional<http::outcome>>();
    h::spawn(f.executor, f.sessions[0].writable(), [writable](auto r) { writable->emplace(std::move(r.value())); });
    f.executor.run_pending();
    LT_ASSERT(f.request(4, h3test::get(), true));
    LT_ASSERT(f.flow.apply({h::detail::quic_flow_kind::max_stream_data, 64, 4}));
    LT_CHECK_EQ(f.reply(4).heads[0][0].value, "204");
    for (std::uint64_t allowance = 6; allowance <= 11; ++allowance) {
        LT_ASSERT(f.flow.apply({h::detail::quic_flow_kind::max_stream_data, allowance, 0})); f.drain();
        LT_CHECK_EQ(f.output[0].size(), allowance);
        LT_CHECK_EQ(writable->has_value(), allowance == 11);
    }
    LT_CHECK_EQ(f.reply().body, std::string("\x82\x02hi", 4)); LT_CHECK_EQ(f.fins[0], 0U);
LT_END_AUTO_TEST(tiny_stream_increments_retain_headers_without_freeing_codec_capacity)
LT_BEGIN_AUTO_TEST(h3_ws_flow_suite, connection_credit_resumes_without_duplicate_bytes)
    fixture f({}, 65536, 6); LT_ASSERT(f.settings()); LT_ASSERT(f.request()); f.drain();
    const std::string message = "abc";
    LT_ASSERT(f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(message))).status.ok());
    f.drain(); LT_CHECK_EQ(f.output[0].size(), 6U);
    LT_ASSERT(f.flow.apply({h::detail::quic_flow_kind::max_data, 12})); f.drain();
    LT_CHECK_EQ(f.output[0].size(), 12U); LT_CHECK_EQ(f.reply().body, std::string("\x81\x03" "abc", 5));
    f.drain(); LT_CHECK_EQ(f.output[0].size(), 12U);
LT_END_AUTO_TEST(connection_credit_resumes_without_duplicate_bytes)
LT_BEGIN_AUTO_TEST(h3_ws_flow_suite, reliable_retention_wakes_writable_before_transmission_and_ack)
    fixture f; f.options.limits.outgoing_messages = 1; LT_ASSERT(f.settings()); LT_ASSERT(f.request()); f.drain();
    const auto head = f.output[0].size();
    LT_ASSERT(f.sessions[0].try_send(h::websocket::message_kind::text, {}).status.ok());
    auto writable = std::make_shared<std::optional<http::outcome>>();
    h::spawn(f.executor, f.sessions[0].writable(), [writable](auto r) { writable->emplace(std::move(r.value())); }); f.executor.run_pending();
    LT_CHECK(!writable->has_value()); f.engine->pump_output(); f.executor.run_pending();
    LT_ASSERT(writable->has_value()); LT_CHECK((*writable)->ok()); LT_CHECK_EQ(f.output[0].size(), head);
    LT_ASSERT(f.sessions[0].try_send(h::websocket::message_kind::text, {}).status.ok()); f.drain();
    LT_CHECK_EQ(f.reply().body, std::string("\x81\x00\x81\x00", 4));
LT_END_AUTO_TEST(reliable_retention_wakes_writable_before_transmission_and_ack)
LT_BEGIN_AUTO_TEST(h3_ws_flow_suite, large_frame_is_staged_as_bounded_data_sections)
    h::detail::http3_request_limits limits; limits.response_buffer_bytes = 3;
    fixture f(limits); LT_ASSERT(f.settings()); LT_ASSERT(f.request());
    const std::string message = "abcdefghij";
    LT_ASSERT(f.sessions[0].try_send(h::websocket::message_kind::binary, std::as_bytes(std::span(message))).status.ok()); f.drain();
    LT_CHECK_EQ(f.reply().body, std::string("\x82\x0a" "abcdefghij", 12));
    h::detail::quic_cursor cursor(f.output[0]); unsigned sections = 0;
    while (cursor.remaining()) {
        auto type = cursor.varint(), length = cursor.varint(); cursor.take(length);
        if (type == 0) {
            LT_CHECK(length <= 3); ++sections;
        }
    }
    LT_CHECK_EQ(sections, 4U);
LT_END_AUTO_TEST(large_frame_is_staged_as_bounded_data_sections)
LT_BEGIN_AUTO_TEST(h3_ws_flow_suite, inline_receiver_frees_blocked_admission_during_codec_notification)
    struct inline_executor : h::executor {
        void post(handler work) override { work(); }
        bool is_current() const noexcept override { return false; }
    } owner;
    fixture f; f.options.limits.incoming_messages = 1;
    f.engine = std::make_unique<h::detail::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, owner);
    LT_ASSERT(f.settings()); LT_ASSERT(f.request());
    auto first = std::make_shared<std::optional<h::websocket::receive_result>>();
    h::spawn(owner, f.sessions[0].receive(), [first](auto r) { first->emplace(std::move(r.value())); });
    auto wire = masked(1, "first"); auto second = masked(1, "second"); wire.insert(wire.end(), second.begin(), second.end());
    LT_ASSERT(f.data(wire)); LT_ASSERT(first->has_value()); LT_CHECK_EQ(text(**first), "first");
    auto received = f.take(); LT_ASSERT(received->has_value()); LT_CHECK_EQ(text(**received), "second");
LT_END_AUTO_TEST(inline_receiver_frees_blocked_admission_during_codec_notification)
LT_BEGIN_AUTO_TEST(h3_ws_flow_suite, inline_observer_keeps_progress_when_receiver_drains_while_feed_is_blocked)
    struct inline_executor : h::executor {
        void post(handler work) override { work(); }
        bool is_current() const noexcept override { return false; }
    } owner;
    fixture f; f.options.limits.incoming_messages = 1;
    f.engine = std::make_unique<h::detail::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, owner);
    LT_ASSERT(f.settings()); LT_ASSERT(f.request());
    auto wire = masked(1, "first");
    for (const std::string& message : {std::string{"second"}, std::string{"third"}}) {
        auto next = masked(1, message); wire.insert(wire.end(), next.begin(), next.end());
    }
    LT_ASSERT(f.data(wire));
    auto all = std::make_shared<std::vector<std::string>>();
    auto read_all = [](fixture* f, std::shared_ptr<std::vector<std::string>> all) -> h::task<void> {
        for (unsigned n = 0; n < 3; ++n) {
            auto result = co_await f->sessions[0].receive();
            if (result.value) all->push_back(text(result));
        }
    };
    h::spawn(owner, read_all(&f, all), [](auto) {});
    LT_ASSERT_EQ(all->size(), 3U); LT_CHECK_EQ(all->at(2), "third");
LT_END_AUTO_TEST(inline_observer_keeps_progress_when_receiver_drains_while_feed_is_blocked)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
