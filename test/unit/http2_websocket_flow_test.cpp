/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <array>
#include <cstdlib>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/http2_body_stream.hpp>
#include "./http2_websocket_fixture.hpp"
#include "./littletest.hpp"
namespace allocation_observer {
bool enabled = false;
std::size_t bytes = 0;
}
void* operator new(std::size_t n) {
    if (allocation_observer::enabled) allocation_observer::bytes += n;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
namespace {
std::uint32_t credit(const std::vector<std::uint8_t>& wire, std::uint32_t id) {
    std::uint32_t total = 0;
    for (const auto& frame : h2test::frames(wire)) {
        if (frame.type == 8 && frame.stream == id) {
            const auto& p = frame.payload;
            total += ((std::uint32_t{p.at(0)} << 24) | (p.at(1) << 16) | (p.at(2) << 8) | p.at(3)) & 0x7fffffff;
        }
    }
    return total;
}
}  // namespace
using namespace h2ws;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(h2_ws_flow_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(h2_ws_flow_suite)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, zero_stream_window_allows_sibling_and_ping_then_resumes_exact_bytes)
    fixture f; LT_ASSERT(f.start());
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(4, 0, 0, h2test::setting(4, 0))));
    LT_ASSERT(f.open()); LT_ASSERT_EQ(f.sessions.size(), 1u);
    std::string text = "blocked"; LT_CHECK(f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(text))).status.ok());
    auto wire = h2test::frame(1, 5, 3, h2test::encode(f.encoder, h2test::get()));
    h2test::append(wire, h2test::frame(6, 0, 0, std::vector<std::uint8_t>(8, 7)));
    LT_ASSERT(h2test::feed(*f.engine, wire)); auto out = f.output();
    LT_CHECK(data(out, 1).empty()); LT_CHECK_EQ(h2test::count_type(out, 6), 1u);
    LT_ASSERT_EQ(h2test::responses(out, true).size(), 2u);
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(8, 0, 1, h2test::increment(3))));
    auto prefix = data(f.output(), 1); LT_CHECK(prefix == std::vector<std::uint8_t>({129, 7, 'b'}));
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(8, 0, 1, h2test::increment(6))));
    h2test::append(prefix, data(f.output(), 1)); LT_CHECK(prefix == std::vector<std::uint8_t>({129, 7, 'b', 'l', 'o', 'c', 'k', 'e', 'd'}));
LT_END_AUTO_TEST(zero_stream_window_allows_sibling_and_ping_then_resumes_exact_bytes)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, blocked_codec_retains_suffix_and_grants_credit_only_after_consumption)
    fixture f; f.options.limits.max_message_bytes = 4; f.options.limits.incoming_bytes = 4;
    f.options.limits.incoming_messages = 1;
    LT_ASSERT(f.start()); LT_ASSERT(h2test::feed(*f.engine, h2test::frame(4, 1)));
    LT_ASSERT(f.open()); f.output();
    auto first = masked(1, "abcd"); h2test::append(first, masked(1, "efgh"));
    first.insert(first.begin(), 3); first.insert(first.end(), {0, 0, 0});
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(0, 8, 1, first)));
    auto blocked = f.output();
    LT_CHECK_EQ(credit(blocked, 1), 4u);  // Only the pad length and padding may restore stream credit.
    LT_CHECK_EQ(credit(blocked, 0), 20u);  // Padding plus first frame and the second frame's parsed header.
    auto sibling = h2test::frame(1, 5, 3, h2test::encode(f.encoder, h2test::get()));
    const std::vector<std::uint8_t> ping(8, 7);
    h2test::append(sibling, h2test::frame(6, 0, 0, ping));
    LT_ASSERT(h2test::feed(*f.engine, sibling)); auto responsive = f.output();
    auto replies = h2test::responses(responsive); LT_ASSERT_EQ(replies.size(), 1u);
    LT_CHECK_EQ(replies[0].stream, 3u); LT_CHECK_EQ(replies[0].fields[0].value, "204");
    std::vector<std::uint8_t> pong;
    for (const auto& frame : h2test::frames(responsive)) if (frame.type == 6 && frame.flags == 1) pong = frame.payload;
    LT_CHECK(pong == ping); LT_CHECK_EQ(credit(responsive, 1), 0u); LT_CHECK_EQ(credit(responsive, 0), 0u);
    std::vector<std::string> messages;
    auto take = [&]() {
        h::spawn(f.executor, f.sessions[0].receive(), [&](auto result) {
            auto received = std::move(result.value());
            if (received.value) messages.emplace_back(reinterpret_cast<const char*>(received.value->data.data()), received.value->data.size());
        });
        f.executor.run_pending();
    };
    take(); LT_ASSERT_EQ(messages.size(), 1u); LT_CHECK_EQ(messages[0], "abcd");
    auto resumed = f.output();
    LT_CHECK_EQ(credit(resumed, 1), 20u); LT_CHECK_EQ(credit(resumed, 0), 4u);
    take(); LT_ASSERT_EQ(messages.size(), 2u); LT_CHECK_EQ(messages[1], "efgh");
    auto consumed = f.output();
    LT_CHECK_EQ(credit(consumed, 1), 0u); LT_CHECK_EQ(credit(consumed, 0), 0u);
    LT_CHECK(!f.engine->failure());
