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

// Server-wide lifecycle hook bus. Internal header; only reachable when
// compiling libhttpserver translation units. NOT part of the installed
// surface; consumers cannot reach it through the public umbrella.
//
// This is the server-scope sibling of detail::resource_hook_table: it
// owns the eleven server-wide phase vectors, the shared registration
// mutex, the advisory any_hooks_ gate array, and the two dedicated
// alias slots (handler_exception, log_access). The dispatch coordinator
// (webserver_impl) holds one by value and forwards fire_* / has_hooks_for
// / phase_hook_count through to it, binding its log_dispatch_error as the
// per-call error logger -- the bus has no back-pointer to the server.
#if !defined(HTTPSERVER_COMPILATION)
#error "hook_bus.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HOOK_BUS_HPP_
#define SRC_HTTPSERVER_DETAIL_HOOK_BUS_HPP_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"

namespace httpserver {

class http_response;

namespace detail {

// hook_bus -- server-wide lifecycle hook storage + firing.
//
// Per-phase callable storage. Each phase has its own
// std::vector<phase_entry<Sig>> because phase signatures differ (some
// return void, some return hook_action; ctx types differ).
//
// Concurrency:
//   - `hook_table_mutex_` is a shared_mutex covering ALL eleven phase
//     vectors. Writers (add, remove) take a unique_lock; firing sites
//     take a shared_lock to snapshot a phase vector before iterating.
//   - `any_hooks_[i]` is an ADVISORY short-circuit gate so that a phase
//     with no registrations costs one atomic load on the dispatch hot
//     path:
//       if (has_hooks_for(phase)) {   // relaxed load
//           std::shared_lock lk(hook_table_mutex_); /* snapshot */ }
//     The relaxed load is sufficient because no reader ever touches a
//     phase vector without first acquiring hook_table_mutex_; the mutex,
//     not the atomic, provides the happens-before for the vector
//     contents. The gate may be momentarily stale in either direction
//     and both races are benign: a stale false skips hooks whose
//     registration ran concurrently with the firing, and a stale true
//     costs one shared_lock acquisition that finds the vector empty. The
//     gate is set on first registration of a phase and cleared when the
//     phase's vector drops to empty, always under the unique_lock (the
//     release ordering on those stores is redundant with the mutex
//     unlock; it carries no extra guarantee).
//   - `next_slot_id_` is a monotonic 64-bit counter. It is never reused,
//     so a hook_handle whose slot has already been erased simply finds
//     no match in remove() -- the idempotent no-op path. 64 bits is
//     unboundedly large in practice.
//
// Members are deliberately public: the dispatch coordinator and the
// HTTPSERVER_COMPILATION-gated webserver_test_access white-box tests
// both reach the vectors / gate / alias slots directly. The boundary
// that matters is between the public header and this internal class --
// not between the coordinator and the bus it owns. The registration /
// removal / firing invariants are still enforced in one place: the only
// mutators are this class's own methods.
class hook_bus {
 public:
    template <class Sig>
    struct phase_entry {
        std::uint64_t slot_id;
        std::function<Sig> fn;
    };

    // Per-call error logger. Firing helpers route hook exceptions
    // through this callback (bound by the coordinator to
    // webserver_impl::log_dispatch_error) -- the bus has no back-pointer
    // to the server. Mirrors resource_hook_table::log_fn.
    using log_fn = std::function<void(std::string_view)>;

    hook_bus() = default;
    ~hook_bus() = default;
    hook_bus(const hook_bus&) = delete;
    hook_bus& operator=(const hook_bus&) = delete;
    hook_bus(hook_bus&&) = delete;
    hook_bus& operator=(hook_bus&&) = delete;

