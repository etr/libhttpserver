/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <memory>
#include <utility>
#include <vector>
#include "fuzz/http3_engine_fuzz.hpp"
#include "./http3_request_fixture.hpp"
#include "./littletest.hpp"
namespace {
using wire = std::vector<std::uint8_t>;
void event(wire& out, unsigned op, unsigned stream, wire payload = {}) {
    out.push_back(op); out.push_back(stream); out.push_back(payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}
wire insertion() { return {2, 0x3f, 0x61, 0x41, 'x', 1, 'a'}; }
wire blocked(unsigned required = 2) { return {1, 3, static_cast<std::uint8_t>(required), 0, 0x80}; }
wire static_head() { return {1, 3, 0, 0, 0xd1}; }
}
LT_BEGIN_SUITE(http3_fuzz_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(http3_fuzz_suite)
LT_BEGIN_AUTO_TEST(http3_fuzz_suite, valid_settings_static_fields_and_fragmented_varints)
    for (unsigned width : {1U, 2U, 4U, 8U}) {
        wire input{0}; event(input, 0, 8, {0, 4, 0});
        auto head = h3test::frame(1, h3test::bytes({0, 0, 0xd1}), width);
        wire payload; for (auto b : head) payload.push_back(std::to_integer<std::uint8_t>(b));
        event(input, 0, 0, payload);
        auto result = http3_fuzz_replay(input); LT_CHECK_EQ(result.error, 0U);
        LT_CHECK_EQ(result.accepted[16], 1U); LT_CHECK(result.settings);
    }
LT_END_AUTO_TEST(valid_settings_static_fields_and_fragmented_varints)
LT_BEGIN_AUTO_TEST(http3_fuzz_suite, known_protocol_failures_have_precise_wire_codes)
    struct scenario { wire payload; unsigned slot; unsigned terminal; std::uint64_t code; };
    for (const auto& s : {scenario{{1}, 8, 255, 0x103}, {{0}, 8, 2, 0x104},
            {{4, 0}, 0, 255, 0x105}, {{1}, 0, 2, 0x106}, {{0, 4, 2, 2, 0}, 8, 255, 0x109},
            {{0, 0, 0}, 8, 255, 0x10a}, {{1, 3, 0, 0, 0xff}, 0, 255, 0x200},
            {{2, 0}, 8, 255, 0x201}, {{3, 0}, 8, 255, 0x202}}) {
        wire input{0}; event(input, 0, s.slot, s.payload);
        if (s.terminal != 255) event(input, s.terminal, s.slot);
        LT_CHECK_EQ(http3_fuzz_replay(input).error, s.code);
    }
    wire duplicate{0}; event(duplicate, 0, 8, {0, 4, 0}); event(duplicate, 0, 9, {0});
    LT_CHECK_EQ(http3_fuzz_replay(duplicate).error, 0x103U);
    wire repeated{0}; event(repeated, 0, 8, {0, 4, 0, 4, 0});
    LT_CHECK_EQ(http3_fuzz_replay(repeated).error, 0x105U);
    wire offset{0}; event(offset, 11, 0, static_head());
    LT_CHECK_EQ(http3_fuzz_replay(offset).error, 0x102U);
LT_END_AUTO_TEST(known_protocol_failures_have_precise_wire_codes)
LT_BEGIN_AUTO_TEST(http3_fuzz_suite, encoder_request_reorder_cancellation_and_late_insert)
    for (bool encoder_first : {false, true}) {
        wire input{0};
        if (encoder_first) event(input, 0, 9, insertion());
        event(input, 0, 0, blocked());
        if (!encoder_first) event(input, 0, 9, insertion());
        auto result = http3_fuzz_replay(input); LT_CHECK_EQ(result.error, 0U);
        LT_CHECK_EQ(result.accepted[16], 1U);
        LT_CHECK_EQ(result.accepted[17], encoder_first ? 0U : 1U);
        LT_CHECK_EQ(result.accepted[19], encoder_first ? 0U : 1U);
    }
    for (bool partial : {false, true}) {
        wire input{0}; event(input, 0, 0, partial ? wire{1, 3, 2} : blocked());
        event(input, 1, 0); event(input, 1, 0); event(input, 0, 9, insertion());
        auto result = http3_fuzz_replay(input); LT_CHECK_EQ(result.error, 0U);
        LT_CHECK_EQ(result.accepted[16], 0U); LT_CHECK_EQ(result.blocked_final, 0U);
        LT_CHECK_EQ(result.accepted[18], partial ? 0U : 1U);
    }
    wire siblings{0}; event(siblings, 0, 0, blocked(3)); event(siblings, 0, 1, blocked());
    event(siblings, 0, 2, static_head()); event(siblings, 0, 9, insertion()); event(siblings, 0, 9, {0});
    auto result = http3_fuzz_replay(siblings); LT_CHECK_EQ(result.error, 0U);
    LT_CHECK_EQ(result.accepted[16], 3U); LT_CHECK_EQ(result.accepted[19], 2U);
LT_END_AUTO_TEST(encoder_request_reorder_cancellation_and_late_insert)
LT_BEGIN_AUTO_TEST(http3_fuzz_suite, delivered_fin_is_not_replaced_by_repeated_terminal)
    wire input{0}; event(input, 0, 0, blocked());
    event(input, 2, 0); event(input, 1, 0); event(input, 2, 0);
    event(input, 0, 9, insertion());
    auto result = http3_fuzz_replay(input);
    LT_CHECK_EQ(result.error, 0U); LT_CHECK_EQ(result.accepted[16], 1U);
    LT_CHECK_EQ(result.accepted[18], 0U); LT_CHECK_EQ(result.accepted[19], 1U);
    LT_CHECK_EQ(result.blocked_final, 0U);
LT_END_AUTO_TEST(delivered_fin_is_not_replaced_by_repeated_terminal)
LT_BEGIN_AUTO_TEST(http3_fuzz_suite, decoded_compressed_retained_and_blocked_limits)
    for (unsigned profile : {1U, 2U, 3U, 8U, 12U}) {
        wire input{static_cast<std::uint8_t>(profile)}; event(input, 0, 0, static_head());
        auto result = http3_fuzz_replay(input); LT_CHECK_EQ(result.error, 0x107U);
        LT_CHECK_EQ(result.accepted[20], 1U);
    }
    wire exact{13}; event(exact, 0, 0, static_head()); LT_CHECK_EQ(http3_fuzz_replay(exact).error, 0U);
    wire no_block{4}; event(no_block, 0, 0, blocked()); LT_CHECK_EQ(http3_fuzz_replay(no_block).error, 0x107U);
    wire too_many{0}; for (unsigned id : {0U, 1U, 2U}) event(too_many, 0, id, blocked());
    LT_CHECK_EQ(http3_fuzz_replay(too_many).error, 0x107U);
    wire retained_limit{5}; event(retained_limit, 0, 0, blocked());
    auto admitted = http3_fuzz_replay(retained_limit);
    LT_CHECK_EQ(admitted.error, 0U); LT_CHECK_EQ(admitted.blocked_final, 1U);
    event(retained_limit, 0, 1, blocked());
    LT_CHECK_EQ(http3_fuzz_replay(retained_limit).error, 0x107U);
    wire retained{7}; event(retained, 0, 0, static_head()); event(retained, 0, 1, static_head());
    event(retained, 12, 0); event(retained, 12, 1); LT_CHECK_EQ(http3_fuzz_replay(retained).error, 0U);
LT_END_AUTO_TEST(decoded_compressed_retained_and_blocked_limits)
LT_BEGIN_AUTO_TEST(http3_fuzz_suite, critical_progress_output_refusal_and_partial_feedback)
    wire input{0}; event(input, 0, 0, blocked()); event(input, 0, 1, static_head());
    event(input, 0, 9, insertion()); event(input, 4, 2, {1}); event(input, 4, 2, {1});
    auto result = http3_fuzz_replay(input); LT_CHECK_EQ(result.error, 0U);
    LT_CHECK_EQ(result.accepted[19], 1U); LT_CHECK_EQ(result.accepted[4], 2U);
    wire refused{6}; event(refused, 0, 9, insertion());
    // Capacity is ten bytes; each Duplicate adds an Insert Count Increment.
    for (unsigned i = 0; i < 12; ++i) event(refused, 0, 9, {0});
    LT_CHECK_EQ(http3_fuzz_replay(refused).error, 0x107U);
LT_END_AUTO_TEST(critical_progress_output_refusal_and_partial_feedback)
LT_BEGIN_AUTO_TEST(http3_fuzz_suite, seeded_mutations_are_deterministic_and_bounded)
    LT_CHECK(http3_fuzz_replay_corpus(HTTP3_SEED_DIR) > 100U);
LT_END_AUTO_TEST(seeded_mutations_are_deterministic_and_bounded)
LT_BEGIN_AUTO_TEST(http3_fuzz_suite, composed_blocked_fin_cancel_and_repeated_response_output)
    namespace hd = httpserver::detail;
    namespace hs = httpserver::server;
    namespace http = httpserver::http;
    hs::resource_budget root;
    {
        hd::http3_request_limits l;
        l.framing.headers = {128, 256, 8}; l.framing.qpack_capacity = 128;
        l.framing.qpack_blocked_streams = 2; l.response_buffer_bytes = 64;
        h3test::request_fixture f(l); root = f.root;
        for (auto role : {hd::http3_role::control, hd::http3_role::qpack_encoder, hd::http3_role::qpack_decoder}) {
            const auto opened = f.flow.open_local(true); LT_ASSERT(opened);
            auto stream = std::make_unique<hd::quic_stream_state>(opened.id, hd::quic_endpoint_role::server,
                f.flow.ids(), hd::quic_stream_limits{}, f.pool.critical());
            LT_ASSERT(!f.engine->attach_local(role, *stream)); f.streams.emplace(opened.id, std::move(stream));
        }
        f.open(2); LT_ASSERT(f.feed(2, h3test::bytes({0, 4, 0})));
        f.open(6); LT_ASSERT(f.feed(6, h3test::bytes({2})));
        unsigned calls = 0;
        LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello",
            [&](httpserver::exchange& x) -> httpserver::task<void> {
                ++calls;
                auto received = x.head().head_fields.all("x");
                LT_ASSERT_EQ(received.size(), 2U); LT_CHECK_EQ(received[0], "a"); LT_CHECK_EQ(received[1], "b");
                http::fields fields; fields.append("x-repeat", "first"); fields.append("x-repeat", "second");
                x.respond(http::status::from_code(200), fields); co_return;
            }).ok());
        hd::qpack_dynamic_table table(hd::qpack_table_role::encoder);
        LT_ASSERT(table.initialize(h3test::budget(), 128).ok()); LT_ASSERT(table.set_capacity(128).ok());
        LT_ASSERT(table.insert("x", "a").ok());
        auto fields = h3test::get("GET", "/hello"); fields.push_back({"x", "a"}); fields.push_back({"x", "b", true});
        std::vector<hd::qpack_field_view> views; for (const auto& field : fields) views.push_back(field.view());
        auto encoded = hd::qpack_encoder{}.encode_dynamic(views, table, 1, {128, 256, 8}); LT_ASSERT(encoded.status.ok());
        auto head = h3test::frame(1, std::as_bytes(std::span(encoded.value)));
        f.open(0); f.open(4); LT_ASSERT(f.feed(0, head, true)); LT_ASSERT(f.feed(4, head, true));
        LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 2U);
        f.engine->stop_sending(4, 0x10c); f.engine->stop_sending(4, 0x10c);
        LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 1U);
        for (auto byte : h3test::bytes({0x3f, 0x61, 0x41, 'x', 1, 'a'})) LT_ASSERT(f.feed(6, std::span(&byte, 1)));
        f.executor.run_pending(); f.drain(16, 128);
        LT_CHECK_EQ(calls, 1U); LT_CHECK_EQ(f.fins[0], 1U); LT_CHECK(f.output[4].empty());
        LT_CHECK(f.output[0].size() <= 128U); LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 0U);
        auto response = h3test::decode_response(f.output[0]); LT_ASSERT_EQ(response.heads.size(), 1U);
        LT_ASSERT_EQ(response.heads[0].size(), 3U);
        LT_CHECK_EQ(response.heads[0][1].value, "first"); LT_CHECK_EQ(response.heads[0][2].value, "second");
        f.engine->disconnect({http::outcome_code::connection_closed, "composed fuzz replay complete"});
        LT_CHECK_EQ(root.in_use(hs::resource::qpack_table_bytes), 0U);
    }
    for (unsigned i = 0; i < hs::resource_count; ++i) LT_CHECK_EQ(root.in_use(static_cast<hs::resource>(i)), 0U);
LT_END_AUTO_TEST(composed_blocked_fin_cancel_and_repeated_response_output)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
