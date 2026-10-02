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

// TASK-116: a consumer including
// <httpserver/forms/urlencoded.hpp> WITHOUT HTTPSERVER_COMPILATION
// (or any other build/TLS configuration macro) must compile and link
// cleanly. The decoder and the adapters live in the library
// (detail/forms_urlencoded.cpp in v3core), so the empty LDADD of this
// target proves the vocabulary surface needs no library linkage to
// consume; every check is a static_assert over unevaluated
// expressions only -- no object whose members live in the library is
// ever constructed (make_urlencoded_route would run library code, so
// it is only name-checked through decltype).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/routes.hpp>

namespace {

using httpserver::exchange;
using httpserver::task;
using httpserver::forms::decode_urlencoded;
using httpserver::forms::form_fields;
using httpserver::forms::form_read;
using httpserver::forms::form_route_handler;
using httpserver::forms::make_urlencoded_route;
using httpserver::forms::read_urlencoded;
using httpserver::forms::urlencoded_limits;
namespace http = httpserver::http;
namespace srv = httpserver::server;

// The limits are a plain aggregate mirroring v2's GET-arg budgets;
// create() validates both caps.
static_assert(std::is_default_constructible_v<urlencoded_limits>);
static_assert(std::is_copy_assignable_v<urlencoded_limits>);
static_assert(urlencoded_limits{}.max_total_bytes
              == static_cast<std::uint64_t>(65536));
static_assert(urlencoded_limits{}.max_fields
              == static_cast<std::uint64_t>(64));
static_assert(std::is_same_v<
    decltype(&urlencoded_limits::create),
    http::outcome (*)(std::uint64_t, std::uint64_t,
                      urlencoded_limits&)>);

// The decoded fields: a copyable ordered value type with
// const-correct accessors; the adoption bridge is explicit.
static_assert(std::is_default_constructible_v<form_fields>);
static_assert(std::is_copy_constructible_v<form_fields>);
static_assert(std::is_move_constructible_v<form_fields>);
static_assert(std::is_constructible_v<
    form_fields,
    std::vector<std::pair<std::string, std::string>>>);
static_assert(!std::is_convertible_v<
    std::vector<std::pair<std::string, std::string>>, form_fields>);
static_assert(std::is_same_v<
    decltype(std::declval<const form_fields&>().entries()),
    const std::vector<std::pair<std::string, std::string>>&>);
static_assert(std::is_same_v<
    decltype(std::declval<const form_fields&>().value(
        std::declval<std::string_view>())),
    std::optional<std::string_view>>);
static_assert(noexcept(std::declval<const form_fields&>().value(
    std::declval<std::string_view>())));
static_assert(std::is_same_v<
    decltype(std::declval<const form_fields&>().all(
        std::declval<std::string_view>())),
    std::vector<std::string_view>>);
static_assert(std::is_same_v<
    decltype(std::declval<const form_fields&>().size()),
    std::size_t>);
static_assert(noexcept(std::declval<const form_fields&>().size()));

// The read verdict: a plain value carrying the typed outcome, the
// fields, and the ready-made rejection vocabulary.
static_assert(std::is_default_constructible_v<form_read>);
static_assert(std::is_copy_constructible_v<form_read>);
static_assert(std::is_move_constructible_v<form_read>);
static_assert(std::is_same_v<
    decltype(std::declval<const form_read&>().ok()), bool>);
static_assert(noexcept(std::declval<const form_read&>().ok()));
static_assert(std::is_same_v<
    decltype(std::declval<const form_read&>().reject_status()),
    http::status>);
static_assert(noexcept(
    std::declval<const form_read&>().reject_status()));
static_assert(std::is_same_v<
    decltype(std::declval<const form_read&>().reject_fields()),
    http::fields>);

// The handler callback is a unique_function (move-only); the adapter
// and the two read forms have the declared shapes.
static_assert(!std::is_copy_constructible_v<form_route_handler>);
static_assert(std::is_move_constructible_v<form_route_handler>);
static_assert(std::is_same_v<
    decltype(make_urlencoded_route),
    srv::route_handler(urlencoded_limits, form_route_handler)>);
static_assert(std::is_same_v<
    decltype(read_urlencoded(std::declval<exchange&>(),
                             std::declval<const urlencoded_limits&>())),
    task<form_read>>);
static_assert(std::is_same_v<
    decltype(decode_urlencoded(
        std::declval<std::span<const std::byte>>(),
        std::declval<const urlencoded_limits&>(),
        std::declval<form_fields&>())),
    http::outcome>);

}  // namespace

int main() {
    // Nothing runs: compiling and linking is the contract.
    return 0;
}
