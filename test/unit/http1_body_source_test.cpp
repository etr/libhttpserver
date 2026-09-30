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

// TASK-106: detail::http1_body_source unit tests — the body_source
// adapter that turns engine-side feed() calls into the pull/park seam
// the exchange's body_reader consumes.
//
// Pins the threading contract: feed() runs the decoder under the
// adapter lock and completes a parked waiter exactly once (CAS + posted
// resumption, never waiter code under the lock); park() mirrors the
// immediate-completion table (already failed, already staged, already
// complete, stop requested — else register, at most one waiter);
// unpark() forgets a destroyed waiter; failures are typed and sticky,
// and a rejected body mode surfaces its close policy.

#include <httpserver/detail/http1_body_source.hpp>

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/http1_body_mode.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/exchange.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;
namespace detail = httpserver::detail;

using httpserver::body_collect;
using httpserver::body_read;
using httpserver::exchange;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::stop_source;
using httpserver::task_result;
using detail::http1_body_budget;
using detail::http1_body_kind;
using detail::http1_body_mode;
using detail::http1_body_source;
using detail::http1_close_policy;

http1_body_budget default_budget() {
    return http1_body_budget();
}

http1_body_mode length_mode(std::uint64_t n) {
    http1_body_mode m;
    m.kind = http1_body_kind::length;
    m.content_length = n;
    return m;
}

http1_body_mode chunked_mode() {
    http1_body_mode m;
    m.kind = http1_body_kind::chunked;
    return m;
}

http1_body_mode rejected_mode() {
    http1_body_mode m;
    m.kind = http1_body_kind::rejected;
    m.failure = http::outcome(http::outcome_code::protocol_error,
                              "http1_body_mode: ambiguous framing");
    m.close_policy = http1_close_policy::close_now;
    return m;
}

http::request_head make_head() {
    http::request_head head;
    head.raw_target = "/things";
    head.route_path = "/things";
    head.request_method = http::method::known(http::method_id::post);
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

// Runs one read_some to completion (inline when a pull resolves without
// parking) and returns the typed result.
body_read run_read(exchange& x, manual_executor& ex,
                   std::span<std::byte> destination) {
    body_read seen;
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    return seen;
}

}  // namespace

LT_BEGIN_SUITE(failure_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(failure_suite)

LT_BEGIN_AUTO_TEST(failure_suite, mid_body_feed_failure_wakes_parked_read)
    detail::recording_sink sink;
    detail::http1_body_source adapter(chunked_mode(), default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    // First read: nothing staged, parks; the feed from the engine side
    // wakes it with the staged segment.
    std::vector<std::byte> destination(16);
    body_read first;
    int first_done = 0;
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              ++first_done;
              if (r.has_value()) first = r.value();
          });
    ex.run_pending();
    LT_CHECK_EQ(first_done, 0);

    // Hoisted: the CHECK_EQ family re-evaluates its argument for the
    // failure diagnostic, and feed() is state-changing.
    const std::size_t fed = adapter.feed("5\r\nhello\r\n");
    LT_CHECK_EQ(fed, 10u);
    ex.run_pending();
    LT_CHECK_EQ(first_done, 1);
    LT_CHECK(first.status.ok());
    LT_CHECK(of(first.data) == "hello");

    // Second read parks; a malformed chunk-size byte fails it typed.
    body_read second;
    int second_done = 0;
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              ++second_done;
              if (r.has_value()) second = r.value();
          });
    ex.run_pending();
    LT_CHECK_EQ(second_done, 0);

    // The rejection fires at line completion, so the whole bad
    // line's worth of bytes is consumed.
    const std::size_t fed_bad = adapter.feed("Z\r\n");
    LT_CHECK_EQ(fed_bad, 3u);
    ex.run_pending();
    LT_CHECK_EQ(second_done, 1);
    LT_CHECK(second.status.code() == http::outcome_code::protocol_error);
    LT_CHECK(!second.status.message().empty());
    LT_CHECK(adapter.failed());

    // Sticky: a later read fails identically without a new feed.
    const body_read third = run_read(x, ex, destination);
    LT_CHECK(third.status.code() == http::outcome_code::protocol_error);
LT_END_AUTO_TEST(mid_body_feed_failure_wakes_parked_read)

