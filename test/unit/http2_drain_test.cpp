/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <array>
#include <chrono>
#include <coroutine>
#include <memory>
#include <thread>
#include <type_traits>
#include <string>
#include <vector>
#include <httpserver/exchange.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include "./http2_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
namespace {
std::vector<std::uint8_t> controls(hd::http2_connection& connection, std::size_t step = 65536) {
    std::vector<std::uint8_t> wire;
    while (!connection.output().empty()) {
        auto bytes = connection.output(); auto n = std::min(step, bytes.size());
        wire.insert(wire.end(), bytes.begin(), bytes.begin() + n);
        if (!connection.advance_output(n)) throw std::runtime_error("control advance");
    }
    return wire;
}
std::vector<h2test::wire_frame> goaways(const std::vector<std::uint8_t>& wire) {
    auto frames = h2test::frames(wire);
    std::erase_if(frames, [](const auto& f) { return f.type != 7; }); return frames;
}
std::vector<std::uint8_t> barrier(const std::vector<std::uint8_t>& wire) {
    for (const auto& f : h2test::frames(wire)) if (f.type == 6 && !f.flags) return f.payload;
    throw std::runtime_error("missing drain PING");
}
}  // namespace
LT_BEGIN_SUITE(http2_drain_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_drain_suite)
LT_BEGIN_AUTO_TEST(http2_drain_suite, staged_goaway_waits_for_transmitted_matching_barrier)
    hd::http2_connection connection(h2test::budget());
    LT_ASSERT(connection.feed(h2test::preface()).progress == hd::http2_progress::control_ready);
    controls(connection); connection.processed_stream(3); connection.begin_graceful_goaway(3);
    auto initial = connection.output(); LT_ASSERT_EQ(initial.size(), 17u); LT_CHECK_EQ(initial[3], 7u);
    LT_CHECK_EQ(h2test::read_u32(initial.data() + 9), 0x7fffffffu);
    LT_CHECK_EQ(h2test::read_u32(initial.data() + 13), 0u);
    LT_ASSERT(connection.advance_output(initial.size()));
    auto ping = connection.output(); LT_ASSERT_EQ(ping[3], 6u);
    std::vector<std::uint8_t> token(ping.begin() + 9, ping.end());
    LT_ASSERT(connection.advance_output(1));
    LT_ASSERT(connection.feed(h2test::frame(6, 1, 0, token)).progress == hd::http2_progress::control_ready);
    auto remainder = connection.output(); LT_CHECK_EQ(remainder.size(), 16u); LT_ASSERT(connection.advance_output(remainder.size()));
    LT_CHECK(connection.output().empty()); LT_CHECK(!connection.graceful_complete());
    LT_ASSERT(connection.feed(h2test::frame(6, 1, 0, std::vector<std::uint8_t>(8, 0))).progress == hd::http2_progress::control_ready);
    LT_CHECK(connection.output().empty());
    LT_ASSERT(connection.feed(h2test::frame(6, 1, 0, token)).progress == hd::http2_progress::control_ready);
    auto final = goaways(controls(connection, 1)); LT_ASSERT_EQ(final.size(), 1u);
    LT_CHECK_EQ(h2test::read_u32(final[0].payload.data()), 3u); LT_CHECK_EQ(final[0].stream, 0u);
    LT_CHECK(connection.graceful_complete()); LT_CHECK(controls(connection).empty());
    connection.processed_stream(99);
    connection.terminate({hd::http2_error_scope::connection, hd::http2_error_code::protocol_error});
    auto terminal = goaways(controls(connection)); LT_ASSERT_EQ(terminal.size(), 1u);
    LT_CHECK_EQ(h2test::read_u32(terminal[0].payload.data()), 3u);
