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

// TASK-108 step 4: the per-connection HTTP/1 engine over the real poll
// driver. The "client" is one half of a loopback pair: the engine
// adopts the other end under a fabricated connection id (exactly what
// the step-8 accept path does), the test drives the raw request bytes
// and parses the wire through the parity response-frame parser. Every
// wait is deadline-bound; a pass condition is always observed data,
// never a sleep.
//
// The step-4 scope pins the skeleton: a routed GET round trip with a
// streaming body (writer loop drains, outbox order holds, close comes
// only after the flush), the 404 miss path with its end-marker
// synthesis, the clean EOF close before any request, and the
// connections-budget refusal that reports the stop without a response.
// TASK-109 adds the Expect-admission, drain-or-close, and suspension
// deadline scenarios on the same rig.

#include <atomic>
#include <chrono>
#include <deque>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/worker_pool.hpp>
#include <httpserver/body_reader.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/routes.hpp>
#include <parity/response_frame.hpp>

#include "./io_loopback.hpp"
#include "./littletest.hpp"

namespace {

namespace pollsys = httpserver::detail::pollsys;
namespace srv = httpserver::server;
namespace http = httpserver::http;

using httpserver::detail::connection_engine;
using httpserver::detail::connection_engine_config;
using httpserver::detail::io_poll_backend;
using httpserver::detail::worker_pool;
using httpserver::body_collect;
using httpserver::body_policy;
using httpserver::body_read;
using httpserver::exchange;
using httpserver::resume_outcome;
using httpserver::resume_signal;
using httpserver::task;
using parity::observed_response;
using parity::response_frame_parser;

constexpr std::chrono::milliseconds kBudget{10000};
constexpr std::uint64_t kConnId = 77;

template<typename Pred>
bool wait_until(Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + kBudget;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

std::string_view as_view(const std::byte* data, std::size_t size) {
    return std::string_view(reinterpret_cast<const char*>(data), size);
}

// The one route the scenarios register: GET /hello answers 200 with a
// five-byte streaming body (explicit Content-Length framing).
task<void> hello_handler(exchange& x) {
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", "5");
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::string body = "hello";
    const std::byte* raw = reinterpret_cast<const std::byte*>(body.data());
    co_await x.writer().write(std::span<const std::byte>(raw, body.size()));
    co_await x.writer().finish();
}

// POST /echo admits the body, collects it bounded, and echoes it back
// with an explicit Content-Length framing.
task<void> echo_handler(exchange& x) {
    const http::outcome admitted = x.admit_body(body_policy{});
    if (!admitted.ok()) co_return;
    const body_collect collected = co_await x.body().collect(1 << 20);
    if (!collected.status.ok()) co_return;
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", std::to_string(collected.data.size()));
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::byte* raw =
        reinterpret_cast<const std::byte*>(collected.data.data());
    co_await x.writer().write(
        std::span<const std::byte>(raw, collected.data.size()));
    co_await x.writer().finish();
}

// POST /ignore commits a one-shot 200 without reading the body (the
// undrained-body close posture scenario).
task<void> ignore_body_handler(exchange& x) {
    http::fields f;
    f.append("Content-Length", "0");
    static_cast<void>(x.respond(http::status::from_code(200), f));
    co_return;
}

// POST /hang admits the body and parks on a collect that never
// completes (the body-idle watchdog scenario).
task<void> hang_body_handler(exchange& x) {
    static_cast<void>(x.admit_body(body_policy{}));
    static_cast<void>(co_await x.body().collect(1 << 20));
}

// POST /oneshot: the same one-shot shape with the body fully staged
// before the handler even runs.
task<void> one_shot_handler(exchange& x) {
    http::fields f;
    f.append("Content-Length", "0");
    static_cast<void>(x.respond(http::status::from_code(200), f));
    co_return;
}

// TASK-109 scenario state. The tests run sequentially (AUTORUN_TESTS),
// and each resets what it reads before starting its engine.
resume_signal late_gate;   // /late_admit parks here before admitting
std::atomic<bool> suspend_entered{false};
std::atomic<int> suspend_wait_done{0};
std::atomic<int> suspend_outcome{-1};   // resume_outcome as int

// POST /late_admit: parks on the shared gate BEFORE admitting, then
// admits + collects + echoes. The test thread releases the gate, so the
// interim-vs-admission ordering is observable on the wire.
task<void> late_admit_handler(exchange& x) {
    const resume_outcome gate = co_await late_gate.wait_for(kBudget);
    if (gate != resume_outcome::resumed) co_return;
    const http::outcome admitted = x.admit_body(body_policy{});
    if (!admitted.ok()) co_return;
    const body_collect collected = co_await x.body().collect(1 << 20);
    if (!collected.status.ok()) co_return;
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", std::to_string(collected.data.size()));
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::byte* raw =
        reinterpret_cast<const std::byte*>(collected.data.data());
    co_await x.writer().write(
        std::span<const std::byte>(raw, collected.data.size()));
    co_await x.writer().finish();
}

// POST /reject: answers 403 straight from the head -- the body is never
// admitted, so no interim may ride ahead of the rejection.
task<void> reject_handler(exchange& x) {
    http::fields f;
    f.append("Content-Length", "0");
    static_cast<void>(x.respond(http::status::from_code(403), f));
    co_return;
}

// POST /suspend_head: suspends the exchange at head state and parks on
// the resume signal; the engine resolves the wait (a disconnect cancels
// it, the suspension deadline ends the exchange). Records how many
// waits completed and with which outcome.
task<void> suspend_head_handler(exchange& x) {
    resume_signal wait;
    if (!x.suspend(wait).ok()) co_return;
    suspend_entered.store(true);
    const resume_outcome outcome = co_await wait.wait_for(kBudget);
    suspend_outcome.store(static_cast<int>(outcome));
    suspend_wait_done.fetch_add(1);
    co_return;   // a cancelled exchange must not respond
}

// POST /suspend_admit: admits, pulls one bounded piece (the first body
// bytes), then suspends -- the post-admission suspension scenario.
task<void> suspend_admit_handler(exchange& x) {
    const http::outcome admitted = x.admit_body(body_policy{});
    if (!admitted.ok()) co_return;
    std::byte piece[8];
    const body_read first = co_await x.body().read_some(piece);
    if (!first.status.ok() || first.end_of_body) co_return;
    resume_signal wait;
    if (!x.suspend(wait).ok()) co_return;
    suspend_entered.store(true);
    const resume_outcome outcome = co_await wait.wait_for(kBudget);
    suspend_outcome.store(static_cast<int>(outcome));
    suspend_wait_done.fetch_add(1);
    co_return;
}

// One scenario rig: pool + poll backend + registry + one loopback
// connection. Declaration order is the teardown order's reverse: the
// engine dies first, then the pair, then the pool, then the
// thread-backed backend.
struct scenario {
    scenario(srv::budget_limits limits, srv::timeout_options timeouts_in)
        : config_limits(limits), timeouts(timeouts_in),
          root(srv::resource_budget::root(limits)) {
        config.timeouts = timeouts;
        static_cast<void>(srv::route_registry::create(root, registry));
        static_cast<void>(registry.route(
            http::method::known(http::method_id::get), "/hello",
            hello_handler));
        static_cast<void>(registry.route(
            http::method::known(http::method_id::post), "/echo",
            echo_handler));
        pair = io_loopback::pair::make();
        if (pair.ok()) {
            pollsys::set_nonblocking(pair.peer(), true);
        }
    }

    explicit scenario(srv::budget_limits limits = srv::budget_limits())
        : scenario(limits, srv::timeout_options()) { }

    explicit scenario(srv::timeout_options timeouts_in)
        : scenario(srv::budget_limits(), timeouts_in) { }

    bool start_engine() {
        if (!pair.ok()) return false;
        backend.adopt_connection(kConnId, pair.detach_local());
        engine = std::make_shared<connection_engine>(backend, pool, registry,
                                                    root, config, kConnId,
                                                    [this] {
                                                        stopped.store(true);
                                                    });
        engine->start();
        return true;
    }

    srv::budget_limits config_limits;
    srv::timeout_options timeouts;
    connection_engine_config config
        = connection_engine_config::from_budget_limits(config_limits);
    srv::resource_budget root;
    srv::route_registry registry;
    io_loopback::pair pair;
    // Teardown is the reverse declaration order: the engine dies first,
    // then the thread-backed backend (close + driver join), then the
    // pool drains what the close enqueued (the documented order).
    worker_pool pool{2};
    io_poll_backend backend;
    std::atomic<bool> stopped{false};
    std::shared_ptr<connection_engine> engine;

    // Stateful response reader: the parity parser buffers across
    // segments and every completed response is queued, so several
    // responses in one segment (an interim followed by the final head)
    // are observed in order.
    std::optional<observed_response> next_response(
        pollsys::native_socket_t peer) {
        const auto deadline = std::chrono::steady_clock::now() + kBudget;
        while (std::chrono::steady_clock::now() < deadline
               && !wire_parser.failed()) {
            if (!completed.empty()) {
                observed_response done = completed.front();
                completed.pop_front();
                return done;
            }
            const pollsys::sys_result r =
                pollsys::read_some(peer, wire_buf, sizeof wire_buf);
            if (r.status == pollsys::sys_status::ok && r.transferred > 0) {
                for (observed_response& done : wire_parser.feed(
                         as_view(wire_buf, r.transferred))) {
                    completed.push_back(done);
                }
                continue;
            }
            if (r.status != pollsys::sys_status::would_block) {
                for (observed_response& done : wire_parser.finish()) {
                    completed.push_back(done);
                }
                continue;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        return std::nullopt;
    }

    response_frame_parser wire_parser;
    std::deque<observed_response> completed;
    std::byte wire_buf[1024];
};

// Deadline-bounded wait for the peer's FIN (drains trailing bytes).
bool reaches_eof(pollsys::native_socket_t peer) {
    std::byte buf[256];
    const auto deadline = std::chrono::steady_clock::now() + kBudget;
    while (std::chrono::steady_clock::now() < deadline) {
        const pollsys::sys_result r =
            pollsys::read_some(peer, buf, sizeof buf);
        if (r.status == pollsys::sys_status::closed_reset) return true;
        if (r.status == pollsys::sys_status::would_block) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }
        return false;
    }
    return false;
}

// Deadline-bounded absence probe: true when NOT one byte arrives on the
// peer within @p window (a byte or a close fails it immediately).
bool no_bytes_within(pollsys::native_socket_t peer,
                     std::chrono::milliseconds window) {
    std::byte buf[64];
    const auto deadline = std::chrono::steady_clock::now() + window;
    for (;;) {
        const pollsys::sys_result r =
            pollsys::read_some(peer, buf, sizeof buf);
        if (r.status == pollsys::sys_status::would_block) {
            if (std::chrono::steady_clock::now() >= deadline) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        return false;   // data arrived, or the peer closed
    }
}

// Deterministic unwind for a failing deadline scenario: never leave a
// live engine writing its stop callback into a destroyed scenario.
void force_stop(scenario& s) {
    if (s.stopped.load()) return;
    s.engine->shutdown();
    static_cast<void>(wait_until([&s] { return s.stopped.load(); }));
}

}  // namespace

LT_BEGIN_SUITE(connection_engine_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(connection_engine_suite)

// A routed GET streams its body through the writer loop and the
// connection closes only after the response fully drained.
LT_BEGIN_AUTO_TEST(connection_engine_suite, get_round_trip_streams_body)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request = "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) {
        LT_CHECK_EQ(response->status, 200);
        LT_CHECK_EQ(response->body, std::string("hello"));
        LT_CHECK_EQ(response->framing, std::string("content-length"));
    }
    // Keep-alive: the client owns the close of a healthy connection.
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
    LT_CHECK(!s.engine->running());
LT_END_AUTO_TEST(get_round_trip_streams_body)

// A route miss synthesizes 404 with a valid (empty) body and closes.
LT_BEGIN_AUTO_TEST(connection_engine_suite, miss_404_then_close)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request = "GET /missing HTTP/1.1\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) {
        LT_CHECK_EQ(response->status, 404);
        LT_CHECK(response->body.empty());
    }
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(miss_404_then_close)

// EOF before any request: clean close, no response, prompt stop.
LT_BEGIN_AUTO_TEST(connection_engine_suite, eof_before_request_closes)
    scenario s;
    LT_CHECK(s.start_engine());
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(eof_before_request_closes)

// Connections-budget exhaustion: the stop is reported, no response is
// attempted, and the transport is released.
LT_BEGIN_AUTO_TEST(connection_engine_suite, budget_refusal_reports_stop)
    srv::budget_limits limits;
    limits.set(srv::resource::connections, 1);
    scenario s(limits);
    srv::reservation seat;
    LT_CHECK(s.root.reserve(srv::resource::connections, 1, seat).ok());
    LT_CHECK(s.start_engine());
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
    LT_CHECK(!s.engine->running());
LT_END_AUTO_TEST(budget_refusal_reports_stop)

// One connection serves two keep-alive GETs in order.
LT_BEGIN_AUTO_TEST(connection_engine_suite, keepalive_two_gets)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string first = "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), first.data(), first.size());
    const std::optional<observed_response> one =
        s.next_response(s.pair.peer());
    LT_CHECK(one.has_value());
    if (one.has_value()) LT_CHECK_EQ(one->status, 200);
    const std::string second =
        "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), second.data(), second.size());
    const std::optional<observed_response> two =
        s.next_response(s.pair.peer());
    LT_CHECK(two.has_value());
    if (two.has_value()) LT_CHECK_EQ(two->status, 200);
    // The client owns the close on a healthy keep-alive connection.
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(keepalive_two_gets)

// Two requests pipelined in ONE segment: both answered, in order, on
// the single connection (the parked tail recycles into the parser).
LT_BEGIN_AUTO_TEST(connection_engine_suite, pipelined_pair_answered_in_order)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string batch =
        "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"
        "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), batch.data(), batch.size());
    const std::optional<observed_response> one =
        s.next_response(s.pair.peer());
    LT_CHECK(one.has_value());
    if (one.has_value()) LT_CHECK_EQ(one->status, 200);
    const std::optional<observed_response> two =
        s.next_response(s.pair.peer());
    LT_CHECK(two.has_value());
    if (two.has_value()) {
        LT_CHECK_EQ(two->status, 200);
        LT_CHECK_EQ(two->body, std::string("hello"));
    }
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(pipelined_pair_answered_in_order)

// A Content-Length POST round-trips through admit + collect.
LT_BEGIN_AUTO_TEST(connection_engine_suite, post_content_length_echo)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /echo HTTP/1.1\r\nHost: h\r\nContent-Length: 4\r\n\r\n"
        "ping";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) {
        LT_CHECK_EQ(response->status, 200);
        LT_CHECK_EQ(response->body, std::string("ping"));
        LT_CHECK_EQ(response->framing, std::string("content-length"));
    }
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(post_content_length_echo)

// A chunked POST decodes through the same exchange body.
LT_BEGIN_AUTO_TEST(connection_engine_suite, post_chunked_echo)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /echo HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n"
        "\r\n4\r\nping\r\n3\r\npon\r\n0\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) {
        LT_CHECK_EQ(response->status, 200);
        LT_CHECK_EQ(response->body, std::string("pingpon"));
    }
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(post_chunked_echo)

