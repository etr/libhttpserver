/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include <memory>
#include <stdexcept>
#include <atomic>
#include <string>
#include "./http2_request_fixture.hpp"
#include "./http3_route_parity_fixture.hpp"
#include "./http3_request_fixture.hpp"
#include "./littletest.hpp"
#include <parity/dispatch_replay.hpp>
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
LT_BEGIN_SUITE(http3_exchange_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(http3_exchange_suite)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, complete_head_snapshots_validated_peer_without_mutating_parked_exchange)
    h3test::request_fixture f;
    httpserver::resume_signal pause;
    httpserver::net::peer_address old_peer{*httpserver::net::parse_address("127.0.0.1"), 4000};
    httpserver::net::peer_address new_peer{*httpserver::net::parse_address("127.0.0.2"), 5000};
    std::vector<httpserver::net::peer_address> seen;
    f.engine->set_peer_for_future_requests(old_peer);
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [&](exchange& x) -> task<void> {
                             auto before = x.peer();
                             seen.push_back(before);
                             if (x.head().head_fields.first("x-park")) co_await pause.wait();
                             LT_CHECK(x.peer() == before);
                             x.respond(http::status::from_code(204), {});
                         })
                  .ok());
    f.open(0);
    f.open(4);
    auto fields = h3test::get();
    fields.push_back({"x-park", "yes"});
    LT_ASSERT(f.feed(0, h3test::headers(fields), true));
    f.executor.run_pending();
    auto wire = h3test::headers(h3test::get());
    LT_ASSERT(f.feed(4, std::span(wire).first(wire.size() - 1)));
    f.engine->set_peer_for_future_requests(new_peer);
    LT_ASSERT(f.feed(4, std::span(wire).last(1), true));
    f.executor.run_pending();
    LT_ASSERT_EQ(seen.size(), 2u);
    LT_CHECK(seen[0] == old_peer);
    LT_CHECK(seen[1] == new_peer);
    pause.signal();
    f.executor.run_pending();
    f.drain();
    LT_CHECK_EQ(f.fins[0], 1u);
    LT_CHECK_EQ(f.fins[4], 1u);
LT_END_AUTO_TEST(complete_head_snapshots_validated_peer_without_mutating_parked_exchange)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, routes_at_initial_headers_before_fin_and_preserves_peer_identity)
    h3test::request_fixture f;
    unsigned calls = 0;
    std::string target;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [&](exchange& x) -> task<void> {
                             ++calls;
                             target = x.head().raw_target;
                             LT_CHECK_EQ(x.connection_id(), 77u);
                             LT_CHECK(x.head().request_protocol == http::protocol::http_3);
                             LT_CHECK_EQ(*x.head().head_fields.first("host"), "example.test");
                             x.respond(http::status::from_code(204), {});
                             co_return;
                         })
                  .ok());
    f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get())));
    f.executor.run_pending();
    LT_CHECK_EQ(calls, 1u);
    LT_CHECK_EQ(target, "/items/../hello?x=1");
    f.drain(16);
    auto r = h3test::decode_response(f.output[0]);
    LT_ASSERT_EQ(r.heads.size(), 1u);
    LT_CHECK_EQ(r.heads[0][0].value, "204");
    LT_CHECK_EQ(f.fins[0], 1u);
    auto action = f.engine->take_action();
    LT_ASSERT(action);
    LT_CHECK(action->stop_sending.has_value());
