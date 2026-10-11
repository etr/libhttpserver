/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include "./http3_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
namespace {
struct fixture : h3test::request_fixture {
    httpserver::manual_executor workers;
    std::shared_ptr<hd::drain_scope> global = std::make_shared<hd::drain_scope>();
    explicit fixture(hd::http3_request_limits limits = {}) {
        engine.reset();
        hd::http3_native_context native; native.workers = &workers; native.drain = global;
        engine = std::make_unique<hd::http3_request_engine>(pool.data(), pool.critical(), flow, recovery, routes, executor, limits, 77,
            httpserver::net::peer_address{}, hd::request_dispatch_context{}, native);
    }
    ~fixture() {
        engine->disconnect({http::outcome_code::connection_closed, "test shutdown"});
        workers.run_pending(); static_cast<void>(engine->native_retired());
    }
};
}
LT_BEGIN_SUITE(native_h3_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(native_h3_suite)
LT_BEGIN_AUTO_TEST(native_h3_suite, destroyed_engine_retires_stream_before_affinity_closure_releases_last_reference)
    h3test::request_fixture f;
    httpserver::manual_executor workers;
    hs::hook_bus hooks;
    unsigned completions = 0;
    auto connection = hooks.add<hs::hook_phase::request_completed>([&](hs::request_completed_ctx&) {
        ++completions; return hs::hook_action::pass();
    });
    httpserver::resume_signal hold;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        co_await hold.wait(); x.respond(http::status::from_code(204), {});
    }).ok());
    f.engine.reset();
    hd::http3_native_context native; native.workers = &workers;
    hd::request_dispatch_context dispatch; dispatch.hooks = &hooks;
    f.engine = std::make_unique<hd::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, f.executor,
        hd::http3_request_limits{}, 77, httpserver::net::peer_address{}, dispatch, native);
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    workers.run_pending();
    LT_CHECK_EQ(completions, 0u);
    f.engine.reset();
    // Native finish releases its last state lease before the enclosing affinity
    // closure drops the stream; sanitizer checks the actual destructor tail.
    workers.run_pending();
    LT_CHECK_EQ(completions, 1u);
    hold.signal(); workers.run_pending();
    LT_CHECK_EQ(completions, 1u);
LT_END_AUTO_TEST(destroyed_engine_retires_stream_before_affinity_closure_releases_last_reference)
LT_BEGIN_AUTO_TEST(native_h3_suite, response_command_owns_cached_field_storage_after_handler_returns)
    fixture f;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-owned-h3", std::string(128, 'a'));
        x.start_response(http::status::from_code(200), fields);
        const std::string body = "bounded owned response";
        co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.workers.run_pending();
    f.drain(); f.workers.run_pending(); f.drain();
    const auto response = h3test::decode_response(f.output[0]);
    LT_CHECK_EQ(response.body, std::string("bounded owned response"));
    LT_ASSERT(!response.heads.empty());
    LT_CHECK(std::any_of(response.heads[0].begin(), response.heads[0].end(), [](const auto& field) { return field.name == "x-owned-h3" && field.value == std::string(128, 'a'); }));
LT_END_AUTO_TEST(response_command_owns_cached_field_storage_after_handler_returns)
LT_BEGIN_AUTO_TEST(native_h3_suite, retained_but_unsent_response_has_write_deadline)
    hd::http3_request_limits limits; limits.timeouts.write_idle = std::chrono::milliseconds(10);
    fixture f(limits);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {});
        const std::string body = "unsent";
        co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.workers.run_pending();
    for (unsigned n = 0; n < 4; ++n) { f.engine->begin_turn(); f.engine->pump_output(); }
    const auto due = f.engine->next_deadline();
    LT_CHECK(due.has_value());
    f.engine->expire(std::chrono::steady_clock::now() + std::chrono::milliseconds(20));
    const auto action = f.engine->take_action();
    LT_CHECK(action && action->reset);
LT_END_AUTO_TEST(retained_but_unsent_response_has_write_deadline)
LT_BEGIN_AUTO_TEST(native_h3_suite, native_upgrade_refuses_before_touching_wire_domain)
    fixture f;
    http::outcome_code result = http::outcome_code::ok;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        result = (co_await x.upgrade({})).status.code();
        x.respond(http::status::from_code(204), {});
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.workers.run_pending();
    LT_CHECK(result == http::outcome_code::not_supported);
