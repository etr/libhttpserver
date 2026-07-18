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
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/ip_access_control.hpp"
#include "httpserver/detail/segment_trie.hpp"
#include "httpserver/detail/route_cache.hpp"
#include "httpserver/detail/route_entry.hpp"

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
    // WebSocket dispatch uses a dedicated registered_ws_handlers_mutex_.
    lookup_result lookup_v2(http_method method, const std::string& path);

    // Lifecycle hook bus.
    //
    // Per-phase callable storage. Each phase has its own
    // std::vector<phase_entry<Sig>> because phase signatures differ
    // (some return void, some return hook_action; ctx types differ).
    // Concurrency:
    //   - `hook_table_mutex_` is a shared_mutex covering ALL eleven
    //     phase vectors. Writers (add_hook, hook_handle::remove) take
    //     a unique_lock; firing sites take a shared_lock to snapshot a
    //     phase vector before iterating.
    //   - `any_hooks_[i]` is an ADVISORY short-circuit gate so that a
    //     phase with no registrations costs one atomic load on the
    //     dispatch hot path:
    //       if (has_hooks_for(phase)) {   // relaxed load
    //           std::shared_lock lk(hook_table_mutex_); /* snapshot */ }
    //     The relaxed load is sufficient because no reader ever touches
    //     a phase vector without first acquiring hook_table_mutex_; the
    //     mutex, not the atomic, provides the happens-before for the
    //     vector contents. The gate may be momentarily stale in either
    //     direction and both races are benign: a stale false skips
    //     hooks whose registration ran concurrently with the firing,
    //     and a stale true costs one shared_lock acquisition that finds
    //     the vector empty. The gate is set on first registration of a
    //     phase and cleared when the phase's vector drops to empty,
    //     always under the unique_lock (the release ordering on those
    //     stores is redundant with the mutex unlock; it carries no
    //     extra guarantee).
    //   - `next_slot_id_` is a monotonic 64-bit counter. It is never
    //     reused, so a hook_handle whose slot has already been erased
    //     simply finds no match in remove() -- the idempotent no-op
    //     path. 64 bits is unboundedly large in practice (centuries
    //     at any realistic registration rate).
    template <class Sig>
    struct phase_entry {
        std::uint64_t slot_id;
        std::function<Sig> fn;
    };

    std::shared_mutex hook_table_mutex_;
    std::atomic<std::uint64_t> next_slot_id_{1};
    std::array<std::atomic<bool>,
               static_cast<std::size_t>(hook_phase::count_)> any_hooks_{};

    // Returns true iff at least one hook is registered for phase @p p.
    // Relaxed load is sufficient -- see the any_hooks_ contract above.
    // Encapsulates the cast so call sites read as
    // `has_hooks_for(hook_phase::X)` instead of the raw index expression.
    bool has_hooks_for(::httpserver::hook_phase p) const noexcept {
        return any_hooks_[static_cast<std::size_t>(p)].load(
            std::memory_order_relaxed);
    }

    std::vector<phase_entry<void(const ::httpserver::connection_open_ctx&)>>
        hooks_connection_opened_;
    std::vector<phase_entry<void(const ::httpserver::accept_ctx&)>>
        hooks_accept_decision_;
    std::vector<phase_entry<::httpserver::hook_action(
            ::httpserver::request_received_ctx&)>>
        hooks_request_received_;
    std::vector<phase_entry<::httpserver::hook_action(
            ::httpserver::body_chunk_ctx&)>>
        hooks_body_chunk_;
    std::vector<phase_entry<void(const ::httpserver::route_resolved_ctx&)>>
        hooks_route_resolved_;
    std::vector<phase_entry<::httpserver::hook_action(
            ::httpserver::before_handler_ctx&)>>
        hooks_before_handler_;
    std::vector<phase_entry<::httpserver::hook_action(
            const ::httpserver::handler_exception_ctx&)>>
        hooks_handler_exception_;
    // internal_error_handler alias slot. Last-position fallback in the
    // handler_exception chain. Distinct from hooks_handler_exception_
    // because it must fire AFTER all user hooks -- the opposite of the
    // functional first-position aliases which run before user hooks. By
    // sitting in a dedicated slot rather than the vector, the alias never
    // contends for ordering when a user does add_hook(handler_exception, ...).
    //
    // Lifetime: written exactly once during install_default_alias_hooks_()
    // at webserver construction, before start() is called -- the daemon is
    // not yet running, so no synchronisation is required for the write.
    // Read on the dispatch hot path from fire_handler_exception with no
    // lock. Slot is immutable after construction; the reader path
    // requires no synchronisation.
    std::function<::httpserver::hook_action(
            const ::httpserver::handler_exception_ctx&)>
        handler_exception_alias_;
    std::vector<phase_entry<::httpserver::hook_action(
            ::httpserver::after_handler_ctx&)>>
        hooks_after_handler_;
    std::vector<phase_entry<void(const ::httpserver::response_sent_ctx&)>>
        hooks_response_sent_;
    // log_access alias slot. Mirrors handler_exception_alias_:
    // single-writer-at-construction, read on the dispatch hot
    // path from fire_response_sent without a lock. webserver's ctor wires
    // this slot from create_webserver().log_access(fn) if the user
    // supplied a non-null callable.
    //
    // The slot fires AFTER user-added response_sent hooks so user hooks
    // observe the response before the legacy access logger formats it.
    //
    // Slot is immutable after construction; the reader path requires no
    // synchronisation.
    std::function<void(const ::httpserver::response_sent_ctx&)>
        log_access_alias_;
    std::vector<phase_entry<void(const ::httpserver::request_completed_ctx&)>>
        hooks_request_completed_;
    std::vector<phase_entry<void(const ::httpserver::connection_close_ctx&)>>
        hooks_connection_closed_;

    // IP allow/deny access control (deny/allow sets + their mutexes) lives
    // behind this collaborator. policy_callback consults it via classify();
    // webserver::{deny,allow,remove_denied,remove_allowed}_ip mutate it. No
    // other state cluster on webserver_impl shares these mutexes.
    ip_access_control acl_;