LT_END_AUTO_TEST(staged_goaway_waits_for_transmitted_matching_barrier)
LT_BEGIN_AUTO_TEST(http2_drain_suite, drain_controls_are_bounded_under_full_queue_and_preserve_borrowed_frames)
    hd::http2_limits limits; limits.control_frames = 2; limits.control_bytes = 62;
    hd::http2_connection connection(h2test::budget(), limits);
    auto borrowed = connection.output(); std::vector<std::uint8_t> saved(borrowed.begin(), borrowed.end());
    LT_CHECK(connection.queue_window_update(0, 1) == http::outcome_code::ok);
    LT_CHECK(connection.queue_window_update(0, 1) == http::outcome_code::limit_exceeded);
    connection.begin_graceful_goaway(0);
    LT_CHECK(std::equal(borrowed.begin(), borrowed.end(), saved.begin()));
    auto wire = controls(connection, 1); auto stages = goaways(wire); LT_ASSERT_EQ(stages.size(), 1u);
    LT_CHECK_EQ(h2test::read_u32(stages[0].payload.data()), 0x7fffffffu);
    LT_ASSERT(connection.feed(h2test::preface()).progress == hd::http2_progress::control_ready);
    LT_ASSERT(connection.feed(h2test::frame(6, 1, 0, barrier(wire))).progress == hd::http2_progress::control_ready);
    auto final = goaways(controls(connection)); LT_ASSERT_EQ(final.size(), 1u);
    LT_CHECK_EQ(h2test::read_u32(final[0].payload.data()), 0u);
LT_END_AUTO_TEST(drain_controls_are_bounded_under_full_queue_and_preserve_borrowed_frames)

LT_BEGIN_AUTO_TEST(http2_drain_suite, admission_cutoff_preserves_accepted_handlers_and_refuses_later_streams)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal pause; unsigned calls = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        ++calls; co_await pause.wait(); x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
    for (unsigned id : {1u, 3u}) LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, id, h2test::encode(encoder, h2test::get()))));
    executor.run_pending(); LT_CHECK_EQ(calls, 2u);
    hs::drain_ticket ticket; LT_ASSERT(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 5, h2test::encode(encoder, h2test::get())))); executor.run_pending();
    LT_CHECK_EQ(calls, 2u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 2u);
    auto wire = h2test::output(engine, 1); auto resets = h2test::resets(wire); LT_ASSERT_EQ(resets.size(), 1u);
    LT_CHECK_EQ(resets[0].stream, 5u); LT_CHECK_EQ(resets[0].code, 7u);
    LT_ASSERT(h2test::feed(engine, h2test::frame(6, 1, 0, barrier(wire))));
    auto final = goaways(h2test::output(engine)); LT_ASSERT_EQ(final.size(), 1u);
    LT_CHECK_EQ(h2test::read_u32(final[0].payload.data()), 3u);
    pause.signal(); executor.run_pending(); auto replies = h2test::responses(h2test::output(engine)); LT_CHECK_EQ(replies.size(), 2u);
    engine.check_drain(std::chrono::steady_clock::now()); hs::drain_result result;
    LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed); LT_CHECK_EQ(result.remaining, 0u);
LT_END_AUTO_TEST(admission_cutoff_preserves_accepted_handlers_and_refuses_later_streams)
LT_BEGIN_AUTO_TEST(http2_drain_suite, partially_assembled_opening_is_refused_without_advancing_cutoff)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
    auto block = h2test::encode(encoder, h2test::get()); auto middle = block.size() / 2;
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 1, 7, {block.begin(), block.begin() + middle})));
    hs::drain_ticket ticket; LT_ASSERT(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    LT_ASSERT(h2test::feed(engine, h2test::frame(9, 4, 7, {block.begin() + middle, block.end()})));
    auto wire = h2test::output(engine); auto resets = h2test::resets(wire); LT_ASSERT_EQ(resets.size(), 1u);
    LT_CHECK_EQ(resets[0].code, 7u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
    LT_ASSERT(h2test::feed(engine, h2test::frame(6, 1, 0, barrier(wire))));
    auto final = goaways(h2test::output(engine)); LT_ASSERT_EQ(final.size(), 1u); LT_CHECK_EQ(h2test::read_u32(final[0].payload.data()), 0u);
LT_END_AUTO_TEST(partially_assembled_opening_is_refused_without_advancing_cutoff)
LT_BEGIN_AUTO_TEST(http2_drain_suite, refused_headers_keep_hpack_context_and_data_refunds_connection_credit)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned calls = 0; std::string trailer;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        ++calls; x.admit_body({8});
        auto result = co_await x.body().collect(8); LT_CHECK(result.status.ok());
        trailer = std::string(x.body().trailers().first("x-context").value_or(""));
        x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine); LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1)));
    auto fields = h2test::get(); fields[0].value = "POST";
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)))); executor.run_pending(); h2test::output(engine);
    hs::drain_ticket ticket; LT_ASSERT(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    fields.push_back({"x-context", "from-refused-stream"});
    auto rejected = h2test::frame(1, 5, 3, h2test::encode(encoder, fields));
    for (auto byte : rejected) {
        LT_ASSERT(h2test::feed(engine, std::span(&byte, 1)));
    }
    for (unsigned i = 0; i < 3; ++i) LT_ASSERT(h2test::feed(engine, h2test::frame(0, 0, 3, {'a', 'b'})));
    LT_ASSERT(h2test::feed(engine, h2test::frame(0, 0, 1, {'x'}))); executor.run_pending();
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, {{"x-context", "from-refused-stream"}})))); executor.run_pending();
    LT_CHECK_EQ(calls, 1u); LT_CHECK_EQ(trailer, "from-refused-stream");
    auto wire = h2test::output(engine); auto resets = h2test::resets(wire); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(resets[0].code, 7u);
    unsigned refunds = 0;
    for (const auto& f : h2test::frames(wire)) {
        if (f.type == 8 && !f.stream) refunds += h2test::read_u32(f.payload.data());
    }
    LT_CHECK_EQ(refunds, 7u); LT_CHECK_EQ(h2test::responses(wire).size(), 1u);
