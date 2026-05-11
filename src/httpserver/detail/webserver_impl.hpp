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
#include <cstddef>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
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

#include "httpserver/http_utils.hpp"
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

// connection_state: per-MHD_Connection arena anchor.
//
// Owns a std::pmr::monotonic_buffer_resource over an embedded initial
// buffer. The arena is allocated once per MHD connection (in
// webserver_impl::connection_notify on MHD_CONNECTION_NOTIFY_STARTED)
// and torn down on MHD_CONNECTION_NOTIFY_CLOSED. Between requests on a
// keep-alive connection, request_completed calls arena_.release() to
// rewind the bump pointer, so a second request reuses the same memory.
//
// Lifetime contract for views returned by http_request getters: they
// remain valid until the request-completion callback fires for the
// request they were derived from. Capturing them past the user
// handler's return is undefined behavior. (See architecture doc
// 04-components/http-request.md.)
//
// Initial-buffer sizing math (8 KiB):
//   - sizeof(http_request_impl) ~= 600-700 B with libstdc++/libc++
//     map/string layouts.
//   - A typical small GET populates ~1.5 KiB across header_view_map,
//     querystring, requestor_ip; a small POST with a few args ~2.5 KiB.
//   - Each std::pmr::map node (unescaped_args) is ~64-96 B on
//     libstdc++/libc++, so 5 headers/args already consume ~400-500 B
//     in tree nodes alone. 4 KiB was undersized for realistic requests
//     with moderate arg counts; 8 KiB matches the test's own generous
//     buffer and covers the common case without overflow to the upstream
//     heap. (performance-reviewer-iter1-1.)
//   - Overflow spills to the upstream resource (default = heap) silently
//     -- it is a correctness fall-through, not a hard limit.
//   - TODO(M5): expose ARENA_INITIAL_BYTES via create_webserver if/when
//     profiling shows tuning value.
struct connection_state {
    static constexpr std::size_t ARENA_INITIAL_BYTES = 8192;

    // The buffer aliases storage for any PMR-aware object the arena
    // hands out, so it must satisfy the strictest fundamental alignment.
    alignas(std::max_align_t) std::array<std::byte, ARENA_INITIAL_BYTES> initial_buffer_{};

    // upstream defaults to new_delete_resource (= get_default_resource).
    // We pass it explicitly to make the contract obvious in source.
    std::pmr::monotonic_buffer_resource arena_{
        initial_buffer_.data(), initial_buffer_.size(),
        std::pmr::new_delete_resource()};

    connection_state() = default;
    connection_state(const connection_state&) = delete;
    connection_state& operator=(const connection_state&) = delete;
    connection_state(connection_state&&) = delete;
    connection_state& operator=(connection_state&&) = delete;

    // reset_arena(): release the bump pointer AND zero the initial buffer.
    //
    // The plain arena_.release() rewinds the bump pointer so the next
    // request reuses the same memory, but it does NOT clear the reclaimed
    // bytes. Credentials (username, password, digested_user) written into
    // the arena by a previous request would therefore linger in the buffer
    // until overwritten by the next request's lazy-cache population.
    // Explicit zeroing after release() closes that residual-credential
    // window. (security-reviewer-iter1-3, CWE-226.)
    //
    // Using std::memset here (rather than explicit_bzero / SecureZeroMemory)
    // is acceptable because the buffer is accessed again immediately by the
    // next request's arena allocation, preventing the compiler from
    // optimising the clear away as a dead store.
    void reset_arena() noexcept {
        arena_.release();
        std::memset(initial_buffer_.data(), 0, ARENA_INITIAL_BYTES);
    }
};

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
    explicit webserver_impl(webserver* parent);
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
    MHD_socket bind_socket = 0;

    pthread_mutex_t mutexwait;
    pthread_cond_t  mutexcond;

    bool running = false;

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
    std::mutex route_cache_mutex;
    std::list<std::pair<std::string, route_cache_entry>> route_cache_list;
    std::unordered_map<std::string,
        std::list<std::pair<std::string, route_cache_entry>>::iterator>
        route_cache_map;

    // TASK-027: 3-tier route table (architecture §4.7).
    //
    // The three tiers below shadow the v1 maps above and are populated
    // alongside them by every register_* / on_methods_ / unregister_*
    // path. The lookup_v2() entry point walks this triple in order
    //   1. exact_routes_           (hash, O(1))
    //   2. param_and_prefix_routes_ (segment-trie, parameterized + prefix)
    //   3. regex_routes_            (linear regex chain, fallback)
    // fronted by the LRU cache `route_cache_v2`.
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
    lookup_result lookup_v2(http_method method, const std::string& path);

    std::shared_mutex bans_mutex;
    std::set<http::ip_representation> bans;

    std::shared_mutex allowances_mutex;
    std::set<http::ip_representation> allowances;

