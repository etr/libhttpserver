/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "./http3_websocket_fixture.hpp"
#include "./littletest.hpp"
using namespace h3ws;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(h3_ws_lifecycle_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(h3_ws_lifecycle_suite)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, fin_without_close_reports_connection_closed_once_and_preserves_sibling)
    fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request()); unsigned calls = 0; h::websocket::close_info info;
    LT_ASSERT(f.sessions[0].on_close([&](auto value) { ++calls; info = std::move(value); }).ok());
    auto received = f.take(); LT_CHECK(!received->has_value());
    LT_ASSERT(f.feed(0, {}, true)); f.executor.run_pending();
    LT_CHECK_EQ(calls, 1U); LT_CHECK(info.status.code() == http::outcome_code::connection_closed); LT_CHECK(!info.clean);
    LT_ASSERT(received->has_value()); LT_CHECK(!(*received)->value);
    LT_ASSERT(f.request(4, h3test::get(), true)); LT_CHECK_EQ(f.reply(4).heads[0][0].value, "204");
    f.engine->terminal(0, {h::detail::quic_terminal_kind::eof, 0}); f.engine->disconnect({http::outcome_code::connection_closed, "test disconnect"}); f.executor.run_pending(); LT_CHECK_EQ(calls, 1U);
LT_END_AUTO_TEST(fin_without_close_reports_connection_closed_once_and_preserves_sibling)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, peer_close_reply_precedes_fin_even_with_immediate_peer_fin)
    for (bool fin : {false, true}) {
        fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request()); unsigned calls = 0; h::websocket::close_info info;
        LT_ASSERT(f.sessions[0].on_close([&](auto value) { ++calls; info = std::move(value); }).ok());
        LT_ASSERT(f.data(masked(8, std::string("\x03\xe8", 2)), 0, fin)); f.drain();
        LT_CHECK_EQ(f.reply().body, std::string("\x88\x02\x03\xe8", 4)); LT_CHECK_EQ(f.fins[0], 1U);
        LT_CHECK_EQ(calls, 1U); LT_CHECK(info.clean); LT_CHECK(info.status.ok()); LT_CHECK_EQ(info.code.value_or(0), 1000U);
        f.engine->disconnect({http::outcome_code::connection_closed, "test disconnect"}); LT_CHECK_EQ(calls, 1U);
    }
LT_END_AUTO_TEST(peer_close_reply_precedes_fin_even_with_immediate_peer_fin)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, local_close_retains_receive_half_until_peer_reply)
    fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request()); unsigned calls = 0;
    LT_ASSERT(f.sessions[0].on_close([&](auto info) { ++calls; LT_CHECK(info.clean); }).ok());
    LT_ASSERT(f.sessions[0].close(1000).ok()); f.drain(); LT_CHECK_EQ(f.fins[0], 1U); LT_CHECK_EQ(calls, 0U);
    LT_CHECK(f.root.in_use(h::server::resource::ws_message_bytes) > 0);
    LT_ASSERT(f.data(masked(8, std::string("\x03\xe8", 2)), 0, true)); f.drain(); LT_CHECK_EQ(calls, 1U);
    LT_CHECK_EQ(f.root.in_use(h::server::resource::ws_message_bytes), 0U);
