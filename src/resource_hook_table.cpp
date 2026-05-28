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

// TASK-051 -- Out-of-line bodies for detail::resource_hook_table.
//
// Mirrors src/hook_handle.cpp's fire_hooks_for_phase /
// fire_short_circuit_hooks_for_phase templates, but the firing helpers
// here are members on a per-resource table (not on webserver_impl).
// The lock discipline (snapshot under shared_lock, release, iterate)
// is identical, so re-entrant add_hook / remove() calls from inside a
// hook callback do not deadlock.

#include "httpserver/detail/resource_hook_table.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_response.hpp"

namespace httpserver {
namespace detail {

namespace {

// append_impl -- shared body for all five append_* methods.
// Each public overload delegates here with its phase constant and
// target vector. Mirrors the DRY pattern already applied to fire_* in
// fire_short_circuit_impl / fire_void_impl below.
template <hook_phase P, typename Sig>
std::uint64_t append_impl(
        std::atomic<std::uint64_t>& next_slot_id,
        std::shared_mutex& mtx,
        std::vector<resource_hook_table::entry<Sig>>& vec,
        std::array<std::atomic<bool>,
                   static_cast<std::size_t>(hook_phase::count_)>& any_hooks,
        std::function<Sig> fn) {
    const std::uint64_t id =
        next_slot_id.fetch_add(1, std::memory_order_relaxed);
    {
        std::unique_lock lock(mtx);
        vec.push_back({id, std::move(fn)});
        any_hooks[static_cast<std::size_t>(P)]
            .store(true, std::memory_order_release);
    }
    return id;
}

// fire_short_circuit_impl -- shared dispatch for the three hook_action-
// returning phases. Snapshots the vector, releases the lock, then
// iterates. A short-circuit (action.is_pass() == false) abandons the
// chain and returns the extracted response.
template <typename Vec, typename Ctx>
std::optional<::httpserver::http_response>
fire_short_circuit_impl(std::shared_mutex& mtx,
                        const Vec& hook_vec,
                        Ctx& ctx,
                        std::string_view phase_name,
                        const resource_hook_table::log_fn& log) {
    Vec snapshot;
    try {
        {
            std::shared_lock lock(mtx);
            snapshot = hook_vec;
        }
        for (auto& e : snapshot) {
            try {
                auto action = e.fn(ctx);
                if (!action.is_pass()) {
                    return std::move(action).take_response();
                }
            } catch (const std::exception& ex) {
                if (log) {
                    log(std::string("per-route hook[") +
                        std::string(phase_name) + "] threw: " + ex.what());
                }
            } catch (...) {
                if (log) {
                    log(std::string("per-route hook[") +
                        std::string(phase_name) + "] threw unknown exception");
                }
            }
        }
    } catch (...) {
        if (log) {
            log(std::string("per-route fire_short_circuit[") +
                std::string(phase_name) + "]: snapshot copy failed");
        }
    }
    return std::nullopt;
}

// fire_void_impl -- shared dispatch for the two void-returning phases.
template <typename Vec, typename Ctx>
void fire_void_impl(std::shared_mutex& mtx,
                    const Vec& hook_vec,
                    const Ctx& ctx,
                    std::string_view phase_name,
                    const resource_hook_table::log_fn& log) {
    Vec snapshot;
    try {
        {
            std::shared_lock lock(mtx);
            snapshot = hook_vec;
        }
        for (const auto& e : snapshot) {
            try {
                e.fn(ctx);
            } catch (const std::exception& ex) {
                if (log) {
                    log(std::string("per-route hook[") +
                        std::string(phase_name) + "] threw: " + ex.what());
                }
            } catch (...) {
                if (log) {
                    log(std::string("per-route hook[") +
                        std::string(phase_name) + "] threw unknown exception");
                }
            }
        }
    } catch (...) {
        if (log) {
            log(std::string("per-route fire_void[") +
                std::string(phase_name) + "]: snapshot copy failed");
        }
    }
}

}  // namespace

// ---- append_* -----------------------------------------------------------
// Each public overload delegates to the shared append_impl<P> template
// (see the anonymous namespace above). The only per-overload variation
// is the hook_phase constant and the target vector.

std::uint64_t resource_hook_table::append_before_handler(
        std::function<hook_action(before_handler_ctx&)> fn) {
    return append_impl<hook_phase::before_handler>(
        next_slot_id_, hook_table_mutex_,
        hooks_before_handler_, any_hooks_, std::move(fn));
}

std::uint64_t resource_hook_table::append_handler_exception(
        std::function<hook_action(const handler_exception_ctx&)> fn) {
    return append_impl<hook_phase::handler_exception>(
        next_slot_id_, hook_table_mutex_,
        hooks_handler_exception_, any_hooks_, std::move(fn));
}

std::uint64_t resource_hook_table::append_after_handler(
        std::function<hook_action(after_handler_ctx&)> fn) {
    return append_impl<hook_phase::after_handler>(
        next_slot_id_, hook_table_mutex_,
        hooks_after_handler_, any_hooks_, std::move(fn));
}

std::uint64_t resource_hook_table::append_response_sent(
        std::function<void(const response_sent_ctx&)> fn) {
    return append_impl<hook_phase::response_sent>(
        next_slot_id_, hook_table_mutex_,
        hooks_response_sent_, any_hooks_, std::move(fn));
}

std::uint64_t resource_hook_table::append_request_completed(
        std::function<void(const request_completed_ctx&)> fn) {
    return append_impl<hook_phase::request_completed>(
        next_slot_id_, hook_table_mutex_,
        hooks_request_completed_, any_hooks_, std::move(fn));
}

// ---- remove_slot --------------------------------------------------------

void resource_hook_table::remove_slot(
        hook_phase phase, std::uint64_t slot_id) noexcept {
    std::unique_lock lock(hook_table_mutex_);
    auto erase_if_found = [slot_id](auto& vec) {
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it->slot_id == slot_id) {
                vec.erase(it);
                return;
            }
        }
    };
    auto reset_if_empty = [&](const auto& vec) {
        if (vec.empty()) {
            any_hooks_[static_cast<std::size_t>(phase)]
                .store(false, std::memory_order_release);
        }
    };
    switch (phase) {
    case hook_phase::before_handler:
        erase_if_found(hooks_before_handler_);
        reset_if_empty(hooks_before_handler_);
        break;
    case hook_phase::handler_exception:
        erase_if_found(hooks_handler_exception_);
        reset_if_empty(hooks_handler_exception_);
        break;
    case hook_phase::after_handler:
        erase_if_found(hooks_after_handler_);
        reset_if_empty(hooks_after_handler_);
        break;
    case hook_phase::response_sent:
        erase_if_found(hooks_response_sent_);
        reset_if_empty(hooks_response_sent_);
        break;
    case hook_phase::request_completed:
        erase_if_found(hooks_request_completed_);
        reset_if_empty(hooks_request_completed_);
        break;
    default:
        // The other six phases are never registered on a resource (the
        // public add_hook overloads reject them with std::invalid_argument
        // before reaching here). A defensive no-op for forward compat.
        break;
    }
}

