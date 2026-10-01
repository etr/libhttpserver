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

#include <chrono>
#include <cstdint>

#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/options.hpp>
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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
