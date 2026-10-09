/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <cstdlib>
#include <new>
#include <vector>
#include "./http3_websocket_fixture.hpp"
namespace {
std::ptrdiff_t fail_after = -1;
bool allocation_failed = false;
}
void* operator new(std::size_t size) {
    if (fail_after == 0) {
        fail_after = -1; allocation_failed = true;
        throw std::bad_alloc();
    }
    if (fail_after > 0) --fail_after;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
#include "./littletest.hpp"
using namespace h3ws;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(h3_ws_handshake_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(h3_ws_handshake_suite)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, enabled_route_admits_stream_only_and_ignores_extensions)
    fixture f; LT_ASSERT(f.settings()); f.options.subprotocols = {"preferred", "chat"}; f.repeat = true;
    auto fields = connect("/items/../chat?q=1");
    fields.push_back({"sec-websocket-protocol", "chat, preferred"});
    fields.push_back({"sec-websocket-extensions", "permessage-deflate"});
    LT_ASSERT(f.request(0, fields)); LT_ASSERT_EQ(f.sessions.size(), 1u);
    auto reply = f.reply(); LT_ASSERT_EQ(reply.heads.size(), 1u);
    LT_ASSERT_EQ(reply.heads[0].size(), 2u); LT_CHECK_EQ(reply.heads[0][0].value, "200");
    LT_CHECK_EQ(reply.heads[0][1].name, "sec-websocket-protocol"); LT_CHECK_EQ(reply.heads[0][1].value, "preferred");
    LT_CHECK_EQ(f.selected, "preferred"); LT_CHECK_EQ(f.fins[0], 0u); LT_CHECK(!f.engine->take_action());
    LT_CHECK(f.second == http::outcome_code::invalid_state); LT_CHECK(f.response_after == http::outcome_code::invalid_state);
    LT_ASSERT(f.request(4, h3test::get(), true)); auto sibling = f.reply(4);
    LT_ASSERT_EQ(sibling.heads.size(), 1u); LT_CHECK_EQ(sibling.heads[0][0].value, "204");
    LT_CHECK_EQ(f.fins[4], 1u); LT_CHECK_EQ(f.fins[0], 0u);
LT_END_AUTO_TEST(enabled_route_admits_stream_only_and_ignores_extensions)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, route_registration_and_explicit_upgrade_are_required)
    fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request(0, connect("/missing")));
    auto missing = f.reply(); LT_ASSERT_EQ(missing.heads.size(), 1u); LT_CHECK_EQ(missing.heads[0][0].value, "404");
    LT_CHECK(f.sessions.empty());
    f.routes.route(http::method::known(http::method_id::connect), "/normal", [](h::exchange& x) -> h::task<void> {
        x.respond(http::status::from_code(403), {}); co_return;
    });
    LT_ASSERT(f.request(4, connect("/normal"))); auto normal = f.reply(4);
    LT_ASSERT_EQ(normal.heads.size(), 1u); LT_CHECK_EQ(normal.heads[0][0].value, "403"); LT_CHECK(f.sessions.empty());
LT_END_AUTO_TEST(route_registration_and_explicit_upgrade_are_required)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, disabled_absent_and_incomplete_settings_reset_only_connect)
    for (unsigned mode = 0; mode < 4; ++mode) {
        fixture f;
        if (mode == 1) LT_ASSERT(f.settings(std::nullopt));
        if (mode == 2) LT_ASSERT(f.settings(0));
        if (mode == 3) {
            f.open(2); LT_ASSERT(f.feed(2, h3test::bytes({0, 4, 2, 8})));
        }
        LT_ASSERT(f.request()); LT_CHECK(f.sessions.empty());
        auto action = f.engine->take_action(); LT_ASSERT(action); LT_ASSERT(action->reset); LT_CHECK_EQ(action->reset->error, 0x10eu);
        LT_CHECK(!f.engine->failure()); LT_ASSERT(f.request(4, h3test::get(), true));
        auto sibling = f.reply(4); LT_ASSERT_EQ(sibling.heads.size(), 1u); LT_CHECK_EQ(sibling.heads[0][0].value, "204");
    }