    // Registration. `requested` is the user-supplied phase tag from
    // webserver::add_hook; each overload's `expected` phase is implied
    // by its signature. Throws std::invalid_argument on a phase mismatch
    // or an empty callable. Returns the freshly-allocated slot_id.
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<void(const ::httpserver::connection_open_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<void(const ::httpserver::accept_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<::httpserver::hook_action(
            ::httpserver::request_received_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<::httpserver::hook_action(
            ::httpserver::body_chunk_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<void(const ::httpserver::route_resolved_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<::httpserver::hook_action(
            ::httpserver::before_handler_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<::httpserver::hook_action(
            const ::httpserver::handler_exception_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<::httpserver::hook_action(
            ::httpserver::after_handler_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<void(const ::httpserver::response_sent_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<void(const ::httpserver::request_completed_ctx&)> fn);
    std::uint64_t add(::httpserver::hook_phase requested,
        std::function<void(const ::httpserver::connection_close_ctx&)> fn);

    // Remove the slot with `slot_id` from `phase`'s vector. Not-found is
    // the idempotent no-op path. Re-evaluates the any_hooks_ gate against
    // vector emptiness after removal, but never clears the gate out from
    // under a still-wired alias slot (handler_exception / response_sent).
    void remove(::httpserver::hook_phase phase,
                std::uint64_t slot_id) noexcept;

    // Returns true iff at least one hook is registered for phase @p p.
    // Relaxed load is sufficient -- see the any_hooks_ contract above.
    [[nodiscard]] bool has_hooks_for(
            ::httpserver::hook_phase p) const noexcept {
        return any_hooks_[static_cast<std::size_t>(p)].load(
            std::memory_order_relaxed);
    }

    // Per-phase registration count (user vector only; excludes alias
    // slots). Single accessor so callers don't name individual vectors.
    [[nodiscard]] std::size_t phase_hook_count(
            ::httpserver::hook_phase p) const noexcept;

    // Alias slots. set_handler_exception_alias ALSO arms the
    // any_hooks_[handler_exception] gate so the gate stays the single
    // source of truth even when only the alias is wired. set_log_access_
    // alias does NOT arm the gate (response_sent firing has its own
    // explicit alias check). Both slots are written exactly once during
    // webserver construction and immutable thereafter; the reader paths
    // require no synchronisation.
    void set_handler_exception_alias(
        std::function<::httpserver::hook_action(
            const ::httpserver::handler_exception_ctx&)> fn);
    void set_log_access_alias(
        std::function<void(const ::httpserver::response_sent_ctx&)> fn);
    [[nodiscard]] bool has_handler_exception_alias() const noexcept {
        return static_cast<bool>(handler_exception_alias_);
    }
    [[nodiscard]] bool has_log_access_alias() const noexcept {
        return static_cast<bool>(log_access_alias_);
    }

    // Firing helpers. Each snapshots the phase vector under a shared_lock,
    // releases the lock, then iterates the snapshot inside a try/catch
    // routed through @p on_error. All are noexcept: even a snapshot-copy
    // failure is contained. See src/detail/hook_bus.cpp for the per-phase
    // rationale (short-circuit vs observation, alias tails).
    void fire_connection_opened(
        const ::httpserver::connection_open_ctx& ctx,
        const log_fn& on_error) noexcept;
    void fire_accept_decision(
        const ::httpserver::accept_ctx& ctx,
        const log_fn& on_error) noexcept;
    void fire_connection_closed(
        const ::httpserver::connection_close_ctx& ctx,
        const log_fn& on_error) noexcept;
    [[nodiscard]] std::optional<::httpserver::http_response>
    fire_request_received(::httpserver::request_received_ctx& ctx,
                          const log_fn& on_error) noexcept;
    [[nodiscard]] std::optional<::httpserver::http_response>
    fire_body_chunk(::httpserver::body_chunk_ctx& ctx,
                    const log_fn& on_error) noexcept;
    void fire_route_resolved(
        const ::httpserver::route_resolved_ctx& ctx,
        const log_fn& on_error) noexcept;
    [[nodiscard]] std::optional<::httpserver::http_response>
    fire_before_handler(::httpserver::before_handler_ctx& ctx,
                        const log_fn& on_error) noexcept;
    [[nodiscard]] std::optional<::httpserver::http_response>
    fire_handler_exception(const ::httpserver::handler_exception_ctx& ctx,
                           const log_fn& on_error) noexcept;
    [[nodiscard]] std::optional<::httpserver::http_response>
    fire_after_handler(::httpserver::after_handler_ctx& ctx,
                       const log_fn& on_error) noexcept;
    void fire_response_sent(
        const ::httpserver::response_sent_ctx& ctx,
        const log_fn& on_error) noexcept;
    void fire_request_completed(
        const ::httpserver::request_completed_ctx& ctx,
        const log_fn& on_error) noexcept;

    // ---- state (deliberately public; see class comment) ----------------
    std::shared_mutex hook_table_mutex_;
    std::atomic<std::uint64_t> next_slot_id_{1};
    std::array<std::atomic<bool>,
               static_cast<std::size_t>(hook_phase::count_)> any_hooks_{};

    std::vector<phase_entry<void(const ::httpserver::connection_open_ctx&)>>
        hooks_connection_opened_;
    std::vector<phase_entry<void(const ::httpserver::accept_ctx&)>>
        hooks_accept_decision_;
    std::vector<phase_entry<::httpserver::hook_action(
            ::httpserver::request_received_ctx&)>>
        hooks_request_received_;
    std::vector<phase_entry<::httpserver::hook_action(
            ::httpserver::body_chunk_ctx&)>>
        hooks_body_chunk_;
    std::vector<phase_entry<void(const ::httpserver::route_resolved_ctx&)>>
        hooks_route_resolved_;
    std::vector<phase_entry<::httpserver::hook_action(
            ::httpserver::before_handler_ctx&)>>
        hooks_before_handler_;
    std::vector<phase_entry<::httpserver::hook_action(
            const ::httpserver::handler_exception_ctx&)>>
        hooks_handler_exception_;
    // internal_error_handler alias slot. Last-position fallback in the
    // handler_exception chain. Distinct from hooks_handler_exception_
    // because it must fire AFTER all user hooks -- the opposite of the
    // functional first-position aliases which run before user hooks.
    // Written exactly once during install_default_alias_hooks_() at
    // webserver construction (before start(), daemon not yet running, so
    // no synchronisation for the write); read on the dispatch hot path
    // from fire_handler_exception with no lock.
    std::function<::httpserver::hook_action(
            const ::httpserver::handler_exception_ctx&)>
        handler_exception_alias_;
    std::vector<phase_entry<::httpserver::hook_action(
            ::httpserver::after_handler_ctx&)>>
        hooks_after_handler_;
    std::vector<phase_entry<void(const ::httpserver::response_sent_ctx&)>>
        hooks_response_sent_;
    // log_access alias slot. Mirrors handler_exception_alias_:
    // single-writer-at-construction, read on the dispatch hot path from
    // fire_response_sent without a lock. Fires AFTER user-added
    // response_sent hooks so user hooks observe the response before the
    // legacy access logger formats it.
    std::function<void(const ::httpserver::response_sent_ctx&)>
        log_access_alias_;
    std::vector<phase_entry<void(const ::httpserver::request_completed_ctx&)>>
        hooks_request_completed_;
    std::vector<phase_entry<void(const ::httpserver::connection_close_ctx&)>>
        hooks_connection_closed_;

 private:
    // Shared registration body: phase-mismatch + empty-callable throws,
    // slot-id alloc, push under unique_lock, arm the gate.
    template <class Vec, class Fn>
    std::uint64_t add_impl(::httpserver::hook_phase requested,
                           ::httpserver::hook_phase expected,
                           Vec& vec, Fn fn);

    // Split the per-phase fanout in two so each helper stays under the
    // project-wide CCN gate. Lifecycle-side phases (connection_opened
    // through route_resolved) vs handler-side phases (before_handler on).
    [[nodiscard]] std::size_t phase_hook_count_lifecycle_(
            ::httpserver::hook_phase p) const noexcept;
    [[nodiscard]] std::size_t phase_hook_count_handler_(
            ::httpserver::hook_phase p) const noexcept;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HOOK_BUS_HPP_
