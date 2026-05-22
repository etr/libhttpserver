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

// TASK-045 -- Out-of-line bodies for hook_handle and peer_address.
//
// hook_handle's destructor + remove() + move ops need the complete
// type of detail::webserver_impl (the per-phase vectors live there),
// so the bodies live here, not in the public header. Same pattern as
// http_response's move/dtor.

#include "httpserver/hook_handle.hpp"

#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

#include "httpserver/hook_context.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

// ---- hook_handle ---------------------------------------------------------

hook_handle::hook_handle(hook_handle&& other) noexcept
    : impl_(other.impl_),
      slot_id_(other.slot_id_),
      phase_(other.phase_),
      armed_(other.armed_) {
    // Disarm the source so its destructor is a no-op.
    other.armed_ = false;
    other.impl_ = nullptr;
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
        other.armed_ = false;
        other.impl_ = nullptr;
    }
    return *this;
}

hook_handle::~hook_handle() {
    if (armed_) {
        remove();
    }
}

void hook_handle::remove() noexcept {
    if (!armed_ || impl_ == nullptr) {
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
    // remove() or never inserted (defensive). Either way we still
    // re-evaluate the any_hooks_ gate against the current vector
    // emptiness.
    auto erase_if_found = [id](auto& vec) -> bool {
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it->slot_id == id) {
                vec.erase(it);
                return true;
            }
        }
        return false;
    };
    auto reset_gate_if_empty = [impl, phase](const auto& vec) {
        if (vec.empty()) {
            impl->any_hooks_[static_cast<std::size_t>(phase)].store(
                false, std::memory_order_release);
        }
    };

    switch (phase) {
    case hook_phase::connection_opened:
        erase_if_found(impl->hooks_connection_opened_);
        reset_gate_if_empty(impl->hooks_connection_opened_);
        break;
    case hook_phase::accept_decision:
        erase_if_found(impl->hooks_accept_decision_);
        reset_gate_if_empty(impl->hooks_accept_decision_);
        break;
    case hook_phase::request_received:
        erase_if_found(impl->hooks_request_received_);
        reset_gate_if_empty(impl->hooks_request_received_);
        break;
    case hook_phase::body_chunk:
        erase_if_found(impl->hooks_body_chunk_);
        reset_gate_if_empty(impl->hooks_body_chunk_);
        break;
    case hook_phase::route_resolved:
        erase_if_found(impl->hooks_route_resolved_);
        reset_gate_if_empty(impl->hooks_route_resolved_);
        break;
    case hook_phase::before_handler:
        erase_if_found(impl->hooks_before_handler_);
        reset_gate_if_empty(impl->hooks_before_handler_);
        break;
    case hook_phase::handler_exception:
        erase_if_found(impl->hooks_handler_exception_);
        reset_gate_if_empty(impl->hooks_handler_exception_);
        break;
    case hook_phase::after_handler:
        erase_if_found(impl->hooks_after_handler_);
        reset_gate_if_empty(impl->hooks_after_handler_);
        break;
    case hook_phase::response_sent:
        erase_if_found(impl->hooks_response_sent_);
        reset_gate_if_empty(impl->hooks_response_sent_);
        break;
    case hook_phase::request_completed:
        erase_if_found(impl->hooks_request_completed_);
        reset_gate_if_empty(impl->hooks_request_completed_);
        break;
    case hook_phase::connection_closed:
        erase_if_found(impl->hooks_connection_closed_);
        reset_gate_if_empty(impl->hooks_connection_closed_);
        break;
    case hook_phase::count_:
        // Unreachable: an armed handle always carries a valid phase.
        break;
    }
}

hook_handle hook_handle::detach() && noexcept {
    // Construct a disarmed handle from the same data; do NOT call the
    // private armed-ctor because the spec says detach() leaves the
    // underlying registration in place (no impl interaction).
    hook_handle out;
    out.impl_ = impl_;
    out.slot_id_ = slot_id_;
    out.phase_ = phase_;
    out.armed_ = false;     // detach() = "destructor will not touch impl"
    // Disarm the source so its destructor is also a no-op.
    armed_ = false;
    impl_ = nullptr;
    return out;
}

// ---- peer_address::to_string ---------------------------------------------

std::string peer_address::to_string() const {
    // No <netinet/in.h> / inet_ntop here so we keep this TU free of
    // backend-platform headers. The format is canonical-enough for
    // log lines without dragging in the full POSIX socket surface.
    // 46 is POSIX INET6_ADDRSTRLEN; we round up for snprintf's NUL.
    char buf[64];
    switch (fam) {
    case family::ipv4:
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                      static_cast<unsigned>(bytes[0]),
                      static_cast<unsigned>(bytes[1]),
                      static_cast<unsigned>(bytes[2]),
                      static_cast<unsigned>(bytes[3]));
        return std::string{buf};
    case family::ipv6: {
        // Group as eight uint16_t big-endian words, colon-separated.
        // Skip zero-compression for simplicity at TASK-045; TASK-046
        // can refine when telemetry/log requirements firm up.
        std::snprintf(buf, sizeof(buf),
                      "%x:%x:%x:%x:%x:%x:%x:%x",
                      (bytes[0] << 8)  | bytes[1],
                      (bytes[2] << 8)  | bytes[3],
                      (bytes[4] << 8)  | bytes[5],
                      (bytes[6] << 8)  | bytes[7],
                      (bytes[8] << 8)  | bytes[9],
                      (bytes[10] << 8) | bytes[11],
                      (bytes[12] << 8) | bytes[13],
                      (bytes[14] << 8) | bytes[15]);
        return std::string{buf};
    }
    case family::unspec:
    default:
        return std::string{};
    }
}

}  // namespace httpserver
