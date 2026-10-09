/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <stdexcept>
#include <string>
#include <httpserver/concurrency/resume_signal.hpp>
#include "./http2_request_fixture.hpp"
#include "./http2_websocket_fixture.hpp"
#include "./http3_websocket_fixture.hpp"
#include "./http3_request_fixture.hpp"
#include "./websocket_engine_helpers.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
namespace {
hs::resource_budget root(hs::resource kind, std::size_t cap) {
    hs::budget_limits limits;
    limits.set(hs::resource::quic_reassembly_bytes, 16777216);
    limits.set(hs::resource::header_fields, 4096);
    limits.set(kind, cap);
    return hs::resource_budget::root(limits);
}
hs::resource_budget child(const hs::resource_budget& enclosing, hs::resource kind, std::size_t cap) {
    hs::budget_limits limits;
    for (std::size_t i = 0; i < hs::resource_count; ++i) {
        const auto resource = static_cast<hs::resource>(i); limits.set(resource, enclosing.capacity(resource));
    }
    limits.set(kind, cap);
    hs::resource_budget result;
    if (!enclosing.child(limits, result).ok()) throw std::runtime_error("child scope refused");
    return result;
}
}  // namespace
LT_BEGIN_SUITE(resource_limits_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(resource_limits_suite)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h2_connection_seats_refuse_siblings_and_reuse_after_destruction)
    auto budget = root(hs::resource::connections, 1);
    auto first = std::make_unique<hd::http2_connection>(budget);
    LT_ASSERT(!first->failure());
    LT_CHECK_EQ(budget.in_use(hs::resource::connections), 1u);
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        hd::http2_connection excess(budget);
        LT_CHECK(excess.failure());
        LT_CHECK_EQ(budget.in_use(hs::resource::connections), 1u);
    }
    first.reset();
    LT_CHECK_EQ(budget.in_use(hs::resource::connections), 0u);
    hd::http2_connection replacement(budget);
    LT_CHECK(!replacement.failure());
LT_END_AUTO_TEST(h2_connection_seats_refuse_siblings_and_reuse_after_destruction)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h3_connection_seats_reach_the_real_enclosing_scope)
    auto budget = root(hs::resource::connections, 1);
    hd::quic_storage_pool pool(4194304, 1048576, budget);
    auto first = std::make_unique<hd::http3_connection>(pool.data(), pool.critical());
    LT_ASSERT(!first->attach_stream(0));
    LT_CHECK_EQ(budget.in_use(hs::resource::connections), 1u);
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        hd::http3_connection excess(pool.data(), pool.critical());
        LT_CHECK(excess.attach_stream(0));
        LT_CHECK_EQ(budget.in_use(hs::resource::connections), 1u);
    }
    first.reset();
    LT_CHECK_EQ(budget.in_use(hs::resource::connections), 0u);
    hd::http3_connection replacement(pool.data(), pool.critical());
    LT_CHECK(!replacement.attach_stream(0));
    LT_CHECK_EQ(budget.in_use(hs::resource::quic_reassembly_bytes), 5242880u);
LT_END_AUTO_TEST(h3_connection_seats_reach_the_real_enclosing_scope)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h3_retained_semantic_headers_charge_ancestors_and_release_on_cancel)
    auto budget = root(hs::resource::header_bytes, 1048576);
    httpserver::resume_signal pause;
    h3test::request_fixture f({}, 65536, {}, 1048576, 64, budget);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> {
        co_await pause.wait();
    }).ok());
    f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    const auto retained = f.pool.data().budget.in_use(hs::resource::header_bytes);
    LT_ASSERT(retained > 0);
    LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), retained);
    LT_CHECK_EQ(budget.in_use(hs::resource::header_fields), f.pool.data().budget.in_use(hs::resource::header_fields));
    f.engine.reset();
    pause.signal(); f.executor.run_pending();
    LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), 0u);
    LT_CHECK_EQ(budget.in_use(hs::resource::header_fields), 0u);
