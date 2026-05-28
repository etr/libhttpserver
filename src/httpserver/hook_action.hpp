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

#include <cassert>
#include <optional>
#include <utility>

#include "httpserver/http_response.hpp"

/**
 * @file hook_action.hpp
 * @brief Return type of short-circuit-capable hook phases.
 *
 * TASK-045 / DR-012 / §5.6 / PRD-HOOK-REQ-003.
 */
namespace httpserver {

/**
 * @brief Outcome value returned from short-circuit-capable hook phases.
 *
 * Used by `request_received`, `body_chunk`, `before_handler`,
 * `handler_exception`, and `after_handler`. A pass action lets the
 * chain continue to the next subscriber (or the original dispatch
 * logic when the chain is exhausted); a `respond_with` action
 * short-circuits and the wrapped `http_response` is sent on the wire
 * in place of any handler output.
 *
 * Storage is `std::optional<http_response>`: empty means pass, engaged
 * means short-circuit. Move-only because `http_response` is move-only
 * (DR-005). `take_response()` is rvalue-qualified so the "consumed
 * once" contract is syntactically explicit at the firing site.
 */
class hook_action {
 public:
    hook_action() noexcept = default;
    hook_action(const hook_action&) = delete;
    hook_action& operator=(const hook_action&) = delete;
    hook_action(hook_action&&) noexcept = default;
    hook_action& operator=(hook_action&&) noexcept = default;
    ~hook_action() = default;

    /**
     * @brief A no-op action that lets dispatch continue.
     *
     * @return an empty action; the next subscriber (or, if this hook
     *         is the last in the chain, the original dispatch logic)
     *         runs unchanged.
     */
    [[nodiscard]] static hook_action pass() noexcept {
        return hook_action{};
    }

    /**
     * @brief Short-circuit the chain with @p r.
     *
     * Subsequent hooks at the same phase are NOT invoked; the wrapped
     * response is sent on the wire.
     *
     * @param r response to send in place of the handler's output.
     * @return action carrying @p r.
     */
    [[nodiscard]] static hook_action respond_with(http_response r) noexcept {
        hook_action a;
        a.response_.emplace(std::move(r));
        return a;
    }

    /**
     * @brief Query whether this action carries a short-circuit response.
     *
     * @return true if this is a `pass()` action; false if it carries a
     *         `respond_with(...)` payload.
     */
    [[nodiscard]] bool is_pass() const noexcept {
        return !response_.has_value();
    }

    /**
     * @brief Consume the wrapped response.
     *
     * Precondition: `!is_pass()`. Calling `take_response()` when
     * `is_pass()` is true (i.e., `response_` is empty) dereferences an
     * empty `std::optional`, which is undefined behavior. A debug-mode
     * assertion catches misuse at the firing sites (CWE-476). The
     * `noexcept` specifier is correct because the UB path is a
     * precondition violation, not an expected exception path.
     *
     * The action is consumed -- calling `take_response()` a second time
     * on the same object yields a moved-from (empty) `http_response`.
     *
     * @return rvalue reference to the wrapped `http_response`.
     */
    [[nodiscard]] http_response&& take_response() && noexcept {
        assert(response_.has_value() &&
               "take_response() called on a pass-action (is_pass() == true)");
        return std::move(*response_);
    }

 private:
    std::optional<http_response> response_{};
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HOOK_ACTION_HPP_