LT_END_AUTO_TEST(routes_at_initial_headers_before_fin_and_preserves_peer_identity)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, parked_request_does_not_block_sibling_and_synthesized_errors)
    h3test::request_fixture f;
    httpserver::resume_signal pause;
    unsigned completed = 0;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [&](exchange& x) -> task<void> {
                             if (x.head().head_fields.first("x-park")) co_await pause.wait();
                             ++completed;
                             x.respond(http::status::from_code(200), {});
                         })
                  .ok());
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/throw",
                         [](exchange&) -> task<void> {
                             throw std::runtime_error("handler");
                             co_return;
                         })
                  .ok());
    auto fields = h3test::get();
    fields.push_back({"x-park", "yes"});
    for (auto id : {0u, 4u, 8u, 12u})
        f.open(id);
    LT_ASSERT(f.feed(0, h3test::headers(fields), true));
    LT_ASSERT(f.feed(4, h3test::headers(h3test::get()), true));
    LT_ASSERT(f.feed(8, h3test::headers(h3test::get("GET", "/missing")), true));
    LT_ASSERT(f.feed(12, h3test::headers(h3test::get("GET", "/throw")), true));
    f.executor.run_pending();
    f.drain();
    LT_CHECK_EQ(completed, 1u);
    LT_CHECK_EQ(h3test::decode_response(f.output[8]).heads[0][0].value, "404");
    LT_CHECK_EQ(h3test::decode_response(f.output[12]).heads[0][0].value, "500");
    pause.signal();
    f.executor.run_pending();
    f.drain();
    LT_CHECK_EQ(completed, 2u);
