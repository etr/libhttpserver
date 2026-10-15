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

// TASK-118 step 2: the hook bus storage and the hook_handle lifetime
// (plan D4). Per-phase vectors behind one shared mutex, a per-phase
// atomic emptiness gate (the zero-cost-when-unused contract), and
// snapshot-copy firing: fire() copies the phase's shared_ptr slots
// under the shared lock and invokes them outside it, so a hook may add
// or remove registrations mid-fire without disturbing the running pass
// and without deadlocking (add/remove take the unique lock; fire
// holds only the shared lock while copying).

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <utility>
#include <vector>

#include <httpserver/detail/hook_bus.hpp>
#include <httpserver/server/hooks.hpp>

namespace httpserver {

namespace server {

hook_handle::hook_handle(std::uint64_t key, std::uint8_t phase, std::weak_ptr<detail::hook_bus_impl> owner) noexcept : key_(key), phase_(phase), owner_(std::move(owner)) {}

hook_handle::~hook_handle() { remove(); }

void hook_handle::remove() noexcept {
    if (key_ == 0) return;
    if (const std::shared_ptr<detail::hook_bus_impl> live = owner_.lock()) {
        detail::hook_bus_remove(live, phase_, key_);
    }
    key_ = 0;
}

void hook_handle::detach() noexcept { key_ = 0; }

hook_bus::hook_bus() : impl_(detail::hook_bus_create()) {}

hook_bus::~hook_bus() = default;

hook_bus::hook_bus(hook_bus&&) noexcept = default;

hook_bus& hook_bus::operator=(hook_bus&&) noexcept = default;

}  // namespace server

}  // namespace httpserver

/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */

namespace httpserver::server {

auto hook_action::respond_with(hook_response r) noexcept -> hook_action {
    hook_action a;
    a.response_.emplace(std::move(r));
    return a;
}

auto hook_action::take_response() && noexcept -> hook_response&& {
    assert(response_.has_value() && "take_response() called on a pass-action");
    return std::move(*response_);
}

}  // namespace httpserver::server