LT_BEGIN_AUTO_TEST(failure_suite, rejected_mode_first_read_fails_typed)
    detail::recording_sink sink;
    const http1_body_mode mode = rejected_mode();
    detail::http1_body_source adapter(mode, default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    std::vector<std::byte> destination(16);
    const body_read seen = run_read(x, ex, destination);
    LT_CHECK(seen.status.code() == http::outcome_code::protocol_error);
    LT_CHECK(seen.status.message() == mode.failure.message());
    // The close posture of the rejection is surfaced for the engine.
    LT_CHECK(adapter.close_policy() == http1_close_policy::close_now);
    LT_CHECK(adapter.failed());
    LT_CHECK(!adapter.message_complete());
LT_END_AUTO_TEST(rejected_mode_first_read_fails_typed)

LT_BEGIN_SUITE(park_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(park_suite)

LT_BEGIN_AUTO_TEST(park_suite, read_stays_parked_until_feed_wakes_it)
    detail::recording_sink sink;
    detail::http1_body_source adapter(length_mode(5), default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    std::vector<std::byte> destination(16);
    body_read seen;
    int deliveries = 0;
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    // Nothing fed: the read stays parked.
    LT_CHECK(adapter.parked());
    LT_CHECK_EQ(deliveries, 0);

    // feed() from the engine thread wakes it via the posted completion.
    adapter.feed("hel");
    ex.run_pending();
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.status.ok());
    LT_CHECK(of(seen.data) == "hel");
    LT_CHECK(!adapter.parked());

    // The remainder completes the body; the next read sees the end.
    adapter.feed("lo");
    const body_read tail = run_read(x, ex, destination);
    LT_CHECK(tail.status.ok());
    LT_CHECK(of(tail.data) == "lo");
    const body_read end = run_read(x, ex, destination);
    LT_CHECK(end.status.ok());
    LT_CHECK(end.end_of_body);
    LT_CHECK(adapter.message_complete());
LT_END_AUTO_TEST(read_stays_parked_until_feed_wakes_it)

LT_BEGIN_AUTO_TEST(park_suite, already_staged_read_never_parks)
    detail::recording_sink sink;
    detail::http1_body_source adapter(length_mode(3), default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    // Data arrives before the read: pull serves it, no park happens.
    LT_CHECK_EQ(adapter.feed("abc"), 3u);
    std::vector<std::byte> destination(8);
    const body_read seen = run_read(x, ex, destination);
    LT_CHECK(seen.status.ok());
    LT_CHECK(of(seen.data) == "abc");
    LT_CHECK(!adapter.parked());
    LT_CHECK_EQ(adapter.staged_bytes(), 0u);
LT_END_AUTO_TEST(already_staged_read_never_parks)

LT_BEGIN_AUTO_TEST(park_suite, already_complete_read_ends_inline)
    // A zero-length body is complete at construction: the read returns
    // the end without ever parking.
    detail::recording_sink sink;
    detail::http1_body_source adapter(length_mode(0), default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    std::vector<std::byte> destination(8);
    const body_read seen = run_read(x, ex, destination);
    LT_CHECK(seen.status.ok());
    LT_CHECK(seen.end_of_body);
    LT_CHECK(!adapter.parked());
LT_END_AUTO_TEST(already_complete_read_ends_inline)

LT_BEGIN_AUTO_TEST(park_suite, unpark_forgets_a_destroyed_wait)
    // A waiter destroyed while parked (the reader's frame went away)
    // detaches through unpark: the adapter forgets it and a later feed
    // still works.
    detail::http1_body_source adapter(length_mode(2), default_budget());
    stop_source never;
    {
        detail::body_wait wait(&adapter, never.get_token());
        static_cast<void>(wait.await_suspend(std::coroutine_handle<>()));
        LT_CHECK(adapter.parked());
    }
    LT_CHECK(!adapter.parked());
    // The seam still functions afterwards.
    LT_CHECK_EQ(adapter.feed("ab"), 2u);
    std::vector<std::byte> destination(4);
    const detail::body_pull_result pulled = adapter.pull(destination);
    LT_CHECK(pulled.kind == detail::body_pull::data);
    LT_CHECK(of(std::span(destination.data(), pulled.copied)) == "ab");
LT_END_AUTO_TEST(unpark_forgets_a_destroyed_wait)

LT_BEGIN_AUTO_TEST(park_suite, feed_after_park_failure_wakes_failed_once)
    // A waiter parked before any wire: when the first feed fails the
    // framing, the waiter wakes failed exactly once.
    detail::recording_sink sink;
    detail::http1_body_source adapter(chunked_mode(), default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    std::vector<std::byte> destination(16);
    body_read seen;
    int deliveries = 0;
    spawn(ex, x.body().read_some(destination),
          [&](task_result<body_read> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    LT_CHECK(adapter.parked());

    // CR parks, LF completes the empty size line: fails at once.
    const std::size_t fed_lf = adapter.feed("\r\n");
    LT_CHECK_EQ(fed_lf, 2u);
    ex.run_pending();
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.status.code() == http::outcome_code::protocol_error);
LT_END_AUTO_TEST(feed_after_park_failure_wakes_failed_once)

LT_BEGIN_SUITE(stream_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(stream_suite)

LT_BEGIN_AUTO_TEST(stream_suite, length_body_streams_splits_between_reads)
    // Acceptance proof: a Content-Length body streams through the
    // PUBLIC body_reader over real framing. The engine feeds in splits
    // between the handler's reads; the payload reassembles and the
    // third read reports the authoritative end.
    detail::recording_sink sink;
    detail::http1_body_source adapter(length_mode(5), default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    std::vector<std::byte> destination(4);
    LT_CHECK_EQ(adapter.feed("hel"), 3u);
    const body_read first = run_read(x, ex, destination);
    LT_CHECK(first.status.ok());
    LT_CHECK(!first.end_of_body);
    LT_CHECK(of(first.data) == "hel");

    LT_CHECK_EQ(adapter.feed("lo"), 2u);
    const body_read second = run_read(x, ex, destination);
    LT_CHECK(second.status.ok());
    LT_CHECK(of(second.data) == "lo");

    const body_read third = run_read(x, ex, destination);
    LT_CHECK(third.status.ok());
    LT_CHECK(third.end_of_body);
    LT_CHECK(third.data.empty());
LT_END_AUTO_TEST(length_body_streams_splits_between_reads)

LT_BEGIN_AUTO_TEST(stream_suite, length_body_streams_bytewise)
    detail::recording_sink sink;
    detail::http1_body_source adapter(length_mode(4), default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    std::vector<std::byte> destination(8);
    std::string payload;
    for (const char c : std::string("abcd")) {
        const std::size_t fed = adapter.feed(std::string(1, c));
        LT_CHECK_EQ(fed, 1u);
        const body_read r = run_read(x, ex, destination);
        LT_CHECK(r.status.ok());
        payload += of(r.data);
    }
    LT_CHECK(payload == "abcd");
    const body_read end = run_read(x, ex, destination);
    LT_CHECK(end.status.ok());
    LT_CHECK(end.end_of_body);
LT_END_AUTO_TEST(length_body_streams_bytewise)

LT_BEGIN_AUTO_TEST(stream_suite, chunked_collect_returns_payload_and_trailers)
    detail::recording_sink sink;
    detail::http1_body_source adapter(chunked_mode(), default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    LT_CHECK_EQ(adapter.feed("5\r\nhello\r\n3\r\nabc\r\n"
                             "0\r\nX-Trace: t1\r\nX-Trace: t2\r\n\r\n"),
                49u);
    body_collect seen;
    spawn(ex, x.body().collect(16),
          [&](task_result<body_collect> r) {
              if (r.has_value()) seen = std::move(r.value());
          });
    ex.run_pending();
    LT_CHECK(seen.status.ok());
    LT_CHECK(of(seen.data) == "helloabc");
    // Trailers are final after a successful collect.
    http::fields expected;
    expected.append("X-Trace", "t1");
    expected.append("X-Trace", "t2");
    LT_CHECK(x.body().trailers() == expected);
LT_END_AUTO_TEST(chunked_collect_returns_payload_and_trailers)

LT_BEGIN_AUTO_TEST(stream_suite, read_after_end_fails_typed)
    detail::recording_sink sink;
    detail::http1_body_source adapter(length_mode(2), default_budget());
    exchange x(make_head(), &sink, 0, &adapter);
    manual_executor ex;
    LT_CHECK(x.admit_body({}).ok());

    std::vector<std::byte> destination(8);
    adapter.feed("hi");
    LT_CHECK(run_read(x, ex, destination).status.ok());
    LT_CHECK(run_read(x, ex, destination).end_of_body);
    const body_read over = run_read(x, ex, destination);
    LT_CHECK(over.status.code() == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(read_after_end_fails_typed)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
