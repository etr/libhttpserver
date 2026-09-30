/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-104: the streaming response writer over the exchange
// (PRD-V3N-REQ-025/026/027). Pins the response-side mirror of the body
// reader pipeline:
//   - one bounded write at a time into the engine's bounded output
//     queue (the push model: the handler produces, the engine drains);
//   - writes that park on a full queue and resume on capacity —
//     backpressure without peer acknowledgement;
//   - finish with trailers, typed sticky engine failures, disconnect
//     cancellation, and exactly-once body end.
//
// The suite runs against detail::recording_sink (decisions) and the
// scripted_body_sink fake (output delivery), so no transport exists
// yet.

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/response_writer.hpp>

#include "./body_sink_fake.hpp"
#include "./littletest.hpp"

using httpserver::body_finish;
using httpserver::body_policy;
using httpserver::body_write;
using httpserver::exchange;
using httpserver::exchange_state;
using httpserver::manual_executor;
using httpserver::response_writer;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;
namespace http = httpserver::http;
namespace detail = httpserver::detail;
namespace fake = httpserver_test;

static_assert(std::is_move_constructible_v<response_writer>,
              "the writer rides exchange's defaulted move (between hops)");
static_assert(!std::is_copy_constructible_v<response_writer>,
              "one writer per streaming response; copies are a bug");

