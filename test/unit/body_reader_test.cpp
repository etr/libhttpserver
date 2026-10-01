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

// TASK-103: the bounded body reader over the exchange (PRD-V3N-REQ-021/
// 022/025). Pins the one body pipeline every admitted request flows
// through:
//   - one bounded incremental read at a time, credited only when bytes
//     are consumed (engine pull == consumption event);
//   - EOF with final trailer access and bounded multi-read progression
//     of an echo larger than the engine's staging queue;
//   - collect(max) with an exact over-limit outcome;
//   - reads that wake on disconnect and typed failures afterwards.
//
// The suite runs against detail::recording_sink (decisions) and the
// scripted_body_source fake (delivery), so no transport exists yet.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/exchange.hpp>

#include "./body_source_fake.hpp"
#include "./littletest.hpp"

using httpserver::body_collect;
using httpserver::body_policy;
using httpserver::body_read;
using httpserver::body_reader;
using httpserver::exchange;
using httpserver::exchange_state;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;
namespace http = httpserver::http;
namespace detail = httpserver::detail;
namespace fake = httpserver_test;

static_assert(std::is_move_constructible_v<body_reader>,
              "the reader rides exchange's defaulted move (between hops)");
static_assert(!std::is_copy_constructible_v<body_reader>,
              "one reader per admitted body; copies are a bug");

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

std::string of(std::span<const std::byte> data) {
    std::string out;
    out.reserve(data.size());
    for (std::byte b : data) {
        out.push_back(static_cast<char>(b));
    }
    return out;
}

// Runs one read_some to completion on `ex` (inline when the gate fails
// or a pull resolves without parking) and returns the typed result. A
// read that parks is left parked: `seen` stays valueless.
body_read run_read(exchange& x, manual_executor& ex,
                   std::span<std::byte> destination) {
    body_read seen;
    int deliveries = 0;
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    return seen;
}

// Runs one collect(maximum) to completion on `ex` and returns the
// typed result.
body_collect run_collect(exchange& x, manual_executor& ex,
                         std::uint64_t maximum) {
    body_collect seen;
    int deliveries = 0;
    spawn(ex, x.body().collect(maximum),
          [&](task_result<body_collect> r) {
              ++deliveries;
              if (r.has_value()) seen = std::move(r.value());
          });
    ex.run_pending();
    return seen;
}

// Drains the executor until `flag` turns non-zero or the spin budget
// (one millisecond per spin) runs out. Failure bound only, never a
// pass condition.
bool drain_until(manual_executor& ex, int& flag, int spins) {
    for (int i = 0; i < spins && flag == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ex.run_pending();
    }
    return flag != 0;
}

// Echo-loop handler: admits, reads through the bounded staging queue
// until the body ends, responds 200. Verdicts land in the out-
// parameters; the test body asserts (helpers carry no LT_CHECK
// context).
task<void> echo_handler(exchange& x, std::vector<std::byte>& echo,
                        int& reads) {
    if (!x.admit_body(body_policy()).ok()) co_return;
    std::vector<std::byte> buffer(48);
    for (;;) {
        const body_read r = co_await x.body().read_some(buffer);
        ++reads;
        if (!r.status.ok() || r.end_of_body) break;
        echo.insert(echo.end(), r.data.begin(), r.data.end());
    }
    static_cast<void>(x.respond(http::status::from_code(200),
                                http::fields()));
}

}  // namespace

LT_BEGIN_SUITE(body_reader_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(body_reader_suite)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_read_returns_staged_segment)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());
    LT_CHECK(source.stage(bytes("hello")));

    std::vector<std::byte> destination(16);
    const body_read seen = run_read(x, ex, destination);

    LT_CHECK(seen.status.ok());
    LT_CHECK(!seen.end_of_body);
    LT_CHECK(of(seen.data) == "hello");
    LT_CHECK_EQ(source.credit_released(), static_cast<std::size_t>(5));
    LT_CHECK_EQ(source.park_count(), 0);
