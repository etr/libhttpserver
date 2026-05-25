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

// TASK-048: lifecycle-hook alias installation for the three v1-derived
// single-slot setters (auth_handler, not_found_handler,
// method_not_allowed_handler).
//
// Each setter, when non-null on the create_webserver builder, registers
// one observation hook at the matching phase:
//
//   - not_found_handler          -> hook_phase::route_resolved
//   - method_not_allowed_handler -> hook_phase::before_handler
//   - auth_handler               -> hook_phase::before_handler
//
// The hooks are intentionally no-op observation stubs. The on-the-wire
// behaviour continues to flow through the existing inline dispatch
// paths (which still consult parent->{not_found,method_not_allowed,auth}
// _handler at the v1 call sites in webserver_dispatch.cpp /
// webserver_error_pages.cpp). Their value is two-fold:
//
//   1. They make the alias relationship documented at PRD-HOOK-REQ-009 /
//      §4.10 visible through the hook bus -- a user querying the hook
//      count sees a slot for each setter they configured.
//   2. They reserve the architectural seat: a future task can graduate
//      these stubs to real hooks that perform the dispatch work,
//      without changing the public API.
//
// The aliases are installed conditionally (only when the user supplied
// a non-null callable). Users who never call any of the three setters
// observe zero default hooks -- the zero-cost-when-unused invariant
// (PRD-HOOK-REQ-008) holds.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <functional>
#include <utility>

#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_phase.hpp"

namespace httpserver {

void webserver::install_default_alias_hooks_() {
    // not_found_handler -> route_resolved (observation-only per DR-012).
    if (not_found_handler != nullptr) {
        std::move(add_hook(hook_phase::route_resolved,
            std::function<void(const route_resolved_ctx&)>(
                [](const route_resolved_ctx&) {
                    // Observation stub. The actual 404 synthesis lives
                    // in webserver_impl::not_found_page, consulted from
                    // finalize_answer at the existing v1 call site.
                })))
            .detach();
    }

    // method_not_allowed_handler -> before_handler.
    if (method_not_allowed_handler != nullptr) {
        std::move(add_hook(hook_phase::before_handler,
            std::function<hook_action(before_handler_ctx&)>(
                [](before_handler_ctx&) {
                    // Observation stub. The actual 405 synthesis lives
                    // in webserver_impl::dispatch_resource_handler at the
                    // existing v1 call site.
                    return hook_action::pass();
                })))
            .detach();
    }

    // auth_handler -> before_handler.
    if (auth_handler != nullptr) {
        std::move(add_hook(hook_phase::before_handler,
            std::function<hook_action(before_handler_ctx&)>(
                [](before_handler_ctx&) {
                    // Observation stub. The auth gate runs in
                    // webserver_impl::apply_auth_short_circuit at the
                    // existing v1 call site in finalize_answer.
                    return hook_action::pass();
                })))
            .detach();
    }
}

}  // namespace httpserver