// Expect: 100-continue emits the interim ahead of the final head when
// the handler admits the body.
LT_BEGIN_AUTO_TEST(connection_engine_suite, expect_continue_interim)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /echo HTTP/1.1\r\nHost: h\r\nExpect: 100-continue\r\n"
        "Content-Length: 4\r\n\r\nping";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> interim =
        s.next_response(s.pair.peer());
    LT_CHECK(interim.has_value());
    if (interim.has_value()) LT_CHECK_EQ(interim->status, 100);
    const std::optional<observed_response> final_response =
        s.next_response(s.pair.peer());
    LT_CHECK(final_response.has_value());
    if (final_response.has_value()) {
        LT_CHECK_EQ(final_response->status, 200);
        LT_CHECK_EQ(final_response->body, std::string("ping"));
    }
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(expect_continue_interim)

// The interim follows ADMISSION, not head arrival: while the handler
// holds the body unadmitted, nothing may hit the wire -- then the 100
// rides ahead of the final head the moment admission happens.
LT_BEGIN_AUTO_TEST(connection_engine_suite, expect_no_interim_before_admit)
    scenario s;
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/late_admit",
        late_admit_handler));
    late_gate = resume_signal{};   // fresh one-shot, engine not running yet
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /late_admit HTTP/1.1\r\nHost: h\r\nExpect: 100-continue\r\n"
        "Content-Length: 4\r\n\r\nping";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    LT_CHECK(no_bytes_within(s.pair.peer(), std::chrono::milliseconds(400)));
    late_gate.signal();
    const std::optional<observed_response> interim =
        s.next_response(s.pair.peer());
    LT_CHECK(interim.has_value());
    if (interim.has_value()) LT_CHECK_EQ(interim->status, 100);
    const std::optional<observed_response> final_response =
        s.next_response(s.pair.peer());
    LT_CHECK(final_response.has_value());
    if (final_response.has_value()) {
        LT_CHECK_EQ(final_response->status, 200);
        LT_CHECK_EQ(final_response->body, std::string("ping"));
    }
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(expect_no_interim_before_admit)

