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

// Route execution over the exchange state machine. NOT part of the
// installed surface; consumers cannot reach it through the public
// umbrella. The engine seam itself (detail::exchange_sink) is declared
// in the public <httpserver/exchange.hpp> because the exchange's
// inline decisions invoke it; this header carries the pieces that only
// the engine (and the unit suites standing in for it) need.
#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/exchange_runner.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_EXCHANGE_RUNNER_HPP_
#define SRC_HTTPSERVER_DETAIL_EXCHANGE_RUNNER_HPP_

#include <cstdint>
#include <utility>

#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/routes.hpp>

namespace httpserver {

namespace detail {

// Call-counting stand-in for the engine seam: the decision suites'
// oracle. Every committed decision is recorded with its payload, plus
// the resume signals the exchange handed out (so a test can fire or
// inspect them the way the engine would).
class recording_sink final : public exchange_sink {
 public:
    void on_admit(const body_policy& policy) override {
        ++admit_calls;
        admitted_bytes = policy.max_buffer_bytes;
    }

    void on_respond(const http::status& s, const http::fields& f) override {
        ++respond_calls;
        respond_code = s.code();
        respond_fields_size = f.size();
    }

    void on_upgrade(const ws_upgrade_options& options) override {
        ++upgrade_calls;
        upgrade_subprotocols = options.subprotocols.size();
    }

    void on_abort() override {
        ++abort_calls;
    }

    int admit_calls = 0;
    int respond_calls = 0;
    int upgrade_calls = 0;
    int abort_calls = 0;
    std::uint16_t respond_code = 0;
    std::size_t respond_fields_size = 0;
    std::uint64_t admitted_bytes = 0;
    std::size_t upgrade_subprotocols = 0;
};

// Best-effort error commit at the route boundary (DR-V3-003). Once the
// exchange is disconnected there is no connection left to answer on,
// so the synthesis is skipped rather than forced; a typed failure of
// the commit itself (the exchange raced a disconnect mid-commit) is
// ignored for the same reason.
inline void commit_error(exchange& x, std::uint16_t code) {
    if (x.disconnected()) return;
    static_cast<void>(x.respond(http::status::from_code(code),
                                http::fields()));
}

// Starts and awaits one handler task -- the invocation core shared by
// run_route and the lifecycle dispatcher (TASK-118). A throw while
// creating the task and a throw inside it both propagate to the
// caller, which owns the exception paths (the dispatcher consults
// handler_exception; run_route synthesizes the 500).
inline task<void> invoke_route_handler(const server::route_handler& handler,
                                       exchange& x) {
    task<void> handler_task = handler(x);
    co_await std::move(handler_task);
}

// Runs one complete request head through the exchange state machine on
// the awaiting executor (architecture §3.1, DR-V3-003). Exactly one
// terminal response is guaranteed: the handler's own, a synthesized
// 501 (invalid method head), 404 (no route), or 500 (handler failure
// or a route that ended without a terminal decision), or a quiet end
// after disconnect cancellation. No handler exception crosses the
// route boundary: a synchronous throw during handler creation, a throw
// inside the handler task, and a throw after the response was commit-
// ted are each contained here (the last one aborts through the engine
// seam instead of committing a second response).
inline task<void> run_route(const server::route_registry& routes,
                            exchange& x) {
    // Defensive: a head without a valid method never matches a
    // registration; answer 501 instead of reporting a miss.
    if (!x.head().request_method.valid()) {
        commit_error(x, 501);
        co_return;
    }
    const server::route_registry::match_result found =
        routes.match(x.head().request_method, x.head().route_path);
    if (found.handler == nullptr) {
        commit_error(x, 404);
        co_return;
    }
    try {
        co_await invoke_route_handler(*found.handler, x);
    } catch (const cancelled_exception&) {
        // Disconnect cancellation is a quiet end, not an error: the
        // engine already owns the connection's fate.
    } catch (...) {
        if (x.terminal()) {
            // The response was already committed; the engine resets or
            // closes instead of committing a second response.
            static_cast<void>(x.abort());
        } else {
            commit_error(x, 500);
        }
        co_return;
    }
    if (!x.terminal()) commit_error(x, 500);
}

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_EXCHANGE_RUNNER_HPP_
