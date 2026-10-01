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

// TASK-111: the bounded synchronous value-returning route adapter
// (architecture §3.1, DR-V3-003, PRD-V3N-REQ-021/022). The sync form is
// the deliberately restricted half of the route surface: a handler
// that computes one bounded value and returns it, with no coroutine
// code of its own. Register through route_registry::route /
// native_server::route_sync with make_sync_route(handler, body_cap).
//
// Restricted versus the coroutine route() form: no suspension, no
// upgrade decision, no admission control beyond the declared cap, no
// streaming of request or response. The request body is admitted and
// buffered only up to body_cap, so the form is unsuitable for
// unbounded uploads and duplex work (PRD-V3N-REQ-021: those stay on
// the coroutine route()); a body one byte past the cap answers 413
// without invoking the handler (REQ-022).

#ifndef SRC_HTTPSERVER_SERVER_ROUTE_SYNC_HPP_
#define SRC_HTTPSERVER_SERVER_ROUTE_SYNC_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <httpserver/body_reader.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/response_writer.hpp>
#include <httpserver/server/routes.hpp>

namespace httpserver {

namespace server {

namespace detail {

// Pins the value's framing when the handler framed nothing: an
// explicit Content-Length or Transfer-Encoding passes through
// verbatim; an unframed value gets Content-Length set to the body
// size (including "0", which keeps a 1.0 keep-alive honest).
inline void pin_value_framing(http::fields& f, std::size_t body_size) {
    if (f.count("content-length") == 0
            && f.count("transfer-encoding") == 0) {
        f.append("Content-Length", std::to_string(body_size));
    }
}

}  // namespace detail

// Wraps a value-returning handler into the canonical route_handler
// (architecture §3.1, DR-V3-003). Per request: admits the body with the
// declared cap, buffers it via collect(cap) -- a body past the cap
// answers 413 and the handler never runs -- invokes the handler on the
// worker thread, and commits the returned value (auto Content-Length
// when the handler framed nothing). The handler runs to completion
// once per request and may throw: the route boundary contains the
// failure. body_cap must be at least 1; native_server::route_sync
// rejects a zero cap at registration.
inline route_handler make_sync_route(sync_route_handler handler,
                                     std::uint64_t body_cap) {
    return [call = std::move(handler), cap = body_cap](
               exchange& x) -> task<void> {
        // The admission carries the cap to the engine (the engine-side
        // staging bound is the bounded-admission task); a typed failure
        // here is a disconnect, so the route ends quietly.
        if (!x.admit_body(body_policy{cap}).ok()) co_return;
        const body_collect collected = co_await x.body().collect(cap);
        if (!collected.status.ok()) {
            // Over-cap: the handler never runs; the undrained remainder
            // is the engine's to settle. Every other failure (disconnect,
            // framing) ends without a local commit; the runner owns the
            // synthesis for the ones that need one.
            if (collected.status.code()
                    == http::outcome_code::limit_exceeded) {
                static_cast<void>(x.respond(http::status::from_code(413),
                                            http::fields()));
            }
            co_return;
        }
        sync_response value = call(
            x.head(), std::span<const std::byte>(collected.data.data(),
                                                 collected.data.size()));
        if (!value.status.valid()) co_return;
        detail::pin_value_framing(value.fields, value.body.size());
        if (value.body.empty()) {
            static_cast<void>(x.respond(value.status, value.fields));
            co_return;
        }
        // A value knows its whole body: commit the head, then push the
        // bytes and the end through the writer. A failure after the
        // committed head leaves the exchange terminal; the engine owns
        // the connection's fate.
        if (!x.start_response(value.status, value.fields).ok()) co_return;
        const body_write written = co_await x.writer().write(
            std::span<const std::byte>(value.body.data(),
                                       value.body.size()));
        if (!written.status.ok()) co_return;
        co_await x.writer().finish();
    };
}

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_ROUTE_SYNC_HPP_
