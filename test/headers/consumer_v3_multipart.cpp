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

// TASK-117: a consumer including
// <httpserver/forms/multipart.hpp> WITHOUT HTTPSERVER_COMPILATION
// (or any other build/TLS configuration macro) must compile and link
// cleanly. The decoder and the adapters live in the library
// (detail/forms_multipart.cpp and detail/forms_multipart_files.cpp in
// v3core), so the empty LDADD of this target proves the vocabulary
// surface needs no library linkage to consume; every check is a
// static_assert over unevaluated expressions only -- no object whose
// members live in the library is ever constructed
// (make_multipart_route and the temp-file sink would run library
// code, so they are only name-checked through decltype).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/forms/multipart.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/routes.hpp>

namespace {

using httpserver::exchange;
using httpserver::task;
using httpserver::forms::decode_multipart;
using httpserver::forms::form_fields;
using httpserver::forms::make_multipart_route;
using httpserver::forms::multipart_limits;
using httpserver::forms::multipart_read;
using httpserver::forms::multipart_route_handler;
using httpserver::forms::part_descriptor;
using httpserver::forms::part_file_info;
using httpserver::forms::part_keep_callback;
using httpserver::forms::part_sink;
using httpserver::forms::read_multipart;
using httpserver::forms::temp_file_options;
using httpserver::forms::temp_file_part_sink;
namespace http = httpserver::http;
namespace srv = httpserver::server;

// The limits are a plain aggregate mirroring v2's argument budgets;
// create() validates every cap.
static_assert(std::is_default_constructible_v<multipart_limits>);
static_assert(std::is_copy_assignable_v<multipart_limits>);
static_assert(multipart_limits{}.max_total_bytes
              == static_cast<std::uint64_t>(65536));
static_assert(multipart_limits{}.max_parts
              == static_cast<std::uint64_t>(64));
static_assert(multipart_limits{}.max_part_bytes
              == static_cast<std::uint64_t>(65536));
static_assert(multipart_limits{}.max_part_header_bytes
              == static_cast<std::uint64_t>(8192));
static_assert(std::is_same_v<
    decltype(&multipart_limits::create),
    http::outcome (*)(std::uint64_t, std::uint64_t, std::uint64_t,
                      std::uint64_t, multipart_limits&)>);

// The part identity the sink callback receives: a views-only value
// type (nothing owns storage here).
static_assert(std::is_aggregate_v<part_descriptor>);
static_assert(std::is_same_v<decltype(part_descriptor{}.name),
                             std::string_view>);
static_assert(std::is_same_v<decltype(part_descriptor{}.filename),
                             std::string_view>);

// The streaming consumer interface: an abstract part_sink with the
// four documented hook points.
static_assert(std::has_virtual_destructor_v<part_sink>);
static_assert(!std::is_copy_constructible_v<temp_file_part_sink>);
static_assert(!std::is_move_constructible_v<temp_file_part_sink>);
static_assert(std::is_base_of_v<part_sink, temp_file_part_sink>);
static_assert(std::is_same_v<
    decltype(static_cast<http::outcome (temp_file_part_sink::*)(
                  const part_descriptor&)>(
        &temp_file_part_sink::on_part_begin)),
    http::outcome (temp_file_part_sink::*)(const part_descriptor&)>);
static_assert(std::is_same_v<
    decltype(&temp_file_part_sink::completed),
    const std::vector<part_file_info>& (
        temp_file_part_sink::*)() const noexcept>);
static_assert(std::is_same_v<
    decltype(static_cast<form_fields (temp_file_part_sink::*)()>(
        &temp_file_part_sink::take_fields)),
    form_fields (temp_file_part_sink::*)()>);

// The completed-file ledger entry and the keep callback.
static_assert(std::is_aggregate_v<part_file_info>);
static_assert(std::is_same_v<decltype(part_file_info{}.file_size),
                             std::uint64_t>);
static_assert(!std::is_copy_constructible_v<part_keep_callback>);
static_assert(std::is_move_constructible_v<part_keep_callback>);
static_assert(std::is_aggregate_v<temp_file_options>);

// The read verdict: a plain value carrying the typed outcome, the
// completed-part count, and the shared rejection vocabulary.
static_assert(std::is_default_constructible_v<multipart_read>);
static_assert(std::is_copy_constructible_v<multipart_read>);
static_assert(std::is_move_constructible_v<multipart_read>);
static_assert(std::is_same_v<
    decltype(std::declval<const multipart_read&>().ok()), bool>);
static_assert(noexcept(std::declval<const multipart_read&>().ok()));
static_assert(std::is_same_v<
    decltype(std::declval<const multipart_read&>().reject_status()),
    http::status>);
static_assert(noexcept(
    std::declval<const multipart_read&>().reject_status()));
static_assert(std::is_same_v<
    decltype(std::declval<const multipart_read&>().reject_fields()),
    http::fields>);

// The handler callback is a unique_function (move-only); the adapter
// and the two read forms have the declared shapes.
static_assert(!std::is_copy_constructible_v<multipart_route_handler>);
static_assert(std::is_move_constructible_v<multipart_route_handler>);
static_assert(std::is_same_v<
    decltype(make_multipart_route),
    srv::route_handler(multipart_limits, multipart_route_handler)>);
static_assert(std::is_same_v<
    decltype(read_multipart(std::declval<exchange&>(),
                            std::declval<const multipart_limits&>(),
                            std::declval<part_sink&>())),
    task<multipart_read>>);
static_assert(std::is_same_v<
    decltype(decode_multipart(
        std::declval<std::span<const std::byte>>(),
        std::declval<std::optional<std::string_view>>(),
        std::declval<const multipart_limits&>(),
        std::declval<part_sink&>(),
        std::declval<multipart_read&>())),
    http::outcome>);

}  // namespace

int main() {
    // Nothing runs: compiling and linking is the contract.
    return 0;
}
