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

// Thin webserver_impl forwarders into the hook_dispatcher behavior service
// (DR-014 §4.11). The real per-phase firing logic (logger binding +
// snapshot-under-lock delegation to hook_bus) moved to
// detail/hook_dispatcher.cpp. These keep the existing impl->fire_X(ctx)
// call sites (webserver_body_pipeline.cpp, webserver_callbacks_lifecycle.cpp,
// webserver_dispatch.cpp, and the gated wrappers) compiling unchanged during
// the migration; removed in the final slim step.

#include <optional>

#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {
namespace detail {

void webserver_impl::fire_connection_opened(
    const ::httpserver::connection_open_ctx& ctx) noexcept {
    hooks_dispatch_.fire_connection_opened(ctx);
}

void webserver_impl::fire_accept_decision(
    const ::httpserver::accept_ctx& ctx) noexcept {
    hooks_dispatch_.fire_accept_decision(ctx);
}

void webserver_impl::fire_connection_closed(
    const ::httpserver::connection_close_ctx& ctx) noexcept {
    hooks_dispatch_.fire_connection_closed(ctx);
}

std::optional<::httpserver::http_response>
webserver_impl::fire_request_received(
    ::httpserver::request_received_ctx& ctx) noexcept {
    return hooks_dispatch_.fire_request_received(ctx);
}

std::optional<::httpserver::http_response>
webserver_impl::fire_body_chunk(
    ::httpserver::body_chunk_ctx& ctx) noexcept {
    return hooks_dispatch_.fire_body_chunk(ctx);
}

void webserver_impl::fire_route_resolved(
    const ::httpserver::route_resolved_ctx& ctx) noexcept {
    hooks_dispatch_.fire_route_resolved(ctx);
}

std::optional<::httpserver::http_response>
webserver_impl::fire_before_handler(
    ::httpserver::before_handler_ctx& ctx) noexcept {
    return hooks_dispatch_.fire_before_handler(ctx);
}

std::optional<::httpserver::http_response>
webserver_impl::fire_handler_exception(
    const ::httpserver::handler_exception_ctx& ctx) noexcept {
    return hooks_dispatch_.fire_handler_exception(ctx);
}

std::optional<::httpserver::http_response>
webserver_impl::fire_after_handler(
    ::httpserver::after_handler_ctx& ctx) noexcept {
    return hooks_dispatch_.fire_after_handler(ctx);
}

void webserver_impl::fire_response_sent(
    const ::httpserver::response_sent_ctx& ctx) noexcept {
    hooks_dispatch_.fire_response_sent(ctx);
}

void webserver_impl::fire_request_completed(
    const ::httpserver::request_completed_ctx& ctx) noexcept {
    hooks_dispatch_.fire_request_completed(ctx);
}

}  // namespace detail
}  // namespace httpserver