LT_END_AUTO_TEST(body_read_returns_staged_segment)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_no_credit_before_consumption)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());
    LT_CHECK(source.stage(bytes("1234567")));

    // Staged bytes hold receive credit; only the engine pull (the
    // consumption event inside read_some) releases it.
    LT_CHECK_EQ(source.credit_released(), static_cast<std::size_t>(0));

    std::vector<std::byte> destination(16);
    const body_read seen = run_read(x, ex, destination);

    LT_CHECK(seen.status.ok());
    LT_CHECK(of(seen.data) == "1234567");
    LT_CHECK_EQ(source.credit_released(), static_cast<std::size_t>(7));
LT_END_AUTO_TEST(body_no_credit_before_consumption)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_read_requires_admission)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);  // no engine delivery source at all
    manual_executor ex;

    std::vector<std::byte> destination(8);
    const body_read fresh = run_read(x, ex, destination);
    LT_CHECK(fresh.status.code() == http::outcome_code::invalid_state);
    LT_CHECK(!fresh.status.message().empty());

    // Even a body-less response terminal closes the reader.
    LT_CHECK(x.respond(http::status::from_code(200), http::fields()).ok());
    const body_read after = run_read(x, ex, destination);
    LT_CHECK(after.status.code() == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(body_read_requires_admission)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_second_outstanding_read_fails)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());

    // Nothing staged: the first read parks.
    std::vector<std::byte> destination(8);
    body_read parked_result;
    int parked_done = 0;
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              ++parked_done;
              if (r.has_value()) parked_result = r.value();
          });
    ex.run_pending();
    LT_CHECK(source.parked());
    LT_CHECK_EQ(parked_done, 0);

    // A second read while one is outstanding fails immediately, typed,
    // without touching the source (still exactly one park).
    const body_read second = run_read(x, ex, destination);
    LT_CHECK(second.status.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(source.park_count(), 1);

    // Staging wakes the first read; it consumes the segment.
    LT_CHECK(source.stage(bytes("abc")));
    ex.run_pending();
    LT_CHECK_EQ(parked_done, 1);
    LT_CHECK(parked_result.status.ok());
    LT_CHECK(of(parked_result.data) == "abc");
    LT_CHECK_EQ(source.credit_released(), static_cast<std::size_t>(3));

    // Collect-while-read hits the same one-outstanding gate, typed,
    // without touching the source.
    body_read read_result;
    int read_done = 0;
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              ++read_done;
              if (r.has_value()) read_result = r.value();
          });
    ex.run_pending();
    LT_CHECK(source.parked());
    LT_CHECK(run_collect(x, ex, 64).status.code()
             == http::outcome_code::invalid_state);

    // The parked read finishes; a parked collect then excludes reads.
    LT_CHECK(source.stage(bytes("zz")));
    ex.run_pending();
    LT_CHECK_EQ(read_done, 1);
    LT_CHECK(of(read_result.data) == "zz");

    body_collect collect_result;
    int collect_done = 0;
    spawn(ex, x.body().collect(64),
          [&](task_result<body_collect> r) {
              ++collect_done;
              if (r.has_value()) collect_result = std::move(r.value());
          });
    ex.run_pending();
    LT_CHECK(source.parked());
    LT_CHECK(run_read(x, ex, destination).status.code()
             == http::outcome_code::invalid_state);

    // Drain: end wakes the parked collect into a typed success.
    source.stage_end();
    ex.run_pending();
    LT_CHECK_EQ(collect_done, 1);
    LT_CHECK(collect_result.status.ok());
    LT_CHECK(collect_result.data.empty());
