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
#include <utility>

#include "httpserver/hook_phase.hpp"

namespace httpserver {

// Forward-declare the PIMPL backing class so this header carries no
// libmicrohttpd / pthread / gnutls baggage. The complete type is
// required only at hook_handle's out-of-line definitions, which live
// in src/hook_handle.cpp where detail/webserver_impl.hpp is in scope.
namespace detail { class webserver_impl; }
class webserver;

// TASK-045 / §5.6.
//
// hook_handle is the move-only RAII receipt returned by
// webserver::add_hook. Destruction (or explicit remove()) re-takes the
// hook table's writer lock and erases the registration. detach()
// disarms the destructor so the registration persists for the
// webserver's lifetime.
//
// Back-reference: (phase, slot_id, impl_) where slot_id is a
// monotonically-increasing 64-bit counter stored alongside each entry.
// Reallocation of the phase vector cannot invalidate this handle: the
// remove() path walks the vector linearly looking for `slot_id`, and a
// not-found result is the idempotent no-op path.
class hook_handle {
 public:
    // Default-constructed handle is in the "disarmed" state: remove()
    // is a no-op and the destructor does not touch any impl. Three
    // paths converge on this state -- default ctor, post-remove(),
    // post-move-from.
    hook_handle() noexcept = default;

    // Non-copyable, move-only with nothrow moves.
    hook_handle(const hook_handle&) = delete;
    hook_handle& operator=(const hook_handle&) = delete;
    hook_handle(hook_handle&& other) noexcept;
    hook_handle& operator=(hook_handle&& other) noexcept;

    // Destructor calls remove() unless the handle has been detach()-ed,
    // moved-from, or already remove()-ed.
    ~hook_handle();

    // Erase the registration this handle refers to. Idempotent: a
    // second call on the same handle is a no-op. After this call the
    // handle is disarmed (the dtor will not touch the impl).
    void remove() noexcept;

    // Disarm the destructor: the underlying registration persists for
    // the webserver's lifetime. Returns *this by value (move) so the
    // call chain reads as `auto h2 = std::move(h).detach();`. The
    // source handle is left in the disarmed state.
    hook_handle detach() && noexcept;

 private:
    // Constructor used by webserver::add_hook to build an armed handle.
    // Private so user code can only obtain a hook_handle through the
    // public add_hook entry points.
    hook_handle(detail::webserver_impl* impl,
                hook_phase phase,
                std::uint64_t slot_id) noexcept
        : impl_(impl), slot_id_(slot_id), phase_(phase), armed_(true) {}

    detail::webserver_impl* impl_ = nullptr;  // non-owning back-ref
    std::uint64_t slot_id_ = 0;
    hook_phase phase_ = hook_phase::count_;
    bool armed_ = false;

    friend class ::httpserver::webserver;
    friend class ::httpserver::detail::webserver_impl;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HOOK_HANDLE_HPP_