namespace {

http::request_head make_head() {
    http::request_head head;
    head.raw_target = "/things";
    head.route_path = "/things";
    head.request_method = http::method::known(http::method_id::get);
    head.request_protocol = http::protocol::http_1_1;
    return head;
}

std::vector<std::byte> bytes(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

// Runs one write to completion on `ex` (inline when the gate fails or
// the queue never fills) and returns the typed result. A write that
// parks is left parked: `seen` stays default.
body_write run_write(exchange& x, manual_executor& ex,
                     std::span<const std::byte> data) {
    body_write seen;
    int deliveries = 0;
    spawn(ex, x.writer().write(data),
          [&](task_result<body_write> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    return seen;
}

// Runs one finish(trailers) to completion on `ex` and returns the
// typed result.
body_finish run_finish(exchange& x, manual_executor& ex,
                       http::fields trailers = http::fields()) {
    body_finish seen;
    int deliveries = 0;
    spawn(ex, x.writer().finish(trailers),
          [&](task_result<body_finish> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    return seen;
}

// Drains the executor until `flag` turns non-zero or the spin budget
// (one millisecond per spin) runs out. Failure bound only, never a
// pass condition.
[[maybe_unused]] bool drain_until(manual_executor& ex, int& flag, int spins) {
    for (int i = 0; i < spins && flag == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ex.run_pending();
    }
    return flag != 0;
}

// Streaming handler: commits the head with start_response, then
// streams `payload` in `chunk_bytes` chunks and finishes with a total
// trailer. A typed write failure ends the stream early; the test body
// asserts (helpers carry no LT_CHECK context).
task<void> stream_handler(exchange& x, const std::string& payload,
                          std::size_t chunk_bytes) {
    if (!x.start_response(http::status::from_code(200),
                          http::fields()).ok()) {
        co_return;
    }
    for (std::size_t offset = 0; offset < payload.size();
         offset += chunk_bytes) {
        const std::vector<std::byte> chunk =
            bytes(payload.substr(offset, chunk_bytes));
        const body_write r = co_await x.writer().write(chunk);
        if (!r.status.ok()) co_return;
    }
    http::fields trailers;
    trailers.append("X-Total", std::to_string(payload.size()));
    static_cast<void>(co_await x.writer().finish(trailers));
}

}  // namespace

LT_BEGIN_SUITE(response_writer_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(response_writer_suite)

LT_BEGIN_AUTO_TEST(response_writer_suite, write_accepts_chunk_into_queue)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());
    LT_CHECK_EQ(sink.respond_calls, 1);

    const body_write seen = run_write(x, ex, bytes("hello"));

    LT_CHECK(seen.status.ok());
    LT_CHECK_EQ(seen.accepted, static_cast<std::size_t>(5));
    LT_CHECK_EQ(out.produced(), static_cast<std::size_t>(5));
    LT_CHECK_EQ(out.queued(), static_cast<std::size_t>(5));
    LT_CHECK_EQ(out.park_count(), 0);
    // The head was committed exactly once by start_response.
    LT_CHECK_EQ(sink.respond_calls, 1);
LT_END_AUTO_TEST(write_accepts_chunk_into_queue)

LT_BEGIN_AUTO_TEST(response_writer_suite, write_requires_started_response)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    const body_write fresh = run_write(x, ex, bytes("hello"));
    LT_CHECK(fresh.status.code() == http::outcome_code::invalid_state);
    LT_CHECK(!fresh.status.message().empty());

    // A one-shot respond() commits the head but leaves the writer
    // inactive: streaming is a separate decision.
    LT_CHECK(x.respond(http::status::from_code(200), http::fields()).ok());
    const body_write after = run_write(x, ex, bytes("hello"));
    LT_CHECK(after.status.code() == http::outcome_code::invalid_state);
    const body_finish finish_gate = run_finish(x, ex);
    LT_CHECK(finish_gate.status.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(out.produced(), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(write_requires_started_response)

LT_BEGIN_AUTO_TEST(response_writer_suite, second_outstanding_write_fails)
    detail::recording_sink sink;
    fake::scripted_body_sink out(8);  // 8-byte bounded output queue
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());

    // 16 bytes into an 8-byte queue: the write parks mid-chunk.
    const std::vector<std::byte> chunk = bytes("0123456789abcdef");
    body_write parked_result;
    int parked_done = 0;
    spawn(ex, x.writer().write(chunk),
          [&](task_result<body_write> r) {
              ++parked_done;
              if (r.has_value()) parked_result = r.value();
          });
    ex.run_pending();
    LT_CHECK_EQ(out.queued(), static_cast<std::size_t>(8));
    LT_CHECK(out.parked());
    LT_CHECK_EQ(parked_done, 0);

    // A second write while one is outstanding fails immediately,
    // typed, without touching the sink (still exactly one park).
    const body_write second = run_write(x, ex, bytes("zz"));
    LT_CHECK(second.status.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(out.park_count(), 1);

    // finish-while-write hits the same one-outstanding gate.
    const body_finish finish_gate = run_finish(x, ex);
    LT_CHECK(finish_gate.status.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(out.end_calls(), 0);

    // Capacity frees; the parked write resumes and finishes the chunk.
    LT_CHECK_EQ(out.drain(16), static_cast<std::size_t>(8));
    ex.run_pending();
    LT_CHECK_EQ(parked_done, 1);
    LT_CHECK(parked_result.status.ok());
    LT_CHECK_EQ(parked_result.accepted, chunk.size());
    LT_CHECK(out.max_queued() <= static_cast<std::size_t>(8));
LT_END_AUTO_TEST(second_outstanding_write_fails)

LT_BEGIN_AUTO_TEST(response_writer_suite, write_empty_chunk_fails)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());

    const body_write seen = run_write(x, ex, std::span<const std::byte>());
    LT_CHECK(seen.status.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!seen.status.message().empty());
    // No sink interaction: the gate fails before any push or park.
    LT_CHECK_EQ(out.produced(), static_cast<std::size_t>(0));
    LT_CHECK_EQ(out.park_count(), 0);
LT_END_AUTO_TEST(write_empty_chunk_fails)

LT_BEGIN_AUTO_TEST(response_writer_suite, write_parks_when_full_and_resumes_on_capacity)
    detail::recording_sink sink;
    fake::scripted_body_sink out(16);  // 16-byte bounded output queue
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());

    // One 64-byte write: accepted only as the queue drains.
    const std::vector<std::byte> chunk = bytes(
        "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOP");
    body_write seen;
    int parked_done = 0;
    spawn(ex, x.writer().write(chunk),
          [&](task_result<body_write> r) {
              ++parked_done;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    LT_CHECK(out.parked());
    LT_CHECK_EQ(out.queued(), static_cast<std::size_t>(16));
    LT_CHECK_EQ(parked_done, 0);

    // Engine progress: each drain wakes the outstanding write, which
    // refills the bounded queue; the loop drives it to completion.
    for (int i = 0; i < 1000 && parked_done == 0; ++i) {
        out.drain(64);
        ex.run_pending();
    }

    // Exactly one delivery of the one outstanding write, whole chunk.
    LT_CHECK_EQ(parked_done, 1);
    LT_CHECK(seen.status.ok());
    LT_CHECK_EQ(seen.accepted, chunk.size());
    LT_CHECK_EQ(out.produced(), chunk.size());
    LT_CHECK(!out.parked());
    LT_CHECK(out.park_count() >= 1);
    // The engine finishes draining what the write queued.
    out.drain(64);
    LT_CHECK_EQ(out.drained(), chunk.size());
    LT_CHECK(out.max_queued() <= static_cast<std::size_t>(16));
LT_END_AUTO_TEST(write_parks_when_full_and_resumes_on_capacity)

LT_BEGIN_AUTO_TEST(response_writer_suite, finish_accepts_trailers_and_ends)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());
    const body_write chunk = run_write(x, ex, bytes("abc"));
    LT_CHECK(chunk.status.ok());

    http::fields trailers;
    trailers.append("X-Checksum", "deadbeef");
    const body_finish seen = run_finish(x, ex, trailers);

    LT_CHECK(seen.status.ok());
    LT_CHECK(out.ended());
    LT_CHECK_EQ(out.end_calls(), 1);
    const std::optional<std::string_view> checksum =
        out.trailers().first("x-checksum");
    LT_CHECK(checksum.has_value());
    LT_CHECK(*checksum == "deadbeef");

    // After finish the writer is closed: further operations fail typed
    // and the sink never sees a second end marker.
    const body_write over = run_write(x, ex, bytes("more"));
    LT_CHECK(over.status.code() == http::outcome_code::invalid_state);
    const body_finish again = run_finish(x, ex);
    LT_CHECK(again.status.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(out.end_calls(), 1);
LT_END_AUTO_TEST(finish_accepts_trailers_and_ends)

LT_BEGIN_AUTO_TEST(response_writer_suite, finish_parks_until_queue_has_room)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());
    // Fill the bounded queue exactly.
    const body_write chunk = run_write(x, ex, bytes(std::string(64, 'x')));
    LT_CHECK(chunk.status.ok());
    LT_CHECK_EQ(out.queued(), static_cast<std::size_t>(64));

    // The end marker needs one queue slot: the finish parks.
    http::fields trailers;
    trailers.append("X-Total", "64");
    body_finish seen;
    int parked_done = 0;
    spawn(ex, x.writer().finish(trailers),
          [&](task_result<body_finish> r) {
              ++parked_done;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    LT_CHECK(out.parked());
    LT_CHECK_EQ(parked_done, 0);
    LT_CHECK_EQ(out.end_calls(), 0);

    // Room frees; the finish re-pushes and ends the body exactly once.
    LT_CHECK_EQ(out.drain(16), static_cast<std::size_t>(16));
    ex.run_pending();
    LT_CHECK_EQ(parked_done, 1);
    LT_CHECK(seen.status.ok());
    LT_CHECK(out.ended());
    LT_CHECK_EQ(out.end_calls(), 1);
LT_END_AUTO_TEST(finish_parks_until_queue_has_room)

LT_BEGIN_AUTO_TEST(response_writer_suite, stream_larger_than_queue_progresses)
    // Acceptance: a 256-byte stream through a 64-byte bounded output
    // queue progresses chunk by chunk; writes park on the full queue
    // and resume on capacity, and finish ends the body exactly once.
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);  // 64-byte bounded output queue
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    const std::string pattern = "abcdefghijklmnopqrstuvwxyz012345";
    std::string payload;
    for (int i = 0; i < 8; ++i) payload += pattern;  // 256 bytes

    int done = 0;
    spawn(ex, stream_handler(x, payload, 32),
          [&](task_result<void>) { ++done; });
    ex.run_pending();  // the handler commits the head and fills the queue
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK(out.parked());

    // Engine progress: drain the bounded queue, let the handler write.
    for (int i = 0; i < 1000 && done == 0; ++i) {
        out.drain(64);
        ex.run_pending();
    }

    LT_CHECK_EQ(done, 1);
    LT_CHECK_EQ(out.produced(), payload.size());
    // The engine finishes draining what the stream queued.
    out.drain(64);
    LT_CHECK_EQ(out.drained(), payload.size());
    LT_CHECK(out.max_queued() <= static_cast<std::size_t>(64));
    LT_CHECK(out.ended());
    LT_CHECK_EQ(out.end_calls(), 1);
    const std::optional<std::string_view> total =
        out.trailers().first("x-total");
    LT_CHECK(total.has_value());
    LT_CHECK(*total == "256");
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(stream_larger_than_queue_progresses)

LT_BEGIN_AUTO_TEST(response_writer_suite, engine_failure_fails_write_sticky)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());
    out.stage_failure(http::outcome_code::protocol_error, "bad drain");

    const body_write first = run_write(x, ex, bytes("hello"));
    LT_CHECK(first.status.code() == http::outcome_code::protocol_error);
    LT_CHECK(first.status.message() == "bad drain");
    LT_CHECK_EQ(first.accepted, static_cast<std::size_t>(0));
    LT_CHECK_EQ(out.push_calls(), 1);

    // Sticky: the second write returns the same failure without a new
    // sink push (produced frozen at zero, the sink untouched).
    const body_write second = run_write(x, ex, bytes("more"));
    LT_CHECK(second.status.code() == http::outcome_code::protocol_error);
    LT_CHECK(second.status.message() == "bad drain");
    LT_CHECK_EQ(out.push_calls(), 1);
    LT_CHECK_EQ(out.produced(), static_cast<std::size_t>(0));
    LT_CHECK_EQ(out.park_count(), 0);
LT_END_AUTO_TEST(engine_failure_fails_write_sticky)

LT_BEGIN_AUTO_TEST(response_writer_suite, finish_reports_engine_failure)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());
    const body_write chunk = run_write(x, ex, bytes("abc"));
    LT_CHECK(chunk.status.ok());
    out.stage_failure(http::outcome_code::timeout, "timed out mid-body");

    const body_finish first = run_finish(x, ex);
    LT_CHECK(first.status.code() == http::outcome_code::timeout);
    LT_CHECK(first.status.message() == "timed out mid-body");
    // A failed finish never ends the body.
    LT_CHECK(!out.ended());
    LT_CHECK_EQ(out.end_calls(), 0);

    // Sticky: a later finish reports the same failure, still no end,
    // and the sink never sees another end attempt.
    const body_finish again = run_finish(x, ex);
    LT_CHECK(again.status.code() == http::outcome_code::timeout);
    LT_CHECK_EQ(out.end_calls(), 0);
    LT_CHECK_EQ(out.push_calls(), 1);  // only the pre-failure chunk
LT_END_AUTO_TEST(finish_reports_engine_failure)

LT_BEGIN_AUTO_TEST(response_writer_suite, write_after_disconnect_fails_closed)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());
    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "peer went away").ok());

    const body_write seen = run_write(x, ex, bytes("hello"));
    LT_CHECK(seen.status.code() == http::outcome_code::connection_closed);
    LT_CHECK(seen.status.message().find("peer went away")
             != std::string::npos);
    // Nothing touched the engine after the disconnect.
    LT_CHECK_EQ(out.produced(), static_cast<std::size_t>(0));
    LT_CHECK_EQ(out.park_count(), 0);
LT_END_AUTO_TEST(write_after_disconnect_fails_closed)

LT_BEGIN_AUTO_TEST(response_writer_suite, start_response_gates)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    // First streaming decision commits the head.
    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());
    LT_CHECK_EQ(sink.respond_calls, 1);

    // A second terminal decision fails typed; the engine still saw
    // exactly one respond.
    const http::outcome again = x.start_response(
        http::status::from_code(500), http::fields());
    LT_CHECK(again.code() == http::outcome_code::invalid_state);
    LT_CHECK(!again.message().empty());
    LT_CHECK_EQ(sink.respond_calls, 1);

    // The one-shot respond() is terminal the same way.
    const http::outcome respond_gate = x.respond(
        http::status::from_code(500), http::fields());
    LT_CHECK(respond_gate.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(sink.respond_calls, 1);

    // After a disconnect the decision fails closed with the detail.
    LT_CHECK(x.disconnect(http::outcome_code::timeout,
                          "peer vanished").ok());
    const http::outcome closed = x.start_response(
        http::status::from_code(200), http::fields());
    LT_CHECK(closed.code() == http::outcome_code::connection_closed);
    LT_CHECK(closed.message().find("peer vanished") != std::string::npos);
    LT_CHECK_EQ(sink.respond_calls, 1);

    // From admitted the streaming decision is legal.
    detail::recording_sink sink2;
    fake::scripted_body_sink out2(64);
    exchange admitted(make_head(), &sink2, 0, nullptr, &out2);
    LT_CHECK(admitted.admit_body(body_policy()).ok());
    LT_CHECK_EQ(sink2.admit_calls, 1);
    LT_CHECK(admitted.start_response(http::status::from_code(200),
                                     http::fields()).ok());
    LT_CHECK_EQ(sink2.respond_calls, 1);
    LT_CHECK(admitted.state() == exchange_state::responded);
LT_END_AUTO_TEST(start_response_gates)

LT_BEGIN_AUTO_TEST(response_writer_suite, disconnect_wakes_parked_write)
    detail::recording_sink sink;
    fake::scripted_body_sink out(8);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());

    const std::vector<std::byte> chunk = bytes("0123456789abcdef");
    body_write seen;
    int deliveries = 0;
    spawn(ex, fake::watch_stop_and_resume(x, out),
          [&](task_result<void>) { });
    spawn(ex, x.writer().write(chunk),
          [&](task_result<body_write> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();  // the watcher awaits the stop; the write parks
    LT_CHECK(out.parked());
    LT_CHECK_EQ(deliveries, 0);

    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "peer went away").ok());
    // The streaming head is a terminal decision: the disconnect moves
    // a non-terminal state to cancelled, but responded stays responded.
    LT_CHECK(x.state() == exchange_state::responded);
    LT_CHECK(x.disconnected());
    LT_CHECK(drain_until(ex, deliveries, 400));

    // Exactly one delivery, typed cancelled, carrying the partial
    // prefix queued before the wake.
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.status.code() == http::outcome_code::cancelled);
    LT_CHECK_EQ(seen.accepted, static_cast<std::size_t>(8));
    LT_CHECK_EQ(out.produced(), static_cast<std::size_t>(8));
    LT_CHECK(!out.parked());
LT_END_AUTO_TEST(disconnect_wakes_parked_write)

LT_BEGIN_AUTO_TEST(response_writer_suite, disconnect_wakes_write_from_other_thread)
    detail::recording_sink sink;
    fake::scripted_body_sink out(8);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());

    const std::vector<std::byte> chunk = bytes("0123456789abcdef");
    body_write seen;
    int deliveries = 0;
    spawn(ex, fake::watch_stop_and_resume(x, out),
          [&](task_result<void>) { });
    spawn(ex, x.writer().write(chunk),
          [&](task_result<body_write> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();  // the watcher awaits the stop; the write parks
    LT_CHECK(out.parked());

    // Disconnect is engine-facing and may run on any thread while the
    // handler is suspended.
    std::thread waker([&x] {
        static_cast<void>(x.disconnect(http::outcome_code::connection_closed,
                                       "peer went away"));
    });
    waker.join();

    LT_CHECK(drain_until(ex, deliveries, 400));
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.status.code() == http::outcome_code::cancelled);
    LT_CHECK_EQ(seen.accepted, static_cast<std::size_t>(8));
LT_END_AUTO_TEST(disconnect_wakes_write_from_other_thread)

LT_BEGIN_AUTO_TEST(response_writer_suite, finish_cancelled_by_disconnect)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());
    const body_write chunk = run_write(x, ex, bytes(std::string(64, 'x')));
    LT_CHECK(chunk.status.ok());

    // The end marker has no queue slot: the finish parks.
    body_finish seen;
    int deliveries = 0;
    spawn(ex, fake::watch_stop_and_resume(x, out),
          [&](task_result<void>) { });
    spawn(ex, x.writer().finish(http::fields()),
          [&](task_result<body_finish> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    LT_CHECK(out.parked());
    LT_CHECK_EQ(deliveries, 0);
    LT_CHECK_EQ(out.end_calls(), 0);

    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "peer went away").ok());
    LT_CHECK(drain_until(ex, deliveries, 400));

    // Exactly one delivery, typed cancelled — and the body was never
    // half-ended: no end marker reached the sink.
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.status.code() == http::outcome_code::cancelled);
    LT_CHECK_EQ(out.end_calls(), 0);
    LT_CHECK(!out.ended());