// A rejection from the head never admits the body, so no interim may
// precede the 403 (the interim would be the slot's first response).
LT_BEGIN_AUTO_TEST(connection_engine_suite, expect_rejection_no_interim)
    scenario s;
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/reject",
        reject_handler));
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /reject HTTP/1.1\r\nHost: h\r\nExpect: 100-continue\r\n"
        "Content-Length: 4\r\n\r\nping";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 403);
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(expect_rejection_no_interim)

// Expect on HTTP/1.0: the 100-continue interim is a 1.1-only posture;
// the final response arrives alone.
LT_BEGIN_AUTO_TEST(connection_engine_suite, expect_http10_no_interim)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /echo HTTP/1.0\r\nExpect: 100-continue\r\n"
        "Content-Length: 4\r\n\r\nping";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) {
        LT_CHECK_EQ(response->status, 200);
        LT_CHECK_EQ(response->body, std::string("ping"));
    }
    LT_CHECK(reaches_eof(s.pair.peer()));
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(expect_http10_no_interim)

// An unknown Expect value is ignored (no 417, no interim): the request
// is simply served.
LT_BEGIN_AUTO_TEST(connection_engine_suite, expect_unknown_value_no_interim)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /echo HTTP/1.1\r\nHost: h\r\nExpect: widgets\r\n"
        "Content-Length: 4\r\n\r\nping";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) {
        LT_CHECK_EQ(response->status, 200);
        LT_CHECK_EQ(response->body, std::string("ping"));
    }
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(expect_unknown_value_no_interim)

