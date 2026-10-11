/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <utility>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <httpserver/detail/worker_pool.hpp>
#include <httpserver/detail/http2_request_state.hpp>
#include "./http2_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
namespace httpserver::detail {
struct http2_native_test_access {
    static void age_unadmitted_body(http2_request_engine& engine, std::uint32_t id) { engine.state_->streams.at(id)->native_body_anchor -= std::chrono::seconds(5); }
    static hs::resource_budget stream_budget(http2_request_engine& engine, std::uint32_t id) { return engine.state_->streams.at(id)->budget; }
};
}
LT_BEGIN_SUITE(http2_native_owner_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_native_owner_suite)
LT_BEGIN_AUTO_TEST(http2_native_owner_suite, reset_running_resume_cancellation_waits_for_stream_affinity)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    std::mutex mutex; std::condition_variable changed;
    bool entered = false, release = false;
    std::atomic<bool> cancelled_inside_running_resume{false};
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](httpserver::exchange& x) -> httpserver::task<void> {
        std::unique_lock lock(mutex); entered = true; changed.notify_all();
        changed.wait(lock, [&] { return release; });
        cancelled_inside_running_resume = x.disconnected();
        x.respond(http::status::from_code(204), {});
        co_return;
    }).ok());
    hd::worker_pool workers(2);
    httpserver::manual_executor owner;
    hd::http2_native_context native; native.workers = &workers;
    hd::http2_request_engine engine(budget, routes, owner, {}, {}, 0, {}, std::move(native));
    hd::hpack_encoder encoder(budget);
    auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire));
    bool started;
    { std::unique_lock lock(mutex); started = changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }); }
    if (started) h2test::feed(engine, h2test::frame(3, 0, 1, h2test::increment(8)));
    { std::lock_guard lock(mutex); release = true; }
    changed.notify_all(); workers.drain_and_join();
    LT_CHECK(started);
    LT_CHECK(!cancelled_inside_running_resume.load());
LT_END_AUTO_TEST(reset_running_resume_cancellation_waits_for_stream_affinity)
LT_BEGIN_AUTO_TEST(http2_native_owner_suite, settings_deadline_expires_without_more_network_input)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::manual_executor owner;
    hd::http2_native_context native; native.workers = &owner;
    hd::http2_request_engine engine(budget, routes, owner, {}, {}, 0, {}, std::move(native));
    const auto now = std::chrono::steady_clock::now();
    auto settings = engine.output(now);
    LT_ASSERT(!settings.empty());
    LT_ASSERT(engine.advance_output(settings.size()));
    engine.check_drain(now + std::chrono::seconds(11));
    LT_CHECK(engine.failure().has_value());
LT_END_AUTO_TEST(settings_deadline_expires_without_more_network_input)
LT_BEGIN_AUTO_TEST(http2_native_owner_suite, reset_queued_resume_never_enters_and_late_signal_is_cancelled)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned entered = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](httpserver::exchange& x) -> httpserver::task<void> {
        ++entered; x.respond(http::status::from_code(204), {}); co_return;
    }).ok());
    httpserver::manual_executor owner, workers;
    hd::http2_native_context native; native.workers = &workers;
    hd::http2_request_engine engine(budget, routes, owner, {}, {}, 0, {}, std::move(native));
    hd::hpack_encoder encoder(budget);
    auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire));
    LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, 1, h2test::increment(8))));
    workers.run_pending(); h2test::output(engine);
    LT_CHECK_EQ(entered, 0u);
    LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
LT_END_AUTO_TEST(reset_queued_resume_never_enters_and_late_signal_is_cancelled)
LT_BEGIN_AUTO_TEST(http2_native_owner_suite, failed_end_with_concurrent_consumer_credits_each_received_byte_once)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal pause;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](httpserver::exchange& x) -> httpserver::task<void> {
        x.admit_body({16}); co_await pause.wait();
        std::array<std::byte, 16> buffer;
        for (;;) {
            auto part = co_await x.body().read_some(buffer);
            if (!part.status.ok() || part.end_of_body) break;
        }
    }).ok());
    httpserver::manual_executor owner, workers;
    bool consume_on_publication = false;
    hd::http2_native_context native; native.workers = &workers;
    native.progress = std::make_shared<httpserver::executor::handler>([&] {
        if (consume_on_publication) {
            consume_on_publication = false;
            pause.signal();
            workers.run_pending();
        }
    });
    hd::http2_request_engine engine(budget, routes, owner, {}, {}, 0, {}, std::move(native));
    hd::hpack_encoder encoder(budget); auto fields = h2test::get(); fields[0].value = "POST";
    fields.push_back({"content-length", "6"});
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    LT_ASSERT(h2test::feed(engine, wire)); workers.run_pending(); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(0, 0, 1, {'a', 'b', 'c'})));
    consume_on_publication = true;
    LT_ASSERT(h2test::feed(engine, h2test::frame(0, 1, 1, {'d', 'e'})));
    workers.run_pending();
    std::uint64_t credited = 0;
    for (const auto& frame : h2test::frames(h2test::output(engine))) if (frame.type == 8 && !frame.stream)
        credited += (frame.payload[0] << 24) | (frame.payload[1] << 16) | (frame.payload[2] << 8) | frame.payload[3];
    LT_CHECK_EQ(credited, 5u);
