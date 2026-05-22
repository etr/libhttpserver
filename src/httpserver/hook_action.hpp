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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_HOOK_ACTION_HPP_
#define SRC_HTTPSERVER_HOOK_ACTION_HPP_

#include <optional>
#include <utility>

#include "httpserver/http_response.hpp"

namespace httpserver {

// TASK-045 / DR-012 / §5.6 / PRD-HOOK-REQ-003.
//
// hook_action is the return type of short-circuit-capable phases
// (request_received, body_chunk, before_handler, handler_exception,
// after_handler). A pass-action lets dispatch continue; a
// respond_with-action short-circuits and the wrapped http_response is
// sent on the wire in place of the handler's output.
//
// Storage is std::optional<http_response>: empty == pass, engaged ==
// short-circuit. Move-only because http_response is move-only (DR-005).
// take_response() is rvalue-qualified so the "consumed once" contract
// is syntactically explicit at the firing site: TASK-046+ will write
// `if (!a.is_pass()) { return std::move(a).take_response(); }`.
class hook_action {
 public:
    hook_action() noexcept = default;
    hook_action(const hook_action&) = delete;
    hook_action& operator=(const hook_action&) = delete;
    hook_action(hook_action&&) noexcept = default;
    hook_action& operator=(hook_action&&) noexcept = default;
    ~hook_action() = default;

    // pass(): a no-op action; dispatch continues to the next hook (or
    // the original logic if this is the last hook in the chain).
    [[nodiscard]] static hook_action pass() noexcept {
        return hook_action{};
    }

    // respond_with(r): short-circuit the chain with @p r. Subsequent
    // hooks in the same phase are NOT invoked; the wrapped response is
    // sent on the wire. (Per-phase exact semantics land in TASK-046..050.)
    [[nodiscard]] static hook_action respond_with(http_response r) noexcept {
        hook_action a;
        a.response_.emplace(std::move(r));
        return a;
    }

    [[nodiscard]] bool is_pass() const noexcept {
        return !response_.has_value();
    }

    // take_response() && returns the wrapped http_response by rvalue
    // reference. Precondition: !is_pass(). The action is "consumed" --
    // calling take_response() a second time on the same object yields
    // an empty response (the optional has been moved-from).
    [[nodiscard]] http_response&& take_response() && noexcept {
        return std::move(*response_);
    }

 private:
    std::optional<http_response> response_{};
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HOOK_ACTION_HPP_