#ifdef HAVE_WEBSOCKET
    std::map<std::string, ::httpserver::websocket_handler*> registered_ws_handlers;

    struct ws_upgrade_data {
        webserver_impl* impl;
        ::httpserver::websocket_handler* handler;
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

    // Dispatch helpers (formerly methods on webserver). Each of these
    // touches both backend state on this impl and const config on the
    // owning webserver (via `parent`).
    std::shared_ptr<::httpserver::http_response> not_found_page(modded_request* mr) const;
    std::shared_ptr<::httpserver::http_response> method_not_allowed_page(modded_request* mr) const;
    // TASK-031: error-propagation entry point. @p msg carries the
    // originating exception's text (e.what() for std::exception, the
    // sentinel "unknown exception" for non-std throws, or a fixed
    // internal-diagnostic string for the few non-handler-throw call
    // sites that synthesise a 500 with no exception in flight).
    //
    // Behaviour matches DR-009:
    //   - parent->internal_error_handler set, !force_our:
    //       invoke it with (*mr->dhr, msg) and return the result.
    //   - force_our=true: return a hardcoded 500 with an EMPTY body
    //       (the "double-fault" fallback used when the user handler
    //       itself threw).
    //   - otherwise (no handler set, !force_our): return a default
    //       500 whose body surfaces @p msg, so the unset-handler
    //       default is informative.
    //
    // Throws nothing on its own; if parent->internal_error_handler
    // throws, that exception propagates to the caller. Callers in the
    // handler-throw path use run_internal_error_handler_safely() to
    // contain that double-fault.
    std::shared_ptr<::httpserver::http_response> internal_error_page(
        modded_request* mr,
        std::string_view msg,
        bool force_our = false) const;

    // TASK-031: log @p msg via parent->log_error if a logger is configured.
    // Swallows any exception thrown by the logger -- dispatch must never
    // re-enter the catch from inside its own catch.
    void log_dispatch_error(std::string_view msg) const;

    // TASK-031: invoke the user-supplied internal_error_handler safely.
    // On success, returns the response it produced. If the user handler
    // itself throws, logs generically via log_dispatch_error and returns
    // a hardcoded empty-body 500.
    std::shared_ptr<::httpserver::http_response>
        run_internal_error_handler_safely(modded_request* mr,
                                          std::string_view msg) const;
    bool should_skip_auth(const std::string& path) const;
    void invalidate_route_cache();

    MHD_Result requests_answer_first_step(MHD_Connection* connection, modded_request* mr);
    MHD_Result requests_answer_second_step(MHD_Connection* connection,
            const char* method, const char* version, const char* upload_data,
            size_t* upload_data_size, modded_request* mr);
    // TASK-021: the wire-string `method` parameter was dropped because
    // finalize_answer now consults mr->method_enum (set once by
    // answer_to_connection) for the is_allowed check.
    MHD_Result finalize_answer(MHD_Connection* connection, modded_request* mr);
    MHD_Result complete_request(MHD_Connection* connection, modded_request* mr,
                                const char* version, const char* method);
    struct MHD_Response* get_raw_response_with_fallback(modded_request* mr);

    static struct MHD_Response* materialize_response(::httpserver::http_response* resp);
    static void decorate_mhd_response(struct MHD_Response* response,
                                      const ::httpserver::http_response& resp);

    // MHD trampolines registered with libmicrohttpd. Closure pointer is
    // `this` (webserver_impl*) for answer_to_connection, otherwise the
    // owning `webserver*` (so callbacks can read the const config bag).
    static void request_completed(void* cls, struct MHD_Connection* connection,
                                  void** con_cls, enum MHD_RequestTerminationCode toe);
    // Per-connection lifetime callback. cls is unused (nullptr).
    // socket_context is MHD's per-connection void* slot: we new/delete a
    // detail::connection_state through it on STARTED/CLOSED, so the
    // arena lives exactly as long as the MHD_Connection does.
    static void connection_notify(void* cls, struct MHD_Connection* connection,
                                  void** socket_context,
                                  enum MHD_ConnectionNotificationCode toe);
    static MHD_Result answer_to_connection(void* cls, MHD_Connection* connection,
            const char* url, const char* method, const char* version,
            const char* upload_data, size_t* upload_data_size, void** con_cls);
    static MHD_Result post_iterator(void* cls, enum MHD_ValueKind kind,
            const char* key, const char* filename, const char* content_type,
            const char* transfer_encoding, const char* data, uint64_t off,
            size_t size);

    // Auxiliary MHD callbacks (formerly free functions in webserver.cpp).
    // Each takes `cls = webserver*` so it can read the const config bag.
    static MHD_Result policy_callback(void* cls, const struct sockaddr* addr,
                                      socklen_t addrlen);
    static void error_log(void* cls, const char* fmt, va_list ap);
    static void* uri_log(void* cls, const char* uri,
                         struct MHD_Connection* con);
    static void access_log(::httpserver::webserver* cls, const std::string& uri);
    static size_t unescaper_func(void* cls, struct MHD_Connection* c, char* s);

#ifdef HAVE_GNUTLS
    static int psk_cred_handler_func(void* cls, struct MHD_Connection* connection,
                                     const char* username, void** psk,
                                     size_t* psk_size);
#ifdef MHD_OPTION_HTTPS_CERT_CALLBACK
    static int sni_cert_callback_func(void* cls, struct MHD_Connection* connection,
                                      const char* server_name,
                                      gnutls_certificate_credentials_t* creds);
#endif  // MHD_OPTION_HTTPS_CERT_CALLBACK
#endif  // HAVE_GNUTLS
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_