LT_END_AUTO_TEST(local_close_retains_receive_half_until_peer_reply)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, reset_stop_sending_and_codec_errors_are_stream_local)
    for (unsigned kind = 0; kind < 3; ++kind) {
        fixture f; f.options.limits.outgoing_messages = 1; LT_ASSERT(f.settings()); LT_ASSERT(f.request());
        unsigned calls = 0; h::websocket::close_info info;
        LT_ASSERT(f.sessions[0].on_close([&](auto value) { ++calls; info = std::move(value); }).ok());
        auto received = f.take(); LT_ASSERT(f.sessions[0].try_send(h::websocket::message_kind::text, {}).status.ok());
        auto writable = std::make_shared<std::optional<http::outcome>>();
        h::spawn(f.executor, f.sessions[0].writable(), [writable](auto r) { writable->emplace(std::move(r.value())); }); f.executor.run_pending();
        if (kind == 0) {
            LT_ASSERT(f.flow.receive(*f.streams[0], h::detail::quic_reset_stream_frame{0, 42, f.incoming[0]})); f.pump(0);
        } else if (kind == 1) {
            f.engine->stop_sending(0, 42);
        } else {
            LT_ASSERT(f.data(ws_test::frame(1, {}, true, false)));
        }
        f.executor.run_pending(); LT_CHECK_EQ(calls, 1U); LT_CHECK(!info.clean);
        LT_CHECK(info.status.code() == (kind == 0 ? http::outcome_code::cancelled : http::outcome_code::protocol_error));
        LT_ASSERT(received->has_value()); LT_ASSERT(writable->has_value()); LT_CHECK(!(*writable)->ok());
        LT_CHECK_EQ(f.root.in_use(h::server::resource::ws_message_bytes), 0U);
        LT_ASSERT(f.request(4, h3test::get(), true)); LT_CHECK_EQ(f.reply(4).heads[0][0].value, "204");
        f.engine->disconnect({http::outcome_code::connection_closed, "test disconnect"}); LT_CHECK_EQ(calls, 1U);
    }
LT_END_AUTO_TEST(reset_stop_sending_and_codec_errors_are_stream_local)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, disconnect_and_destruction_preserve_reason_under_reentrant_callback)
    for (unsigned kind = 0; kind < 3; ++kind) {
        fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request()); unsigned calls = 0; http::outcome_code code{};
        LT_ASSERT(f.sessions[0].on_close([&](auto info) {
            ++calls; code = info.status.code();
            if (kind == 2) f.engine.reset();
        }).ok());
        if (kind == 0) {
            f.engine.reset();
        } else {
            f.engine->disconnect({http::outcome_code::connection_closed, "test disconnect"});
        }
        f.executor.run_pending(); LT_CHECK_EQ(calls, 1U); LT_CHECK(code == http::outcome_code::connection_closed);
    }
LT_END_AUTO_TEST(disconnect_and_destruction_preserve_reason_under_reentrant_callback)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, trailing_headers_refused_without_corrupting_sibling_qpack)
    fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request()); unsigned calls = 0;
    LT_ASSERT(f.sessions[0].on_close([&](auto info) { ++calls; LT_CHECK(info.status.code() == http::outcome_code::protocol_error); }).ok());
    LT_ASSERT(f.feed(0, h3test::headers({{"x-trailer", "no"}}))); LT_CHECK_EQ(calls, 1U);
    auto action = f.engine->take_action(); LT_ASSERT(action); LT_ASSERT(action->reset); LT_CHECK_EQ(action->reset->stream, 0U);
    LT_ASSERT(f.request(4, h3test::get(), true)); LT_CHECK_EQ(f.reply(4).heads[0][0].value, "204");
LT_END_AUTO_TEST(trailing_headers_refused_without_corrupting_sibling_qpack)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, drain_sends_close_1001_and_deadline_cancels_silent_peer_once)
    fixture f; f.local(); LT_ASSERT(f.settings()); LT_ASSERT(f.request()); unsigned calls = 0; h::websocket::close_info info;
    LT_ASSERT(f.sessions[0].on_close([&](auto value) { ++calls; info = std::move(value); }).ok()); auto received = f.take();
    h::server::drain_ticket ticket; auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
    LT_ASSERT(f.engine->begin_drain(deadline, ticket).ok()); f.drain();
    LT_CHECK_EQ(f.reply().body, std::string("\x88\x02\x03\xe9", 4)); LT_CHECK_EQ(f.fins[0], 1U); LT_CHECK_EQ(calls, 0U);
    LT_CHECK(!f.engine->take_connection_close()); f.engine->check_drain(deadline); f.executor.run_pending();
    LT_CHECK_EQ(calls, 1U); LT_CHECK(info.status.code() == http::outcome_code::timeout); LT_ASSERT(received->has_value());
    h::server::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == h::server::drain_status::deadline_expired);
    f.engine->check_drain(deadline + std::chrono::seconds(1)); f.engine->disconnect({http::outcome_code::connection_closed, "test disconnect"}); LT_CHECK_EQ(calls, 1U);
