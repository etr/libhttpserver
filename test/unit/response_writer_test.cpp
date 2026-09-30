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
#include <span>
#include <string>
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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