LT_END_AUTO_TEST(refused_headers_keep_hpack_context_and_data_refunds_connection_credit)

LT_BEGIN_AUTO_TEST(http2_drain_suite, owner_deadline_cancels_parked_and_queued_work_with_stable_remaining_snapshot)
    for (bool started : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::resume_signal pause; httpserver::stop_token token; unsigned destroyed = 0, after = 0;
        struct lifetime { unsigned& destroyed; ~lifetime() { ++destroyed; } };
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            lifetime guard{destroyed}; token = x.cancellation(); co_await pause.wait(); ++after;
        }).ok());
        httpserver::manual_executor executor; hs::drain_ticket ticket;
        {
            hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
            LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
            LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get()))));
            if (started) executor.run_pending();
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            LT_ASSERT(engine.begin_drain(deadline, ticket).ok());
            auto borrowed = engine.output(); std::vector<std::uint8_t> saved(borrowed.begin(), borrowed.end());
            LT_ASSERT(engine.advance_output(1)); engine.check_drain(deadline);
            if (started) LT_CHECK(token.stop_requested());
            LT_CHECK_EQ(destroyed, started ? 1u : 0u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
            auto tail = engine.output(); LT_CHECK(std::equal(tail.begin(), tail.end(), saved.begin() + 1));
            LT_ASSERT(engine.advance_output(tail.size())); auto terminal = goaways(h2test::output(engine)); LT_ASSERT_EQ(terminal.size(), 1u);
            LT_CHECK_EQ(h2test::read_u32(terminal[0].payload.data()), 1u);
            for (unsigned i = 0; i < 2; ++i) {
                hs::drain_result result; LT_ASSERT(ticket.wait(result).ok());
                LT_CHECK(result.status == hs::drain_status::deadline_expired); LT_CHECK_EQ(result.remaining, 2u);
                engine.check_drain(deadline);
            }
            pause.signal(); executor.run_pending(); LT_CHECK_EQ(after, 0u);
        }
        for (auto kind : {hs::resource::streams, hs::resource::body_buffer_bytes, hs::resource::header_bytes,
            hs::resource::header_fields, hs::resource::response_queue_bytes}) LT_CHECK_EQ(budget.in_use(kind), 0u);
        hs::drain_result late; LT_ASSERT(ticket.wait(late).ok()); LT_CHECK_EQ(late.remaining, 2u);
    }
LT_END_AUTO_TEST(owner_deadline_cancels_parked_and_queued_work_with_stable_remaining_snapshot)
LT_BEGIN_AUTO_TEST(http2_drain_suite, handler_wait_rejects_deadlock_and_deadline_reaping_waits_for_resume_return)
    for (bool inline_owner : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor manual; httpserver::inline_executor immediate;
        httpserver::executor& executor = inline_owner ? static_cast<httpserver::executor&>(immediate) : static_cast<httpserver::executor&>(manual);
        hd::http2_request_engine engine(budget, routes, executor); hs::drain_ticket ticket; unsigned destroyed = 0, after = 0;
        struct lifetime { unsigned& destroyed; ~lifetime() { ++destroyed; } };
        httpserver::resume_signal pause;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            lifetime guard{destroyed}; auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            LT_ASSERT(engine.begin_drain(deadline, ticket).ok());
            hs::drain_result result; LT_CHECK(ticket.wait(result).code() == http::outcome_code::would_deadlock);
            engine.check_drain(deadline); LT_CHECK(x.cancellation().stop_requested()); LT_CHECK_EQ(destroyed, 0u);
            engine.output(); engine.begin_turn(); LT_CHECK_EQ(destroyed, 0u);
            co_await pause.wait(); ++after;
        }).ok());
        hd::hpack_encoder encoder(budget); LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); manual.run_pending();
        engine.check_drain(std::chrono::steady_clock::now()); LT_CHECK_EQ(destroyed, 1u);
        pause.signal(); manual.run_pending(); LT_CHECK_EQ(after, 0u);
        hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK_EQ(result.remaining, 2u);
    }