LT_END_AUTO_TEST(failed_end_with_concurrent_consumer_credits_each_received_byte_once)
LT_BEGIN_AUTO_TEST(http2_native_owner_suite, admitted_stream_owns_seat_metadata_and_headers_in_child_scope)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::manual_executor owner, workers;
    hd::http2_native_context native; native.workers = &workers;
    hd::http2_request_engine engine(budget, routes, owner, {}, {}, 0, {}, std::move(native));
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire));
    auto child = hd::http2_native_test_access::stream_budget(engine, 1);
    LT_CHECK_EQ(child.in_use(hs::resource::streams), 1u);
    LT_CHECK(child.in_use(hs::resource::body_buffer_bytes) > 0);
    LT_CHECK(child.in_use(hs::resource::header_bytes) > 0);
    LT_CHECK(child.in_use(hs::resource::header_fields) > 0);
    engine.stop(http::outcome_code::cancelled); workers.run_pending(); engine.begin_turn();
    LT_CHECK_EQ(child.in_use(hs::resource::streams), 0u);
    LT_CHECK_EQ(child.in_use(hs::resource::header_bytes), 0u);
LT_END_AUTO_TEST(admitted_stream_owns_seat_metadata_and_headers_in_child_scope)
LT_BEGIN_AUTO_TEST(http2_native_owner_suite, body_idle_starts_at_admission_after_header_suspension)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [](httpserver::exchange& x) -> httpserver::task<void> {
        x.admit_body({16}); std::array<std::byte, 16> bytes; co_await x.body().read_some(bytes);
    }).ok());
    httpserver::manual_executor owner, workers;
    hd::http2_native_context native; native.workers = &workers;
    hd::http2_request_limits limits; limits.timeouts.body_idle = std::chrono::seconds(1);
    hd::http2_request_engine engine(budget, routes, owner, limits, {}, 0, {}, std::move(native));
    hd::hpack_encoder encoder(budget); auto fields = h2test::get(); fields[0].value = "POST";
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    LT_ASSERT(h2test::feed(engine, wire));
    hd::http2_native_test_access::age_unadmitted_body(engine, 1);
    workers.run_pending();
    const auto now = std::chrono::steady_clock::now();
    auto deadline = engine.next_deadline(now);
    LT_CHECK(deadline && *deadline > now);
    engine.stop(http::outcome_code::cancelled); workers.run_pending(); engine.begin_turn();
LT_END_AUTO_TEST(body_idle_starts_at_admission_after_header_suspension)
LT_BEGIN_AUTO_TEST(http2_native_owner_suite, complete_headers_do_not_leave_an_idle_header_deadline)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [](httpserver::exchange& x) -> httpserver::task<void> {
        x.admit_body({16}); std::array<std::byte, 16> bytes; co_await x.body().read_some(bytes);
    }).ok());
    httpserver::manual_executor owner, workers;
    hd::http2_native_context native; native.workers = &workers;
    hd::http2_request_limits limits;
    limits.timeouts.header = std::chrono::milliseconds(1);
    limits.timeouts.body_idle = std::chrono::seconds(2);
    hd::http2_request_engine engine(budget, routes, owner, limits, {}, 0, {}, std::move(native));
    hd::hpack_encoder encoder(budget); auto fields = h2test::get(); fields[0].value = "POST";
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    LT_ASSERT(h2test::feed(engine, wire)); workers.run_pending();
    const auto now = std::chrono::steady_clock::now();
    engine.check_drain(now + std::chrono::milliseconds(10));
    LT_CHECK(!engine.failure());
    engine.stop(http::outcome_code::cancelled); workers.run_pending(); engine.begin_turn();
LT_END_AUTO_TEST(complete_headers_do_not_leave_an_idle_header_deadline)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS();
LT_END_AUTO_TEST_ENV()