LT_END_AUTO_TEST(body_second_outstanding_read_fails)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_read_into_empty_buffer_fails)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());

    const body_read seen = run_read(x, ex, std::span<std::byte>());
    LT_CHECK(seen.status.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!seen.status.message().empty());
    // No source interaction: the gate fails before any pull or park.
    LT_CHECK_EQ(source.park_count(), 0);
    LT_CHECK_EQ(source.credit_released(), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(body_read_into_empty_buffer_fails)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_eof_read_returns_end_and_trailers)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());
    LT_CHECK(source.stage(bytes("abc")));
    http::fields trailers;
    trailers.append("X-Checksum", "deadbeef");
    source.stage_end(trailers);

    std::vector<std::byte> destination(8);
    const body_read first = run_read(x, ex, destination);
    LT_CHECK(first.status.ok());
    LT_CHECK(!first.end_of_body);
    LT_CHECK(of(first.data) == "abc");

    // Terminal read: authoritative end, trailers final.
    const body_read last = run_read(x, ex, destination);
    LT_CHECK(last.status.ok());
    LT_CHECK(last.end_of_body);
    LT_CHECK(last.data.empty());
    LT_CHECK_EQ(source.credit_released(), static_cast<std::size_t>(3));

    const std::optional<std::string_view> checksum =
        x.body().trailers().first("x-checksum");
    LT_CHECK(checksum.has_value());
    LT_CHECK(*checksum == "deadbeef");

    // At end the reader is finished: further reads fail typed.
    const body_read over = run_read(x, ex, destination);
    LT_CHECK(over.status.code() == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(body_eof_read_returns_end_and_trailers)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_echo_larger_than_queue_progresses)
    // Acceptance: an echo larger than the engine's bounded staging
    // queue progresses through reads, releasing receive credit only as
    // bytes are consumed.
    detail::recording_sink sink;
    fake::scripted_body_source source(64);  // 64-byte bounded queue
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    const std::string pattern = "abcdefghijklmnopqrstuvwxyz012345";
    std::string payload;
    for (int i = 0; i < 8; ++i) payload += pattern;  // 256 bytes

    std::vector<std::byte> echo;
    int reads = 0;
    int done = 0;
    spawn(ex, echo_handler(x, echo, reads),
          [&](task_result<void>) { ++done; });
    ex.run_pending();  // the handler admits and issues the first read
    LT_CHECK(source.parked());

    // Producer: stages 32-byte segments only when they fit the queue,
    // interleaved with executor progress.
    for (std::size_t offset = 0; offset < payload.size(); offset += 32) {
        const std::vector<std::byte> segment = bytes(payload.substr(offset, 32));
        while (!source.stage(segment)) ex.run_pending();
        ex.run_pending();
    }
    source.stage_end();
    for (int i = 0; i < 1000 && done == 0; ++i) ex.run_pending();

    LT_CHECK_EQ(done, 1);
    LT_CHECK(of(echo) == payload);
    LT_CHECK(reads >= 6);
    LT_CHECK_EQ(source.credit_released(), payload.size());
    LT_CHECK(source.max_queued() <= static_cast<std::size_t>(64));
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(200));
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(body_echo_larger_than_queue_progresses)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_empty_body_reads_end_immediately)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());
    source.stage_end();  // no data, no trailers

    std::vector<std::byte> destination(16);
    const body_read seen = run_read(x, ex, destination);
    LT_CHECK(seen.status.ok());
    LT_CHECK(seen.end_of_body);
    LT_CHECK(seen.data.empty());
    LT_CHECK_EQ(x.body().trailers().size(), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(body_empty_body_reads_end_immediately)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_collect_within_cap_returns_whole)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());
    LT_CHECK(source.stage(bytes("abc")));
    LT_CHECK(source.stage(bytes("def")));
    http::fields trailers;
    trailers.append("X-Total", "6");
    source.stage_end(trailers);

    const body_collect seen = run_collect(x, ex, 64);
    LT_CHECK(seen.status.ok());
    LT_CHECK(of(seen.data) == "abcdef");
    // A successful collect leaves the reader at end of body, with the
    // trailers final.
    const std::optional<std::string_view> total =
        x.body().trailers().first("x-total");
    LT_CHECK(total.has_value());
    LT_CHECK(*total == "6");