LT_END_AUTO_TEST(drain_sends_close_1001_and_deadline_cancels_silent_peer_once)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, drain_closes_multiple_tunnels_and_accepted_sibling)
    fixture f; f.local(); LT_ASSERT(f.settings()); LT_ASSERT(f.request()); LT_ASSERT(f.request(4));
    LT_ASSERT(f.request(8, h3test::get(), true)); unsigned calls = 0;
    for (auto& session : f.sessions) {
        LT_ASSERT(session.on_close([&](auto info) { ++calls; LT_CHECK(info.clean); }).ok());
    }
    h::server::drain_ticket ticket; auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
    LT_ASSERT(f.engine->begin_drain(deadline, ticket).ok()); f.drain(); LT_CHECK_EQ(f.reply(8).heads[0][0].value, "204");
    LT_ASSERT(f.data(masked(8, std::string("\x03\xe9", 2)), 0, true));
    LT_ASSERT(f.data(masked(8, std::string("\x03\xe9", 2)), 4, true)); f.drain();
    LT_CHECK_EQ(calls, 2U); LT_ASSERT(f.engine->take_connection_close());
    h::server::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == h::server::drain_status::completed);
LT_END_AUTO_TEST(drain_closes_multiple_tunnels_and_accepted_sibling)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, blocked_empty_message_defers_fin_until_application_receive)
    fixture f; f.options.limits.incoming_messages = 1; LT_ASSERT(f.settings()); LT_ASSERT(f.request());
    unsigned calls = 0; LT_ASSERT(f.sessions[0].on_close([&](auto info) { ++calls; LT_CHECK(info.status.code() == http::outcome_code::connection_closed); }).ok());
    auto wire = masked(1, "first"); auto empty = masked(1, ""); wire.insert(wire.end(), empty.begin(), empty.end());
    LT_ASSERT(f.data(wire, 0, true)); LT_CHECK_EQ(calls, 0U);
    auto received = f.take(); LT_ASSERT(received->has_value()); LT_CHECK_EQ(text(**received), "first");
    LT_CHECK_EQ(calls, 1U); LT_CHECK_EQ(f.credit(0), 65536 + f.incoming[0]);
LT_END_AUTO_TEST(blocked_empty_message_defers_fin_until_application_receive)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, inline_close_callbacks_can_disconnect_or_destroy_during_feed_and_retention)
    struct inline_executor : h::executor {
        void post(handler work) override { work(); }
        bool is_current() const noexcept override { return false; }
    } owner;
    for (bool output : {false, true}) for (bool destroy : {false, true}) {
        fixture f;
        f.engine = std::make_unique<h::detail::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, owner);
        LT_ASSERT(f.settings()); LT_ASSERT(f.request()); unsigned calls = 0;
        LT_ASSERT(f.sessions[0].on_close([&](auto) {
            ++calls;
            if (destroy) f.engine.reset();
            else f.engine->disconnect({http::outcome_code::connection_closed, "callback disconnect"});
        }).ok());
        auto bytes = h3test::frame(0, output ? masked(8, std::string("\x03\xe8", 2)) : ws_test::frame(1, {}, true, false));
        LT_ASSERT(f.flow.receive(*f.streams[0], {0, f.incoming[0], bytes, false}));
        f.engine->begin_turn(); f.engine->pump_receive(0);
        if (output) {
            LT_ASSERT(f.engine); f.engine->pump_output(); f.engine->pump_output();
        }
        LT_CHECK_EQ(calls, 1U); LT_CHECK_EQ(f.root.in_use(h::server::resource::ws_message_bytes), 0U);
        f.executor.run_pending(); LT_CHECK_EQ(calls, 1U);
    }