LT_END_AUTO_TEST(parked_request_does_not_block_sibling_and_synthesized_errors)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, malformed_message_resets_only_its_stream)
    h3test::request_fixture f;
    auto bad = h3test::get();
    bad.push_back({":method", "POST"});
    f.open(0);
    f.open(4);
    LT_ASSERT(f.feed(0, h3test::headers(bad), true));
    auto a = f.engine->take_action();
    LT_ASSERT(a);
    LT_ASSERT(a->reset);
    LT_CHECK_EQ(a->reset->error, 0x10eu);
    LT_ASSERT(f.feed(4, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    f.drain();
    LT_CHECK_EQ(h3test::decode_response(f.output[4]).heads[0][0].value, "404");
    LT_CHECK(!f.engine->failure());
LT_END_AUTO_TEST(malformed_message_resets_only_its_stream)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, identical_get_post_handlers_observe_semantic_parity_over_three_real_engines)
    for (bool post : {false, true}) {
        std::vector<std::string> observed;
        for (unsigned version : {1u, 2u, 3u}) {
            h3test::request_fixture f;
            std::string observation, body;
            std::atomic<unsigned> entered{0};
            auto method = http::method::known(post ? http::method_id::post : http::method_id::get);
            auto handler = [&](exchange& x) -> task<void> {
                observation = x.head().raw_target + "|" + x.head().route_path + "|" + std::string(*x.head().head_fields.first("host"));
                for (auto value : x.head().head_fields.all("x-repeat"))
                    observation += "|" + std::string(value);
                if (post) {
                    x.admit_body({4});
                    ++entered;
                    std::array<std::byte, 2> bytes;
                    auto first = co_await x.body().read_some(bytes);
                    LT_CHECK(first.status.ok());
                    body.append(reinterpret_cast<const char*>(first.data.data()), first.data.size());
                    auto rest = co_await x.body().collect(16);
                    LT_CHECK(rest.status.ok());
                    body.append(reinterpret_cast<const char*>(rest.data.data()), rest.data.size());
                    observation += "|" + std::string(*x.body().trailers().first("x-end"));
                } else {
                    body = "hello";
                    ++entered;
                }
                http::fields fields;
                if (post) fields.append("trailer", "x-reply");
                x.start_response(http::status::from_code(200), fields);
                co_await x.writer().write(std::as_bytes(std::span(body)));
                http::fields trailers;
                if (post) trailers.append("x-reply", "yes");
                co_await x.writer().finish(trailers);
            };
            LT_ASSERT(f.routes.route(method, "/hello", handler).ok());
            if (version == 1) {
                h3parity::http1_fixture h1(f.root, f.routes);
                std::string wire = post ? "POST" : "GET";
                wire += " /items/../hello?x=1 HTTP/1.1\r\nHost: example.test\r\nx-repeat: a\r\nx-repeat: b\r\n";
                if (post) wire += "Transfer-Encoding: chunked\r\nTrailer: x-end\r\n";
                wire += "\r\n";
                h1.send(wire);
                LT_ASSERT(h3parity::until([&] { return entered.load() == 1; }));
                if (post) h1.send("4\r\nabcd\r\n0\r\nx-end: yes\r\n\r\n");
                auto response = h1.response();
                LT_CHECK_EQ(response.status, 200);
                LT_CHECK_EQ(response.body, post ? "abcd" : "hello");
                if (post) {
                    LT_CHECK(std::any_of(response.headers.begin(), response.headers.end(), [](auto field) { return field.name == "x-reply" && field.value == "yes"; }));
                }
            } else if (version == 2) {
                h3test::hd::http2_request_engine h2(f.root, f.routes, f.executor);
                h3test::hd::hpack_encoder encoder(f.root);
                auto fields = h2test::get();
                fields[0].value = post ? "POST" : "GET";
                fields.push_back({"x-repeat", "a"});
                fields.push_back({"x-repeat", "b"});
                auto wire = h2test::preface();
                h2test::append(wire, h2test::frame(1, post ? 4 : 5, 1, h2test::encode(encoder, fields)));
                LT_ASSERT(h2test::feed(h2, wire));
                f.executor.run_pending();
                LT_CHECK_EQ(entered.load(), 1u);
                if (post) {
                    LT_ASSERT(h2test::feed(h2, h2test::frame(0, 0, 1, {'a', 'b', 'c', 'd'})));
                    LT_ASSERT(h2test::feed(h2, h2test::frame(1, 5, 1, h2test::encode(encoder, {{"x-end", "yes"}}))));
                    f.executor.run_pending();
                }
                auto output = h2test::output(h2);
                auto responses = h2test::responses(output, true);
                LT_ASSERT(!responses.empty());
                LT_CHECK_EQ(responses[0].fields[0].value, "200");
                std::string actual;
                for (auto frame : h2test::frames(output))
                    if (frame.type == 0) actual.append(frame.payload.begin(), frame.payload.end());
                LT_CHECK_EQ(actual, post ? "abcd" : "hello");
                if (post) {
                    LT_ASSERT_EQ(responses.size(), 2u);
                    LT_CHECK_EQ(responses.back().fields[0].name, "x-reply");
                }
            } else {
                f.open(0);
                auto fields = h3test::get(post ? "POST" : "GET");
                fields.push_back({"x-repeat", "a"});
                fields.push_back({"x-repeat", "b"});
                LT_ASSERT(f.feed(0, h3test::headers(fields), !post));
                f.executor.run_pending();
                LT_CHECK_EQ(entered.load(), 1u);
                if (post) {
                    LT_ASSERT(f.feed(0, h3test::data("abcd")));
                    LT_ASSERT(f.feed(0, h3test::headers({{"x-end", "yes"}}), true));
                    f.executor.run_pending();
                }
                f.drain();
                auto response = h3test::decode_response(f.output[0]);
                LT_CHECK_EQ(response.body, post ? "abcd" : "hello");
                LT_CHECK_EQ(response.heads[0][0].value, "200");
                if (post) {
                    LT_ASSERT_EQ(response.heads.size(), 2u);
                    LT_CHECK_EQ(response.heads.back()[0].name, "x-reply");
                }
            }
            observed.push_back(observation);
        }
        LT_CHECK_EQ(observed[0], observed[1]);
        LT_CHECK_EQ(observed[1], observed[2]);
    }
LT_END_AUTO_TEST(identical_get_post_handlers_observe_semantic_parity_over_three_real_engines)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, parity_parameter_capture_and_method_miss)
    h3test::request_fixture f;
    std::string capture;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/params/{id}", [&](exchange& x) -> task<void> {
        for (const auto& arg : x.path_args()) capture = arg.value;
        x.respond(http::status::from_code(204), {}); co_return;
    }).ok());
    f.open(0); f.open(4);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get("GET", "/params/42")), true));
    LT_ASSERT(f.feed(4, h3test::headers(h3test::get("POST", "/params/42")), true));
    f.executor.run_pending(); f.drain();
    auto response = h3test::decode_response(f.output[4]);
    LT_CHECK_EQ(capture, "42");
    LT_ASSERT_EQ(response.heads.size(), 1u);
    LT_CHECK_EQ(response.heads[0][0].value, "405");
    bool allow = false;
    for (const auto& field : response.heads[0]) if (field.name == "allow" && field.value == "GET") allow = true;
    LT_CHECK(allow);
    LT_CHECK_EQ(response.body, "Method not Allowed");
