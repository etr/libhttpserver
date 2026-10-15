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
void pin_value_framing(http::fields& f, std::size_t body_size);

// The shared commit tail of the value-returning route adapters (the
// sync route and the urlencoded form route): an invalid status ends
// without a terminal decision (the runner synthesizes the 500),
// framing is pinned when the handler framed nothing, HEAD retains the
// corresponding body length without writer traffic, an empty body
// commits one-shot with no writer traffic, and a body commits the
// head then streams the bytes and the end. A failure after the
// committed head leaves the exchange terminal; the engine owns the
// connection's fate.
task<void> commit_sync_value(exchange& x, sync_response value);

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
route_handler make_sync_route(sync_route_handler handler,
                                     std::uint64_t body_cap);

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_ROUTE_SYNC_HPP_