LT_END_AUTO_TEST(disabled_absent_and_incomplete_settings_reset_only_connect)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, opening_eligibility_survives_fragmented_type_length_and_payload)
    for (unsigned mode = 0; mode < 3; ++mode) {
        fixture f; f.open(0);
        auto wire = mode == 0 ? h3test::frame(1, h3test::encode(connect()), 2) : h3test::headers(connect());
        std::size_t prefix = mode == 0 ? 1 : mode == 1 ? 2 : wire.size() - 1;
        LT_ASSERT(f.feed(0, std::span(wire).first(prefix))); LT_ASSERT(f.settings());
        LT_ASSERT(f.feed(0, std::span(wire).subspan(prefix))); f.executor.run_pending();
        LT_CHECK(f.sessions.empty()); auto action = f.engine->take_action(); LT_ASSERT(action); LT_ASSERT(action->reset);
        LT_CHECK_EQ(action->reset->error, 0x10eu);
        LT_ASSERT(f.request(4)); LT_CHECK_EQ(f.sessions.size(), 1u);
    }
LT_END_AUTO_TEST(opening_eligibility_survives_fragmented_type_length_and_payload)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, ignored_frame_does_not_freeze_later_headers_eligibility)
    fixture f; f.open(0); LT_ASSERT(f.feed(0, h3test::frame(33, {}))); LT_ASSERT(f.settings());
    LT_ASSERT(f.feed(0, h3test::headers(connect()))); f.executor.run_pending(); LT_CHECK_EQ(f.sessions.size(), 1u);
LT_END_AUTO_TEST(ignored_frame_does_not_freeze_later_headers_eligibility)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, policy_refusals_preserve_ordinary_error_responses)
    for (unsigned mode = 0; mode < 14; ++mode) {
        fixture f; LT_ASSERT(f.settings()); auto fields = connect();
        switch (mode) {
            case 0: fields[1].value = "other"; break;
            case 1: fields[2].value = "ftp"; break;
            case 2: fields.back().value = "12"; break;
            case 3: fields.pop_back(); break;
            case 4: fields.push_back({"origin", "https://denied.test"}); f.options.allowed_origins = {"https://allowed.test"}; break;
            case 5: f.options.allow_absent_origin = false; break;
            case 6: f.options.require_subprotocol = true; break;
            case 7: fields.push_back({"sec-websocket-protocol", "chat, chat"}); break;
            case 8: fields.push_back({"content-length", "0"}); break;
            case 9: fields.push_back({"expect", "100-continue"}); break;
            case 10: fields.push_back({"trailer", "x-end"}); break;
            case 11: f.options.limits.incoming_messages = 0; break;
            case 12: f.options.subprotocols = {"bad protocol"}; break;
            case 13: fields.push_back({"sec-websocket-version", "13"}); break;
        }
        LT_ASSERT(f.request(0, fields)); LT_CHECK(f.sessions.empty()); LT_CHECK_EQ(f.refusals, 1u);
        auto reply = f.reply(); LT_ASSERT_EQ(reply.heads.size(), 1u);
        LT_CHECK_EQ(reply.heads[0][0].value, mode == 2 ? "426" : (mode == 4 || mode == 5) ? "403" : "400");
        if (mode == 2) {
            LT_ASSERT_EQ(reply.heads[0].size(), 2u); LT_CHECK_EQ(reply.heads[0][1].name, "sec-websocket-version");
        }
        LT_CHECK_EQ(f.fins[0], 1u); LT_CHECK(!f.engine->failure());
    }
LT_END_AUTO_TEST(policy_refusals_preserve_ordinary_error_responses)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, malformed_connect_is_message_error_without_sibling_failure)
    for (unsigned mode = 0; mode < 9; ++mode) {
        fixture f; LT_ASSERT(f.settings()); auto fields = connect();
        switch (mode) {
            case 0: fields[1].value = ""; break;
            case 1: fields.push_back({":protocol", "websocket"}); break;
            case 2: fields.erase(fields.begin() + 2); break;
            case 3: fields[0].value = "GET"; break;
            case 4: fields[4].value = "/bad#fragment"; break;
            case 5: fields.push_back({"host", "other.test"}); break;
            case 6: fields.push_back({"connection", "upgrade"}); break;
            case 7: fields[1].name = ":unknown"; break;
            case 8: fields = {{":method", "CONNECT"}, {":authority", "example.test:443"}}; break;
        }
        LT_ASSERT(f.request(0, fields)); LT_CHECK(f.sessions.empty()); auto action = f.engine->take_action();
        LT_ASSERT(action); LT_ASSERT(action->reset); LT_CHECK_EQ(action->reset->error, 0x10eu);
        LT_ASSERT(f.request(4, h3test::get(), true)); LT_CHECK_EQ(f.reply(4).heads[0][0].value, "204");
    }