// A peer hangup while the exchange is suspended cancels the resume
// wait exactly once; the engine unwinds and stops promptly.
LT_BEGIN_AUTO_TEST(connection_engine_suite, suspension_head_disconnect_cancels)
    scenario s;
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/suspend_head",
        suspend_head_handler));
    suspend_entered.store(false);
    suspend_wait_done.store(0);
    suspend_outcome.store(-1);
    LT_CHECK(s.start_engine());
    const std::string head =
        "POST /suspend_head HTTP/1.1\r\nHost: h\r\nContent-Length: 1000\r\n"
        "\r\n";
    io_loopback::write_all(s.pair.peer(), head.data(), head.size());
    LT_CHECK(wait_until([] { return suspend_entered.load(); }));
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
    LT_CHECK_EQ(suspend_wait_done.load(), 1);
    LT_CHECK_EQ(suspend_outcome.load(),
                static_cast<int>(resume_outcome::cancelled));
LT_END_AUTO_TEST(suspension_head_disconnect_cancels)

// The suspension deadline is not masked by a trickling body: the
// first-sight anchor does not slide on transport activity (body_idle
// does), so a suspended head exchange ends exactly once at its
// deadline even while body bytes keep arriving.
LT_BEGIN_AUTO_TEST(connection_engine_suite,
                   suspension_head_not_masked_by_trickle_times_out_once)
    srv::timeout_options timeouts;
    timeouts.suspension = std::chrono::milliseconds(400);
    timeouts.body_idle = std::chrono::milliseconds(2000);
    scenario s(timeouts);
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/suspend_head",
        suspend_head_handler));
    suspend_entered.store(false);
    suspend_wait_done.store(0);
    suspend_outcome.store(-1);
    LT_CHECK(s.start_engine());
    const std::string head =
        "POST /suspend_head HTTP/1.1\r\nHost: h\r\nContent-Length: 1000\r\n"
        "\r\n";
    io_loopback::write_all(s.pair.peer(), head.data(), head.size());
    LT_CHECK(wait_until([] { return suspend_entered.load(); }));
    const auto began = std::chrono::steady_clock::now();
    const char dot = 'x';
    while (!s.stopped.load()
           && std::chrono::steady_clock::now() - began
                  < std::chrono::milliseconds(1200)) {
        io_loopback::write_all(s.pair.peer(), &dot, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const auto elapsed = std::chrono::steady_clock::now() - began;
    LT_CHECK(s.stopped.load());
    LT_CHECK(elapsed < std::chrono::milliseconds(1500));
    LT_CHECK_EQ(suspend_wait_done.load(), 1);
    LT_CHECK_EQ(suspend_outcome.load(),
                static_cast<int>(resume_outcome::cancelled));
    // A timed-out peer reads no response: close observed, zero bytes.
    LT_CHECK(reaches_eof(s.pair.peer()));
    force_stop(s);
LT_END_AUTO_TEST(suspension_head_not_masked_by_trickle_times_out_once)

// The same suspension deadline governs AFTER admission: a handler that
// read a partial body and suspended still ends exactly once, at the
// anchored deadline, despite the continuing trickle.
LT_BEGIN_AUTO_TEST(connection_engine_suite,
                   suspension_after_admit_times_out_once)
    srv::timeout_options timeouts;
    timeouts.suspension = std::chrono::milliseconds(400);
    timeouts.body_idle = std::chrono::milliseconds(2000);
    scenario s(timeouts);
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/suspend_admit",
        suspend_admit_handler));
    suspend_entered.store(false);
    suspend_wait_done.store(0);
    suspend_outcome.store(-1);
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /suspend_admit HTTP/1.1\r\nHost: h\r\n"
        "Content-Length: 1000\r\n\r\n" + std::string(16, 'p');
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    LT_CHECK(wait_until([] { return suspend_entered.load(); }));
    const auto began = std::chrono::steady_clock::now();
    const char dot = 'x';
    while (!s.stopped.load()
           && std::chrono::steady_clock::now() - began
                  < std::chrono::milliseconds(1200)) {
        io_loopback::write_all(s.pair.peer(), &dot, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const auto elapsed = std::chrono::steady_clock::now() - began;
    LT_CHECK(s.stopped.load());
    LT_CHECK(elapsed < std::chrono::milliseconds(1500));
    LT_CHECK_EQ(suspend_wait_done.load(), 1);
    LT_CHECK_EQ(suspend_outcome.load(),
                static_cast<int>(resume_outcome::cancelled));
    LT_CHECK(reaches_eof(s.pair.peer()));
    force_stop(s);
LT_END_AUTO_TEST(suspension_after_admit_times_out_once)

// Early upload bytes park bounded while admission is pending, and are
// delivered whole at admission: with the staging cap at 2048 the body
// fully parks in the early buffer (the reader gate engages at the
// cap), admission seeds the decoder from it, and the echo returns
// every byte. Bodies larger than the cap flow through the admitted
// path's combined re-feed (bounded by the TASK-106 seam: a pull-driven
// engine wake is out of scope), so the no-loss pin here is the early
// phase at exactly the cap.
LT_BEGIN_AUTO_TEST(connection_engine_suite, early_bytes_bounded_no_loss)
    srv::budget_limits limits;
    limits.set(srv::resource::body_buffer_bytes, 2048);
    scenario s(limits);
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/late_admit",
        late_admit_handler));
    late_gate = resume_signal{};
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /late_admit HTTP/1.1\r\nHost: h\r\nExpect: 100-continue\r\n"
        "Content-Length: 2048\r\n\r\n" + std::string(2048, 'e');
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    LT_CHECK(no_bytes_within(s.pair.peer(), std::chrono::milliseconds(200)));
    late_gate.signal();
    const std::optional<observed_response> interim =
        s.next_response(s.pair.peer());
    LT_CHECK(interim.has_value());
    if (interim.has_value()) LT_CHECK_EQ(interim->status, 100);
    const std::optional<observed_response> final_response =
        s.next_response(s.pair.peer());
    LT_CHECK(final_response.has_value());
    if (final_response.has_value()) {
        LT_CHECK_EQ(final_response->status, 200);
        LT_CHECK_EQ(final_response->body.size(), std::size_t{2048});
    }
    // Healthy after the burst: another exchange on the same connection.
    const std::string follow = "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), follow.data(), follow.size());
    const std::optional<observed_response> again =
        s.next_response(s.pair.peer());
    LT_CHECK(again.has_value());
    if (again.has_value()) {
        LT_CHECK_EQ(again->status, 200);
        LT_CHECK_EQ(again->body, std::string("hello"));
    }
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(early_bytes_bounded_no_loss)

