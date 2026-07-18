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
#include <utility>

#include "httpserver/webserver.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

// webserver_add_hook.cpp -- the typed webserver::add_hook overload family
// and webserver::make_hook_handle_. Each overload delegates registration
// (phase-mismatch + empty-callable validation, slot-id alloc, push under
// lock, gate arming) to detail::hook_bus::add and wraps the returned
// slot_id in an armed hook_handle.
//
// Carved out of src/webserver.cpp to keep both translation
// units under the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh).

// Tiny static helper that materialises an armed hook_handle.
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
    return make_hook_handle_(impl_.get(), hook_phase::connection_opened,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const accept_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::accept_decision,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(request_received_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::request_received,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(body_chunk_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::body_chunk,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const route_resolved_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::route_resolved,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(before_handler_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::before_handler,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(const handler_exception_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::handler_exception,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(after_handler_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::after_handler,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const response_sent_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::response_sent,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const request_completed_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::request_completed,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const connection_close_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::connection_closed,
        impl_->hooks_.add(phase, std::move(fn)));
}

}  // namespace httpserver