#ifdef HAVE_WEBSOCKET
    // shared_ptr storage. The dispatch path
    // (complete_websocket_upgrade) takes a shared_ptr copy under the
    // shared lock that keeps the handler alive across an MHD upgrade
    // callback, even if unregister_ws_resource races to drop the
    // registration mid-upgrade. The webserver always holds one reference
    // until the slot is erased.
    //
    // Lock: registered_ws_handlers_mutex_ guards this map exclusively,
    // independent of the HTTP route_table_mutex_; no call site ever
    // holds both mutexes simultaneously.
    std::shared_mutex registered_ws_handlers_mutex_;
    std::map<std::string, std::shared_ptr<::httpserver::websocket_handler>>
        registered_ws_handlers;

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

    // Single accessor for the per-phase vector size.
    // Exposes the per-phase registration count through one switch rather
    // than requiring callers (e.g., test code) to name individual per-phase
    // vector members. The HTTPSERVER_COMPILATION friend bridge in
    // webserver.hpp (webserver_test_access) gives test TUs access to impl*.
    [[nodiscard]] std::size_t phase_hook_count(
            ::httpserver::hook_phase p) const noexcept {
        // Split the per-phase fanout in two so each helper stays under the
        // project-wide CCN gate. Lifecycle-side phases (start through
        // route_resolved) route through phase_hook_count_lifecycle_;
        // handler-side phases (before_handler onwards) route through
        // phase_hook_count_handler_.
        if (static_cast<int>(p) <=
                static_cast<int>(::httpserver::hook_phase::route_resolved)) {
            return phase_hook_count_lifecycle_(p);
        }
        return phase_hook_count_handler_(p);
    }

 private:
    [[nodiscard]] std::size_t phase_hook_count_lifecycle_(
            ::httpserver::hook_phase p) const noexcept {
        switch (p) {
        case ::httpserver::hook_phase::connection_opened:
            return hooks_connection_opened_.size();
        case ::httpserver::hook_phase::accept_decision:
            return hooks_accept_decision_.size();
        case ::httpserver::hook_phase::request_received:
            return hooks_request_received_.size();
        case ::httpserver::hook_phase::body_chunk:
            return hooks_body_chunk_.size();
        case ::httpserver::hook_phase::route_resolved:
            return hooks_route_resolved_.size();
        default:
            return 0;
        }
    }
    [[nodiscard]] std::size_t phase_hook_count_handler_(
            ::httpserver::hook_phase p) const noexcept {
        switch (p) {
        case ::httpserver::hook_phase::before_handler:
            return hooks_before_handler_.size();
        case ::httpserver::hook_phase::handler_exception:
            return hooks_handler_exception_.size();
        case ::httpserver::hook_phase::after_handler:
            return hooks_after_handler_.size();
        case ::httpserver::hook_phase::response_sent:
            return hooks_response_sent_.size();
        case ::httpserver::hook_phase::request_completed:
            return hooks_request_completed_.size();
        case ::httpserver::hook_phase::connection_closed:
            return hooks_connection_closed_.size();
        default:
            return 0;
        }
    }

 public:
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