LT_END_AUTO_TEST(body_collect_within_cap_returns_whole)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_collect_fails_exactly_at_cap)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange over(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(over.admit_body(body_policy()).ok());
    LT_CHECK(source.stage(bytes("0123456789")));  // 10 bytes staged

    // One byte under: exact over-limit outcome, diagnostic names the cap.
    const body_collect refused = run_collect(over, ex, 9);
    LT_CHECK(refused.status.code() == http::outcome_code::limit_exceeded);
    LT_CHECK(refused.status.message().find("9") != std::string::npos);
    LT_CHECK(refused.data.empty());

    // Exactly at cap succeeds (fresh exchange: a refused collect left
    // the previous reader sticky).
    fake::scripted_body_source source2;
    exchange at_cap(make_head(), &sink, 0, &source2);
    LT_CHECK(at_cap.admit_body(body_policy()).ok());
    LT_CHECK(source2.stage(bytes("0123456789")));
    source2.stage_end();
    const body_collect seen = run_collect(at_cap, ex, 10);
    LT_CHECK(seen.status.ok());
    LT_CHECK(of(seen.data) == "0123456789");
LT_END_AUTO_TEST(body_collect_fails_exactly_at_cap)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_collect_zero_cap)
    detail::recording_sink sink;
    manual_executor ex;

    // Zero cap over an immediately-ending body: ok, empty.
    fake::scripted_body_source empty_source;
    exchange empty_body(make_head(), &sink, 0, &empty_source);
    LT_CHECK(empty_body.admit_body(body_policy()).ok());
    empty_source.stage_end();
    const body_collect nothing = run_collect(empty_body, ex, 0);
    LT_CHECK(nothing.status.ok());
    LT_CHECK(nothing.data.empty());

    // Zero cap with any data staged: limit_exceeded.
    fake::scripted_body_source data_source;
    exchange data_body(make_head(), &sink, 0, &data_source);
    LT_CHECK(data_body.admit_body(body_policy()).ok());
    LT_CHECK(data_source.stage(bytes("x")));
    const body_collect refused = run_collect(data_body, ex, 0);
    LT_CHECK(refused.status.code() == http::outcome_code::limit_exceeded);
LT_END_AUTO_TEST(body_collect_zero_cap)

// The engine's bodyless-request construction: the head carries no body
// framing, so serve_one hands the exchange NO delivery source. The
// admission still accepts, and the admitted body is empty and complete
// -- a read ends the body, a collect returns zero bytes, and a second
// read fails at-end like any finished body (TASK-111's sync adapter
// serves bodyless requests through exactly this shape).
LT_BEGIN_AUTO_TEST(body_reader_suite, body_admitted_without_source_is_empty)
    detail::recording_sink sink;
    manual_executor ex;
    std::vector<std::byte> destination(8);

    // Before admission the gate still refuses (body not admitted).
    exchange fresh(make_head(), &sink, 0, nullptr);
    const body_read before = run_read(fresh, ex, destination);
    LT_CHECK(before.status.code() == http::outcome_code::invalid_state);

    // Incremental shape: the first read ends the (empty) body; a second
    // read fails at-end like any finished body.
    exchange reading(make_head(), &sink, 0, nullptr);
    LT_CHECK(reading.admit_body(body_policy()).ok());
    const body_read ended = run_read(reading, ex, destination);
    LT_CHECK(ended.status.ok());
    LT_CHECK(ended.end_of_body);
    LT_CHECK(ended.data.empty());
    const body_read again = run_read(reading, ex, destination);
    LT_CHECK(again.status.code() == http::outcome_code::invalid_state);

    // Buffered shape: a collect returns the zero-byte body successfully
    // and the trailers are final (empty).
    exchange collecting(make_head(), &sink, 0, nullptr);
    LT_CHECK(collecting.admit_body(body_policy()).ok());
    const body_collect whole = run_collect(collecting, ex, 16);
    LT_CHECK(whole.status.ok());
    LT_CHECK(whole.data.empty());
    LT_CHECK(collecting.body().trailers().size()
             == static_cast<std::size_t>(0));