LT_END_AUTO_TEST(native_upgrade_refuses_before_touching_wire_domain)
LT_BEGIN_AUTO_TEST(native_h3_suite, already_admitted_native_connection_is_not_charged_twice)
    fixture f;
    f.engine.reset();
    hs::budget_limits limits; limits.set(hs::resource::connections, 1); limits.set(hs::resource::quic_reassembly_bytes, 16777216);
    auto root = hs::resource_budget::root(limits);
    hd::quic_storage_pool pool(4194304, 1048576, root);
    hs::reservation seat; LT_ASSERT(root.reserve(hs::resource::connections, 1, seat).ok());
    hd::http3_native_context native; native.workers = &f.workers;
    f.engine = std::make_unique<hd::http3_request_engine>(pool.data(), pool.critical(), f.flow, f.recovery, f.routes, f.executor,
        hd::http3_request_limits{}, 77, httpserver::net::peer_address{}, hd::request_dispatch_context{}, native);
    bool attached = false;
    try { f.open(0); attached = true; } catch (const std::runtime_error&) {}
    LT_CHECK(attached); LT_CHECK(!f.engine->failure()); LT_CHECK_EQ(root.in_use(hs::resource::connections), 1u);
LT_END_AUTO_TEST(already_admitted_native_connection_is_not_charged_twice)
LT_BEGIN_AUTO_TEST(native_h3_suite, admitted_body_progress_reanchors_only_that_stream_idle_deadline)
    hd::http3_request_limits limits; limits.timeouts.body_idle = std::chrono::milliseconds(100);
    fixture f(limits);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::post), "/hello", [](exchange& x) -> task<void> {
        x.admit_body({1024}); std::array<std::byte, 64> buffer;
        while (!(co_await x.body().read_some(buffer)).end_of_body && !x.disconnected()) {}
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get("POST")))); f.workers.run_pending();
    const auto first = f.engine->next_deadline(); LT_ASSERT(first);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    LT_ASSERT(f.feed(0, h3test::data("progress"))); f.workers.run_pending();
    const auto second = f.engine->next_deadline(); LT_ASSERT(second);
    LT_CHECK(*second > *first + std::chrono::milliseconds(5));
LT_END_AUTO_TEST(admitted_body_progress_reanchors_only_that_stream_idle_deadline)
LT_BEGIN_AUTO_TEST(native_h3_suite, later_partial_header_has_its_own_deadline)
    hd::http3_request_limits limits; limits.timeouts.header = std::chrono::milliseconds(10);
    fixture f(limits);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> { x.respond(http::status::from_code(204), {}); co_return; }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.workers.run_pending(); f.drain();
    f.open(4); LT_ASSERT(f.feed(4, std::vector<std::byte>{std::byte{1}}));
    LT_CHECK(f.engine->next_deadline().has_value());
    f.engine->expire(std::chrono::steady_clock::now() + std::chrono::milliseconds(20));
    auto action = f.engine->take_action();
    LT_CHECK(action && action->reset && action->reset->stream == 4);
LT_END_AUTO_TEST(later_partial_header_has_its_own_deadline)
LT_BEGIN_AUTO_TEST(native_h3_suite, native_global_and_protocol_tickets_both_count_unretired_request)
    fixture f; httpserver::resume_signal held;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> { co_await held.wait(); }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.workers.run_pending();
    LT_CHECK_EQ(f.global->active(), 1u);
    hs::drain_ticket ticket;
    const auto now = std::chrono::steady_clock::now();
    LT_ASSERT(f.engine->begin_drain(now + std::chrono::milliseconds(10), ticket, now).ok());
    f.engine->check_drain(now + std::chrono::milliseconds(10));
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok());
    LT_CHECK(result.status == hs::drain_status::deadline_expired); LT_CHECK_EQ(result.remaining, 2u);
    held.signal(); f.workers.run_pending();
LT_END_AUTO_TEST(native_global_and_protocol_tickets_both_count_unretired_request)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
