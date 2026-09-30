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

// Request-head byte and field limits for the native HTTP/1 engine
// (TASK-105, DR-V3-006). The limits are a projection of the
// hierarchical server budget surface onto the two resources a request
// head consumes: header_bytes and header_fields.
//
// Internal detail header. Strict gate: reachable only from libhttpserver
// translation units, never from the public umbrella. The #error check
// intentionally precedes the include guard: a consumer TU must never
// reach this file at all, so the check has to fire on every inclusion
// attempt.
#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_head_limits.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_HEAD_LIMITS_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_HEAD_LIMITS_HPP_

#include <cstddef>

#include <httpserver/server/budgets.hpp>

namespace httpserver {

namespace detail {

// Per-head admission limits. max_head_bytes bounds the aggregate head —
// the request line, every field line, and the CRLFCRLF terminator — so
// a head is parseable iff it fits in max_head_bytes octets.
// max_fields bounds the number of received field occurrences regardless
// of name. Defaults mirror the production defaults of
// server::budget_limits; from_budget_limits() is the projection used by
// the engine.
struct http1_head_budget {
    std::size_t max_head_bytes = 1048576;  // server::resource::header_bytes default
    std::size_t max_fields = 256;          // server::resource::header_fields default

    // Projects the two head-relevant resources out of a budget_limits
    // scope. Zero capacities are legal and fail every admission (an
    // empty budget refuses all heads).
    static http1_head_budget from_budget_limits(
        const server::budget_limits& limits) noexcept {
        http1_head_budget budget;
        budget.max_head_bytes =
            limits.get(server::resource::header_bytes);
        budget.max_fields = limits.get(server::resource::header_fields);
        return budget;
    }
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_HEAD_LIMITS_HPP_
