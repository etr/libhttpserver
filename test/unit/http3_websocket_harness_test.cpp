/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <map>
#include <memory>
#include <string>
#if __has_include("../support/http3_websocket_routes.hpp")
#include "../support/http3_websocket_routes.hpp"
#include "./http3_websocket_fixture.hpp"
#define HAS_WS_ROUTES 1
#endif
#include "./littletest.hpp"
LT_BEGIN_SUITE(h3_ws_harness_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(h3_ws_harness_suite)
#ifdef HAS_WS_ROUTES
struct harness : h3ws::fixture {
    std::map<std::uint64_t, std::shared_ptr<h3wstest::observation>> observations;
    std::uint64_t entering_stream = 0;
    explicit harness(std::uint64_t credit = 65536) : fixture({}, credit) {
        h3wstest::install(routes, [this](httpserver::exchange&) {
            auto state = std::make_shared<h3wstest::observation>();
            observations[entering_stream] = state;
            return state;
        });
    }
    bool tunnel(const char* path, unsigned id = 0) {
        entering_stream = id;
        auto fields = h3ws::connect(path);
        fields.push_back({"sec-websocket-protocol", "other, harness.v1"});
        fields.push_back({"sec-websocket-extensions", "permessage-deflate"});
        return request(id, fields);
    }
};
LT_BEGIN_AUTO_TEST(h3_ws_harness_suite, shared_echo_upgrades_and_echoes_binary)
    harness f; LT_ASSERT(f.settings()); LT_ASSERT(f.tunnel("/ws/echo"));
    auto response = f.reply(); LT_ASSERT_EQ(response.heads.size(), 1U);
    LT_CHECK_EQ(response.heads[0][0].value, "200");
    LT_CHECK_EQ(f.observations[0]->subprotocol, "harness.v1");
    const std::string input("\0\xff\x80", 3);
    LT_ASSERT(f.data(h3ws::masked(2, input))); f.executor.run_pending(); f.drain();
    LT_CHECK_EQ(f.reply().body, std::string("\x82\x03", 2) + input);
    LT_CHECK_EQ(f.observations[0]->received_bytes, 3U);
    LT_ASSERT(f.data(h3ws::masked(8, std::string("\x03\xe8", 2)), 0, true)); f.executor.run_pending(); f.drain();
    LT_CHECK_EQ(f.observations[0]->close_count, 1U); LT_CHECK(f.observations[0]->clean);
    LT_CHECK(f.observations[0]->retired);
LT_END_AUTO_TEST(shared_echo_upgrades_and_echoes_binary)
LT_BEGIN_AUTO_TEST(h3_ws_harness_suite, flood_waits_for_capacity_and_resumes_real_output)
    harness f(4096); LT_ASSERT(f.settings()); LT_ASSERT(f.tunnel("/ws/flood")); f.drain();
    auto state = f.observations[0];
    LT_CHECK(state->waiting); LT_CHECK(state->blocked > 0); LT_CHECK(state->accepted < 64);
    LT_ASSERT_EQ(f.output[0].size(), 4096U);
    LT_ASSERT(f.flow.apply({httpserver::detail::quic_flow_kind::max_stream_data, 1048576, 0})); f.drain();
    LT_CHECK_EQ(state->accepted, 64U); LT_CHECK(state->resumed > 0); LT_CHECK(!state->waiting);
    std::string expected;
    for (unsigned index = 0; index < 64; ++index) expected += std::string("\x82\x7e\x20\x00", 4) + std::string(8192, static_cast<char>(index));
    expected += std::string("\x88\x02\x03\xe8", 4);
    LT_CHECK_EQ(f.reply().body, expected);
    LT_ASSERT(f.data(h3ws::masked(8, std::string("\x03\xe8", 2)), 0, true)); f.executor.run_pending(); f.drain();
    LT_CHECK_EQ(state->close_count, 1U); LT_CHECK(state->retired);
LT_END_AUTO_TEST(flood_waits_for_capacity_and_resumes_real_output)
LT_BEGIN_AUTO_TEST(h3_ws_harness_suite, reset_observed_once_and_sibling_survives)
    harness f; LT_ASSERT(f.settings()); LT_ASSERT(f.tunnel("/ws/echo")); f.drain();
    LT_ASSERT(f.flow.receive(*f.streams[0], httpserver::detail::quic_reset_stream_frame{0, 268, f.incoming[0]})); f.pump(0); f.executor.run_pending();
    LT_CHECK_EQ(f.observations[0]->close_count, 1U); LT_CHECK_EQ(f.observations[0]->reason, "cancelled");
    LT_ASSERT(f.request(4, h3test::get(), true)); LT_CHECK_EQ(f.reply(4).heads[0][0].value, "204");
    f.engine->disconnect({httpserver::http::outcome_code::connection_closed, "done"}); f.executor.run_pending();
    LT_CHECK_EQ(f.observations[0]->close_count, 1U);
LT_END_AUTO_TEST(reset_observed_once_and_sibling_survives)
#else
LT_BEGIN_AUTO_TEST(h3_ws_harness_suite, shared_routes_required)
    LT_CHECK(false);
LT_END_AUTO_TEST(shared_routes_required)
#endif
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