LT_END_AUTO_TEST(parity_parameter_capture_and_method_miss)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, parity_hook_mutation_short_circuit_and_custom_pages)
    httpserver::server::hook_bus bus;
    parity::v3_replay::install_routing_hooks(bus);
    h3test::request_fixture f;
    f.routes = parity::v3_replay::build_routing_basic(f.root);
    unsigned admin_calls = 0;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::del), "/admin", [&](exchange& x) -> task<void> {
        ++admin_calls; x.respond(http::status::from_code(204), {}); co_return;
    }).ok());
    httpserver::detail::request_dispatch_context context{&bus, parity::v3_replay::custom_hook_pages(), nullptr};
    f.engine = std::make_unique<httpserver::detail::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, f.executor,
        httpserver::detail::http3_request_limits{}, 77, httpserver::net::peer_address{}, context);
    std::vector<std::pair<std::string, std::string>> requests{{"GET", "/hello"}, {"DELETE", "/admin"}, {"GET", "/nope"}, {"POST", "/get_only"}};
    for (unsigned i = 0; i < requests.size(); ++i) {
        f.open(i * 4); LT_ASSERT(f.feed(i * 4, h3test::headers(h3test::get(requests[i].first, requests[i].second)), true));
    }
    f.executor.run_pending(); f.drain(); LT_CHECK_EQ(admin_calls, 0u);
    const std::vector<std::string> statuses{"200", "403", "404", "405"};
    const std::vector<std::string> bodies{"OK", "hooked403", "custom-not-found", "custom-not-allowed"};
    for (unsigned i = 0; i < requests.size(); ++i) {
        auto response = h3test::decode_response(f.output[i * 4]);
        LT_ASSERT_EQ(response.heads.size(), 1u); LT_CHECK_EQ(response.heads[0][0].value, statuses[i]);
        bool hook = false, allow = false;
        for (const auto& field : response.heads[0]) {
            if (field.name == "x-hook" && field.value == "after") hook = true;
            if (field.name == "allow" && field.value == "GET") allow = true;
        }
        LT_CHECK_EQ(hook, i != 1);
        if (i == 3) {
            LT_CHECK(allow);
        }
        LT_CHECK_EQ(response.body, bodies[i]); LT_CHECK_EQ(f.fins[i * 4], 1u);
    }
LT_END_AUTO_TEST(parity_hook_mutation_short_circuit_and_custom_pages)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, refused_head_suppresses_response_sent_and_fails_completion)
    httpserver::server::hook_bus bus;
    unsigned sent = 0, completed = 0; bool succeeded = true;
    auto sent_hook = bus.add<httpserver::server::hook_phase::response_sent>([&](httpserver::server::response_sent_ctx&) { ++sent; return httpserver::server::hook_action::pass(); });
    auto completed_hook = bus.add<httpserver::server::hook_phase::request_completed>([&](httpserver::server::request_completed_ctx& ctx) {
        ++completed; succeeded = ctx.succeeded; return httpserver::server::hook_action::pass();
    });
    h3test::request_fixture f;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("Connection", "close");
        x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    httpserver::detail::request_dispatch_context context{&bus, {}, nullptr};
    f.engine = std::make_unique<httpserver::detail::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, f.executor,
        httpserver::detail::http3_request_limits{}, 77, httpserver::net::peer_address{}, context);
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending(); f.drain();
    LT_CHECK(f.output[0].empty());
    auto action = f.engine->take_action(); LT_ASSERT(action); LT_CHECK(action->reset.has_value());
    LT_CHECK_EQ(sent, 0u); LT_CHECK_EQ(completed, 1u); LT_CHECK(!succeeded);
