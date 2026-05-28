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

// TASK-014: webserver PIMPL backing class.
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
#include "httpserver/detail/radix_tree.hpp"
#include "httpserver/detail/route_cache.hpp"
#include "httpserver/detail/route_entry.hpp"

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

class webserver;
class http_resource;
class http_response;
#ifdef HAVE_WEBSOCKET
class websocket_handler;
#endif  // HAVE_WEBSOCKET

namespace detail {

struct modded_request;
class lambda_resource;

// connection_state lives in its own header to keep webserver_impl.hpp
// under the project per-file LOC ceiling. See
// httpserver/detail/connection_state.hpp for documentation and
// rationale.

// webserver_impl: backing object holding all backend-coupled state of
// `webserver` (MHD daemon, mutexes, ban/allowance sets, route table,
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

    struct MHD_Daemon* daemon = nullptr;
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

    // --- v1 route table (legacy; to be removed once lookup_v2 is wired
    //     into finalize_answer at Cycle K dispatch cutover) ---------------

    // TASK-023: route-table entries hold shared_ptr<http_resource>.
    // The two public register_resource overloads (unique_ptr / shared_ptr)
    // both funnel into the shared_ptr branch, so storage is always
    // shared_ptr regardless of which overload the caller used. This also
    // closes a latent dispatch race: a thread holding a shared_ptr copy
    // for the duration of an in-flight call cannot be invalidated by a
    // concurrent unregister_resource — the resource lives until the call
    // returns and its shared_ptr drops.
    std::shared_mutex registered_resources_mutex;
    std::map<detail::http_endpoint,
             std::shared_ptr<::httpserver::http_resource>> registered_resources;
    std::map<std::string,
             std::shared_ptr<::httpserver::http_resource>> registered_resources_str;
    std::map<detail::http_endpoint,
             std::shared_ptr<::httpserver::http_resource>> registered_resources_regex;

    struct route_cache_entry {
        detail::http_endpoint matched_endpoint;
        std::shared_ptr<::httpserver::http_resource> resource;
    };
    static constexpr size_t ROUTE_CACHE_MAX_SIZE = 256;
    std::mutex route_cache_mutex_;
    std::list<std::pair<std::string, route_cache_entry>> route_cache_list;
    std::unordered_map<std::string,
        std::list<std::pair<std::string, route_cache_entry>>::iterator>
        route_cache_map;

    // --- v2 3-tier route table (TASK-027, architecture §4.7) -------------

    // TASK-027: 3-tier route table.
    //
    // The three tiers below shadow the v1 maps above and are populated
    // alongside them by every register_* / on_methods_ / unregister_*
    // path. The lookup_v2() entry point walks this triple in order:
    //   1. exact_routes_           (hash, O(1))
    //   2. param_and_prefix_routes_ (segment-trie, parameterized + prefix)
    //   3. regex_routes_            (linear regex chain, fallback)
    // fronted by the LRU cache `route_cache_v2`.
    //
    // TODO(Cycle K): rename route_cache_v2 → route_lru_cache once the v1
    // fields above are removed at dispatch cutover.
    //
    // **Lock order.** When both locks must be held, route_table_mutex_
    // is acquired BEFORE route_cache_mutex_. The lookup pipeline never
    // holds both at once: it takes a brief shared_lock on the table to
    // walk the tiers, releases it, then takes route_cache_mutex_ to
    // promote the hit. Registration takes a unique_lock on the table,
    // releases it, then clears the cache.
    std::shared_mutex route_table_mutex_;
    std::unordered_map<std::string, route_entry> exact_routes_;
    radix_tree<route_entry> param_and_prefix_routes_;
    // Pre-compiled regex objects. Architecture §4.7 specifies this as a
    // vector of (compiled std::regex, route_entry) pairs so that lookup_v2
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

    // TASK-027 LRU front-end. 256 entries per architecture spec.
    route_cache route_cache_v2{ROUTE_CACHE_MAX_SIZE};

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
    // NOTE (CWE-284 guard): lookup_v2 is a SHADOW TABLE only. The live
    // HTTP dispatch path (finalize_answer -> resolve_resource_for_request)
    // still uses the v1 registered_resources* maps. lookup_v2 is used
    // by tests to pin the v2 tier pipeline and will be wired into
    // finalize_answer at Cycle K dispatch cutover. Until then, any access
    // control registered via the v2 table has NO effect on live traffic.
    lookup_result lookup_v2(http_method method, const std::string& path);