// A body the handler never reads blocks keep-alive: the handler
// one-shot responds and the body arrives afterwards -- the connection
// closes instead of misframing the next head.
// TASK-109: a rejected length-framed body drains to its counted
// remainder and the connection is reused (the RFC 9110 section 10.1.1
// "continue reading" posture) instead of closing.
LT_BEGIN_AUTO_TEST(connection_engine_suite,
                   undrained_length_body_drains_and_reuses)
    scenario s;
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/ignore",
        ignore_body_handler));
    LT_CHECK(s.start_engine());
    const std::string head_only =
        "POST /ignore HTTP/1.1\r\nHost: h\r\nContent-Length: 8\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), head_only.data(),
                           head_only.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 200);
    // The late body feeds the drain, not the parser: no FIN arrives.
    const std::string body = "12345678";
    io_loopback::write_all(s.pair.peer(), body.data(), body.size());
    LT_CHECK(no_bytes_within(s.pair.peer(), std::chrono::milliseconds(300)));
    // The drained connection serves the next exchange untouched.
    const std::string follow = "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), follow.data(), follow.size());
    const std::optional<observed_response> again =
        s.next_response(s.pair.peer());
    LT_CHECK(again.has_value());
    if (again.has_value()) {
        LT_CHECK_EQ(again->status, 200);
        LT_CHECK_EQ(again->body, std::string("hello"));
    }
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(undrained_length_body_drains_and_reuses)