LT_END_AUTO_TEST(handler_wait_rejects_deadlock_and_deadline_reaping_waits_for_resume_return)
LT_BEGIN_AUTO_TEST(http2_drain_suite, external_waiters_claim_expiry_without_mutating_owner_stream_storage)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal pause; unsigned destroyed = 0;
    struct lifetime { unsigned& destroyed; ~lifetime() { ++destroyed; } };
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> {
        lifetime guard{destroyed}; co_await pause.wait();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); executor.run_pending();
    hs::drain_ticket ticket; auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    LT_ASSERT(engine.begin_drain(deadline, ticket).ok()); std::array<hs::drain_result, 2> results; std::array<http::outcome, 2> outcomes;
    std::thread first([&] { outcomes[0] = ticket.wait(results[0]); }); std::thread second([&] { outcomes[1] = ticket.wait(results[1]); });
    first.join(); second.join();
    for (unsigned i = 0; i < 2; ++i) {
        LT_CHECK(outcomes[i].ok()); LT_CHECK(results[i].status == hs::drain_status::deadline_expired); LT_CHECK_EQ(results[i].remaining, 2u);
    }
    LT_CHECK_EQ(destroyed, 0u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 1u);
    engine.check_drain(deadline); LT_CHECK_EQ(destroyed, 1u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
LT_END_AUTO_TEST(external_waiters_claim_expiry_without_mutating_owner_stream_storage)
LT_BEGIN_AUTO_TEST(http2_drain_suite, failed_initiation_preserves_ticket_and_engine_destruction_retires_counted_work)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::manual_executor executor; hs::drain_ticket ticket;
    {
        hd::http2_request_engine engine(budget, routes, executor); LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        LT_CHECK(engine.begin_drain(std::chrono::steady_clock::now(), ticket).code() == http::outcome_code::invalid_argument);
        hs::drain_result empty; LT_CHECK(ticket.wait(empty).code() == http::outcome_code::invalid_state);
        LT_ASSERT(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
        LT_CHECK(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).code() == http::outcome_code::invalid_state);
    }
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
    hd::http2_request_engine terminal(budget, routes, executor); LT_CHECK(!h2test::feed(terminal, h2test::frame(6)));
    LT_CHECK(terminal.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).code() == http::outcome_code::invalid_state);
    LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
LT_END_AUTO_TEST(failed_initiation_preserves_ticket_and_engine_destruction_retires_counted_work)

LT_BEGIN_AUTO_TEST(http2_drain_suite, deadline_cancels_body_reader_and_response_writer_without_late_resumes)
    for (bool inline_owner : {true, false}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        unsigned destroyed = 0, after = 0;
        struct lifetime { unsigned& destroyed; ~lifetime() { ++destroyed; } };
        LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
            lifetime guard{destroyed}; x.admit_body({8}); co_await x.body().collect(8); ++after;
        }).ok());
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            lifetime guard{destroyed}; x.start_response(http::status::from_code(200), {});
            const std::string bytes = "blocked-writer"; co_await x.writer().write(std::as_bytes(std::span(bytes))); ++after;
        }).ok());
        httpserver::manual_executor manual; httpserver::inline_executor immediate;
        httpserver::executor& executor = inline_owner ? static_cast<httpserver::executor&>(immediate) : static_cast<httpserver::executor&>(manual);
        hd::http2_request_limits limits; limits.response_buffer_bytes = 2;
        hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
        LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        auto fields = h2test::get(); fields[0].value = "POST";
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 4, 1, h2test::encode(encoder, fields))));
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get())))); manual.run_pending();
        LT_CHECK_EQ(destroyed, 0u); auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10); hs::drain_ticket ticket;
        LT_ASSERT(engine.begin_drain(deadline, ticket).ok()); engine.check_drain(deadline); manual.run_pending();
        LT_CHECK_EQ(destroyed, 2u); LT_CHECK_EQ(after, 0u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
        hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK_EQ(result.remaining, 3u);
    }