LT_END_AUTO_TEST(h3_retained_semantic_headers_charge_ancestors_and_release_on_cancel)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_live_head_and_exchange_reach_shared_budgets)
    for (const auto* version : {"HTTP/1.0", "HTTP/1.1"}) {
        httpserver::resume_signal pause;
        std::atomic<unsigned> calls{0};
        ws_engine_test::rig f;
        f.root = root(hs::resource::streams, 1);
        LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/", [&](exchange&) -> task<void> {
            ++calls; co_await pause.wait();
        }).ok());
        LT_ASSERT(f.start());
        const std::string head = std::string("GET / ") + version + "\r\nHost: example.test\r\n\r\n";
        io_loopback::write_all(f.pair.peer(), head.data(), head.size());
        LT_ASSERT(ws_engine_test::until([&] { return calls.load() == 1; }));
        LT_CHECK(f.root.in_use(hs::resource::header_bytes) > 0);
        LT_CHECK_EQ(f.root.in_use(hs::resource::header_fields), 1u);
        LT_CHECK_EQ(f.root.in_use(hs::resource::streams), 1u);
        f.engine->shutdown();
        pause.signal();
        LT_ASSERT(ws_engine_test::until([&] { return f.stopped.load(); }));
        f.engine.reset();
        LT_CHECK_EQ(f.root.in_use(hs::resource::header_bytes), 0u);
        LT_CHECK_EQ(f.root.in_use(hs::resource::header_fields), 0u);
        LT_CHECK_EQ(f.root.in_use(hs::resource::streams), 0u);
    }
LT_END_AUTO_TEST(h1_live_head_and_exchange_reach_shared_budgets)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_body_staging_is_charged_until_its_owner_retires)
    httpserver::resume_signal pause;
    std::atomic<bool> entered{false};
    ws_engine_test::rig f;
    f.config.body.max_staged_bytes = 16;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::post), "/", [&](exchange& x) -> task<void> {
        x.admit_body({}); entered = true; co_await pause.wait();
    }).ok());
    LT_ASSERT(f.start());
    const std::string head = "POST / HTTP/1.1\r\nHost: h\r\nContent-Length: 64\r\n\r\n";
    io_loopback::write_all(f.pair.peer(), head.data(), head.size());
    LT_ASSERT(ws_engine_test::until([&] { return entered.load(); }));
    const std::string body(32, 'b');
    io_loopback::write_all(f.pair.peer(), body.data(), body.size());
    LT_ASSERT(ws_engine_test::until([&] { return hd::connection_engine_test_access::queues(*f.engine).body == 16; }));
    LT_CHECK(f.root.in_use(hs::resource::body_buffer_bytes) >= 16);
    f.engine->shutdown(); pause.signal();
    LT_ASSERT(ws_engine_test::until([&] { return f.stopped.load(); }));
    f.engine.reset();
    LT_CHECK_EQ(f.root.in_use(hs::resource::body_buffer_bytes), 0u);
LT_END_AUTO_TEST(h1_body_staging_is_charged_until_its_owner_retires)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_outbox_retains_shared_charge_until_engine_destruction)
    ws_engine_test::rig f;
    f.config.outbox.max_queue_bytes = 32;
    f.create_engine();
    auto& sink = hd::connection_engine_test_access::output_slot(*f.engine);
    http::request_head head; head.request_protocol = http::protocol::http_1_1;
    http::fields fields; fields.append("Content-Length", "64");
    LT_ASSERT(sink.start(head, http::status::from_code(200), fields).ok());
    LT_CHECK(f.root.in_use(hs::resource::response_queue_bytes) > 0);
    f.engine->shutdown(); f.engine.reset();
    LT_CHECK_EQ(f.root.in_use(hs::resource::response_queue_bytes), 0u);
