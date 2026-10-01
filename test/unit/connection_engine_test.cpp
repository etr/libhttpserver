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

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/worker_pool.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/budgets.hpp>
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
using httpserver::exchange;
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

// One scenario rig: pool + poll backend + registry + one loopback
// connection. Declaration order is the teardown order's reverse: the
// engine dies first, then the pair, then the pool, then the
// thread-backed backend.
struct scenario {
    explicit scenario(srv::budget_limits limits = srv::budget_limits())
        : root(srv::resource_budget::root(limits)) {
        static_cast<void>(srv::route_registry::create(root, registry));
        static_cast<void>(registry.route(
            http::method::known(http::method_id::get), "/hello",
            hello_handler));
        pair = io_loopback::pair::make();
        if (pair.ok()) {
            pollsys::set_nonblocking(pair.peer(), true);
        }
    }

    bool start_engine() {
        if (!pair.ok()) return false;
        backend.adopt_connection(kConnId, pair.detach_local());
        engine = std::make_shared<connection_engine>(
            backend, pool, registry, root,
            connection_engine_config::from_budget_limits(config_limits),
            kConnId, [this] { stopped.store(true); });
        engine->start();
        return true;
    }

    srv::budget_limits config_limits;
    srv::resource_budget root;
    srv::route_registry registry;
    io_loopback::pair pair;
    io_poll_backend backend;
    worker_pool pool{2};
    std::atomic<bool> stopped{false};
    std::shared_ptr<connection_engine> engine;
};

// Reads one complete response off the peer, deadline-bounded.
std::optional<observed_response> receive_response(
    pollsys::native_socket_t peer) {
    response_frame_parser parser;
    std::byte buf[1024];
    const auto deadline = std::chrono::steady_clock::now() + kBudget;
    while (std::chrono::steady_clock::now() < deadline && !parser.failed()) {
        const pollsys::sys_result r =
            pollsys::read_some(peer, buf, sizeof buf);
        if (r.status == pollsys::sys_status::ok && r.transferred > 0) {
            for (observed_response& done :
                 parser.feed(as_view(buf, r.transferred))) {
                return done;
            }
            continue;
        }
        if (r.status != pollsys::sys_status::would_block) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return std::nullopt;
}

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
        receive_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) {
        LT_CHECK_EQ(response->status, 200);
        LT_CHECK_EQ(response->body, std::string("hello"));
        LT_CHECK_EQ(response->framing, std::string("content-length"));
    }
    LT_CHECK(reaches_eof(s.pair.peer()));
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
        receive_response(s.pair.peer());
    LT_CHECK(response.has_value());
    if (response.has_value()) {
        LT_CHECK_EQ(response->status, 404);
        LT_CHECK(response->body.empty());
    }
    LT_CHECK(reaches_eof(s.pair.peer()));
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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
