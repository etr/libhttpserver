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

// webserver PIMPL backing class.
//
// This header is *internal*. It is reachable only when compiling the
// libhttpserver translation units themselves (HTTPSERVER_COMPILATION
// is supplied through src/Makefile.am AM_CPPFLAGS). It is NOT included
// from the public umbrella <httpserver.hpp>, so the gate is the strict
// one-mode form, not the dual-mode form used by other detail headers.
//
// Members below are accessed from src/webserver.cpp; cppcheck analyses
// each TU in isolation and cannot see the uses, so the unusedStructMember
// check must be suppressed at the file level.
// cppcheck-suppress-file unusedStructMember
#if !defined(HTTPSERVER_COMPILATION)
#error "webserver_impl.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_
#define SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_

#include <microhttpd.h>
#include <pthread.h>
#include <stdarg.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif  // HAVE_GNUTLS

#include "httpserver/file_info.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/detail/connection_state.hpp"
#include "httpserver/detail/daemon_lifecycle.hpp"
#include "httpserver/detail/error_pages.hpp"
#include "httpserver/detail/hook_bus.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/ip_access_control.hpp"
#include "httpserver/detail/route_table.hpp"
#include "httpserver/detail/ws_registry.hpp"

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

class webserver;
class http_resource;
class http_response;
// Forward declaration is unconditional to match the public webserver.hpp
// surface. The class body and member functions remain
// conditionally compiled; only the declaration is always present.
class websocket_handler;

namespace detail {

struct modded_request;
class lambda_resource;

// connection_state lives in its own header to keep webserver_impl.hpp
// under the project per-file LOC ceiling. See
// httpserver/detail/connection_state.hpp for documentation and
// rationale.

/**
 * @brief Whether the runtime opt-in for raw-body debug dumping is in
 *        effect for this process.
 *
 * Reads the env var LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY once on
 * first call via a function-local static; subsequent setenv() calls
 * are intentionally ignored. Returns true iff the variable is set to
 * any non-empty, non-`"0"` value. Default behaviour is silent on both
 * RELEASE and DEBUG builds.
 */
bool debug_dump_request_body_opted_in();

/**
 * @brief Emit a one-shot SECURITY WARNING when raw-body dumping is
 *        opted in.
 *
 * Called from webserver::start() before MHD_start_daemon. If the env
 * var opt-in is active, writes a single line naming the env var, the
 * SECURITY WARNING marker, and the credential / PII risk to stderr,
 * and (when wired) forwards the same line to the owning webserver's
 * log_error callback. Process-wide idempotent: multiple webservers in
 * the same process produce exactly one stderr emission.
 *
 * @param parent  Owning webserver pointer (used to find log_error).
 *                May be nullptr; the stderr emission still fires.
 */
void maybe_warn_debug_dump_request_body(const webserver* parent);

// webserver_impl: backing object holding all backend-coupled state of
// `webserver` (MHD daemon, mutexes, deny/allow IP sets, route table,
// route cache, websocket registry, optional GnuTLS SNI cache) plus the
// dispatch helpers and MHD trampolines that operate on those.
//
// Members are deliberately public: webserver and the free-function MHD
// callbacks all need direct access. The boundary that matters is between
// the public header and this internal class -- not between webserver and
// its own impl.
class webserver_impl {
 public:
    // `bind_socket_val` is the caller-supplied pre-bound socket from
    // create_webserver().bind_socket(), or MHD_INVALID_SOCKET if none was
    // provided. Initialised here so the impl is fully constructed from the
    // parent's member-initialiser list with no post-construction mutations.
    explicit webserver_impl(webserver* parent,
                            MHD_socket bind_socket_val = MHD_INVALID_SOCKET);
    ~webserver_impl();
    webserver_impl(const webserver_impl&) = delete;
    webserver_impl& operator=(const webserver_impl&) = delete;
    webserver_impl(webserver_impl&&) = delete;
    webserver_impl& operator=(webserver_impl&&) = delete;

    // Back-pointer used by the dispatch helpers to read the const config
    // bag still living on `webserver` (port, max_threads, certs, etc.).
    // Set in the constructor to the owning webserver.
    webserver* parent = nullptr;

    // MHD daemon handle + start/stop threading state + the daemon-
    // construction builders (MHD option array + start-flag composers) live
    // behind this collaborator. webserver::start/stop/is_running/
    // get_bound_port/run/... (webserver_lifecycle.cpp) drive it via
    // impl_->daemon_.{daemon,running,mutexwait,mutexcond,bind_socket} and
    // daemon_.build_mhd_option_array / compose_start_flags. Constructed with
    // a back-pointer to this impl so the builders can read parent config +
    // the ws registry. Its pthread primitives are RAII (ctor init / dtor
    // destroy).
    daemon_lifecycle daemon_;

#ifdef HAVE_DAUTH
    // Per-webserver-instance `opaque` value handed to
    // MHD_queue_auth_required_response3 when the user's
    // digest_challenge factory leaves the field empty. Generated once
    // at construction from std::random_device (16 bytes hex-encoded ->
    // 32 chars). Single-writer-at-construction, lock-free read on the
    // dispatch hot path: same posture as handler_exception_alias_ /
    // log_access_alias_ below. RFC 7616 §5.10: opaque is an identifier,
    // not a secret nor a replay token; reuse is allowed.
    std::string digest_opaque_;
#endif  // HAVE_DAUTH

