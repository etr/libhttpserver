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
#include "httpserver/detail/hook_bus.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/ip_access_control.hpp"
#include "httpserver/detail/segment_trie.hpp"
#include "httpserver/detail/route_cache.hpp"
#include "httpserver/detail/route_entry.hpp"
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

    // Atomic so start() publishes the daemon pointer (and the immutable
    // MHD daemon struct it points at, including the ephemeral bind port set
    // before publication) with release semantics, and get_bound_port() et al.
    // read it with acquire. This lets the ephemeral port be read safely from
    // another thread while a blocking start() runs on a worker thread; fixes
    // a TSan-flagged data race in the ws_start_stop integ test.
    std::atomic<struct MHD_Daemon*> daemon{nullptr};
    // MHD_socket (int on POSIX, SOCKET on Windows) for a caller-supplied
    // pre-bound socket passed via create_webserver().bind_socket().
    // MHD_INVALID_SOCKET (-1 on POSIX, INVALID_SOCKET on Windows) is the
    // sentinel meaning "no pre-bound socket was provided".
    MHD_socket bind_socket = MHD_INVALID_SOCKET;

    pthread_mutex_t mutexwait;
    pthread_cond_t  mutexcond;

    // Atomic to allow lock-free reads in stop()/is_running() concurrent
    // with the mutex-guarded writes in start()/stop(). TSan-flagged in the
    // ws_start_stop integ test (start on worker thread, stop on main).
    std::atomic<bool> running{false};

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

    // LRU cache size: 256 entries.
    static constexpr size_t ROUTE_CACHE_MAX_SIZE = 256;

    // --- v2 3-tier route table -------------------------------------------

    // The 3-tier route table is fronted by `route_lru_cache`.
    // Tier walk order and rationale live in lookup_v2's implementation
    // comment (src/detail/webserver_dispatch.cpp); not duplicated here.
    //
    // **Lock order.** When both locks must be held, route_table_mutex_
    // is acquired BEFORE the cache's internal mutex. The lookup
    // pipeline never holds both at once: it takes a brief shared_lock
    // on the table to walk the tiers, releases it, then promotes/inserts
    // into the LRU cache. Registration takes a unique_lock on the table,
    // releases it, then clears the cache.
    //
    // **CWE-407 hash-flooding immunity.** exact_routes_ uses std::map
    // (not std::unordered_map) so the keyed lookup on the dispatch hot
    // path is hash-free. Same posture as the segment-trie per-segment
    // child container. std::less<> enables transparent
    // string_view lookup without constructing a temporary std::string.
    std::shared_mutex route_table_mutex_;
    std::map<std::string, route_entry, std::less<>> exact_routes_;
    segment_trie<route_entry> param_and_prefix_routes_;
    // Pre-compiled regex objects: a vector of (compiled std::regex,
    // route_entry) pairs so that lookup_v2
    // calls std::regex_match on an already-compiled object without paying
    // the compilation cost on every cache miss. Compiled at registration
    // time in register_impl_ / on_methods_ when idx.is_regex_compiled()
    // is true, the path has no {name} wildcard segments (url_pars empty),
    // and the literal url_complete does NOT match its own compiled regex
    // (i.e., the path has meaningful regex metacharacters).
    //
    // url_complete is stored alongside the compiled regex to support
    // O(n) removal in unregister_impl_ without a second map.
    struct regex_route {
        std::string url_complete;
        std::regex compiled_re;
        route_entry entry;
    };
    std::vector<regex_route> regex_routes_;

    // LRU front-end for the route table.
    route_cache route_lru_cache{ROUTE_CACHE_MAX_SIZE};

    // tier_hit identifies which tier of the v2 route table answered a
    // lookup. Returned alongside the route_entry copy from lookup_v2()
    // so the dispatch site (and tests) can pin the lookup pipeline.
    enum class tier_hit {
        none,
        cache,
        exact,
        radix,
        regex
    };

    struct lookup_result {
        bool found = false;
        tier_hit tier = tier_hit::none;
        route_entry entry{};
        std::vector<std::pair<std::string, std::string>> captured_params;
    };

    // Walk the v2 route table for (method, path) per the lookup pipeline
    // documented above. Returns lookup_result; populates `tier` even on
    // miss (tier_hit::none) so callers can branch deterministically.
    //
    // NOTE: lookup_v2 is the only dispatch path --
    // resolve_resource_for_request() calls it exclusively, and the
    // 3-tier table is the single routing surface. Lambda/class conflict
    // detection probes the same tiers (find_v2_entry_by_path_);
    // WebSocket dispatch resolves handlers through the separate ws_ registry.
    lookup_result lookup_v2(http_method method, const std::string& path);

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
