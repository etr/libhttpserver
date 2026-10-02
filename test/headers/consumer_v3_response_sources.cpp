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

// TASK-113: a consumer including
// <httpserver/response_sources.hpp> without
// HTTPSERVER_COMPILATION (or any other build/TLS configuration
// macro) must compile and link cleanly. The surface is header-only,
// so the empty LDADD of this target is itself part of the contract.
// Everything is pinned through static_asserts over unevaluated
// expressions only: no object whose special members live in the
// library is ever constructed.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/response_sources.hpp>

namespace {

using httpserver::body_chunk;
using httpserver::body_factory;
using httpserver::body_lease;
using httpserver::body_producer;
using httpserver::owned_close_fn;
using httpserver::response_source_kind;
namespace http = httpserver::http;

// The pull vocabulary moved here from response_definition.hpp
// (REQ-026): still one aggregate carrying status, span, and end.
static_assert(std::is_same_v<decltype(std::declval<body_chunk&>().status),
                             http::outcome>);
static_assert(std::is_same_v<decltype(std::declval<body_chunk&>().data),
                             std::span<const std::byte>>);
static_assert(std::is_same_v<decltype(std::declval<body_chunk&>().end),
                             bool>);
static_assert(std::is_constructible_v<body_producer,
                                      body_chunk (*)()>);
static_assert(std::is_constructible_v<body_factory,
                                      body_producer (*)()>);

// The source-kind taxonomy: the three replayable TASK-112 kinds plus
// the TASK-113 transfer and borrowed kinds, all distinct, all packed
// in one byte.
static_assert(response_source_kind::owned_bytes
              != response_source_kind::reopen_file);
static_assert(response_source_kind::reopen_file
              != response_source_kind::factory);
static_assert(response_source_kind::factory
              != response_source_kind::borrowed);
static_assert(response_source_kind::borrowed
              != response_source_kind::owned_file);
static_assert(response_source_kind::owned_file
              != response_source_kind::owned_pipe);
static_assert(std::is_same_v<
              std::underlying_type_t<response_source_kind>,
              std::uint8_t>);

// The close operation riding an owned handle transfer (REQ-028):
// constructible from a plain function pointer (the std::fclose
// default) and from stateful lambdas, move-only and transferable.
static_assert(std::is_constructible_v<owned_close_fn,
                                      void (*)(std::FILE*)>);
static_assert(std::is_move_constructible_v<owned_close_fn>);
static_assert(!std::is_copy_constructible_v<owned_close_fn>);

// The borrowed-memory lifetime lease (REQ-028, DR-V3-005): a plain
// value type that pins any shared keeper; validity is observable.
static_assert(std::is_default_constructible_v<body_lease>);
static_assert(std::is_nothrow_default_constructible_v<body_lease>);
static_assert(std::is_copy_constructible_v<body_lease>);
static_assert(std::is_move_constructible_v<body_lease>);
static_assert(std::is_copy_assignable_v<body_lease>);
static_assert(std::is_constructible_v<body_lease, std::shared_ptr<int>>);
static_assert(std::is_constructible_v<body_lease,
                                      std::shared_ptr<const int>>);
static_assert(std::is_same_v<
              decltype(std::declval<const body_lease&>().valid()),
              bool>);

}  // namespace

int main() {
    // Nothing runs: compiling and linking is the contract.
    return 0;
}
