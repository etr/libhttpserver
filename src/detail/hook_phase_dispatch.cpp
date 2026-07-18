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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// hook_phase_dispatch.cpp -- the detail::webserver_impl::fire_* forwarder
// family. The actual per-phase firing logic (snapshot-under-shared_lock,
// try/catch containment, short-circuit, alias tails) lives in
// detail::hook_bus (src/detail/hook_bus.cpp). Each forwarder here binds
// webserver_impl::log_dispatch_error as the bus's per-call error logger,
// so every existing `impl->fire_X(ctx)` call site (in
// webserver_callbacks_lifecycle.cpp, webserver_body_pipeline.cpp,
// webserver_dispatch.cpp, and the gated wrappers in
// webserver_hook_firing.cpp) stays unchanged.
//
// The logger lambda is constructed per call: it captures a single `this`
// pointer, so it rides the std::function small-buffer with no heap
// allocation, and these forwarders only run once a phase gate is open
// (hooks registered) -- never on the zero-hook hot path. The same lambda
// is inlined in the gated wrappers (webserver_hook_firing.cpp), so the
// repetition matches house style rather than hiding behind a helper.

#include <optional>
#include <string>
#include <string_view>

#include "httpserver/hook_action.hpp"
#include "httpserver/http_response.hpp"

#include "httpserver/hook_context.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

// ---- lifecycle (void, observation) ---------------------------------------

void detail::webserver_impl::fire_connection_opened(
    const ::httpserver::connection_open_ctx& ctx) noexcept {
    hooks_.fire_connection_opened(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

void detail::webserver_impl::fire_accept_decision(
    const ::httpserver::accept_ctx& ctx) noexcept {
    hooks_.fire_accept_decision(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

void detail::webserver_impl::fire_connection_closed(
    const ::httpserver::connection_close_ctx& ctx) noexcept {
    hooks_.fire_connection_closed(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

// ---- pre-handler short-circuit -------------------------------------------

std::optional<::httpserver::http_response>
detail::webserver_impl::fire_request_received(
    ::httpserver::request_received_ctx& ctx) noexcept {
    return hooks_.fire_request_received(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

std::optional<::httpserver::http_response>
detail::webserver_impl::fire_body_chunk(
    ::httpserver::body_chunk_ctx& ctx) noexcept {
    return hooks_.fire_body_chunk(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

// ---- route_resolved + before_handler -------------------------------------

void detail::webserver_impl::fire_route_resolved(
    const ::httpserver::route_resolved_ctx& ctx) noexcept {
    hooks_.fire_route_resolved(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

std::optional<::httpserver::http_response>
detail::webserver_impl::fire_before_handler(
    ::httpserver::before_handler_ctx& ctx) noexcept {
    return hooks_.fire_before_handler(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

// ---- handler_exception (alias tail lives in hook_bus) --------------------

std::optional<::httpserver::http_response>
detail::webserver_impl::fire_handler_exception(
    const ::httpserver::handler_exception_ctx& ctx) noexcept {
    return hooks_.fire_handler_exception(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

// ---- post-handler --------------------------------------------------------

std::optional<::httpserver::http_response>
detail::webserver_impl::fire_after_handler(
    ::httpserver::after_handler_ctx& ctx) noexcept {
    return hooks_.fire_after_handler(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

void detail::webserver_impl::fire_response_sent(
    const ::httpserver::response_sent_ctx& ctx) noexcept {
    hooks_.fire_response_sent(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

void detail::webserver_impl::fire_request_completed(
    const ::httpserver::request_completed_ctx& ctx) noexcept {
    hooks_.fire_request_completed(ctx,
        [this](std::string_view m) { log_dispatch_error(std::string(m)); });
}

}  // namespace httpserver
