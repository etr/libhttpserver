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

// TASK-108 step 8: the native_server lifecycle over the real listener
// engines: a validated listen() binds (ephemeral port resolved),
// stop() joins everything, invalid options fail typed with nothing
// bound (REQ-016), route() after listen() is invalid_state, and the
// destructor stops a still-running server. Wire behavior (requests
// through the listener) is the step-9 end-to-end suite's subject.

#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <utility>

#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/route_sync.hpp>
#include <httpserver/server/server.hpp>

#include "./littletest.hpp"

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;

using httpserver::exchange;
using httpserver::task;

srv::server_options loopback_options() {
    srv::server_options options;
    srv::listener_options listener;
    listener.address = "127.0.0.1";
    listener.port = 0;
    options.add_listener(listener);
    options.timeouts().header = std::chrono::milliseconds(2000);
    return options;
}

task<void> ok_handler(exchange&) { co_return; }

}  // namespace

LT_BEGIN_SUITE(native_server_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(native_server_suite)

// A validated listen() binds the endpoint (ephemeral port resolved),
// reports running, and stop() joins promptly and idempotently.
LT_BEGIN_AUTO_TEST(native_server_suite, listen_binds_then_stop_joins)
    srv::native_server server(loopback_options());
    LT_CHECK(server.route(http::method::known(http::method_id::get),
                          "/health", ok_handler)
                 .ok());
    const http::outcome listened = server.listen();
    LT_CHECK(listened.ok());
    LT_CHECK(server.is_running());
    LT_CHECK(server.get_bound_port(0) != 0);
    server.stop();
    LT_CHECK(!server.is_running());
    // Idempotent: a second stop (and the destructor's) is a no-op.
    server.stop();
    LT_CHECK(!server.is_running());
LT_END_AUTO_TEST(listen_binds_then_stop_joins)

// REQ-016: validate() is the pre-listen gate. An option set with no
// listener fails typed and nothing runs or binds.
LT_BEGIN_AUTO_TEST(native_server_suite, invalid_options_fail_typed)
    srv::server_options empty;
    srv::native_server server(empty);
    const http::outcome listened = server.listen();
    LT_CHECK(!listened.ok());
    LT_CHECK(listened.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!listened.message().empty());
    LT_CHECK(!server.is_running());
    LT_CHECK_EQ(server.get_bound_port(0), 0);
LT_END_AUTO_TEST(invalid_options_fail_typed)

// route() after a successful listen() is invalid_state; the accepted
// configuration is frozen at listen time.
LT_BEGIN_AUTO_TEST(native_server_suite, route_after_listen_rejected)
    srv::native_server server(loopback_options());
    LT_CHECK(server.listen().ok());
    const http::outcome routed = server.route(
        http::method::known(http::method_id::get), "/late", ok_handler);
    LT_CHECK(!routed.ok());
    LT_CHECK(routed.code() == http::outcome_code::invalid_state);
    server.stop();
LT_END_AUTO_TEST(route_after_listen_rejected)

// TASK-111: the sync registration surface. A zero body cap is
// invalid_argument (zero means "engine default" in the admission
// policy -- a silent foot-gun), an empty handler is invalid_argument,
// and a duplicate (method, pattern) is invalid_state from the registry.
LT_BEGIN_AUTO_TEST(native_server_suite, route_sync_registration_validated)
    srv::native_server server(loopback_options());
    const http::method post = http::method::known(http::method_id::post);
    const auto echo = [](const http::request_head&,
                         std::span<const std::byte> body)
        -> srv::sync_response {
        srv::sync_response out;
        out.status = http::status::from_code(200);
        out.body.assign(body.begin(), body.end());
        return out;
    };

    const http::outcome zero_cap = server.route_sync(
        post, "/sync", echo, 0);
    LT_CHECK(zero_cap.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!zero_cap.message().empty());

    srv::sync_route_handler empty;
    const http::outcome empty_handler =
        server.route_sync(post, "/sync", std::move(empty), 64);
    LT_CHECK(empty_handler.code() == http::outcome_code::invalid_argument);

    LT_CHECK(server.route_sync(post, "/sync", echo, 16).ok());
    const http::outcome duplicate = server.route_sync(post, "/sync", echo, 16);
    LT_CHECK(duplicate.code() == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(route_sync_registration_validated)

// TASK-111: route_sync() after listen() is invalid_state like route()
// -- the accepted route generation is frozen.
LT_BEGIN_AUTO_TEST(native_server_suite, route_sync_after_listen_rejected)
    srv::native_server server(loopback_options());
    LT_CHECK(server.route_sync(
                 http::method::known(http::method_id::post), "/early",
                 [](const http::request_head&,
                    std::span<const std::byte>) -> srv::sync_response {
                     return srv::sync_response{};
                 },
                 64)
                 .ok());
    LT_CHECK(server.listen().ok());
    const http::outcome late = server.route_sync(
        http::method::known(http::method_id::post), "/late",
        [](const http::request_head&,
           std::span<const std::byte>) -> srv::sync_response {
            return srv::sync_response{};
        },
        64);
    LT_CHECK(late.code() == http::outcome_code::invalid_state);
    server.stop();
LT_END_AUTO_TEST(route_sync_after_listen_rejected)

// listen() runs once; a second call is invalid_state (not a re-bind).
LT_BEGIN_AUTO_TEST(native_server_suite, double_listen_rejected)
    srv::native_server server(loopback_options());
    LT_CHECK(server.listen().ok());
    const http::outcome again = server.listen();
    LT_CHECK(again.code() == http::outcome_code::invalid_state);
    server.stop();
    // After a stop the object is spent: a fresh listen is refused.
    const http::outcome after_stop = server.listen();
    LT_CHECK(after_stop.code() == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(double_listen_rejected)

// The destructor stops a still-running server without hanging.
LT_BEGIN_AUTO_TEST(native_server_suite, destructor_stops_running_server)
    std::uint16_t port = 0;
    {
        srv::native_server server(loopback_options());
        LT_CHECK(server.listen().ok());
        port = server.get_bound_port(0);
        LT_CHECK(port != 0);
    }
    // Scope exit ran ~native_server -> stop(); the bound port is free
    // again: a fresh server may bind the very same address.
    srv::native_server reborn(loopback_options());
    LT_CHECK(reborn.listen().ok());
    LT_CHECK(reborn.get_bound_port(0) != 0);
LT_END_AUTO_TEST(destructor_stops_running_server)

// TASK-110: begin_drain() before listen() is invalid_state (nothing to
// run down yet) and leaves the ticket empty.
LT_BEGIN_AUTO_TEST(native_server_suite, begin_drain_before_listen_invalid_state)
    srv::native_server server(loopback_options());
    srv::drain_ticket ticket;
    const http::outcome began =
        server.begin_drain(std::chrono::milliseconds(1000), ticket);
    LT_CHECK(began.code() == http::outcome_code::invalid_state);
    LT_CHECK(!began.message().empty());
    srv::drain_result observed;
    LT_CHECK(ticket.wait(observed).code()
             == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(begin_drain_before_listen_invalid_state)

// TASK-110: after a stop the object is spent -- begin_drain() refuses.
LT_BEGIN_AUTO_TEST(native_server_suite, begin_drain_after_stop_invalid_state)
    srv::native_server server(loopback_options());
    LT_CHECK(server.listen().ok());
    server.stop();
    srv::drain_ticket ticket;
    const http::outcome began =
        server.begin_drain(std::chrono::milliseconds(1000), ticket);
    LT_CHECK(began.code() == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(begin_drain_after_stop_invalid_state)

// TASK-110: one drain per server object; the second is invalid_state
// and the first ticket stays usable.
LT_BEGIN_AUTO_TEST(native_server_suite, begin_drain_twice_invalid_state)
    srv::native_server server(loopback_options());
    LT_CHECK(server.listen().ok());
    srv::drain_ticket first;
    LT_CHECK(server.begin_drain(std::chrono::milliseconds(5000), first)
                 .ok());
    srv::drain_ticket second;
    const http::outcome again =
        server.begin_drain(std::chrono::milliseconds(5000), second);
    LT_CHECK(again.code() == http::outcome_code::invalid_state);
    server.stop();
    srv::drain_result observed;
    LT_CHECK(first.wait(observed).ok());
    LT_CHECK(observed.status == srv::drain_status::completed);
    LT_CHECK_EQ(observed.remaining, std::size_t{0});
LT_END_AUTO_TEST(begin_drain_twice_invalid_state)

// TASK-110: a non-positive budget is invalid_argument (the ticket's
// wait needs a deadline to be bounded by).
LT_BEGIN_AUTO_TEST(native_server_suite, begin_drain_zero_budget_invalid_argument)
    srv::native_server server(loopback_options());
    LT_CHECK(server.listen().ok());
    srv::drain_ticket ticket;
    const http::outcome began =
        server.begin_drain(std::chrono::milliseconds(0), ticket);
    LT_CHECK(began.code() == http::outcome_code::invalid_argument);
    server.stop();
LT_END_AUTO_TEST(begin_drain_zero_budget_invalid_argument)

// TASK-110: a drain over a listening server with no live work reports
// completion at once. is_running() keeps its meaning across the drain
// (a run-down is not a stop); stop() still closes the object out.
LT_BEGIN_AUTO_TEST(native_server_suite, begin_drain_completes_with_no_work)
    srv::native_server server(loopback_options());
    LT_CHECK(server.listen().ok());
    srv::drain_ticket ticket;
    const http::outcome began =
        server.begin_drain(std::chrono::milliseconds(5000), ticket);
    LT_CHECK(began.ok());
    LT_CHECK(server.is_running());
    srv::drain_result observed;
    const http::outcome waited = ticket.wait(observed);
    LT_CHECK(waited.ok());
    LT_CHECK(observed.status == srv::drain_status::completed);
    LT_CHECK_EQ(observed.remaining, std::size_t{0});
    server.stop();
    LT_CHECK(!server.is_running());
LT_END_AUTO_TEST(begin_drain_completes_with_no_work)

// TASK-110: a default (or moved-from) ticket has nothing to wait on.
LT_BEGIN_AUTO_TEST(native_server_suite, empty_ticket_wait_invalid_state)
    srv::drain_ticket ticket;
    srv::drain_result observed;
    const http::outcome waited = ticket.wait(observed);
    LT_CHECK(waited.code() == http::outcome_code::invalid_state);
    LT_CHECK(!waited.message().empty());
LT_END_AUTO_TEST(empty_ticket_wait_invalid_state)

// TASK-110: the ticket is move-only state -- waiting through a moved
// ticket works, the moved-from one reports invalid_state, and
// move-assign re-homes the drain.
LT_BEGIN_AUTO_TEST(native_server_suite, wait_on_default_ticket_moves)
    srv::native_server server(loopback_options());
    LT_CHECK(server.listen().ok());
    srv::drain_ticket ticket;
    LT_CHECK(server.begin_drain(std::chrono::milliseconds(5000), ticket)
                 .ok());
    srv::drain_ticket moved = std::move(ticket);
    srv::drain_result observed;
    LT_CHECK(moved.wait(observed).ok());
    LT_CHECK(observed.status == srv::drain_status::completed);
    const http::outcome from_empty = ticket.wait(observed);
    LT_CHECK(from_empty.code() == http::outcome_code::invalid_state);
    server.stop();
    srv::drain_ticket target;
    target = std::move(moved);
    srv::drain_result after_move;
    LT_CHECK(target.wait(after_move).ok());
    LT_CHECK(after_move.status == srv::drain_status::completed);
LT_END_AUTO_TEST(wait_on_default_ticket_moves)

LT_BEGIN_AUTO_TEST(native_server_suite, external_driver_lifetime_and_stop_without_host)
    auto options = loopback_options();
    options.loop() = srv::loop_mode::external;
    options.concurrency().workers = 2;
    srv::native_server server(options);
    auto* driver = server.readiness();
    LT_ASSERT(driver != nullptr);
    LT_CHECK(driver->interests().sockets.empty());
    LT_CHECK(!driver->interests().wake.has_value());
    LT_CHECK(driver->dispatch({}, std::chrono::steady_clock::now()).code()
             == http::outcome_code::invalid_state);
    LT_CHECK(server.listen().ok());
    LT_CHECK(driver->interests().wake.has_value());
    LT_CHECK(server.get_bound_port(0) != 0);
    server.stop();
    LT_CHECK(server.readiness() == driver);
    LT_CHECK(driver->interests().sockets.empty());
    LT_CHECK(!driver->interests().wake.has_value());
    LT_CHECK(driver->dispatch({}, std::chrono::steady_clock::now()).code()
             == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(external_driver_lifetime_and_stop_without_host)

LT_BEGIN_AUTO_TEST(native_server_suite, managed_has_no_driver_and_external_validation_precedes_bind)
    srv::native_server managed(loopback_options());
    LT_CHECK(managed.readiness() == nullptr);
    auto options = loopback_options();
    options.loop() = srv::loop_mode::external;
    options.tls().provider = srv::tls_provider::system_default;
    options.tls().profile = srv::tls_profile::certificates;
    srv::native_server external(options);
    LT_CHECK(external.listen().code() == http::outcome_code::not_supported);
    LT_CHECK_EQ(external.get_bound_port(0), 0);
    LT_CHECK(!external.readiness()->interests().wake.has_value());
LT_END_AUTO_TEST(managed_has_no_driver_and_external_validation_precedes_bind)

#if !defined(_WIN32)
LT_BEGIN_AUTO_TEST(native_server_suite, unavailable_wake_fails_listen_before_bind)
    const pid_t child = ::fork();
    LT_ASSERT(child >= 0);
    if (child == 0) {
        rlimit limit{};
        if (::getrlimit(RLIMIT_NOFILE, &limit) != 0) ::_exit(2);
        limit.rlim_cur = 0;
        if (::setrlimit(RLIMIT_NOFILE, &limit) != 0) ::_exit(3);
        auto options = loopback_options();
        options.loop() = srv::loop_mode::external;
        options.concurrency().workers = 1;
        srv::native_server server(options);
        const auto outcome = server.listen();
        const bool correct = outcome.code() == http::outcome_code::connection_closed
            && server.get_bound_port(0) == 0 && !server.is_running()
            && !server.readiness()->interests().wake;
        server.stop();
        ::_exit(correct ? 0 : 4);
    }
    int status = 0;
    pid_t exited = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (exited == 0 && std::chrono::steady_clock::now() < deadline) {
        exited = ::waitpid(child, &status, WNOHANG);
        std::this_thread::yield();
    }
    if (exited == 0) {
        ::kill(child, SIGKILL);
        ::waitpid(child, &status, 0);
    }
    LT_CHECK(exited == child);
    LT_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
LT_END_AUTO_TEST(unavailable_wake_fails_listen_before_bind)
#endif

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