LT_END_AUTO_TEST(malformed_connect_is_message_error_without_sibling_failure)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, response_admission_failure_preserves_error_response_and_budget)
    fixture f; LT_ASSERT(f.settings(1, 42)); f.options.subprotocols = {"chat"};
    auto fields = connect(); fields.push_back({"sec-websocket-protocol", "chat"});
    const auto messages = f.root.in_use(h3test::hs::resource::ws_message_bytes);
    LT_ASSERT(f.request(0, fields)); LT_CHECK(f.sessions.empty()); LT_CHECK_EQ(f.refusals, 1u);
    LT_CHECK(f.refusal == http::outcome_code::limit_exceeded); LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::ws_message_bytes), messages);
    auto reply = f.reply(); LT_ASSERT_EQ(reply.heads.size(), 1u); LT_CHECK_EQ(reply.heads[0][0].value, "400");
LT_END_AUTO_TEST(response_admission_failure_preserves_error_response_and_budget)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, admission_storage_exhaustion_is_typed_and_reversible)
    fixture f; LT_ASSERT(f.settings());
    h3test::hs::reservation held;
    LT_ASSERT(f.root.reserve(h3test::hs::resource::ws_message_bytes, f.root.capacity(h3test::hs::resource::ws_message_bytes), held).ok());
    LT_ASSERT(f.request()); LT_CHECK(f.sessions.empty()); LT_CHECK_EQ(f.refusals, 1u);
    LT_CHECK(f.refusal == http::outcome_code::limit_exceeded); auto reply = f.reply(); LT_ASSERT_EQ(reply.heads.size(), 1u); LT_CHECK_EQ(reply.heads[0][0].value, "400");
LT_END_AUTO_TEST(admission_storage_exhaustion_is_typed_and_reversible)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, receive_fin_makes_upgrade_unavailable)
    fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request(0, connect(), true));
    LT_CHECK(f.sessions.empty()); LT_CHECK_EQ(f.refusals, 1u); LT_CHECK(f.refusal == http::outcome_code::connection_closed);
    auto reply = f.reply(); LT_ASSERT_EQ(reply.heads.size(), 1u); LT_CHECK_EQ(reply.heads[0][0].value, "400");
LT_END_AUTO_TEST(receive_fin_makes_upgrade_unavailable)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, invalid_codec_caps_are_rejected_before_request_allocation)
    for (unsigned mode = 0; mode < 4; ++mode) {
        h::detail::http3_request_limits limits;
        if (mode == 0) limits.websocket.incoming_messages = SIZE_MAX;
        if (mode == 1) limits.websocket.outgoing_messages = SIZE_MAX;
        if (mode == 2) limits.websocket.incoming_bytes = SIZE_MAX;
        if (mode == 3) limits.websocket.max_message_bytes = limits.websocket.incoming_bytes + 1;
        fixture f(limits); LT_ASSERT(f.engine->failure()); LT_CHECK_EQ(f.engine->failure()->wire_code, 0x107u);
    }
LT_END_AUTO_TEST(invalid_codec_caps_are_rejected_before_request_allocation)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, allocation_refusals_release_session_reservations_and_keep_response_open)
    unsigned refused = 0, accepted = 0;
    for (std::ptrdiff_t index = 0; index < 64 && accepted < 2; ++index) {
        fixture f; LT_ASSERT(f.settings()); f.options.subprotocols = {"chat"};
        auto fields = connect(); fields.push_back({"sec-websocket-protocol", "chat"});
        allocation_failed = false;
        f.before_upgrade = [index] { fail_after = index; };
        f.after_upgrade = [] { fail_after = -1; };
        const auto body = f.root.in_use(h3test::hs::resource::body_buffer_bytes);
        const auto output = f.root.in_use(h3test::hs::resource::response_queue_bytes);
        const auto messages = f.root.in_use(h3test::hs::resource::ws_message_bytes);
        LT_ASSERT(f.request(0, fields)); fail_after = -1;
        auto reply = f.reply(); LT_ASSERT_EQ(reply.heads.size(), 1u);
        if (allocation_failed) {
            ++refused; LT_CHECK(f.sessions.empty()); LT_CHECK_EQ(f.refusals, 1u);
            LT_CHECK(f.refusal == http::outcome_code::limit_exceeded); LT_CHECK_EQ(reply.heads[0][0].value, "400");
            LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::body_buffer_bytes), body);
            LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::response_queue_bytes), output);
            LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::ws_message_bytes), messages);
        } else {
            ++accepted; LT_CHECK_EQ(f.sessions.size(), 1u); LT_CHECK_EQ(reply.heads[0][0].value, "200");
        }
        LT_CHECK(!f.engine->failure());
    }
    LT_CHECK(refused >= 8u); LT_CHECK_EQ(accepted, 2u);
