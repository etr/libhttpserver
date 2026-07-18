/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// Lifecycle MHD callbacks (connection_notify, policy_callback)
// plus the sockaddr-to-peer_address adapter. Extracted out of
// webserver_callbacks.cpp because adding the
// firing-site code pushed that TU past the project FILE_LOC_MAX gate
// (scripts/check-file-size.sh).
//
// Scope:
//   - connection_notify: MHD per-connection STARTED/CLOSED callback;
//     owns the per-connection arena AND fires the
//     `connection_opened` / `connection_closed` hooks.
//   - policy_callback: MHD accept-policy callback; computes the
//     accept/reject decision (deny + allow IP lists) and fires the
//     `accept_decision` hook AFTER the decision is fixed.
//   - anonymous-namespace `make_peer_address` adapter: converts MHD's
//     `const struct sockaddr*` into the public `peer_address` POD.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <winsock2.h>
#include <ws2tcpip.h>
#define _WINDOWS
#else
#if defined(__CYGWIN__)
#include <sys/select.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include <microhttpd.h>

#include <cstring>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <tuple>
#include <utility>

#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/detail/connection_state.hpp"
#include "httpserver/detail/modded_request.hpp"

namespace httpserver {

using httpserver::http::http_utils;
using httpserver::http::ip_representation;

namespace {

// Convert MHD's POSIX sockaddr (AF_INET / AF_INET6) into the
// libhttpserver-defined peer_address POD that the hook public API
// exposes. Keeps every <sys/socket.h> reference inside this TU so the
// public hook surface stays MHD-clean.
//
// On AF_INET, the four address bytes go into bytes[0..3] (the rest stay
// zero); on AF_INET6, all sixteen bytes are copied. `port` is stored in
// host byte order (ntohs). A null sockaddr (defensive — MHD does not
// document this case but the lookup_v2 / connection_info paths can
// return nullptr in edge cases) yields a zeroed peer_address with
// family::unspec so the hook still observes the event with a
// meaningful "unknown peer" signal.
::httpserver::peer_address make_peer_address(const struct sockaddr* addr) {
    ::httpserver::peer_address out{};
    if (addr == nullptr) return out;
    if (addr->sa_family == AF_INET) {
        const auto* sin = reinterpret_cast<const struct sockaddr_in*>(addr);
        out.fam = ::httpserver::peer_address::family::ipv4;
        std::memcpy(out.bytes.data(), &sin->sin_addr, 4);
        out.port = ntohs(sin->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const struct sockaddr_in6*>(addr);
        out.fam = ::httpserver::peer_address::family::ipv6;
        std::memcpy(out.bytes.data(), &sin6->sin6_addr, 16);
        out.port = ntohs(sin6->sin6_port);
    }
    return out;
}

// Reduce a {default_policy, is_denied, is_allowed} triple to the
// (accepted, reason) decision exposed via accept_ctx. Extracted out of
// policy_callback to keep that function under the CCN gate.
//
// Truth table:
//   default=ACCEPT, denied, !allowed -> reject "denied"
//   default=REJECT, denied           -> reject "denied"
//   default=REJECT, !denied, !allowed-> reject "not-on-allow-list"
//   anything else                    -> accept
// An allow-list entry always overrides a deny-list entry (allow wins).
std::pair<bool, std::optional<std::string_view>>
classify_decision(int default_policy, bool is_denied, bool is_allowed) {
    if (default_policy == ::httpserver::http::http_utils::ACCEPT
            && is_denied && !is_allowed) {
        return {false, std::string_view{"denied"}};
    }
    if (default_policy == ::httpserver::http::http_utils::REJECT) {
        if (is_denied) return {false, std::string_view{"denied"}};
        if (!is_allowed) return {false, std::string_view{"not-on-allow-list"}};
    }
    return {true, std::nullopt};
}

// Short-circuit gate for a single phase: returns true when at least one
// hook is registered. Shared by connection_notify (via the hooks_armed
// lambda which delegates to this) and policy_callback (which calls it
// directly since the hooks_armed lambda is local to connection_notify).
// Uses relaxed memory order: a false-negative on a very early concurrent
// add is acceptable; the hook simply fires on the next event.
inline bool is_phase_armed(const detail::webserver_impl* impl,
                           ::httpserver::hook_phase p) noexcept {
    return impl != nullptr && impl->has_hooks_for(p);
}

}  // namespace

namespace detail {

void webserver_impl::connection_notify(void* cls, struct MHD_Connection* connection,
                                       void** socket_context,
                                       enum MHD_ConnectionNotificationCode toe) {
    // cls is the owning webserver* (set in webserver_lifecycle.cpp at
    // MHD_OPTION_NOTIFY_CONNECTION). It MAY be null in tests that
    // exercise the callback without an enclosing webserver; defensive
    // null-check gates every hook fire on a non-null impl.
    auto* ws = static_cast<webserver*>(cls);
    // `ws_impl` names the owning webserver's impl explicitly, distinguishing
    // it from the static trampoline's implicit `this` (which is the
    // webserver_impl on which connection_notify is declared, but the
    // trampoline pattern means `this` is never used here directly).
    webserver_impl* ws_impl = (ws != nullptr) ? ws->impl_.get() : nullptr;