// A rejected chunked body has no counted remainder to drain: the
// connection closes rather than guess the framing (the pinned
// not-drainable posture).
LT_BEGIN_AUTO_TEST(connection_engine_suite, undrained_chunked_rejection_closes)
    scenario s;
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/ignore",
        ignore_body_handler));
    LT_CHECK(s.start_engine());
    // An unterminated chunked body: the handler answers one-shot, the
    // framing never completes.
    const std::string request =
        "POST /ignore HTTP/1.1\r\nHost: h\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "4\r\nabcd";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 200);
    LT_CHECK(reaches_eof(s.pair.peer()));
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(undrained_chunked_rejection_closes)

// A stalled drain hits its non-sliding deadline: the client sends only
// a prefix of a rejected 1 MiB body, so the drain anchor is set and the
// deadline closes the connection instead of hanging on the remainder.
LT_BEGIN_AUTO_TEST(connection_engine_suite, rejection_drain_deadline_closes)
    srv::timeout_options timeouts;
    timeouts.drain = std::chrono::milliseconds(200);
    scenario s(timeouts);
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/ignore",
        ignore_body_handler));
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /ignore HTTP/1.1\r\nHost: h\r\nContent-Length: 1048576"
        "\r\n\r\n" + std::string(256, 'x');
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 200);
    // In-drain quiet window: the 200 ms deadline has not arrived, so
    // nothing -- not even the FIN -- may appear yet. The always-close
    // posture fails here (its FIN is immediate).
    LT_CHECK(no_bytes_within(s.pair.peer(), std::chrono::milliseconds(50)));
    const auto began = std::chrono::steady_clock::now();
    LT_CHECK(reaches_eof(s.pair.peer()));
    const auto elapsed = std::chrono::steady_clock::now() - began;
    LT_CHECK(elapsed < std::chrono::milliseconds(2000));
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(rejection_drain_deadline_closes)

