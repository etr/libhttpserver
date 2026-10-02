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

// TASK-112: a consumer including
// <httpserver/response_definition.hpp> without
// HTTPSERVER_COMPILATION (or any other build/TLS configuration
// macro) must compile and link cleanly. The surface is header-only,
// so the empty LDADD of this target is itself part of the contract.
// Everything is pinned through static_asserts over unevaluated
// expressions only: no object whose special members live in the
// library is ever constructed.

#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/response_definition.hpp>

namespace {

using httpserver::body_chunk;
using httpserver::body_factory;
using httpserver::body_producer;
using httpserver::exchange;
using httpserver::response_definition;
using httpserver::response_overlay;
using httpserver::send_definition;
using httpserver::send_report;
using httpserver::task;
namespace http = httpserver::http;

// The typed body-source vocabulary (REQ-026): one pull carries a
// status, a span, and the end marker; producers and factories are the
// move-only callables.
static_assert(std::is_same_v<decltype(std::declval<body_chunk&>().status),
                             http::outcome>);
static_assert(std::is_same_v<decltype(std::declval<body_chunk&>().data),
                             std::span<const std::byte>>);
static_assert(std::is_same_v<decltype(std::declval<body_chunk&>().end),
                             bool>);
static_assert(std::is_aggregate_v<body_chunk>);
static_assert(
    std::is_constructible_v<body_producer, body_chunk (*)()>);
static_assert(
    std::is_constructible_v<body_factory, body_producer (*)()>);

// The immutable definition: default, copy, and move all exist; the
// three source kinds are distinct; the accessors are const.
static_assert(std::is_default_constructible_v<response_definition>);
static_assert(std::is_copy_constructible_v<response_definition>);
static_assert(std::is_move_constructible_v<response_definition>);
static_assert(std::is_copy_assignable_v<response_definition>);
static_assert(std::is_move_assignable_v<response_definition>);
static_assert(response_definition::source_kind::owned_bytes
              != response_definition::source_kind::reopen_file);
static_assert(response_definition::source_kind::reopen_file
              != response_definition::source_kind::factory);
static_assert(
    std::is_same_v<
        decltype(std::declval<const response_definition&>().kind()),
        response_definition::source_kind>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const response_definition&>().valid()),
        bool>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const response_definition&>().status()),
        const http::status&>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const response_definition&>().fields()),
        const http::fields&>);

// The validating factories: status, fields by value, the source, and
// the out parameter -- nothing mutates on failure.
static_assert(std::is_same_v<
              decltype(&response_definition::owned_bytes),
              http::outcome (*)(const http::status&, http::fields,
                                std::vector<std::byte>,
                                response_definition&)>);
static_assert(std::is_same_v<
              decltype(&response_definition::reopen_file),
              http::outcome (*)(const http::status&, http::fields,
                                std::string, response_definition&)>);
static_assert(std::is_same_v<
              decltype(&response_definition::factory),
              http::outcome (*)(const http::status&, http::fields,
                                body_factory, response_definition&)>);

// The TASK-113 factories (REQ-028): borrowed takes the span and the
// lease; the transfer kinds take the handle, optionally with the
// custom close operation riding the transfer. Each static_cast
// selects one of an overloaded pair by signature, so a changed or
// missing factory fails the build here.
using borrowed_fn = http::outcome (*)(const http::status&, http::fields, std::span<const std::byte>, httpserver::body_lease, response_definition&);
using owned_file_fn = http::outcome (*)(const http::status&, http::fields, std::FILE*, response_definition&);
using owned_file_close_fn = http::outcome (*)(const http::status&, http::fields, std::FILE*, httpserver::owned_close_fn, response_definition&);
using owned_pipe_close_fn = http::outcome (*)(const http::status&, http::fields, std::FILE*, httpserver::owned_close_fn, response_definition&);
static_assert(std::is_same_v<decltype(static_cast<borrowed_fn>(&response_definition::borrowed)), borrowed_fn>);
static_assert(std::is_same_v<decltype(static_cast<owned_file_fn>(&response_definition::owned_file)), owned_file_fn>);
static_assert(std::is_same_v<decltype(static_cast<owned_file_close_fn>(&response_definition::owned_file)), owned_file_close_fn>);
static_assert(std::is_same_v<decltype(static_cast<owned_file_fn>(&response_definition::owned_pipe)), owned_file_fn>);
static_assert(std::is_same_v<decltype(static_cast<owned_pipe_close_fn>(&response_definition::owned_pipe)), owned_pipe_close_fn>);

// The per-send overlay (REQ-030) and the send report: plain deep
// value types.
static_assert(std::is_same_v<
              decltype(std::declval<response_overlay&>().headers),
              http::fields>);
static_assert(std::is_same_v<
              decltype(std::declval<response_overlay&>().trailers),
              http::fields>);
static_assert(std::is_default_constructible_v<response_overlay>);
static_assert(std::is_copy_constructible_v<response_overlay>);
static_assert(std::is_same_v<
              decltype(std::declval<send_report&>().status),
              http::outcome>);
static_assert(std::is_same_v<
              decltype(std::declval<send_report&>().body_bytes),
              std::size_t>);
static_assert(std::is_default_constructible_v<send_report>);

// The send helper: a task<send_report> over a borrowed definition
// and an overlay taken by value (the frame owns its copy).
static_assert(std::is_same_v<
              decltype(send_definition(std::declval<exchange&>(),
                                       std::declval<
                                           const response_definition&>())),
              task<send_report>>);
static_assert(std::is_same_v<
              decltype(send_definition(std::declval<exchange&>(),
                                       std::declval<
                                           const response_definition&>(),
                                       std::declval<response_overlay>())),
              task<send_report>>);

}  // namespace

int main() {
    // Nothing runs: compiling and linking is the contract.
    return 0;
}