    // Resolve the peer address from MHD via the live MHD_Connection*.
    // The connection is valid in both the STARTED and CLOSED branches
    // (MHD tears it down only after NOTIFY_CLOSED returns).
    auto resolve_peer = [&]() -> ::httpserver::peer_address {
        if (connection == nullptr) return {};
        const MHD_ConnectionInfo* ci = MHD_get_connection_info(
            connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
        if (ci == nullptr) return {};
        return make_peer_address(ci->client_addr);
    };

    // Short-circuit predicate: delegates to the file-scope is_phase_armed
    // free function so that both connection_notify and policy_callback use
    // the same idiom without duplicating the cast/load expression.
    auto hooks_armed = [&ws_impl](::httpserver::hook_phase p) -> bool {
        return is_phase_armed(ws_impl, p);
    };

    switch (toe) {
        case MHD_CONNECTION_NOTIFY_STARTED: {
            // Allocate the per-connection state (and its embedded arena)
            // on connection start. The new is the only heap allocation
            // tied to a connection's lifetime; afterwards every request
            // on this connection draws its impl out of the arena.
            auto* cs = new detail::connection_state();
            // Copy the per-request args DoS limits from the owning
            // webserver so populate_args() can size the
            // arguments_accumulator from the socket_context. 0 means
            // "use the compile-time defaults" -- see connection_state.hpp.
            if (ws != nullptr) {
                cs->max_args_count = ws->config.max_args_count;
                cs->max_args_bytes = ws->config.max_args_bytes;
            }
            *socket_context = cs;
            // Fire connection_opened. Zero-cost when no hook is
            // registered: a single relaxed atomic load + branch.
            if (hooks_armed(::httpserver::hook_phase::connection_opened)) {
                ::httpserver::connection_open_ctx ctx{resolve_peer()};
                ws_impl->fire_connection_opened(ctx);
            }
            break;
        }
        case MHD_CONNECTION_NOTIFY_CLOSED:
            // Fire connection_closed BEFORE the per-connection state is
            // deleted. The arena is not exposed through
            // connection_close_ctx today (only peer_address), but the
            // ordering choice is safe regardless and pins the contract.
            if (hooks_armed(::httpserver::hook_phase::connection_closed)) {
                ::httpserver::connection_close_ctx ctx{resolve_peer()};
                ws_impl->fire_connection_closed(ctx);
            }
            // MHD ordering guarantee: NOTIFY_COMPLETED fires before
            // NOTIFY_CLOSED for the same connection. By the time we reach
            // this branch, request_completed has already called reset_arena()
            // and the modded_request has already been deleted -- so the
            // connection_state is no longer referenced by any live object.
            // (Documents the invariant that prevents the concurrent
            // request_completed + NOTIFY_CLOSED race described in CWE-362.)
            delete static_cast<detail::connection_state*>(*socket_context);
            *socket_context = nullptr;
            break;
    }
}

MHD_Result webserver_impl::policy_callback(void *cls, const struct sockaddr* addr, socklen_t addrlen) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = addrlen;

    const auto ws = static_cast<webserver*>(cls);
    auto* impl = ws->impl_.get();

    // Compute the accept/reject decision (and its reason) up front,
    // then fire `accept_decision` strictly AFTER the decision is fixed
    // in `decision`. A throwing hook lands in fire_accept_decision's
    // catch and cannot change `decision` — this is a structural
    // guarantee, not a runtime check.
    bool accepted = true;
    std::optional<std::string_view> reason{};

    if (ws->config.ip_access_control_enabled) {
        // Consistent deny/allow snapshot under the acl's own locks, released
        // before the user hook fires.
        const auto membership = impl->acl_.classify(ip_representation(addr));
        std::tie(accepted, reason) =
            classify_decision(ws->config.default_policy,
                              membership.denied, membership.allowed);
    }

    const MHD_Result decision = accepted ? MHD_YES : MHD_NO;

    // Fire the hook strictly after `decision` is fixed. The relaxed
    // atomic gate keeps zero-cost-when-unused.
    if (is_phase_armed(impl, ::httpserver::hook_phase::accept_decision)) {
        ::httpserver::accept_ctx ctx{
            make_peer_address(addr), accepted, reason};
        impl->fire_accept_decision(ctx);
    }

    return decision;
}

}  // namespace detail
}  // namespace httpserver