// The one-shot shape: head and body in one segment, the handler
// responds without ever touching the writer or the body.
LT_BEGIN_AUTO_TEST(connection_engine_suite, one_shot_respond_with_body)
    scenario s;
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/oneshot",
        one_shot_handler));
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /oneshot HTTP/1.1\r\nHost: h\r\nContent-Length: 8\r\n\r\n"
        "12345678";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 200);
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(one_shot_respond_with_body)

// POST to a missing route: run_route's own 404 synthesis answers a
// request whose body is already fully staged.
LT_BEGIN_AUTO_TEST(connection_engine_suite, post_miss_404_with_staged_body)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /missing HTTP/1.1\r\nHost: h\r\nContent-Length: 4\r\n\r\n"
        "ping";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 404);
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(post_miss_404_with_staged_body)

// A malformed request line synthesizes 400 and closes; nothing parses
// after a close_now posture.
LT_BEGIN_AUTO_TEST(connection_engine_suite, malformed_line_400_then_close)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request = "GET /no-version\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 400);
    LT_CHECK(reaches_eof(s.pair.peer()));
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(malformed_line_400_then_close)

// A head over the configured budget synthesizes 431 and closes.
LT_BEGIN_AUTO_TEST(connection_engine_suite, head_over_budget_431)
    srv::budget_limits limits;
    limits.set(srv::resource::header_bytes, 128);
    scenario s(limits);
    LT_CHECK(s.start_engine());
    const std::string request =
        "GET /" + std::string(256, 'a') + " HTTP/1.1\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 431);
    LT_CHECK(reaches_eof(s.pair.peer()));
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(head_over_budget_431)