LT_END_AUTO_TEST(h1_outbox_retains_shared_charge_until_engine_destruction)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_output_retirement_restores_shared_capacity_for_a_long_response)
    auto budget = root(hs::resource::response_queue_bytes, 2048);
    hd::http1_response_outbox outbox({32, 1024}, budget);
    auto& sink = outbox.open(1, {});
    http::request_head request; request.request_protocol = http::protocol::http_1_1;
    http::fields fields; fields.append("Content-Length", "4096");
    LT_ASSERT(sink.start(request, http::status::from_code(200), fields).ok());
    outbox.consume_front(outbox.queued_bytes());
    const std::array<std::byte, 32> payload{};
    for (unsigned turn = 0; turn < 128; ++turn) {
        auto result = sink.push(payload);
        LT_ASSERT(result.kind == hd::body_push::accepted);
        LT_CHECK_EQ(result.copied, payload.size());
        outbox.consume_front(outbox.queued_bytes());
        LT_CHECK(budget.in_use(hs::resource::response_queue_bytes) <= 256);
    }
    LT_ASSERT(sink.push_end({}).kind == hd::body_push::accepted);
    outbox.consume_front(outbox.queued_bytes());
    LT_CHECK_EQ(budget.in_use(hs::resource::response_queue_bytes), 0u);
LT_END_AUTO_TEST(h1_output_retirement_restores_shared_capacity_for_a_long_response)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_websocket_upgrade_reserves_shared_message_and_queue_envelopes)
    httpserver::resume_signal pause;
    std::atomic<bool> upgraded{false};
    std::atomic<unsigned> closes{0};
    ws_engine_test::rig f;
    f.config.websocket_limits.max_message_bytes = 64;
    f.config.websocket_limits.incoming_bytes = 128;
    f.config.websocket_limits.output_bytes = 128;
    f.config.websocket_limits.incoming_messages = 2;
    f.config.websocket_limits.outgoing_messages = 2;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/", [&](exchange& x) -> task<void> {
        auto result = co_await x.upgrade({});
        if (!result.session) co_return;
        result.session->on_close([&](auto) { ++closes; }); upgraded = true;
        co_await pause.wait();
    }).ok());
    LT_ASSERT(f.start());
    auto head = ws_engine_test::opening();
    io_loopback::write_all(f.pair.peer(), head.data(), head.size());
    LT_ASSERT(ws_engine_test::until([&] { return upgraded.load(); }));
    LT_CHECK_EQ(f.root.in_use(hs::resource::ws_message_bytes), 64u);
    LT_CHECK(f.root.in_use(hs::resource::body_buffer_bytes) >= 128u);
    LT_CHECK(f.root.in_use(hs::resource::response_queue_bytes) >= 128u);
    f.engine->shutdown(); pause.signal();
    LT_ASSERT(ws_engine_test::until([&] { return f.stopped.load(); }));
    f.engine.reset();
    LT_CHECK_EQ(f.root.in_use(hs::resource::ws_message_bytes), 0u);
    LT_CHECK_EQ(f.root.in_use(hs::resource::body_buffer_bytes), 0u);
    LT_CHECK_EQ(f.root.in_use(hs::resource::response_queue_bytes), 0u);
    LT_CHECK_EQ(closes.load(), 1u);
LT_END_AUTO_TEST(h1_websocket_upgrade_reserves_shared_message_and_queue_envelopes)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h3_cancelled_request_releases_stream_admission_for_replacement)
    auto budget = root(hs::resource::streams, 1);
    httpserver::resume_signal pause;
    h3test::request_fixture f({}, 65536, {}, 1048576, 64, budget);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> {
        co_await pause.wait();
    }).ok());
    auto& first = f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get())));
    f.executor.run_pending();
    LT_CHECK_EQ(budget.in_use(hs::resource::streams), 1u);
    LT_ASSERT(f.flow.receive(first, hd::quic_reset_stream_frame{0, 9, f.incoming[0]}));
    auto terminal = first.take_terminal(); LT_ASSERT(terminal);
    f.engine->terminal(0, *terminal);
    pause.signal(); f.executor.run_pending(); f.engine->begin_turn();
    LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), 0u);
    LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
    LT_ASSERT(f.flow.observe_peer(4));
    auto replacement = std::make_unique<hd::quic_stream_state>(4, hd::quic_endpoint_role::server, f.flow.ids(), hd::quic_stream_limits{}, f.pool.data());
    LT_CHECK(!f.engine->attach_peer(*replacement));
    f.engine.reset();