LT_END_AUTO_TEST(deadline_cancels_body_reader_and_response_writer_without_late_resumes)

LT_BEGIN_AUTO_TEST(http2_drain_suite, response_bytes_and_continuation_block_retire_before_successful_ticket)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-long", std::string(42000, '~')); x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); executor.run_pending();
    hs::drain_ticket ticket; LT_ASSERT(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    std::vector<std::uint8_t> wire;
    for (unsigned i = 0; i < 2; ++i) {
        auto bytes = engine.output(); LT_ASSERT(!bytes.empty()); wire.insert(wire.end(), bytes.begin(), bytes.end()); LT_ASSERT(engine.advance_output(bytes.size()));
    }
    auto token = barrier(wire); auto bytes = engine.output(); LT_ASSERT_EQ(bytes[3], 1u); LT_ASSERT(bytes.size() > 16384);
    std::vector<std::uint8_t> saved(bytes.begin(), bytes.end());
    LT_ASSERT(h2test::feed(engine, h2test::frame(6, 1, 0, token)));
    LT_ASSERT(engine.advance_output(1)); wire.push_back(saved[0]);
    auto remainder = engine.output(); LT_CHECK(std::equal(remainder.begin(), remainder.end(), saved.begin() + 1));
    http::outcome waiting; hs::drain_result result;
    executor.post([&] { waiting = ticket.wait(result); }); executor.run_pending(); LT_CHECK(waiting.code() == http::outcome_code::would_deadlock);
    wire.insert(wire.end(), remainder.begin(), remainder.end()); LT_ASSERT(engine.advance_output(remainder.size()));
    auto final = engine.output(); LT_ASSERT_EQ(final[3], 7u); LT_ASSERT(engine.advance_output(1)); wire.push_back(final[0]);
    executor.post([&] { waiting = ticket.wait(result); }); executor.run_pending(); LT_CHECK(waiting.code() == http::outcome_code::would_deadlock);
    h2test::append(wire, h2test::output(engine, 1)); LT_CHECK_EQ(h2test::responses(wire).size(), 1u); LT_CHECK_EQ(goaways(wire).size(), 2u);
    LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed); LT_CHECK_EQ(result.remaining, 0u);
LT_END_AUTO_TEST(response_bytes_and_continuation_block_retire_before_successful_ticket)
LT_BEGIN_AUTO_TEST(http2_drain_suite, accepted_flow_blocked_response_completes_after_window_update)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {}); std::string body = "hello";
        co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto input = h2test::preface(); h2test::append(input, h2test::frame(4, 0, 0, h2test::setting(4, 0)));
    LT_ASSERT(h2test::feed(engine, input)); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); executor.run_pending();
    hs::drain_ticket ticket; LT_ASSERT(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    auto wire = h2test::output(engine); LT_ASSERT(h2test::feed(engine, h2test::frame(6, 1, 0, barrier(wire)))); h2test::append(wire, h2test::output(engine));
    LT_CHECK_EQ(h2test::count_type(wire, 0), 0u); http::outcome waiting; hs::drain_result result;
    executor.post([&] { waiting = ticket.wait(result); }); executor.run_pending(); LT_CHECK(waiting.code() == http::outcome_code::would_deadlock);
    LT_ASSERT(h2test::feed(engine, h2test::frame(8, 0, 1, h2test::increment(5)))); h2test::append(wire, h2test::output(engine, 1));
    auto data = h2test::frames(wire); std::erase_if(data, [](const auto& f) { return f.type != 0; }); LT_ASSERT_EQ(data.size(), 2u);
    LT_CHECK_EQ(std::string(data[0].payload.begin(), data[0].payload.end()), "hello"); LT_CHECK_EQ(data[1].flags, 1u);
    LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
LT_END_AUTO_TEST(accepted_flow_blocked_response_completes_after_window_update)
LT_BEGIN_AUTO_TEST(http2_drain_suite, invalid_and_capacity_refused_openings_never_raise_accepted_cutoff)
    for (bool capacity : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::resume_signal pause;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> { co_await pause.wait(); }).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits; limits.max_streams = 1;
        hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
        LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); executor.run_pending();
        auto fields = h2test::get();
        if (!capacity) fields.push_back({"connection", "close"});
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 9, h2test::encode(encoder, fields)))); h2test::output(engine);
        hs::drain_ticket ticket; auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10); LT_ASSERT(engine.begin_drain(deadline, ticket).ok());
        auto wire = h2test::output(engine); LT_ASSERT(h2test::feed(engine, h2test::frame(6, 1, 0, barrier(wire))));
        auto final = goaways(h2test::output(engine)); LT_ASSERT_EQ(final.size(), 1u); LT_CHECK_EQ(h2test::read_u32(final[0].payload.data()), 1u);
        engine.check_drain(deadline);
    }
