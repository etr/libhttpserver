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
    // Snapshot the source state and disarm BEFORE delegating the erase,
    // so any exception (very unlikely, but remove() is declared noexcept
    // and std::terminate is the observable outcome anyway) does not leave
    // the handle in a half-removed state. hook_bus::remove is the single
    // owner of the erase-and-gate-clear logic (scan the phase vector,
    // erase, clear the any_hooks_ gate if the vector empties -- but not
    // out from under a still-wired alias slot).
    auto* impl = impl_;
    const auto phase = phase_;
    const auto id = slot_id_;
    armed_ = false;
    impl_ = nullptr;

    impl->hooks_.remove(phase, id);
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