// ---- fire_* -------------------------------------------------------------

std::optional<http_response>
resource_hook_table::fire_before_handler(
        before_handler_ctx& ctx, const log_fn& log) {
    return fire_short_circuit_impl(
        hook_table_mutex_, hooks_before_handler_, ctx,
        "before_handler", log);
}

std::optional<http_response>
resource_hook_table::fire_handler_exception(
        const handler_exception_ctx& ctx, const log_fn& log) {
    return fire_short_circuit_impl(
        hook_table_mutex_, hooks_handler_exception_, ctx,
        "handler_exception", log);
}

std::optional<http_response>
resource_hook_table::fire_after_handler(
        after_handler_ctx& ctx, const log_fn& log) {
    return fire_short_circuit_impl(
        hook_table_mutex_, hooks_after_handler_, ctx,
        "after_handler", log);
}

void resource_hook_table::fire_response_sent(
        const response_sent_ctx& ctx, const log_fn& log) {
    fire_void_impl(hook_table_mutex_, hooks_response_sent_, ctx,
                   "response_sent", log);
}

void resource_hook_table::fire_request_completed(
        const request_completed_ctx& ctx, const log_fn& log) {
    fire_void_impl(hook_table_mutex_, hooks_request_completed_, ctx,
                   "request_completed", log);
}

}  // namespace detail
}  // namespace httpserver