LT_END_AUTO_TEST(invalid_and_capacity_refused_openings_never_raise_accepted_cutoff)

LT_BEGIN_AUTO_TEST(http2_drain_suite, protocol_failure_during_drain_retires_ticket_after_terminal_output)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine); hs::drain_ticket ticket;
    LT_ASSERT(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    auto initial = engine.output(); LT_ASSERT_EQ(initial[3], 7u); LT_ASSERT(engine.advance_output(1));
    LT_CHECK(!h2test::feed(engine, h2test::frame(0, 0, 0)));
    auto tail = engine.output(); LT_ASSERT_EQ(tail.size(), 16u); LT_ASSERT(engine.advance_output(tail.size()));
    auto wire = h2test::output(engine); LT_ASSERT_EQ(goaways(wire).size(), 1u); LT_CHECK_EQ(goaways(wire)[0].payload[7], 1u);
    hs::drain_result result; http::outcome waited;
    executor.post([&] { waited = ticket.wait(result); }); executor.run_pending();
    LT_CHECK(waited.ok()); LT_CHECK(result.status == hs::drain_status::completed);
LT_END_AUTO_TEST(protocol_failure_during_drain_retires_ticket_after_terminal_output)

LT_BEGIN_AUTO_TEST(http2_drain_suite, default_owner_pumps_enforce_absolute_deadline_without_waiters)
    for (bool turn : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::resume_signal pause; unsigned destroyed = 0;
        struct lifetime { unsigned& destroyed; ~lifetime() { ++destroyed; } };
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> {
            lifetime guard{destroyed}; co_await pause.wait();
        }).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
        LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); executor.run_pending();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10); hs::drain_ticket ticket;
        LT_ASSERT(engine.begin_drain(deadline, ticket).ok()); std::this_thread::sleep_until(deadline);
        if (turn) {
            engine.begin_turn();
        } else {
            engine.output();
        }
        LT_CHECK_EQ(destroyed, 1u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
        hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK_EQ(result.remaining, 2u);
    }
LT_END_AUTO_TEST(default_owner_pumps_enforce_absolute_deadline_without_waiters)