LT_END_AUTO_TEST(inline_close_callbacks_can_disconnect_or_destroy_during_feed_and_retention)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, drain_waits_for_reliable_fin_completions)
    fixture f; f.local(); LT_ASSERT(f.settings()); LT_ASSERT(f.request()); f.drain();
    unsigned closed = 0;
    LT_ASSERT(f.sessions[0].on_close([&](auto info) { ++closed; LT_CHECK(info.clean); }).ok());
    h::server::drain_ticket ticket; auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
    LT_ASSERT(f.engine->begin_drain(deadline, ticket).ok());
    std::vector<h::detail::quic_information_completion> held_fin;
    for (unsigned n = 0; n < 100; ++n) {
        std::vector<h::detail::quic_information_completion> completions;
        if (!f.transmit_held(completions)) break;
        for (const auto& completion : completions) {
            if (completion.stream == 0 && completion.fin) held_fin.push_back(completion);
            else f.engine->information_completed(completion);
        }
    }
    // Retire both GOAWAY stages and all other output; only the tunnel FIN is held.
    f.drain(); LT_CHECK_EQ(f.fins[0], 1U); LT_ASSERT_EQ(held_fin.size(), 1U);
    LT_ASSERT(f.data(masked(8, std::string("\x03\xe9", 2)), 0, true));
    f.drain(); LT_CHECK_EQ(closed, 1U);
    LT_CHECK(!f.engine->take_connection_close());
    LT_CHECK(f.engine->drain_deadline() == deadline);
    f.engine->information_completed(held_fin.front());
    f.drain(); LT_ASSERT(f.engine->take_connection_close());
    LT_CHECK(!f.engine->drain_deadline());
    h::server::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == h::server::drain_status::completed);
LT_END_AUTO_TEST(drain_waits_for_reliable_fin_completions)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, drain_deadline_cancels_credit_blocked_tunnel_and_ignores_late_completion)
    fixture f({}, 5); f.local(); LT_ASSERT(f.settings()); LT_ASSERT(f.request()); f.drain(); unsigned calls = 0;
    LT_ASSERT(f.sessions[0].on_close([&](auto info) { ++calls; LT_CHECK(info.status.code() == http::outcome_code::timeout); }).ok());
    auto received = f.take(); h::server::drain_ticket ticket; auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
    LT_ASSERT(f.engine->begin_drain(deadline, ticket).ok()); f.drain(); LT_CHECK_EQ(f.fins[0], 0U);
    f.engine->check_drain(deadline); f.executor.run_pending(); LT_CHECK_EQ(calls, 1U); LT_ASSERT(received->has_value());
    f.engine->information_completed({1000000, h::detail::quic_information_kind::stream, h::detail::quic_pn_space::application, 0, 100});
    f.engine->check_drain(deadline + std::chrono::seconds(1)); LT_CHECK_EQ(calls, 1U);
    h::server::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == h::server::drain_status::deadline_expired);
LT_END_AUTO_TEST(drain_deadline_cancels_credit_blocked_tunnel_and_ignores_late_completion)
LT_BEGIN_AUTO_TEST(h3_ws_lifecycle_suite, peer_request_cancelled_preserves_close_reason_in_both_frame_orders)
    for (bool stop_first : {false, true}) {
        fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request()); f.drain();
        unsigned calls = 0; h::websocket::close_info info;
        LT_ASSERT(f.sessions[0].on_close([&](auto value) { ++calls; info = std::move(value); }).ok());
        auto received = f.take();
        if (stop_first) f.engine->stop_sending(0, 0x10c);
        LT_ASSERT(f.flow.receive(*f.streams[0], h::detail::quic_reset_stream_frame{0, 0x10c, f.incoming[0]})); f.pump(0);
        if (!stop_first) f.engine->stop_sending(0, 0x10c);
        f.executor.run_pending(); LT_CHECK_EQ(calls, 1U);
        LT_CHECK(info.status.code() == http::outcome_code::cancelled); LT_CHECK(!info.clean);
        LT_ASSERT(received->has_value()); LT_CHECK((*received)->status.code() == http::outcome_code::cancelled);
        LT_ASSERT(f.request(4, h3test::get(), true)); LT_CHECK_EQ(f.reply(4).heads[0][0].value, "204");
        f.engine->disconnect({http::outcome_code::connection_closed, "test disconnect"}); LT_CHECK_EQ(calls, 1U);
    }
LT_END_AUTO_TEST(peer_request_cancelled_preserves_close_reason_in_both_frame_orders)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
