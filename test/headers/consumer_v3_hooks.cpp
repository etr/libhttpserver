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

// TASK-118 step 5: a consumer including
// <httpserver/server/hooks.hpp> WITHOUT HTTPSERVER_COMPILATION (or any
// other build/TLS configuration macro) must compile and link cleanly.
// The bus storage lives in the library, so every check here is a
// static_assert over unevaluated expressions (the consumer_v3_auth
// convention): the empty LDADD of this target proves the vocabulary
// surface itself needs no linkage.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/server/hooks.hpp>

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;

using srv::hook_action;
using srv::hook_phase;

// The eight phases plus the sentinel, in firing order: accept_decision
// first (connection admission), then the seven request-scoped phases.
static_assert(hook_phase::accept_decision < hook_phase::request_received);
static_assert(hook_phase::request_received < hook_phase::route_resolved);
static_assert(hook_phase::route_resolved < hook_phase::before_handler);
static_assert(hook_phase::before_handler < hook_phase::handler_exception);
static_assert(hook_phase::handler_exception < hook_phase::after_handler);
static_assert(hook_phase::after_handler < hook_phase::response_sent);
static_assert(hook_phase::response_sent < hook_phase::request_completed);
static_assert(hook_phase::request_completed < hook_phase::count_);
static_assert(std::is_same_v<std::underlying_type_t<hook_phase>,
                             std::uint8_t>,
              "the phase enum stays one byte");

// The action vocabulary.
static_assert(std::is_move_constructible_v<hook_action>);
static_assert(!std::is_copy_constructible_v<hook_action>);
static_assert(std::is_same_v<decltype(std::declval<hook_action&>().is_pass()),
                             bool>);

// The response value a hook may supply.
static_assert(std::is_same_v<decltype(std::declval<srv::hook_response&>()
                                          .status),
                             http::status>);
static_assert(std::is_same_v<decltype(std::declval<srv::hook_response&>()
                                          .fields),
                             http::fields>);

// The route descriptor carries the Allow inputs.
static_assert(std::is_same_v<decltype(
                  std::declval<const srv::route_descriptor&>().methods),
              http::method_set>);
static_assert(std::is_same_v<decltype(
                  std::declval<const srv::route_descriptor&>().is_prefix),
              bool>);

// Contexts are aggregate, semantic-exchange-only values.
static_assert(std::is_aggregate_v<srv::request_received_ctx>);
static_assert(std::is_aggregate_v<srv::route_resolved_ctx>);
static_assert(std::is_aggregate_v<srv::before_handler_ctx>);
static_assert(std::is_aggregate_v<srv::handler_exception_ctx>);
static_assert(std::is_aggregate_v<srv::after_handler_ctx>);
static_assert(std::is_aggregate_v<srv::response_sent_ctx>);
static_assert(std::is_aggregate_v<srv::request_completed_ctx>);
static_assert(std::is_same_v<decltype(
                  std::declval<const srv::request_received_ctx&>().request),
              const http::request_head&>);
static_assert(std::is_same_v<decltype(
                  std::declval<const srv::handler_exception_ctx&>().error),
              std::exception_ptr>);
static_assert(std::is_same_v<
                  decltype(std::declval<srv::after_handler_ctx&>().status),
              http::status>,
              "after_handler mutates the status in place");
static_assert(std::is_same_v<
                  decltype(std::declval<const srv::request_completed_ctx&>()
                               .succeeded),
              bool>,
              "request_completed reports the settle verdict");

// The handle: RAII ownership, move-only, default-constructible
// (disarmed).
static_assert(std::is_default_constructible_v<srv::hook_handle>);
static_assert(!std::is_copy_constructible_v<srv::hook_handle>);
static_assert(std::is_nothrow_move_constructible_v<srv::hook_handle>);
static_assert(std::is_same_v<decltype(std::declval<srv::hook_handle&>()
                                          .armed()),
                             bool>);
static_assert(std::is_same_v<
                  decltype(std::declval<srv::hook_handle&>().remove()),
                  void>);
static_assert(std::is_same_v<
                  decltype(std::declval<srv::hook_handle&>().detach()),
                  void>);

// The bus surface: move-only, phase-tagged add returning the owning
// handle, the emptiness probe, and the engine-facing fire.
static_assert(!std::is_copy_constructible_v<srv::hook_bus>);
static_assert(std::is_move_constructible_v<srv::hook_bus>);
static_assert(std::is_same_v<decltype(std::declval<const srv::hook_bus&>()
                                          .any_hooks(hook_phase::after_handler)),
                             bool>);
static_assert(std::is_same_v<
                  decltype(std::declval<const srv::hook_bus&>()
                               .fire<hook_phase::before_handler>(
                                   std::declval<
                                       srv::before_handler_ctx&>())),
                  srv::hook_action>,
              "fire is phase-tagged and returns the chain action");
srv::hook_action sent_probe(srv::response_sent_ctx&);
static_assert(std::is_same_v<
                  decltype(std::declval<srv::hook_bus&>()
                               .add<hook_phase::response_sent>(&sent_probe)),
                  srv::hook_handle>,
              "add hands back the owning handle");

}  // namespace

int main() {
    // The compile is the contract; nothing runs.
    return 0;
}
