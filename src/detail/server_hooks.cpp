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

#include <httpserver/server/hooks.hpp>

namespace httpserver {

namespace server {

namespace detail {

class hook_bus_impl {
 public:
    using erased = concurrency::unique_function<hook_action(void*)>;

    // One registration: the identity key and the type-erased invoke.
    // Shared ownership is what makes snapshot-copy firing possible
    // (the erased callable is move-only, the snapshot copies slots).
    struct registration {
        std::uint64_t key;
        std::shared_ptr<erased> call;
    };

    std::uint64_t add(std::uint8_t phase, erased call) {
        std::unique_lock<std::shared_mutex> lock(mu_);
        const std::uint64_t key = next_key_++;
        slots_[phase].push_back(
            registration{key, std::make_shared<erased>(std::move(call))});
        any_[phase].store(true, std::memory_order_release);
        return key;
    }

    void remove(std::uint8_t phase, std::uint64_t key) noexcept {
        std::unique_lock<std::shared_mutex> lock(mu_);
        std::vector<registration>& slots = slots_[phase];
        for (std::vector<registration>::iterator it = slots.begin();
             it != slots.end(); ++it) {
            if (it->key != key) continue;
            slots.erase(it);
            break;
        }
        if (slots.empty()) {
            any_[phase].store(false, std::memory_order_release);
        }
    }

    bool any(std::uint8_t phase) const noexcept {
        return any_[phase].load(std::memory_order_acquire);
    }

    hook_action fire(std::uint8_t phase, void* ctx) {
        std::vector<std::shared_ptr<erased>> snapshot;
        {
            std::shared_lock<std::shared_mutex> lock(mu_);
            const std::vector<registration>& slots = slots_[phase];
            if (slots.empty()) return hook_action::pass();
            snapshot.reserve(slots.size());
            for (const registration& slot : slots) {
                snapshot.push_back(slot.call);
            }
        }
        for (const std::shared_ptr<erased>& call : snapshot) {
            hook_action outcome;
            try {
                outcome = (*call)(ctx);
            } catch (...) {
                // A throwing hook is treated as pass() and the chain
                // continues (the v2 rule; the logging surface for the
                // swallowed diagnostic is a deferred milestone item).
                continue;
            }
            if (!outcome.is_pass()) return outcome;
        }
        return hook_action::pass();
    }

 private:
    static constexpr std::size_t k_phases =
        static_cast<std::size_t>(hook_phase::count_);

    std::array<std::vector<registration>, k_phases> slots_;
    std::array<std::atomic<bool>, k_phases> any_{};
    std::shared_mutex mu_;
    std::uint64_t next_key_ = 1;  // 0 is the disarmed-handle sentinel
};

std::uint64_t hook_bus_add(
        const std::shared_ptr<hook_bus_impl>& owner, std::uint8_t phase,
        concurrency::unique_function<hook_action(void*)> call) {
    return owner->add(phase, std::move(call));
}

void hook_bus_remove(const std::shared_ptr<hook_bus_impl>& owner,
                     std::uint8_t phase, std::uint64_t key) noexcept {
    owner->remove(phase, key);
}

bool hook_bus_any(const std::shared_ptr<hook_bus_impl>& owner,
                  std::uint8_t phase) noexcept {
    return owner->any(phase);
}

hook_action hook_bus_fire(const std::shared_ptr<hook_bus_impl>& owner,
                          std::uint8_t phase, void* ctx) {
    return owner->fire(phase, ctx);
}

}  // namespace detail

hook_handle::hook_handle(std::uint64_t key, std::uint8_t phase,
                         std::weak_ptr<detail::hook_bus_impl> owner) noexcept
    : key_(key), phase_(phase), owner_(std::move(owner)) { }

hook_handle::~hook_handle() { remove(); }

void hook_handle::remove() noexcept {
    if (key_ == 0) return;
    if (const std::shared_ptr<detail::hook_bus_impl> live = owner_.lock()) {
        detail::hook_bus_remove(live, phase_, key_);
    }
    key_ = 0;
}

void hook_handle::detach() noexcept { key_ = 0; }

hook_bus::hook_bus()
    : impl_(std::make_shared<detail::hook_bus_impl>()) { }

hook_bus::~hook_bus() = default;

hook_bus::hook_bus(hook_bus&&) noexcept = default;

hook_bus& hook_bus::operator=(hook_bus&&) noexcept = default;

}  // namespace server

}  // namespace httpserver