// Transfer-Encoding together with Content-Length: 400 (smuggling
// posture), connection closed.
LT_BEGIN_AUTO_TEST(connection_engine_suite, te_with_cl_400)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /echo HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n"
        "Content-Length: 4\r\n\r\n0\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 400);
    LT_CHECK(reaches_eof(s.pair.peer()));
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(te_with_cl_400)

// A transfer coding before chunked is well-formed but unsupported: 501.
LT_BEGIN_AUTO_TEST(connection_engine_suite, te_coding_before_chunked_501)
    scenario s;
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /echo HTTP/1.1\r\nHost: h\r\n"
        "Transfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 501);
    LT_CHECK(reaches_eof(s.pair.peer()));
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(te_coding_before_chunked_501)

// An idle connection closes at the header timeout (watchdog-enforced,
// observed as the stop within a tight deadline -- never a sleep pass).
LT_BEGIN_AUTO_TEST(connection_engine_suite, idle_header_timeout_closes)
    srv::timeout_options timeouts;
    timeouts.header = std::chrono::milliseconds(200);
    scenario s(timeouts);
    LT_CHECK(s.start_engine());
    const auto began = std::chrono::steady_clock::now();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
    const auto elapsed = std::chrono::steady_clock::now() - began;
    LT_CHECK(elapsed < std::chrono::milliseconds(5000));
LT_END_AUTO_TEST(idle_header_timeout_closes)

// Activity re-arms the watchdog: a request completed well inside the
// timeout still gets its response.
LT_BEGIN_AUTO_TEST(connection_engine_suite, activity_rearms_watchdog)
    srv::timeout_options timeouts;
    timeouts.header = std::chrono::milliseconds(400);
    scenario s(timeouts);
    LT_CHECK(s.start_engine());
    io_loopback::write_all(s.pair.peer(), "GET /he", 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const std::string rest = "llo HTTP/1.1\r\nHost: h\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), rest.data(), rest.size());
    const std::optional<observed_response> response =
        s.next_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) LT_CHECK_EQ(response->status, 200);
    s.pair.close_peer();
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(activity_rearms_watchdog)

// A body that never arrives trips the body-idle watchdog: the parked
// handler unwinds through the disconnect and the connection closes.
LT_BEGIN_AUTO_TEST(connection_engine_suite, body_idle_disconnects)
    srv::timeout_options timeouts;
    timeouts.body_idle = std::chrono::milliseconds(200);
    scenario s(timeouts);
    static_cast<void>(s.registry.route(
        http::method::known(http::method_id::post), "/hang",
        hang_body_handler));
    LT_CHECK(s.start_engine());
    const std::string request =
        "POST /hang HTTP/1.1\r\nHost: h\r\nContent-Length: 8\r\n\r\n";
    io_loopback::write_all(s.pair.peer(), request.data(), request.size());
    // No body ever arrives; the response never comes either.
    const std::optional<observed_response> none =
        s.next_response(s.pair.peer());
    LT_CHECK(!none.has_value());
    LT_CHECK(reaches_eof(s.pair.peer()));
    LT_CHECK(wait_until([&s] { return s.stopped.load(); }));
LT_END_AUTO_TEST(body_idle_disconnects)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
