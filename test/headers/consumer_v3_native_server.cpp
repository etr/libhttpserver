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

// TASK-108 step 8 (Check A): a consumer including
// <httpserver/server/server.hpp> without HTTPSERVER_COMPILATION (or
// any other build/TLS configuration macro) must compile and link
// cleanly. The header must stay free of engine vocabulary: the pimpl
// keeps every detail/ type out, so the empty LDADD of this target
// links (the definitions live in the library, which step 10's
// v3_native_linkage audit exercises). No server is constructed or
// listened here: this sentinel never needs a thread. The test passes
// by virtue of compiling and linking; main() asserts nothing.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/server.hpp>

// Name every lifecycle member once, all unevaluated (decltype over
// declval): the compiler checks the signatures without emitting a
// single call, so nothing links against the library.
int shape_of_native_server() {
    using httpserver::exchange;
    using httpserver::task;
    namespace http = httpserver::http;
    namespace srv = httpserver::server;

    srv::server_options options;
    srv::listener_options listener;
    listener.address = "127.0.0.1";
    listener.port = 0;
    options.add_listener(listener);
    options.timeouts().header = std::chrono::milliseconds(30000);
    const std::size_t count = options.listener_count();

    srv::route_handler handler = [](exchange&) -> task<void> {
        co_return;
    };
    const http::method get = http::method::known(http::method_id::get);

    using server_ref = srv::native_server&;
    static_assert(
        std::is_same<decltype(std::declval<server_ref>().route(
                          get, "/health", std::move(handler))),
                     http::outcome>::value,
        "route() returns the typed outcome");
    static_assert(
        std::is_same<decltype(std::declval<server_ref>().listen()),
                     http::outcome>::value,
        "listen() returns the typed outcome");
    static_assert(
        std::is_same<decltype(std::declval<server_ref>().get_bound_port(0)),
                     std::uint16_t>::value,
        "get_bound_port() resolves the bound port");
    static_assert(
        std::is_same<decltype(std::declval<server_ref>().is_running()),
                     bool>::value,
        "is_running() reports the lifecycle state");
    static_assert(!std::is_copy_constructible<srv::native_server>::value,
                  "native_server is not copyable");
    static_assert(std::is_constructible<srv::native_server,
                                        srv::server_options>::value,
                  "native_server constructs from server_options alone");

    return count == 1 ? 0 : 1;
}

int main() {
    return shape_of_native_server() == 0 ? 0 : 0;
}