LT_END_AUTO_TEST(h3_cancelled_request_releases_stream_admission_for_replacement)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_pre_admission_body_reaches_the_enclosing_quota)
    httpserver::resume_signal pause;
    std::atomic<bool> entered{false};
    ws_engine_test::rig f;
    f.config.body.max_staged_bytes = 32;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::post), "/", [&](exchange&) -> task<void> {
        entered = true; co_await pause.wait();
    }).ok());
    LT_ASSERT(f.start());
    const std::string head = "POST / HTTP/1.1\r\nHost: h\r\nContent-Length: 64\r\n\r\n";
    io_loopback::write_all(f.pair.peer(), head.data(), head.size());
    LT_ASSERT(ws_engine_test::until([&] { return entered.load(); }));
    const std::string body(16, 'b');
    io_loopback::write_all(f.pair.peer(), body.data(), body.size());
    LT_ASSERT(ws_engine_test::until([&] { return hd::connection_engine_test_access::queues(*f.engine).tail == 16; }));
    LT_CHECK(f.root.in_use(hs::resource::body_buffer_bytes) >= 16u);
    f.engine->shutdown(); pause.signal();
    LT_ASSERT(ws_engine_test::until([&] { return f.stopped.load(); }));
    LT_CHECK_EQ(f.root.in_use(hs::resource::body_buffer_bytes), 0u);
LT_END_AUTO_TEST(h1_pre_admission_body_reaches_the_enclosing_quota)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_fragmented_headers_enforce_child_and_shared_parent_without_residue)
    const std::string head = "GET / HTTP/1.1\r\nHost: h\r\n\r\n";
    for (bool child_limited : {false, true}) {
        auto budget = root(hs::resource::header_bytes, 6 * head.size());
        hs::budget_limits limits; limits.set(hs::resource::quic_reassembly_bytes, 16777216);
        limits.set(hs::resource::header_fields, 4096);
        limits.set(hs::resource::header_bytes, (child_limited ? 3 : 6) * head.size());
        hs::resource_budget a, b;
        LT_ASSERT(budget.child(limits, a).ok()); LT_ASSERT(budget.child(limits, b).ok());
        auto first = std::make_unique<hd::http1_head_parser>(hd::http1_head_budget{head.size(), 1}, a);
        for (char c : head) first->feed(std::string_view(&c, 1));
        LT_ASSERT(first->state() == hd::http1_head_state::complete);
        LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), 3 * head.size());
        auto second = std::make_unique<hd::http1_head_parser>(hd::http1_head_budget{head.size(), 1}, b);
        second->feed(head); LT_ASSERT(second->state() == hd::http1_head_state::complete);
        for (unsigned i = 0; i < 16; ++i) {
            hd::http1_head_parser refused({head.size(), 1}, child_limited ? a : b);
            refused.feed(head);
            LT_CHECK(refused.failure().code() == http::outcome_code::limit_exceeded);
            LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), 6 * head.size());
        }
        first.reset();
        hd::http1_head_parser replacement({head.size(), 1}, a); replacement.feed(head);
        LT_CHECK(replacement.state() == hd::http1_head_state::complete);
    }