LT_BEGIN_AUTO_TEST(http2_drain_suite, transport_eof_during_drain_retires_owned_work_without_waiting_for_barrier)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal pause; unsigned destroyed = 0;
    struct lifetime { unsigned& destroyed; ~lifetime() { ++destroyed; } };
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> {
        lifetime guard{destroyed}; co_await pause.wait();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); executor.run_pending();
    hs::drain_ticket ticket; LT_ASSERT(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    engine.eof(); LT_CHECK_EQ(destroyed, 1u); h2test::output(engine);
    hs::drain_result result; http::outcome waited; executor.post([&] { waited = ticket.wait(result); }); executor.run_pending();
    LT_CHECK(waited.ok()); LT_CHECK(result.status == hs::drain_status::completed);
    pause.signal(); executor.run_pending(); LT_CHECK_EQ(destroyed, 1u);
LT_END_AUTO_TEST(transport_eof_during_drain_retires_owned_work_without_waiting_for_barrier)

LT_BEGIN_AUTO_TEST(http2_drain_suite, deadline_snapshot_counts_connection_until_borrowed_response_is_retired)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-long", std::string(42000, '~')); x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); executor.run_pending();
    auto bytes = engine.output(); LT_ASSERT_EQ(bytes[3], 1u); std::vector<std::uint8_t> saved(bytes.begin(), bytes.end());
    LT_ASSERT(engine.advance_output(1)); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
    hs::drain_ticket ticket; auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    LT_ASSERT(engine.begin_drain(deadline, ticket).ok()); engine.check_drain(deadline);
    auto tail = engine.output(); LT_CHECK(std::equal(tail.begin(), tail.end(), saved.begin() + 1));
    LT_ASSERT(engine.advance_output(tail.size())); auto terminal = h2test::output(engine); h2test::append(saved, terminal);
    LT_CHECK_EQ(h2test::responses(saved).size(), 1u); LT_CHECK_EQ(goaways(terminal).size(), 1u);
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::deadline_expired); LT_CHECK_EQ(result.remaining, 1u);
LT_END_AUTO_TEST(deadline_snapshot_counts_connection_until_borrowed_response_is_retired)
LT_BEGIN_AUTO_TEST(http2_drain_suite, drain_reservation_refusal_preserves_existing_ticket_and_can_retry)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::manual_executor executor; hs::drain_ticket ticket;
    {
        hd::http2_request_engine prior(budget, routes, executor);
        LT_ASSERT(prior.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    }
    hd::http2_request_engine engine(budget, routes, executor); LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
    hs::reservation held; auto kind = hs::resource::response_queue_bytes;
    LT_ASSERT(budget.reserve(kind, budget.capacity(kind) - budget.in_use(kind), held).ok());
    LT_CHECK(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).code() == http::outcome_code::limit_exceeded);
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
    held.release(); LT_ASSERT(engine.begin_drain(std::chrono::steady_clock::now() + std::chrono::seconds(10), ticket).ok());
    auto wire = h2test::output(engine); LT_ASSERT(h2test::feed(engine, h2test::frame(6, 1, 0, barrier(wire)))); h2test::output(engine);
    LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
    engine.check_drain(hd::http2_connection::time_point::max()); LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
LT_END_AUTO_TEST(drain_reservation_refusal_preserves_existing_ticket_and_can_retry)
LT_BEGIN_AUTO_TEST(http2_drain_suite, engine_remains_single_owner)
    LT_CHECK(!std::is_copy_constructible_v<hd::http2_request_engine>);
    LT_CHECK(!std::is_copy_assignable_v<hd::http2_request_engine>);
LT_END_AUTO_TEST(engine_remains_single_owner)

LT_BEGIN_AUTO_TEST(http2_drain_suite, application_signal_racing_engine_teardown_keeps_executor_affinity_alive)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal* pause = nullptr;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> { co_await pause->wait(); }).ok());
    httpserver::manual_executor executor;
    for (unsigned i = 0; i < 1000; ++i) {
        httpserver::resume_signal signal; pause = &signal;
        auto engine = std::make_unique<hd::http2_request_engine>(budget, routes, executor); hd::hpack_encoder encoder(budget);
        LT_ASSERT(h2test::feed(*engine, h2test::preface())); h2test::output(*engine);
        LT_ASSERT(h2test::feed(*engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); executor.run_pending();
        std::thread trigger([&] { signal.signal(); }); engine.reset(); trigger.join(); executor.run_pending();
        LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
    }
LT_END_AUTO_TEST(application_signal_racing_engine_teardown_keeps_executor_affinity_alive)

LT_BEGIN_AUTO_TEST(http2_drain_suite, retained_resume_witness_keeps_disabled_affinity_alive_after_engine_destruction)
    struct capture_affinity {
        std::shared_ptr<hd::frame_witness>& witness;
        httpserver::executor*& target;
        capture_affinity& bind_frame(hd::task_frame_base* frame) {
            witness = frame->frame_witness_ptr(); target = frame->frame_executor(); return *this;
        }
        bool await_ready() const { return true; }
        void await_suspend(std::coroutine_handle<>) const {}
        void await_resume() const {}
    };
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    std::shared_ptr<hd::frame_witness> witness; httpserver::executor* target = nullptr; httpserver::resume_signal pause;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> {
        co_await capture_affinity{witness, target}; co_await pause.wait();
    }).ok());
    httpserver::manual_executor executor;
    {
        hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
        LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())))); executor.run_pending();
    }
    LT_ASSERT(witness); LT_ASSERT(target); LT_CHECK(!witness->valid);
    // A trigger can already own a node when teardown invalidates its witness.
    // Its target must remain callable until that node releases the witness.
    unsigned calls = 0; target->post([&] { ++calls; }); executor.run_pending(); LT_CHECK_EQ(calls, 0u);
LT_END_AUTO_TEST(retained_resume_witness_keeps_disabled_affinity_alive_after_engine_destruction)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