LT_END_AUTO_TEST(finish_cancelled_by_disconnect)

LT_BEGIN_AUTO_TEST(response_writer_suite, finish_is_exactly_once)
    detail::recording_sink sink;
    fake::scripted_body_sink out(64);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              http::fields()).ok());
    const body_write chunk = run_write(x, ex, bytes("abc"));
    LT_CHECK(chunk.status.ok());

    const body_finish first = run_finish(x, ex);
    LT_CHECK(first.status.ok());
    LT_CHECK(out.ended());
    LT_CHECK_EQ(out.end_calls(), 1);

    // A second finish fails typed; still exactly one end marker.
    const body_finish second = run_finish(x, ex);
    LT_CHECK(second.status.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(out.end_calls(), 1);

    // abort() closes the writer (idempotent); no further write passes.
    LT_CHECK(x.abort().ok());
    LT_CHECK(x.abort().ok());
    const body_write after_abort = run_write(x, ex, bytes("more"));
    LT_CHECK(after_abort.status.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(out.end_calls(), 1);

    // A disconnect after finish neither re-ends the body nor reopens
    // the writer.
    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "late peer").ok());
    LT_CHECK_EQ(out.end_calls(), 1);
    LT_CHECK(x.abort().ok());
    LT_CHECK_EQ(out.end_calls(), 1);
LT_END_AUTO_TEST(finish_is_exactly_once)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
