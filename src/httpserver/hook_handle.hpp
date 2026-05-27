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

#ifndef SRC_HTTPSERVER_HOOK_HANDLE_HPP_
#define SRC_HTTPSERVER_HOOK_HANDLE_HPP_

#include <cstdint>
#include <memory>
#include <utility>

#include "httpserver/hook_phase.hpp"

/**
 * @file hook_handle.hpp
 * @brief Move-only RAII receipt returned by webserver / http_resource
 *        `add_hook` calls.
 *
 * TASK-045 / §5.6 / DR-012. The PIMPL backing classes are forward-
 * declared so this header carries no libmicrohttpd / pthread / gnutls
 * baggage.
 */
namespace httpserver {

namespace detail { class webserver_impl; }
// TASK-051: per-resource hook table -- the per-route handle holds a
// weak_ptr<resource_hook_table>, expired when the resource is
// destroyed (remove() then becomes a safe no-op).
namespace detail { class resource_hook_table; }
class webserver;
class http_resource;

/**
 * @brief Move-only RAII receipt for a hook registration.
 *
 * Returned by `webserver::add_hook` (server-wide) or
 * `http_resource::add_hook` (per-route). Destruction or explicit
 * `remove()` re-takes the hook table's writer lock and erases the
 * registration. `detach()` disarms the destructor so the registration
 * persists for the webserver's lifetime.
 *
 * Back-reference: `(phase, slot_id, impl_ or table_weak_)` where
 * `slot_id` is a monotonically-increasing 64-bit counter. Reallocation
 * of the phase vector cannot invalidate this handle: `remove()` walks
 * the vector linearly looking for `slot_id`; a not-found result is
 * the idempotent no-op path.
 */
class hook_handle {
 public:
    /**
     * @brief Default-construct a disarmed handle.
     *
     * `remove()` is a no-op and the destructor does not touch any
     * impl. Three paths converge on this state: default ctor,
     * post-`remove()`, and post-move-from.
     */
    hook_handle() noexcept = default;

    /// hook_handle is non-copyable.
    hook_handle(const hook_handle&) = delete;
    /// hook_handle is non-copy-assignable.
    hook_handle& operator=(const hook_handle&) = delete;
    /// Move-construct from @p other, leaving it disarmed.
    hook_handle(hook_handle&& other) noexcept;
    /// Move-assign from @p other, leaving it disarmed.
    hook_handle& operator=(hook_handle&& other) noexcept;

    /**
     * @brief Destructor.
     *
     * Calls `remove()` unless the handle has been `detach()`-ed,
     * moved-from, or already `remove()`-ed.
     */
    ~hook_handle();

    /**
     * @brief Erase the registration this handle refers to.
     *
     * Idempotent: a second call on the same handle is a no-op. After
     * this call the handle is disarmed (the destructor will not touch
     * the impl). Safe to call on a default-constructed or moved-from
     * handle.
     */
    void remove() noexcept;

    /**
     * @brief Disarm the destructor.
     *
     * The underlying registration persists for the webserver's
     * lifetime. The source handle is left in the disarmed state.
     * Conventionally invoked as `auto h2 = std::move(h).detach();`.
     *
     * @return a new handle that owns the registration (or a disarmed
     *         handle if the source was already disarmed).
     */
    hook_handle detach() && noexcept;

 private:
    // Constructor used by webserver::add_hook to build an armed handle
    // bound to the server-wide hook table.
    hook_handle(detail::webserver_impl* impl,
                hook_phase phase,
                std::uint64_t slot_id) noexcept
        : impl_(impl), slot_id_(slot_id), phase_(phase), armed_(true) {}

    // TASK-051: constructor used by http_resource::add_hook to build an
    // armed handle bound to a per-resource hook table. Discriminator is
    // the non-empty weak_ptr -- remove() inspects table_weak_ first, and
    // falls through to the server-wide impl_ path only if the weak_ptr
    // is empty (default-constructed = "never had a table" sentinel).
    //
    // The handle holds a weak_ptr so a resource that outlives the handle
    // is not kept alive by the handle (the handle is non-owning), and a
    // resource destroyed before the handle expires the weak_ptr cleanly
    // (lock() returns null, remove() is a no-op).
    hook_handle(std::weak_ptr<detail::resource_hook_table> table_weak,
                hook_phase phase,
                std::uint64_t slot_id) noexcept
        : slot_id_(slot_id), phase_(phase), armed_(true),
          table_weak_(std::move(table_weak)) {}

    detail::webserver_impl* impl_ = nullptr;  // non-owning back-ref
    std::uint64_t slot_id_ = 0;
    hook_phase phase_ = hook_phase::count_;
    bool armed_ = false;
    // Non-empty (has-a-control-block) iff this handle is for a per-route
    // hook on an http_resource. Empty otherwise.
    std::weak_ptr<detail::resource_hook_table> table_weak_{};

    friend class ::httpserver::webserver;
    friend class ::httpserver::http_resource;
    friend class ::httpserver::detail::webserver_impl;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HOOK_HANDLE_HPP_