    // TASK-045 -- Lifecycle hook bus (skeleton, no firing yet).
    //
    // Per-phase callable storage. Each phase has its own
    // std::vector<phase_entry<Sig>> because phase signatures differ
    // (some return void, some return hook_action; ctx types differ).
    // Concurrency:
    //   - `hook_table_mutex_` is a shared_mutex covering ALL eleven
    //     phase vectors. Writers (add_hook, hook_handle::remove) take
    //     a unique_lock; future firing sites (TASK-046+) will take
    //     a shared_lock to snapshot a phase vector before iterating.
    //   - `any_hooks_[i]` is a short-circuit gate read with relaxed/
    //     acquire semantics on the dispatch hot path so a phase with
    //     no registrations costs one atomic load. Set on first
    //     registration of a phase and cleared when the phase's vector
    //     drops to empty (memory_order_release on both edges).
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
    // TASK-049: internal_error_handler alias slot. Last-position fallback
    // in the handler_exception chain. Distinct from hooks_handler_exception_
    // because it must fire AFTER all user hooks (DR-012 §4.10) -- the
    // opposite of the TASK-048 aliases which run before user hooks. By
    // sitting in a dedicated slot rather than the vector, the alias never
    // contends for ordering when a user does add_hook(handler_exception, ...).
    //
    // Lifetime: written exactly once during install_default_alias_hooks_()
    // at webserver construction, before start() is called -- the daemon is
    // not yet running, so no synchronisation is required for the write.
    // Read on the dispatch hot path from fire_handler_exception with no
    // lock (single-writer-before-readers contract, same as
    // parent->internal_error_handler). If a future task adds a runtime
    // setter the writer MUST take hook_table_mutex_ exclusively and the
    // reader MUST take it shared -- same pattern as the per-phase vectors.
    std::function<::httpserver::hook_action(
            const ::httpserver::handler_exception_ctx&)>
        handler_exception_alias_;
    std::vector<phase_entry<::httpserver::hook_action(
            ::httpserver::after_handler_ctx&)>>
        hooks_after_handler_;
    std::vector<phase_entry<void(const ::httpserver::response_sent_ctx&)>>
        hooks_response_sent_;
    // TASK-050: log_access alias slot. Mirrors handler_exception_alias_
    // (TASK-049): single-writer-at-construction, read on the dispatch hot
    // path from fire_response_sent without a lock. webserver's ctor wires
    // this slot from create_webserver().log_access(fn) if the user
    // supplied a non-null callable.
    //
    // The slot fires AFTER user-added response_sent hooks so user hooks
    // observe the response before the legacy access logger formats it.
    //
    // A future runtime setter that allows re-registration MUST take
    // hook_table_mutex_ exclusively for the write, and the reader in
    // fire_response_sent MUST take it shared. At v2.0 there is no runtime
    // setter -- the slot is written exactly once at webserver construction
    // before start() is called.
    std::function<void(const ::httpserver::response_sent_ctx&)>
        log_access_alias_;
    std::vector<phase_entry<void(const ::httpserver::request_completed_ctx&)>>
        hooks_request_completed_;
    std::vector<phase_entry<void(const ::httpserver::connection_close_ctx&)>>
        hooks_connection_closed_;

    std::shared_mutex bans_mutex;
    std::set<http::ip_representation> bans;

    std::shared_mutex allowances_mutex;
    std::set<http::ip_representation> allowances;

#ifdef HAVE_WEBSOCKET
    // TASK-035: shared_ptr storage mirrors the registered_resources map
    // type and lets the dispatch path (finalize_answer) take a shared_ptr
    // copy under the shared lock that keeps the handler alive across an
    // MHD upgrade callback, even if unregister_ws_resource races to drop
    // the registration mid-upgrade. The webserver always holds one
    // reference until the slot is erased.
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