LT_END_AUTO_TEST(allocation_refusals_release_session_reservations_and_keep_response_open)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, cancelled_stream_releases_driver_and_global_budget)
    fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request()); LT_ASSERT_EQ(f.sessions.size(), 1u);
    LT_CHECK(f.root.in_use(h3test::hs::resource::ws_message_bytes) > 0u);
    f.engine->disconnect({http::outcome_code::connection_closed, "test disconnect"});
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::ws_message_bytes), 0u);
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::body_buffer_bytes), 0u);
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::response_queue_bytes), 0u);
    LT_CHECK(!f.sessions[0].try_send(h::websocket::message_kind::text, {}).status.ok());
LT_END_AUTO_TEST(cancelled_stream_releases_driver_and_global_budget)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, qpack_blocked_opening_does_not_gain_eligibility_on_unblock)
    h::detail::http3_request_limits limits; limits.framing.qpack_capacity = 256; limits.framing.qpack_blocked_streams = 4;
    fixture f(limits); h::detail::qpack_dynamic_table table(h::detail::qpack_table_role::encoder);
    LT_ASSERT(table.initialize(f.root, 256).ok()); LT_ASSERT(table.set_capacity(256).ok()); LT_ASSERT(table.insert("x-block", "yes").ok());
    auto fields = connect(); fields.push_back({"x-block", "yes"});
    std::vector<h::detail::qpack_field_view> views;
    for (const auto& field : fields) views.push_back(field.view());
    auto encoded = h::detail::qpack_encoder{}.encode_dynamic(views, table, table.insert_count(), {65536, 65536, 256});
    LT_ASSERT(encoded.status.ok()); f.open(0);
    LT_ASSERT(f.feed(0, h3test::frame(1, std::as_bytes(std::span(encoded.value)))));
    f.executor.run_pending(); LT_CHECK_EQ(f.refusals, 0u); LT_CHECK(f.sessions.empty());
    LT_ASSERT(f.settings()); f.open(6); LT_ASSERT(f.feed(6, h3test::bytes({2})));
    for (auto instruction : {h::detail::qpack_encoder_instruction{h::detail::qpack_encoder_instruction_kind::capacity, 256},
             h::detail::qpack_encoder_instruction{h::detail::qpack_encoder_instruction_kind::insert_literal, 0, false, "x-block", "yes"}}) {
        auto wire = h::detail::qpack_encode_encoder_instruction(instruction, {4096, 4096, 4096}); LT_ASSERT(wire.status.ok());
        LT_ASSERT(f.feed(6, std::as_bytes(std::span(wire.value))));
    }
    f.pump(0); f.executor.run_pending(); LT_CHECK(f.sessions.empty());
    auto action = f.engine->take_action(); LT_ASSERT(action); LT_ASSERT(action->reset); LT_CHECK_EQ(action->reset->error, 0x10eu);
    LT_ASSERT(f.request(4)); LT_CHECK_EQ(f.sessions.size(), 1u);
LT_END_AUTO_TEST(qpack_blocked_opening_does_not_gain_eligibility_on_unblock)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, known_fin_with_receive_gap_refuses_upgrade_before_eof)
    fixture f; LT_ASSERT(f.settings()); auto& stream = f.open(0);
    auto wire = h3test::headers(connect()); LT_ASSERT(f.feed(0, wire));
    LT_ASSERT(f.flow.receive(stream, {0, wire.size() + 10, {}, true}));
    f.executor.run_pending(); LT_CHECK(f.sessions.empty()); LT_CHECK_EQ(f.refusals, 1u);
    LT_CHECK(f.refusal == http::outcome_code::connection_closed);
LT_END_AUTO_TEST(known_fin_with_receive_gap_refuses_upgrade_before_eof)
LT_BEGIN_AUTO_TEST(h3_ws_handshake_suite, drain_started_before_handler_runs_refuses_session_admission)
    fixture f; LT_ASSERT(f.settings()); f.open(0); LT_ASSERT(f.feed(0, h3test::headers(connect())));
    h3test::hs::drain_ticket ticket;
    LT_ASSERT(f.engine->begin_drain(std::chrono::steady_clock::now() + std::chrono::hours(1), ticket).ok());
    f.executor.run_pending(); LT_CHECK(f.sessions.empty()); LT_CHECK_EQ(f.refusals, 1u);
    LT_CHECK(f.refusal == http::outcome_code::connection_closed);
    auto reply = f.reply(); LT_ASSERT_EQ(reply.heads.size(), 1u); LT_CHECK_EQ(reply.heads[0][0].value, "400");
LT_END_AUTO_TEST(drain_started_before_handler_runs_refuses_session_admission)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