LT_END_AUTO_TEST(body_admitted_without_source_is_empty)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_collect_over_limit_is_sticky)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());
    LT_CHECK(source.stage(bytes("0123456789")));

    LT_CHECK(run_collect(x, ex, 9).status.code()
             == http::outcome_code::limit_exceeded);
    const std::size_t credit_after_trip = source.credit_released();

    // Sticky: later reads return the same failure without touching the
    // source (credit frozen — the unconsumed remainder gets none).
    std::vector<std::byte> destination(16);
    const body_read after = run_read(x, ex, destination);
    LT_CHECK(after.status.code() == http::outcome_code::limit_exceeded);
    LT_CHECK_EQ(source.credit_released(), credit_after_trip);
LT_END_AUTO_TEST(body_collect_over_limit_is_sticky)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_collect_on_disconnected_fails_closed)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());
    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "peer went away").ok());

    const body_collect seen = run_collect(x, ex, 64);
    LT_CHECK(seen.status.code() == http::outcome_code::connection_closed);
    LT_CHECK(seen.status.message().find("peer went away")
             != std::string::npos);
LT_END_AUTO_TEST(body_collect_on_disconnected_fails_closed)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_disconnect_wakes_parked_read)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());

    std::vector<std::byte> destination(8);
    body_read seen;
    int deliveries = 0;
    spawn(ex, fake::watch_stop_and_cancel(x, source),
          [&](task_result<void>) { });
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();  // the watcher awaits the stop; the read parks
    LT_CHECK(source.parked());
    LT_CHECK_EQ(deliveries, 0);

    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "peer went away").ok());
    LT_CHECK(x.state() == exchange_state::cancelled);
    LT_CHECK(drain_until(ex, deliveries, 400));

    // Exactly one delivery, typed cancelled.
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.status.code() == http::outcome_code::cancelled);
LT_END_AUTO_TEST(body_disconnect_wakes_parked_read)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_disconnect_wakes_read_from_other_thread)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());

    std::vector<std::byte> destination(8);
    body_read seen;
    int deliveries = 0;
    spawn(ex, fake::watch_stop_and_cancel(x, source),
          [&](task_result<void>) { });
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();  // the watcher awaits the stop; the read parks
    LT_CHECK(source.parked());

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
LT_END_AUTO_TEST(body_disconnect_wakes_read_from_other_thread)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_read_after_disconnect_fails_closed)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());
    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "peer went away").ok());

    std::vector<std::byte> destination(8);
    const body_read seen = run_read(x, ex, destination);
    LT_CHECK(seen.status.code() == http::outcome_code::connection_closed);
    LT_CHECK(seen.status.message().find("peer went away")
             != std::string::npos);
LT_END_AUTO_TEST(body_read_after_disconnect_fails_closed)

LT_BEGIN_AUTO_TEST(body_reader_suite, body_engine_failure_fails_read_sticky)
    detail::recording_sink sink;
    fake::scripted_body_source source;
    exchange x(make_head(), &sink, 0, &source);
    manual_executor ex;

    LT_CHECK(x.admit_body(body_policy()).ok());
    source.stage_failure(http::outcome_code::protocol_error, "bad chunk");

    std::vector<std::byte> destination(8);
    const body_read first = run_read(x, ex, destination);
    LT_CHECK(first.status.code() == http::outcome_code::protocol_error);
    LT_CHECK(first.status.message() == "bad chunk");

    // Sticky: the second read returns the same failure without a new
    // source pull (credit frozen at zero).
    const body_read second = run_read(x, ex, destination);
    LT_CHECK(second.status.code() == http::outcome_code::protocol_error);
    LT_CHECK_EQ(source.credit_released(), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(body_engine_failure_fails_read_sticky)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
