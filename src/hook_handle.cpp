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

// Out-of-line bodies for hook_handle and peer_address.
//
// hook_handle's destructor + remove() + move ops need the complete
// type of detail::webserver_impl (the per-phase vectors live there),
// so the bodies live here, not in the public header. Same pattern as
// http_response's move/dtor.

#include "httpserver/hook_handle.hpp"

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/hook_action.hpp"
#include "httpserver/http_response.hpp"

#include "httpserver/hook_context.hpp"
#include "httpserver/detail/resource_hook_table.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

// ---- hook_handle ---------------------------------------------------------

hook_handle::hook_handle(hook_handle&& other) noexcept
    : impl_(other.impl_),
      slot_id_(other.slot_id_),
      phase_(other.phase_),
      armed_(other.armed_),
      table_weak_(std::move(other.table_weak_)) {
    // Disarm the source so its destructor is a no-op.
    other.armed_ = false;
    other.impl_ = nullptr;
    other.table_weak_.reset();
}

hook_handle& hook_handle::operator=(hook_handle&& other) noexcept {
    if (this != &other) {
        // Remove any registration this object currently owns before
        // taking over the source's. remove() is idempotent and
        // noexcept, so this is safe even if `armed_` is already false.
        remove();
        impl_ = other.impl_;
        slot_id_ = other.slot_id_;
        phase_ = other.phase_;
        armed_ = other.armed_;
        table_weak_ = std::move(other.table_weak_);
        other.armed_ = false;
        other.impl_ = nullptr;
        other.table_weak_.reset();
    }
    return *this;
}

hook_handle::~hook_handle() {
    if (armed_) {
        remove();
    }
}

// Drain a per-route registration. Called by hook_handle::remove
// when impl_ == nullptr (handles produced by http_resource::add_hook).
// Disarm BEFORE the table call mirrors the server-wide discipline in
// hook_handle::remove. If the resource owning the table has been
// destroyed, the weak_ptr is expired and this is a no-op (the
// contract: "if the resource is destroyed before the handle, remove()
// is a no-op").
static void remove_per_route(std::weak_ptr<detail::resource_hook_table>& weak,
                             hook_phase phase,
                             std::uint64_t slot_id) noexcept {
    auto table = weak.lock();
    weak.reset();
    if (table) {
        table->remove_slot(phase, slot_id);
    }
}

namespace {

// erase_slot_for_phase: route the (impl, phase, slot_id) tuple to the
// correctly-typed per-phase vector and invoke `erase_and_reset` (a generic
// lambda) on it. Split out of hook_handle::remove so the parent function
// stays under the project-wide CCN gate; the per-phase typed dispatch is
// intrinsically one-arm-per-phase so the CCN cost lives here instead.
// The dispatch is split into two helpers (`_lifecycle` / `_handler`) so
// each helper stays inside the CCN ceiling. The helper names mirror the
// enum ranges (phase <= route_resolved vs the rest), not true
// lifecycle-vs-handler semantics -- e.g. connection_closed lands in the
// `_handler_` helper.

template <class EraseFn>
void erase_slot_for_lifecycle_phase_(detail::webserver_impl* impl,
                                     hook_phase phase,
                                     const EraseFn& erase_and_reset) {
    switch (phase) {
    case hook_phase::connection_opened:
        erase_and_reset(impl->hooks_connection_opened_); break;
    case hook_phase::accept_decision:
        erase_and_reset(impl->hooks_accept_decision_); break;
    case hook_phase::request_received:
        erase_and_reset(impl->hooks_request_received_); break;
    case hook_phase::body_chunk:
        erase_and_reset(impl->hooks_body_chunk_); break;
    case hook_phase::route_resolved:
        erase_and_reset(impl->hooks_route_resolved_); break;
    default:
        break;
    }
}

template <class EraseFn>
void erase_slot_for_handler_phase_(detail::webserver_impl* impl,
                                   hook_phase phase,
                                   const EraseFn& erase_and_reset) {
    switch (phase) {
    case hook_phase::before_handler:
        erase_and_reset(impl->hooks_before_handler_); break;
    case hook_phase::handler_exception:
        // The alias slot (handler_exception_alias_) is immutable after
        // construction, so remove() only touches the
        // user vector here. install_internal_error_alias()
        // (src/detail/webserver_aliases.cpp) is the place that sets
        // any_hooks_[handler_exception] = true at construction time when
        // only the alias is wired (no user hooks yet), so the gate stays
        // the single source of truth even before any add_hook() call.
        // erase_and_reset (below, in hook_handle::remove()) re-checks
        // handler_exception_alias_ before clearing any_hooks_ on an
        // empty user vector, so the gate correctly remains true for as
        // long as the alias is still wired.
        erase_and_reset(impl->hooks_handler_exception_); break;
    case hook_phase::after_handler:
        erase_and_reset(impl->hooks_after_handler_); break;
    case hook_phase::response_sent:
        erase_and_reset(impl->hooks_response_sent_); break;
    case hook_phase::request_completed:
        erase_and_reset(impl->hooks_request_completed_); break;
    case hook_phase::connection_closed:
        erase_and_reset(impl->hooks_connection_closed_); break;
    default:
        break;
    }
}

template <class EraseFn>
void erase_slot_for_phase(detail::webserver_impl* impl,
                          hook_phase phase,
                          const EraseFn& erase_and_reset) {
    // The switch arms in the helpers below cannot collapse into a table
    // lookup because each phase vector is a differently-typed
    // phase_entry<Sig>.
    if (static_cast<int>(phase) <=
            static_cast<int>(hook_phase::route_resolved)) {
        erase_slot_for_lifecycle_phase_(impl, phase, erase_and_reset);
        return;
    }
    erase_slot_for_handler_phase_(impl, phase, erase_and_reset);
}

}  // namespace