    // v2 3-tier route table + LRU cache collaborator. Owns
    // route_table_mutex_, the three tiers (exact_routes_,
    // param_and_prefix_routes_, regex_routes_) and route_lru_cache. The
    // dispatch hot path resolves through it (resolve_resource_for_request
    // -> routes_.lookup_v2); on_*/route + register/unregister mutate it
    // (register_v2_route / the lock_for_write() + upsert primitives). The
    // lambda_resource shim-creation POLICY for on_*/route stays on this
    // class (prepare_or_create_lambda_shim / commit_handlers_to_shim); the
    // orchestration holds routes_.lock_for_write() across the probe +
    // mutation. Its mutex is independent of every other cluster's; no call
    // site holds two of them at once.
    route_table routes_;

    // tier_hit / lookup_result moved into route_table; these aliases keep
    // the many white-box tests that name webserver_impl::tier_hit /
    // ::lookup_result compiling unchanged.
    using tier_hit = route_table::tier_hit;
    using lookup_result = route_table::lookup_result;

    // Thin forwarders to the collaborator so the dispatch call sites and
    // the white-box tests read `lookup_v2(...)` / `invalidate_route_cache()`
    // on webserver_impl unchanged.
    lookup_result lookup_v2(http_method method, const std::string& path) {
        return routes_.lookup_v2(method, path);
    }
    void invalidate_route_cache() { routes_.invalidate_route_cache(); }

    // Lifecycle hook bus. Owns the eleven server-wide phase vectors, the
    // shared registration mutex, the advisory any_hooks_ gate array, and
    // the handler_exception / log_access alias slots. webserver::add_hook
    // registers into it (hooks_.add); hook_handle::remove erases
    // (hooks_.remove); the fire_* / has_hooks_for / phase_hook_count
    // forwarders below delegate to it, binding log_dispatch_error as the
    // per-call error logger. Its mutex is independent of every other
    // cluster's; no call site holds two of them at once.
    hook_bus hooks_;

    // Thin forwarder to the collaborator (see hook_bus::has_hooks_for).
    // Kept on webserver_impl so the many gate-check call sites and the
    // webserver_test_access bridge read `has_hooks_for(hook_phase::X)`
    // unchanged.
    bool has_hooks_for(::httpserver::hook_phase p) const noexcept {
        return hooks_.has_hooks_for(p);
    }

    // IP allow/deny access control (deny/allow sets + their mutexes) lives
    // behind this collaborator. policy_callback consults it via classify();
    // webserver::{deny,allow,remove_denied,remove_allowed}_ip mutate it. No
    // other state cluster on webserver_impl shares these mutexes.
    ip_access_control acl_;

#ifdef HAVE_WEBSOCKET
    // WebSocket handler registry (URL -> handler map + its mutex) lives
    // behind this collaborator. register/unregister_ws_resource mutate it;
    // complete_websocket_upgrade resolves a handler via find() (taking a
    // shared_ptr copy that keeps the handler alive across the MHD upgrade
    // callback even if unregister races mid-upgrade); start() consults
    // empty() for MHD_ALLOW_UPGRADE. Its mutex is independent of every
    // other cluster's; no call site holds two of them at once.
    ws_registry ws_;

    struct ws_upgrade_data {
        webserver_impl* impl;
        std::shared_ptr<::httpserver::websocket_handler> handler;
    };

    static void upgrade_handler(void *cls, struct MHD_Connection* connection,
                                void *req_cls, const char *extra_in,
                                size_t extra_in_size, MHD_socket sock,
                                struct MHD_UpgradeResponseHandle *urh);
#endif  // HAVE_WEBSOCKET

#if defined(HAVE_GNUTLS) && defined(MHD_OPTION_HTTPS_CERT_CALLBACK)
    mutable std::map<std::string, gnutls_certificate_credentials_t>
        sni_credentials_cache;
    mutable std::shared_mutex sni_credentials_mutex;
#endif  // HAVE_GNUTLS && MHD_OPTION_HTTPS_CERT_CALLBACK

    // Per-phase registration count. Thin forwarder to the collaborator
    // (see hook_bus::phase_hook_count). Kept on webserver_impl so the
    // HTTPSERVER_COMPILATION friend bridge in webserver.hpp
    // (webserver_test_access) reaches it as impl->phase_hook_count(p)
    // unchanged.
    [[nodiscard]] std::size_t phase_hook_count(
            ::httpserver::hook_phase p) const noexcept {
        return hooks_.phase_hook_count(p);
    }

    // Behavior service (DR-014 §4.11): synthesises 404/405/500 responses.
    // A leaf — reads only the const config bag (via parent->config, bound
    // at construction) and mr->request. The webserver_impl error-page
    // methods (not_found_page / internal_error_page / ...) forward here.
    error_pages errors_;

    // Dispatch helpers, start helpers, MHD trampolines, and the route /
    // upload sub-types live in a sibling header to keep this class
    // definition under the project per-file LOC ceiling. The inner gate
    // forces the header to be included only from within this class body.
#define SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_INSIDE_CLASS_
#include "httpserver/detail/webserver_impl_dispatch.hpp"
#undef SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_INSIDE_CLASS_
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_