LT_END_AUTO_TEST(blocked_codec_retains_suffix_and_grants_credit_only_after_consumption)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, header_retirement_does_not_release_codec_output_and_partial_reset_preserves_borrow)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    auto baseline = f.budget.in_use(h::server::resource::response_queue_bytes);
    const std::string text = "pending"; f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(text)));
    auto borrowed = f.engine->output(); LT_ASSERT_EQ(borrowed.size(), 18u); LT_ASSERT_EQ(borrowed[3], 0u);
    const std::vector<std::uint8_t> saved(borrowed.begin(), borrowed.end());
    LT_ASSERT(f.engine->advance_output(9));
    LT_CHECK(f.budget.in_use(h::server::resource::response_queue_bytes) > baseline);
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(3, 0, 1, h2test::increment(8))));
    LT_CHECK(std::equal(saved.begin(), saved.end(), borrowed.begin()));
    LT_ASSERT(f.engine->advance_output(9)); LT_CHECK(h2test::resets(f.output()).empty());
    LT_CHECK_EQ(f.budget.in_use(h::server::resource::streams), 0u);
LT_END_AUTO_TEST(header_retirement_does_not_release_codec_output_and_partial_reset_preserves_borrow)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, writable_waits_until_full_data_payload_retirement)
    fixture f; f.options.limits.outgoing_messages = 1;
    LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    const std::string text = "pending";
    LT_ASSERT(f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(text))).disposition == h::websocket::send_disposition::accepted);
    std::optional<http::outcome> writable;
    h::spawn(f.executor, f.sessions[0].writable(), [&](auto result) { writable.emplace(std::move(result.value())); });
    f.executor.run_pending(); LT_CHECK(!writable);
    LT_CHECK(f.sessions[0].try_send(h::websocket::message_kind::text, {}).disposition == h::websocket::send_disposition::backpressured);
    auto borrowed = f.engine->output(); LT_ASSERT_EQ(borrowed.size(), 18u); LT_ASSERT_EQ(borrowed[3], 0u);
    LT_ASSERT(f.engine->advance_output(9)); f.executor.run_pending(); LT_CHECK(!writable);
    LT_CHECK(f.sessions[0].try_send(h::websocket::message_kind::text, {}).disposition == h::websocket::send_disposition::backpressured);
    LT_ASSERT(f.engine->advance_output(3)); f.executor.run_pending(); LT_CHECK(!writable);
    LT_CHECK(f.sessions[0].try_send(h::websocket::message_kind::text, {}).disposition == h::websocket::send_disposition::backpressured);
    LT_ASSERT(f.engine->advance_output(6)); f.executor.run_pending();
    LT_ASSERT(writable); LT_CHECK(writable->ok());
    LT_CHECK(f.sessions[0].try_send(h::websocket::message_kind::text, {}).disposition == h::websocket::send_disposition::accepted);
LT_END_AUTO_TEST(writable_waits_until_full_data_payload_retirement)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, empty_and_tiny_queue_allocations_fit_reserved_storage)
    for (const std::string& text : {std::string{}, std::string{"x"}}) {
        fixture f; f.options.limits.max_message_bytes = 1; f.options.limits.incoming_bytes = 64;
        f.options.limits.output_bytes = 512; f.options.limits.incoming_messages = 16; f.options.limits.outgoing_messages = 16;
        LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
        auto charge = f.budget.in_use(h::server::resource::response_queue_bytes);
        std::array<bool, 16> accepted{};
        allocation_observer::bytes = 0; allocation_observer::enabled = true;
        for (auto& result : accepted) result = f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(text))).disposition == h::websocket::send_disposition::accepted;
        allocation_observer::enabled = false;
        const auto allocated = allocation_observer::bytes;
        for (bool result : accepted) LT_CHECK(result);
        LT_CHECK(allocated <= charge);
        LT_CHECK(f.sessions[0].try_send(h::websocket::message_kind::text, {}).disposition == h::websocket::send_disposition::backpressured);
        LT_CHECK_EQ(data(f.output(), 1).size(), 16 * (text.size() + 2));
    }