LT_END_AUTO_TEST(h1_fragmented_headers_enforce_child_and_shared_parent_without_residue)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h2_websocket_siblings_share_and_reuse_message_capacity)
    auto budget = root(hs::resource::ws_message_bytes, 64);
    h2ws::fixture first({}, child(budget, hs::resource::ws_message_bytes, 32)), second({}, child(budget, hs::resource::ws_message_bytes, 32)),
        excess({}, child(budget, hs::resource::ws_message_bytes, 32));
    for (auto* f : {&first, &second, &excess}) {
        f->options.limits.max_message_bytes = 32;
        f->options.limits.incoming_bytes = 64; f->options.limits.output_bytes = 64;
        LT_ASSERT(f->start()); LT_ASSERT(f->open());
    }
    LT_CHECK_EQ(first.sessions.size(), 1u); LT_CHECK_EQ(second.sessions.size(), 1u);
    LT_CHECK(excess.sessions.empty()); LT_CHECK_EQ(excess.refusals, 1u);
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 64u);
    for (auto kind : {hs::resource::connections, hs::resource::streams, hs::resource::header_bytes, hs::resource::header_fields,
                      hs::resource::body_buffer_bytes, hs::resource::response_queue_bytes, hs::resource::ws_message_bytes})
        LT_CHECK_EQ(budget.in_use(kind), first.budget.in_use(kind) + second.budget.in_use(kind) + excess.budget.in_use(kind));
    LT_ASSERT(h2test::feed(*first.engine, h2test::frame(3, 0, 1, {0, 0, 0, 8})));
    first.executor.run_pending();
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 32u);
    LT_ASSERT(excess.open(3)); LT_CHECK_EQ(excess.sessions.size(), 1u);
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 64u);
LT_END_AUTO_TEST(h2_websocket_siblings_share_and_reuse_message_capacity)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h3_websocket_siblings_share_and_reuse_message_capacity)
    auto budget = root(hs::resource::ws_message_bytes, 64);
    h3ws::fixture first({}, 65536, 1048576, child(budget, hs::resource::ws_message_bytes, 32)),
        second({}, 65536, 1048576, child(budget, hs::resource::ws_message_bytes, 32)),
        excess({}, 65536, 1048576, child(budget, hs::resource::ws_message_bytes, 32));
    for (auto* f : {&first, &second, &excess}) {
        f->options.limits.max_message_bytes = 32;
        f->options.limits.incoming_bytes = 64; f->options.limits.output_bytes = 64;
        LT_ASSERT(f->settings()); LT_ASSERT(f->request());
    }
    LT_CHECK_EQ(first.sessions.size(), 1u); LT_CHECK_EQ(second.sessions.size(), 1u);
    LT_CHECK(excess.sessions.empty()); LT_CHECK_EQ(excess.refusals, 1u);
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 64u);
    for (auto kind : {hs::resource::connections, hs::resource::streams, hs::resource::header_bytes, hs::resource::header_fields,
                      hs::resource::body_buffer_bytes, hs::resource::response_queue_bytes, hs::resource::ws_message_bytes})
        LT_CHECK_EQ(budget.in_use(kind), first.root.in_use(kind) + second.root.in_use(kind) + excess.root.in_use(kind));
    auto& stream = *first.streams.at(0);
    LT_ASSERT(first.flow.receive(stream, hd::quic_reset_stream_frame{0, 9, first.incoming[0]}));
    auto terminal = stream.take_terminal(); LT_ASSERT(terminal); first.engine->terminal(0, *terminal);
    first.executor.run_pending(); first.engine->begin_turn();
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 32u);
    LT_ASSERT(excess.request(4)); LT_CHECK_EQ(excess.sessions.size(), 1u);
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 64u);
LT_END_AUTO_TEST(h3_websocket_siblings_share_and_reuse_message_capacity)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_chunked_trailers_share_header_capacity_and_release_with_decoder)
    auto budget = root(hs::resource::header_fields, 1);
    hd::http1_body_mode mode; mode.kind = hd::http1_body_kind::chunked;
    auto first = std::make_unique<hd::http1_body_decoder>(mode, hd::http1_body_budget{}, budget);
    first->decode("0\r\nX: yes\r\n\r\n");
    LT_ASSERT(first->failure().ok());
    LT_CHECK_EQ(budget.in_use(hs::resource::header_fields), 1u);
    LT_CHECK(budget.in_use(hs::resource::header_bytes) > 0);
    {
        hd::http1_body_decoder excess(mode, {}, budget);
        excess.decode("0\r\nY: no\r\n\r\n");
        LT_CHECK(excess.failure().code() == http::outcome_code::limit_exceeded);
        LT_CHECK_EQ(budget.in_use(hs::resource::header_fields), 1u);
    }
    first.reset();
    LT_CHECK_EQ(budget.in_use(hs::resource::header_fields), 0u);
    LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), 0u);
    hd::http1_body_decoder replacement(mode, {}, budget);
    replacement.decode("0\r\nZ: ok\r\n\r\n"); LT_CHECK(replacement.failure().ok());
