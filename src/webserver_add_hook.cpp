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

#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "httpserver/webserver.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

// webserver_add_hook.cpp -- the typed webserver::add_hook overload family,
// webserver::make_hook_handle_, and their shared register_hook_impl helper.
//
// Carved out of src/webserver.cpp in TASK-086 to keep both translation
// units under the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh). No behaviour change: the bodies are moved
// verbatim.

namespace {

// register_hook_impl: helper used by every add_hook overload. Lives in
// an anonymous namespace; it would normally call hook_handle's private
// constructor through webserver's friendship, but webserver's friend
// status doesn't extend to a free function -- so we route the
// hook_handle creation through a tiny webserver static helper that
// IS a member and IS a friend of hook_handle (transitively). See
// webserver::make_hook_handle_ below.
template <class Vec, class Fn>
::httpserver::hook_handle register_hook_impl(
        ::httpserver::detail::webserver_impl* impl,
        ::httpserver::hook_phase requested,
        ::httpserver::hook_phase expected,
        Vec& vec,
        Fn fn) {
    if (requested != expected) {
        throw std::invalid_argument(
            std::string("hook phase mismatch: add_hook overload for ")
            + std::string(::httpserver::to_string(expected))
            + " received phase tag "
            + std::string(::httpserver::to_string(requested)));
    }
    if (!fn) {
        throw std::invalid_argument("hook callable must not be empty");
    }
    const std::uint64_t id =
        impl->next_slot_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::unique_lock lock(impl->hook_table_mutex_);
        vec.push_back({id, std::move(fn)});
        // Store under the unique_lock. memory_order_release here is
        // technically redundant because the subsequent mutex unlock also
        // acts as a release fence. It is kept for clarity, but dispatch
        // hot-path readers MUST use memory_order_acquire on the load, and
        // they pair with the MUTEX RELEASE (unlock), not this store. See
        // webserver_impl.hpp comment on any_hooks_ for the full pairing
        // rationale.
        impl->any_hooks_[static_cast<std::size_t>(expected)].store(
            true, std::memory_order_release);
    }
    return ::httpserver::webserver::make_hook_handle_(impl, expected, id);
}

}  // namespace

// TASK-045: tiny static helper that materialises an armed hook_handle.
// hook_handle's constructor is private but webserver is friend; this
// static gives the anonymous-namespace register_hook_impl a way to
// reach into that private surface without making it a friend itself.
hook_handle webserver::make_hook_handle_(detail::webserver_impl* impl,
                                         hook_phase phase,
                                         std::uint64_t slot_id) noexcept {
    return hook_handle{impl, phase, slot_id};
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const connection_open_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::connection_opened,
        impl_->hooks_connection_opened_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const accept_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::accept_decision,
        impl_->hooks_accept_decision_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(request_received_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::request_received,
        impl_->hooks_request_received_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(body_chunk_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::body_chunk,
        impl_->hooks_body_chunk_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const route_resolved_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::route_resolved,
        impl_->hooks_route_resolved_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(before_handler_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::before_handler,
        impl_->hooks_before_handler_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(const handler_exception_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::handler_exception,
        impl_->hooks_handler_exception_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(after_handler_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::after_handler,
        impl_->hooks_after_handler_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const response_sent_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::response_sent,
        impl_->hooks_response_sent_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const request_completed_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::request_completed,
        impl_->hooks_request_completed_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const connection_close_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::connection_closed,
        impl_->hooks_connection_closed_, std::move(fn));
}

}  // namespace httpserver