LT_END_AUTO_TEST(empty_and_tiny_queue_allocations_fit_reserved_storage)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, codec_storage_is_reserved_and_refusal_rolls_back_before_success)
    fixture f; LT_ASSERT(f.start()); auto before = f.budget.in_use(h::server::resource::ws_message_bytes);
    LT_ASSERT(f.open()); LT_CHECK(f.budget.in_use(h::server::resource::ws_message_bytes) > before);
    f.output(); LT_ASSERT(h2test::feed(*f.engine, h2test::frame(3, 0, 1, h2test::increment(8))));
    f.output(); LT_CHECK_EQ(f.budget.in_use(h::server::resource::ws_message_bytes), before);
    h::server::reservation full;
    LT_ASSERT(f.budget.reserve(h::server::resource::ws_message_bytes, f.budget.capacity(h::server::resource::ws_message_bytes), full).ok());
    auto used = f.budget.in_use(h::server::resource::response_queue_bytes);
    LT_ASSERT(f.open(3)); LT_CHECK_EQ(f.sessions.size(), 1u); LT_CHECK_EQ(f.refusals, 1u);
    auto responses = h2test::responses(f.output()); LT_ASSERT_EQ(responses.size(), 1u); LT_CHECK_EQ(responses[0].fields[0].value, "400");
    LT_CHECK_EQ(f.budget.in_use(h::server::resource::response_queue_bytes), used);
LT_END_AUTO_TEST(codec_storage_is_reserved_and_refusal_rolls_back_before_success)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, connection_window_exhaustion_resumes_both_streams_fairly)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open(1)); LT_ASSERT(f.open(3)); f.output();
    const std::string text(12000, 'x'); std::vector<std::uint8_t> observed;
    for (unsigned round = 0; round < 3; ++round) {
        for (auto& session : f.sessions) LT_CHECK(session.try_send(h::websocket::message_kind::binary, std::as_bytes(std::span(text))).status.ok());
        h2test::append(observed, f.output());
    }
    const auto before = data(observed, 1).size() + data(observed, 3).size(); LT_CHECK_EQ(before, 65535u);
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(8, 0, 0, h2test::increment(10000))));
    auto out = f.output(); LT_CHECK_EQ(data(out, 1).size() + data(out, 3).size(), 3 * 2 * 12004 - 65535u);
    for (auto id : {1u, 3u}) LT_ASSERT(h2test::feed(*f.engine, h2test::frame(8, 0, id, h2test::increment(10000))));
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(8, 0, 0, h2test::increment(30000))));
    for (auto& session : f.sessions) session.try_send(h::websocket::message_kind::binary, std::as_bytes(std::span(text)));
    auto frames = h2test::frames(f.output()); std::vector<std::uint32_t> turns;
    for (const auto& frame : frames) if (frame.type == 0) turns.push_back(frame.stream);
    LT_ASSERT(turns.size() >= 2u); LT_CHECK(turns[0] != turns[1]);
LT_END_AUTO_TEST(connection_window_exhaustion_resumes_both_streams_fairly)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, settings_window_reduction_pauses_existing_tunnel_without_double_debit)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    const std::string text = "first"; f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(text)));
    LT_CHECK_EQ(data(f.output(), 1).size(), 7u);
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(4, 0, 0, h2test::setting(4, 0))));
    f.sessions[0].try_send(h::websocket::message_kind::text, std::as_bytes(std::span(text))); LT_CHECK(data(f.output(), 1).empty());
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(8, 0, 1, h2test::increment(14))));
    LT_CHECK(data(f.output(), 1) == std::vector<std::uint8_t>({129, 5, 'f', 'i', 'r', 's', 't'}));
LT_END_AUTO_TEST(settings_window_reduction_pauses_existing_tunnel_without_double_debit)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, tight_storage_admission_preserves_capacity_for_wire_output)
    fixture f; LT_ASSERT(f.start()); f.output();
    h::server::reservation occupied;
    const auto kind = h::server::resource::response_queue_bytes;
    LT_ASSERT(f.budget.reserve(kind, f.budget.capacity(kind) - f.budget.in_use(kind) - 12000, occupied).ok());
    LT_ASSERT(f.open());
    if (!f.sessions.empty()) LT_CHECK(f.sessions[0].try_send(h::websocket::message_kind::text, {}).status.ok());
    f.output(); LT_CHECK(!f.engine->failure());
LT_END_AUTO_TEST(tight_storage_admission_preserves_capacity_for_wire_output)
LT_BEGIN_AUTO_TEST(h2_ws_flow_suite, discarded_receive_ring_does_not_double_consume_after_reentrant_cancellation)
    auto budget = h2test::budget(); h::detail::http2_body_stream body(budget);
    LT_ASSERT(body.prepare_receive(8, false, {})); LT_ASSERT(body.receive(std::vector<std::uint8_t>{1, 2, 3, 4}, false));
    auto borrowed = body.receive_prefix(); LT_ASSERT_EQ(borrowed.size(), 4u);
    // A driver notification may reset the stream before feed returns.
    body.discard_received(); body.consume_received(borrowed.size());
    LT_CHECK_EQ(body.unread(), 0u); LT_CHECK_EQ(body.consumed, 0u);
LT_END_AUTO_TEST(discarded_receive_ring_does_not_double_consume_after_reentrant_cancellation)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