LT_END_AUTO_TEST(h1_chunked_trailers_share_header_capacity_and_release_with_decoder)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_connection_seats_refuse_siblings_and_reuse_after_shutdown)
    auto budget = root(hs::resource::connections, 1);
    ws_engine_test::rig first; first.root = budget; LT_ASSERT(first.start());
    LT_CHECK_EQ(budget.in_use(hs::resource::connections), 1u);
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        ws_engine_test::rig refused; refused.root = budget; LT_ASSERT(refused.start());
        LT_ASSERT(ws_engine_test::until([&] { return refused.stopped.load(); }));
        LT_CHECK_EQ(budget.in_use(hs::resource::connections), 1u);
    }
    first.engine->shutdown();
    LT_ASSERT(ws_engine_test::until([&] { return first.stopped.load(); }));
    LT_CHECK_EQ(budget.in_use(hs::resource::connections), 0u);
    ws_engine_test::rig replacement; replacement.root = budget; LT_ASSERT(replacement.start());
    LT_CHECK_EQ(budget.in_use(hs::resource::connections), 1u);
LT_END_AUTO_TEST(h1_connection_seats_refuse_siblings_and_reuse_after_shutdown)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_body_segments_refuse_siblings_before_copy_and_reuse_after_consumption)
    auto budget = root(hs::resource::body_buffer_bytes, 144);
    hd::http1_body_mode mode; mode.kind = hd::http1_body_kind::length; mode.content_length = 32;
    hd::http1_body_decoder first(mode, {16}, budget);
    LT_CHECK_EQ(first.decode(std::string(16, 'a')).consumed, 16u);
    LT_CHECK_EQ(budget.in_use(hs::resource::body_buffer_bytes), 144u);
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        hd::http1_body_decoder excess(mode, {16}, budget);
        LT_CHECK_EQ(excess.decode(std::string(16, 'b')).consumed, 0u);
        LT_CHECK(excess.failure().code() == http::outcome_code::limit_exceeded);
        LT_CHECK_EQ(budget.in_use(hs::resource::body_buffer_bytes), 144u);
    }
    std::array<std::byte, 16> read{}; first.pull(read);
    LT_CHECK_EQ(budget.in_use(hs::resource::body_buffer_bytes), 0u);
    hd::http1_body_decoder replacement(mode, {16}, budget);
    LT_CHECK_EQ(replacement.decode(std::string(16, 'c')).consumed, 16u);
LT_END_AUTO_TEST(h1_body_segments_refuse_siblings_before_copy_and_reuse_after_consumption)
LT_BEGIN_AUTO_TEST(resource_limits_suite, child_websocket_capacity_refuses_while_parent_still_has_room)
    auto budget = root(hs::resource::ws_message_bytes, 128);
    {
        h2ws::fixture f({}, child(budget, hs::resource::ws_message_bytes, 32));
        f.options.limits.max_message_bytes = 32;
        LT_ASSERT(f.start()); LT_ASSERT(f.open()); LT_ASSERT(f.open(3));
        LT_CHECK_EQ(f.sessions.size(), 1u); LT_CHECK_EQ(f.refusals, 1u);
        LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 32u);
    }
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 0u);
    {
        h3ws::fixture f({}, 65536, 1048576, child(budget, hs::resource::ws_message_bytes, 32));
        f.options.limits.max_message_bytes = 32;
        LT_ASSERT(f.settings()); LT_ASSERT(f.request()); LT_ASSERT(f.request(4));
        LT_CHECK_EQ(f.sessions.size(), 1u); LT_CHECK_EQ(f.refusals, 1u);
        LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 32u);
    }
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 0u);
LT_END_AUTO_TEST(child_websocket_capacity_refuses_while_parent_still_has_room)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_consumed_transport_tail_frees_allocation_before_returning_capacity)
    httpserver::resume_signal read, pause;
    std::atomic<bool> entered{false}, consumed{false};
    ws_engine_test::rig f; f.config.body.max_staged_bytes = 16;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::post), "/", [&](exchange& x) -> task<void> {
        x.admit_body({}); entered = true; co_await read.wait();
        std::array<std::byte, 16> bytes{};
        for (;;) {
            auto part = co_await x.body().read_some(bytes);
            if (!part.status.ok()) co_return;
            if (part.end_of_body) break;
        }
        consumed = true; co_await pause.wait();
    }).ok());
    LT_ASSERT(f.start());
    const std::string head = "POST / HTTP/1.1\r\nHost: h\r\nContent-Length: 64\r\n\r\n";
    io_loopback::write_all(f.pair.peer(), head.data(), head.size());
    LT_ASSERT(ws_engine_test::until([&] { return entered.load(); }));
    const std::string body(64, 'b'); io_loopback::write_all(f.pair.peer(), body.data(), body.size());
    LT_ASSERT(ws_engine_test::until([&] { return hd::connection_engine_test_access::queues(*f.engine).tail > 16; }));
    read.signal(); LT_ASSERT(ws_engine_test::until([&] { return consumed.load(); }));
    LT_CHECK_EQ(hd::connection_engine_test_access::retained_tail_capacity(*f.engine), 0u);
    LT_CHECK_EQ(f.root.in_use(hs::resource::body_buffer_bytes), 0u);
    f.engine->shutdown(); pause.signal();
    LT_ASSERT(ws_engine_test::until([&] { return f.stopped.load(); }));
