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

// TASK-111: a consumer including <httpserver/server/route_sync.hpp>
// without HTTPSERVER_COMPILATION (or any other build/TLS configuration
// macro) must compile and link cleanly. The adapter is header-only, so
// the empty LDADD of this target is itself part of the contract.
// Everything is pinned through static_asserts over unevaluated
// expressions only: no object whose special members live in the
// library is ever constructed.

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/http/fields.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/route_sync.hpp>
#include <httpserver/server/routes.hpp>

namespace {

using httpserver::server::make_sync_route;
using httpserver::server::route_handler;
using httpserver::server::sync_response;
using httpserver::server::sync_route_handler;
namespace http = httpserver::http;

// make_sync_route adapts a value handler into the canonical handler.
static_assert(std::is_same_v<
              decltype(make_sync_route(std::declval<sync_route_handler>(),
                                       std::declval<std::uint64_t>())),
              route_handler>);

// The response value is a plain aggregate of status, fields, and body
// bytes.
static_assert(std::is_same_v<decltype(std::declval<sync_response&>().status),
                             http::status>);
static_assert(std::is_same_v<decltype(std::declval<sync_response&>().fields),
                             http::fields>);
static_assert(std::is_same_v<decltype(std::declval<sync_response&>().body),
                             std::vector<std::byte>>);
static_assert(std::is_default_constructible_v<sync_response>);
static_assert(std::is_move_constructible_v<sync_response>);

// The handler vocabulary: the complete request head plus the bounded
// buffered body in, the response value out.
static_assert(std::is_constructible_v<
              sync_route_handler,
              sync_response (*)(const http::request_head&,
                                std::span<const std::byte>)>);

// A plain value-returning callable -- no coroutine code -- adapts.
struct value_echo {
    sync_response operator()(const http::request_head&,
                             std::span<const std::byte>) const {
        return sync_response{};
    }
};

static_assert(std::is_same_v<
              decltype(make_sync_route(value_echo{}, std::uint64_t{64})),
              route_handler>);

}  // namespace

int main() {
    // Nothing runs: compiling and linking is the contract.
    return 0;
}