LT_END_AUTO_TEST(refused_head_suppresses_response_sent_and_fails_completion)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, cancelled_parked_request_completes_exactly_once)
    httpserver::server::hook_bus bus;
    unsigned completed = 0, resumed = 0; bool succeeded = true;
    auto completed_hook = bus.add<httpserver::server::hook_phase::request_completed>([&](httpserver::server::request_completed_ctx& ctx) {
        ++completed; succeeded = ctx.succeeded; return httpserver::server::hook_action::pass();
    });
    httpserver::resume_signal parked;
    h3test::request_fixture f;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        co_await parked.wait(); ++resumed; x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::detail::request_dispatch_context context{&bus, {}, nullptr};
    f.engine = std::make_unique<httpserver::detail::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, f.executor,
        httpserver::detail::http3_request_limits{}, 77, httpserver::net::peer_address{}, context);
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    f.engine->terminal(0, {httpserver::detail::quic_terminal_kind::reset, 8}); f.drain();
    parked.signal(); f.executor.run_pending(); f.drain();
    LT_CHECK_EQ(completed, 1u); LT_CHECK(!succeeded); LT_CHECK_EQ(resumed, 0u);
LT_END_AUTO_TEST(cancelled_parked_request_completes_exactly_once)
LT_BEGIN_AUTO_TEST(http3_exchange_suite, runtime_peer_refusal_resets_without_headers_and_releases_stream)
    h3test::request_fixture f;
    unsigned calls = 0, refused = 0;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        ++calls; x.respond(http::status::from_code(204), {}); co_return;
    }).ok());
    httpserver::server::peer_policy peers; httpserver::server::hook_bus hooks;
    auto completed = hooks.add<httpserver::server::hook_phase::request_completed>([&](httpserver::server::request_completed_ctx& ctx) {
        if (ctx.end.code() == http::outcome_code::peer_refused) {
            ++refused; LT_CHECK(!ctx.succeeded);
        }
        return httpserver::server::hook_action::pass();
    });
    httpserver::net::peer_address peer{*httpserver::net::parse_address("127.0.0.1"), 4000};
    httpserver::detail::request_dispatch_context context{&hooks, {}, &peers};
    httpserver::detail::http3_request_limits limits; limits.max_active_requests = 1;
    f.engine = std::make_unique<httpserver::detail::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, f.executor,
        limits, 99, peer, context);
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending(); f.drain();
    LT_ASSERT(peers.deny("127.0.0.1").ok());
    f.open(4); LT_ASSERT(f.feed(4, h3test::headers(h3test::get()), true)); f.executor.run_pending(); f.drain();
    LT_CHECK(f.output[4].empty());
    const auto action = f.engine->take_action();
    LT_CHECK(action.has_value());
    if (action) {
        LT_ASSERT(action->reset); LT_CHECK_EQ(action->reset->error, 0x10bu);
    }
    LT_CHECK_EQ(calls, 1u); LT_CHECK_EQ(refused, 1u);
    LT_ASSERT(peers.remove_denied("127.0.0.1").ok());
    f.open(8); LT_ASSERT(f.feed(8, h3test::headers(h3test::get()), true)); f.executor.run_pending(); f.drain();
    const auto resumed = h3test::decode_response(f.output[8]);
    LT_ASSERT_EQ(resumed.heads.size(), 1u); LT_CHECK_EQ(resumed.heads.front().front().value, "204");
    LT_CHECK_EQ(calls, 2u); LT_CHECK_EQ(refused, 1u);
LT_END_AUTO_TEST(runtime_peer_refusal_resets_without_headers_and_releases_stream)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