LT_END_AUTO_TEST(h1_consumed_transport_tail_frees_allocation_before_returning_capacity)
LT_BEGIN_AUTO_TEST(resource_limits_suite, h1_websocket_child_and_parent_refusals_restore_capacity_after_close)
    auto budget = root(hs::resource::ws_message_bytes, 64);
    auto a = child(budget, hs::resource::ws_message_bytes, 32), b = child(budget, hs::resource::ws_message_bytes, 32);
    ws_engine_test::rig first, child_excess, second, parent_excess, replacement;
    httpserver::resume_signal first_pause, second_pause, unused, replacement_pause;
    std::atomic<unsigned> accepted{0}, refused{0}, bad_refusals{0};
    auto start = [&](ws_engine_test::rig& f, hs::resource_budget scope, httpserver::resume_signal& pause) {
        f.root = scope; f.config.websocket_limits.max_message_bytes = 32;
        f.config.websocket_limits.incoming_bytes = 64; f.config.websocket_limits.output_bytes = 64;
        f.config.websocket_limits.incoming_messages = 2; f.config.websocket_limits.outgoing_messages = 2;
        f.routes.route(http::method::known(http::method_id::get), "/", [&, signal = &pause](exchange& x) -> task<void> {
            auto result = co_await x.upgrade({});
            if (!result.session) {
                if (result.status.code() != http::outcome_code::limit_exceeded) ++bad_refusals;
                ++refused; co_return;
            }
            ++accepted; co_await signal->wait();
        });
        if (!f.start()) return false;
        auto head = ws_engine_test::opening(); io_loopback::write_all(f.pair.peer(), head.data(), head.size()); return true;
    };
    LT_ASSERT(start(first, a, first_pause)); LT_ASSERT(ws_engine_test::until([&] { return accepted.load() == 1; }));
    LT_ASSERT(start(child_excess, a, unused)); LT_ASSERT(ws_engine_test::until([&] { return refused.load() == 1; }));
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 32u);
    LT_ASSERT(start(second, b, second_pause)); LT_ASSERT(ws_engine_test::until([&] { return accepted.load() == 2; }));
    LT_ASSERT(start(parent_excess, child(budget, hs::resource::ws_message_bytes, 32), unused));
    LT_ASSERT(ws_engine_test::until([&] { return refused.load() == 2; }));
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 64u);
    first.engine->shutdown(); first_pause.signal(); LT_ASSERT(ws_engine_test::until([&] { return first.stopped.load(); }));
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 32u);
    LT_ASSERT(start(replacement, a, replacement_pause)); LT_ASSERT(ws_engine_test::until([&] { return accepted.load() == 3; }));
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 64u);
    second.engine->shutdown(); second_pause.signal(); replacement.engine->shutdown(); replacement_pause.signal();
    LT_ASSERT(ws_engine_test::until([&] { return second.stopped.load() && replacement.stopped.load(); }));
    LT_CHECK_EQ(budget.in_use(hs::resource::ws_message_bytes), 0u);
    LT_CHECK_EQ(bad_refusals.load(), 0u);
LT_END_AUTO_TEST(h1_websocket_child_and_parent_refusals_restore_capacity_after_close)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