void hook_handle::remove() noexcept {
    if (!armed_) {
        return;
    }
    // Handles produced by http_resource::add_hook have
    // impl_ == nullptr and carry a (possibly-expired) weak_ptr to
    // the resource's hook table. Handles produced by webserver::add_hook
    // have a non-null impl_ and a default-constructed (empty) weak_ptr.
    if (impl_ == nullptr) {
        const auto phase = phase_;
        const auto id = slot_id_;
        armed_ = false;
        remove_per_route(table_weak_, phase, id);
        return;
    }
    // Snapshot the source state and disarm BEFORE doing the erase, so
    // any exception from the lambda's destructor (very unlikely, but
    // remove() is declared noexcept and std::terminate is the
    // observable outcome anyway) does not leave the handle in a
    // half-removed state.
    auto* impl = impl_;
    const auto phase = phase_;
    const auto id = slot_id_;
    armed_ = false;
    impl_ = nullptr;

    std::unique_lock lock(impl->hook_table_mutex_);

    // Linear scan for the matching slot. Phase vectors are tiny in
    // practice (single-digit hook counts). A not-found result is the
    // idempotent no-op case: the slot was already erased by an earlier
    // remove() or never inserted (defensive).
    // erase_and_reset: linear scan for the slot, erase if found, and
    // clear the any_hooks_ gate if the vector becomes empty. Phase
    // vectors are tiny in practice (single-digit hook counts), so
    // linear scan is the right shape here.
    auto erase_and_reset = [id, impl, phase](auto& vec) {
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it->slot_id == id) {
                vec.erase(it);
                if (vec.empty()) {
                    // Don't clear the gate out from under a still-wired
                    // alias slot: handler_exception_alias_ / log_access_
                    // alias_ are the "still has a hook" signal for their
                    // phases even after the last user-vector entry is
                    // removed. Both slots are write-once at webserver
                    // construction and immutable thereafter, so
                    // reading them here under
                    // hook_table_mutex_ needs no extra synchronization.
                    const bool alias_still_wired =
                        (phase == hook_phase::handler_exception &&
                         impl->handler_exception_alias_) ||
                        (phase == hook_phase::response_sent &&
                         impl->log_access_alias_);
                    if (!alias_still_wired) {
                        impl->any_hooks_[static_cast<std::size_t>(phase)]
                            .store(false, std::memory_order_release);
                    }
                }
                return;
            }
        }
    };

    erase_slot_for_phase(impl, phase, erase_and_reset);
}

hook_handle hook_handle::detach() && noexcept {
    // Construct a disarmed handle from the same data; do NOT call the
    // private armed-ctor because the spec says detach() leaves the
    // underlying registration in place (no impl interaction).
    hook_handle out;
    out.impl_ = impl_;
    out.slot_id_ = slot_id_;
    out.phase_ = phase_;
    // detached: registration persists in the phase vector; destructor
    // is disabled. This disarmed state is semantically distinct from a
    // default-constructed (never-registered) handle: both have armed_==false,
    // but here the backing slot is intentionally left alive.
    out.armed_ = false;
    out.table_weak_ = table_weak_;
    // Disarm the source so its destructor is also a no-op.
    armed_ = false;
    impl_ = nullptr;
    table_weak_.reset();
    return out;
}

// ---- peer_address::to_string ---------------------------------------------
//
// Defined in src/peer_address.cpp. See the rationale comment at the
// top of that file.


}  // namespace httpserver
