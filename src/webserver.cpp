/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#include <errno.h>
#include <microhttpd.h>
// TASK-034: <microhttpd_ws.h> remains gated (only the upgrade trampolines
// need it). The public websocket_handler header is unconditional and is
// included below with the other project headers.
#ifdef HAVE_WEBSOCKET
#include <microhttpd_ws.h>
#endif  // HAVE_WEBSOCKET
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <algorithm>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/create_webserver.hpp"
// TASK-034: feature_unavailable + websocket_handler are public headers
// included unconditionally so the public surface is identical across
// HAVE_WEBSOCKET-on and HAVE_WEBSOCKET-off builds.
#include "httpserver/feature_unavailable.hpp"
#include "httpserver/websocket_handler.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/lambda_resource.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"
#include "httpserver/detail/body.hpp"

#define _REENTRANT 1

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

// TASK-019: pin httpserver::http::http_utils::cred_type_T values to the
// GnuTLS credentials enum. The cred_type_T enum body in
// src/httpserver/http_utils.hpp hard-codes the integer values rather
// than referencing GNUTLS_CRD_* (which would force the public header to
// drag <gnutls/gnutls.h> through the umbrella). This block is the
// compile-time guard that those hard-coded values stay in lockstep
// with the upstream definitions; an upstream renumber breaks the build
// here, where someone with full context can react.
static_assert(static_cast<int>(::httpserver::http::http_utils::CERTIFICATE) ==
              static_cast<int>(GNUTLS_CRD_CERTIFICATE),
              "cred_type_T::CERTIFICATE drifted from GNUTLS_CRD_CERTIFICATE");
static_assert(static_cast<int>(::httpserver::http::http_utils::ANON) ==
              static_cast<int>(GNUTLS_CRD_ANON),
              "cred_type_T::ANON drifted from GNUTLS_CRD_ANON");
static_assert(static_cast<int>(::httpserver::http::http_utils::SRP) ==
              static_cast<int>(GNUTLS_CRD_SRP),
              "cred_type_T::SRP drifted from GNUTLS_CRD_SRP");
static_assert(static_cast<int>(::httpserver::http::http_utils::PSK) ==
              static_cast<int>(GNUTLS_CRD_PSK),
              "cred_type_T::PSK drifted from GNUTLS_CRD_PSK");
static_assert(static_cast<int>(::httpserver::http::http_utils::IA) ==
              static_cast<int>(GNUTLS_CRD_IA),
              "cred_type_T::IA drifted from GNUTLS_CRD_IA");
#endif  // HAVE_GNUTLS

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif

using std::string;
using std::pair;
using std::vector;
using std::map;
using std::set;

using httpserver::http::http_utils;
using httpserver::http::ip_representation;
using httpserver::http::base_unescaper;

namespace httpserver {

#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
static void catcher(int) { }
#endif

static void ignore_sigpipe() {
// Mingw doesn't implement SIGPIPE
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
    struct sigaction oldsig;
    struct sigaction sig;

    sig.sa_handler = &catcher;
    sigemptyset(&sig.sa_mask);
#ifdef SA_INTERRUPT
    sig.sa_flags = SA_INTERRUPT;  /* SunOS */
#else  // SA_INTERRUPT
    sig.sa_flags = SA_RESTART;
#endif  // SA_INTERRUPTT
    if (0 != sigaction(SIGPIPE, &sig, &oldsig)) {
        fprintf(stderr, "Failed to install SIGPIPE handler: %s\n", strerror(errno));
    }
#endif
}

namespace detail {

// ----- webserver_impl construction / destruction -------------------------

webserver_impl::webserver_impl(webserver* parent_ptr) : parent(parent_ptr) {
    pthread_mutex_init(&mutexwait, nullptr);
    pthread_cond_init(&mutexcond, nullptr);
}

webserver_impl::~webserver_impl() {
    pthread_mutex_destroy(&mutexwait);
    pthread_cond_destroy(&mutexcond);

#if defined(HAVE_GNUTLS) && defined(MHD_OPTION_HTTPS_CERT_CALLBACK)
    // Clean up cached SNI credentials
    for (auto& [name, creds] : sni_credentials_cache) {
        gnutls_certificate_free_credentials(creds);
    }
    sni_credentials_cache.clear();
#endif  // HAVE_GNUTLS && MHD_OPTION_HTTPS_CERT_CALLBACK
}

}  // namespace detail

// ----- webserver construction / destruction ------------------------------

webserver::webserver(const create_webserver& params):
    port(params._port),
    start_method(params._start_method),
    max_threads(params._max_threads),
    max_connections(params._max_connections),
    memory_limit(params._memory_limit),
    content_size_limit(params._content_size_limit),
    connection_timeout(params._connection_timeout),
    per_IP_connection_limit(params._per_IP_connection_limit),
    log_access(params._log_access),
    log_error(params._log_error),
    validator(params._validator),
    unescaper(params._unescaper),
    bind_address(params._bind_address),
    bind_address_storage(params._bind_address_storage),
    max_thread_stack_size(params._max_thread_stack_size),
    use_ssl(params._use_ssl),
    use_ipv6(params._use_ipv6),
    use_dual_stack(params._use_dual_stack),
    debug(params._debug),
    pedantic(params._pedantic),
    https_mem_key(params._https_mem_key),
    https_mem_cert(params._https_mem_cert),
    https_mem_trust(params._https_mem_trust),
    https_priorities(params._https_priorities),
    cred_type(params._cred_type),
    psk_cred_handler(params._psk_cred_handler),
    digest_auth_random(params._digest_auth_random),
    nonce_nc_size(params._nonce_nc_size),
    default_policy(params._default_policy),
    // TASK-034: stored unconditionally; the ctor body below throws
    // feature_unavailable if this is true on a HAVE_BAUTH-off build.
    basic_auth_enabled(params._basic_auth_enabled),
    digest_auth_enabled(params._digest_auth_enabled),
    regex_checking(params._regex_checking),
    ban_system_enabled(params._ban_system_enabled),
    post_process_enabled(params._post_process_enabled),
    put_processed_data_to_content(params._put_processed_data_to_content),
    file_upload_target(params._file_upload_target),
    file_upload_dir(params._file_upload_dir),
    generate_random_filename_on_upload(params._generate_random_filename_on_upload),
    deferred_enabled(params._deferred_enabled),
    single_resource(params._single_resource),
    tcp_nodelay(params._tcp_nodelay),
    not_found_handler(params._not_found_handler),
    method_not_allowed_handler(params._method_not_allowed_handler),
    internal_error_handler(params._internal_error_handler),
    file_cleanup_callback(params._file_cleanup_callback),
    auth_handler(params._auth_handler),
    auth_skip_paths(params._auth_skip_paths),
    sni_callback(params._sni_callback),
    no_listen_socket(params._no_listen_socket),
    no_thread_safety(params._no_thread_safety),
    turbo(params._turbo),
    suppress_date_header(params._suppress_date_header),
    listen_backlog(params._listen_backlog),
    address_reuse(params._address_reuse),
    connection_memory_increment(params._connection_memory_increment),
    tcp_fastopen_queue_size(params._tcp_fastopen_queue_size),
    sigpipe_handled_by_app(params._sigpipe_handled_by_app),
    https_mem_dhparams(params._https_mem_dhparams),
    https_key_password(params._https_key_password),
    https_priorities_append(params._https_priorities_append),
    no_alpn(params._no_alpn),
    client_discipline_level(params._client_discipline_level),
    impl_(std::make_unique<detail::webserver_impl>(this)) {
        // TASK-034 §7: any feature the builder asked for that the
        // library was not compiled with must fail loudly here. Throwing
        // from the ctor body (after the member-initialiser list) lets
        // the just-constructed impl_ unique_ptr destroy itself cleanly
        // — no MHD daemon is running yet.
#ifndef HAVE_GNUTLS
        if (use_ssl) {
            throw feature_unavailable("tls", "HAVE_GNUTLS");
        }
#endif
#ifndef HAVE_BAUTH
        if (basic_auth_enabled) {
            throw feature_unavailable("basic_auth", "HAVE_BAUTH");
        }
#endif
#ifndef HAVE_DAUTH
        // security-reviewer-iter1-1 / CWE-287: symmetric guard for digest
        // auth. Without this a HAVE_DAUTH-off build silently accepts
        // digest_auth_enabled=true and the request handler returns
        // WRONG_HEADER, making the authentication gate a silent no-op.
        if (digest_auth_enabled) {
            throw feature_unavailable("digest_auth", "HAVE_DAUTH");
        }
#endif
        ignore_sigpipe();
        impl_->bind_socket = params._bind_socket;
}

// TASK-034: build-time feature reporting (PRD-FLG-REQ-003). The body
// lives in this TU rather than in the header so consumers see whatever
// HAVE_* the library was built with — not whatever HAVE_* their own TU
// happens to define.
//
// The struct-tag spelling `struct webserver::features` disambiguates the
// return type from the function name (both spelled `features`). The
// returned aggregate uses the same elaborated form for the same reason.
struct webserver::features webserver::features() noexcept {
    // The aggregate-init braces below are spelled without the type name
    // because `features` would name-collide with the surrounding member
    // function. `return {a,b,c,d};` uses the function's declared return
    // type (resolved via the elaborated-type-specifier above), avoiding
    // the ambiguity entirely.
    return {
#ifdef HAVE_BAUTH
        /*basic_auth =*/ true,
#else
        /*basic_auth =*/ false,
#endif
#ifdef HAVE_DAUTH
        /*digest_auth =*/ true,
#else
        /*digest_auth =*/ false,
#endif
#ifdef HAVE_GNUTLS
        /*tls =*/ true,
#else
        /*tls =*/ false,
#endif
#ifdef HAVE_WEBSOCKET
        /*websocket =*/ true,
#else
        /*websocket =*/ false,
#endif
    };
}

webserver::~webserver() {
    stop();
    // impl_'s destructor (running pthread destroys + GnuTLS cleanup) runs
    // when the unique_ptr is destroyed, after this body finishes.
}

void webserver::stop_and_wait() {
    stop();
}

// ----- Resource registration --------------------------------------------

// classify_route_tier: single source-of-truth for v2 tier placement.
//
// Given a non-prefix http_endpoint, returns which of three storage tiers
// owns the route and, for the regex tier, the already-compiled std::regex
// so callers need not compile it a second time.
//
// Tier rules (in priority order):
//   radix  — parameterized path (url_pars non-empty); no regex needed.
//   regex  — is_regex_compiled() true AND the literal url_complete does NOT
//             match the compiled pattern (true metacharacters present).
//   exact  — everything else: plain paths with regex_checking enabled
//             (literal matches its own pattern, so the fast hash tier
//             is equivalent), or regex_checking disabled entirely.
//
// Prefix routes (family == true) are NOT classified here; callers handle
// them before invoking this helper.
namespace {

enum class route_tier_kind { exact, radix, regex_ };

struct route_tier_result {
    route_tier_kind kind = route_tier_kind::exact;
    std::optional<std::regex> re;  // populated iff kind == regex_
};

static route_tier_result classify_route_tier(const detail::http_endpoint& idx) {
    route_tier_result res;

    if (!idx.get_url_pars().empty()) {
        res.kind = route_tier_kind::radix;
        return res;
    }

    if (idx.is_regex_compiled()) {
        // Compile the normalized pattern once and run the self-match
        // check. If the literal url_complete matches its own regex, the
        // pattern is trivially ^/literal$ and the exact hash tier is
        // faster and correct. Otherwise the path has meaningful regex
        // metacharacters and belongs in the regex tier.
        std::regex re(idx.get_url_normalized(),
                      std::regex::extended | std::regex::icase);
        if (std::regex_match(idx.get_url_complete(), re)) {
            res.kind = route_tier_kind::exact;
        } else {
            res.kind = route_tier_kind::regex_;
            res.re   = std::move(re);
        }
        return res;
    }

    res.kind = route_tier_kind::exact;
    return res;
}

}  // namespace

// TASK-024: register_path / register_prefix split. Both public methods
// funnel into this private helper, which carries the validation and
// insertion logic. Keeping the work in one place prevents drift between
// the two registration kinds.
void webserver::register_impl_(const std::string& resource,
                               std::shared_ptr<http_resource> res,
                               bool family) {
    if (res == nullptr) {
        throw std::invalid_argument("The http_resource pointer cannot be null");
    }

    if (single_resource && ((resource != "" && resource != "/") || !family)) {
        throw std::invalid_argument("The resource should be '' or '/' and be registered via register_prefix when using a single_resource server");
    }

    detail::http_endpoint idx(resource, family, true, regex_checking);

    std::unique_lock registered_resources_lock(impl_->registered_resources_mutex);
    auto result = impl_->registered_resources.insert({idx, res});

    if (!result.second) {
        // TASK-023: v1 returned false on duplicate. The new void API
        // throws so the caller cannot silently lose ownership of a
        // moved-in unique_ptr (it was destroyed inside the conversion
        // to shared_ptr above; throwing surfaces the failure).
        throw std::invalid_argument(
            "A resource is already registered at this path");
    }

    bool is_exact = !family && idx.get_url_pars().empty();
    if (is_exact) {
        impl_->registered_resources_str.insert(
            {idx.get_url_complete(), result.first->second});
    }
    if (idx.is_regex_compiled()) {
        impl_->registered_resources_regex.insert({idx, res});
    }
    registered_resources_lock.unlock();

    impl_->register_v2_route(idx, std::move(res), family);
    impl_->invalidate_route_cache();
}

namespace detail {

void webserver_impl::register_v2_route(const detail::http_endpoint& idx,
        std::shared_ptr<http_resource> res, bool family) {
    // TASK-027: mirror a register_path / register_prefix call into the
    // v2 3-tier route table. Tier placement via classify_route_tier()
    // (single source-of-truth):
    //   - family=true  -> radix tree (prefix terminus).
    //   - radix tier   -> radix tree (exact terminus, wildcard nodes).
    //   - regex tier   -> regex_routes_ (pre-compiled at registration time).
    //   - exact tier   -> exact_routes_ hash map.
    std::unique_lock table_lock(route_table_mutex_);
    detail::route_entry entry;
    entry.methods = method_set{}.set_all();
    entry.handler = std::move(res);
    entry.is_prefix = family;
    if (family) {
        param_and_prefix_routes_.insert(idx.get_url_complete(), std::move(entry),
                                        /*is_prefix=*/true);
        return;
    }
    auto tier = classify_route_tier(idx);
    switch (tier.kind) {
    case route_tier_kind::radix:
        param_and_prefix_routes_.insert(idx.get_url_complete(), std::move(entry),
                                        /*is_prefix=*/false);
        break;
    case route_tier_kind::regex_:
        regex_routes_.push_back(
            {idx.get_url_complete(), std::move(*tier.re), std::move(entry)});
        break;
    case route_tier_kind::exact:
        exact_routes_.emplace(idx.get_url_complete(), std::move(entry));
        break;
    }
}

}  // namespace detail

void webserver::register_path(const std::string& path,
                              std::shared_ptr<http_resource> res) {
    register_impl_(path, std::move(res), /*family=*/false);
}

void webserver::register_prefix(const std::string& path,
                                std::shared_ptr<http_resource> res) {
    register_impl_(path, std::move(res), /*family=*/true);
}

// Deprecated TASK-023-era forwarder. The 3-arg `bool family` overload is
// gone; users that wanted prefix matching must migrate to
// register_prefix.
void webserver::register_resource(const std::string& resource,
                                  std::shared_ptr<http_resource> res) {
    register_path(resource, std::move(res));
}

// TASK-025/TASK-026: lambda registration plumbing.
//
// All seven public on_* overloads and both public route() overloads
// forward to on_methods_, which:
//   1. Validates the handler, the method set (non-empty, no count_
//      sentinel bit), and the path (single_resource constraints).
//   2. Looks up any existing entry at the path. If it's a class-based
//      http_resource, throw -- lambda and class registrations cannot
//      share a path. If it's an existing lambda_resource shim, check
//      that EVERY requested method slot is empty before mutating any
//      of them (atomic all-or-nothing); otherwise throw.
//   3. If no entry exists, build a fresh lambda_resource shim and
//      insert it into the same three storage maps used by
//      register_impl_ (master ordered map; the str fast-path map iff
//      exact non-parameterized; the regex map iff parameterized).
//   4. Write @p handler into each requested method slot.
//   5. Invalidate the LRU route cache.
//
// The dispatch path in finalize_answer is not modified: it already
// looks up via shared_ptr<http_resource>, calls is_allowed(method)
// gating the 405 path, then dispatches via the per-method member-
// function pointer set in answer_to_connection. The lambda_resource
// shim's render_* overrides invoke the stored slot.
namespace {

// Iterate enum-declaration order (get, head, post, ...) over the bits
// set in @p methods, invoking @p fn for each. Used by the on_methods_
// pre-check loop and the commit loop; pulling the scaffolding into a
// single helper dedupes the iteration boilerplate. The order matches
// http_method::count_'s enum order (TASK-021), which is also the
// serialization order for the `Allow:` header.
template <typename Fn>
void for_each_requested_method(method_set methods, Fn&& fn) {
    for (std::uint8_t i = 0;
            i < static_cast<std::uint8_t>(http_method::count_); ++i) {
        auto m = static_cast<http_method>(i);
        if (methods.contains(m)) fn(m);
    }
}

}  // namespace

namespace detail {

std::shared_ptr<detail::lambda_resource>
webserver_impl::prepare_or_create_lambda_shim(const detail::http_endpoint& idx,
                                              method_set methods,
                                              bool& fresh_out) {
    auto it = registered_resources.find(idx);
    if (it == registered_resources.end()) {
        fresh_out = true;
        return std::make_shared<detail::lambda_resource>();
    }
    // Existing entry. Must be a lambda_resource shim, otherwise a
    // class-based register_path/register_prefix has already taken
    // this path -- lambda and class registrations cannot coexist
    // on the same path.
    auto shim = std::dynamic_pointer_cast<detail::lambda_resource>(it->second);
    if (!shim) {
        throw std::invalid_argument(
            "A non-lambda http_resource is already registered at "
            "this path; on_*/route cannot share a path with "
            "register_path/register_prefix");
    }
    // Atomicity pre-check: every requested slot must be empty BEFORE
    // we mutate any of them.
    for_each_requested_method(methods, [&](http_method m) {
        if (shim->has_slot(m)) {
            throw std::invalid_argument(
                "A handler is already registered for one of the "
                "requested methods on this path");
        }
    });
    fresh_out = false;
    return shim;
}

void webserver_impl::commit_handlers_to_shim(detail::lambda_resource& shim,
        method_set methods,
        const std::function<::httpserver::http_response(
            const ::httpserver::http_request&)>& handler) {
    // The shared std::function copies cheaply (type-erased callable),
    // so each slot owns its own copy.
    for_each_requested_method(methods, [&](http_method m) {
        shim.set_slot(m, handler);
    });
}

void webserver_impl::insert_fresh_v1_entries(const detail::http_endpoint& idx,
        std::shared_ptr<http_resource> shim) {
    registered_resources.insert({idx, shim});
    if (idx.get_url_pars().empty()) {
        registered_resources_str.insert({idx.get_url_complete(), shim});
    }
    if (idx.is_regex_compiled()) {
        registered_resources_regex.insert({idx, shim});
    }
}

void webserver_impl::upsert_v2_param_route(const std::string& key,
        method_set methods, std::shared_ptr<http_resource> shim) {
    // Read-merge-reinsert: radix_tree::insert always overwrites the
    // terminus, so we must fold any existing entry's methods in first.
    detail::radix_match<detail::route_entry> existing;
    detail::route_entry merged;
    if (param_and_prefix_routes_.find(key, existing)
            && existing.entry && !existing.is_prefix_match) {
        merged = *existing.entry;
    }
    merged.methods = merged.methods | methods;
    merged.handler = std::move(shim);
    merged.is_prefix = false;
    param_and_prefix_routes_.insert(key, std::move(merged), /*is_prefix=*/false);
}

void webserver_impl::insert_fresh_v2_entry(const detail::http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim) {
    auto tier = classify_route_tier(idx);
    switch (tier.kind) {
    case route_tier_kind::radix:
        // Unreachable: callers route url_pars-non-empty through
        // upsert_v2_param_route, never here.
        break;
    case route_tier_kind::exact: {
        detail::route_entry entry;
        entry.methods   = methods;
        entry.handler   = std::move(shim);
        entry.is_prefix = false;
        exact_routes_.emplace(idx.get_url_complete(), std::move(entry));
        break;
    }
    case route_tier_kind::regex_: {
        detail::route_entry entry;
        entry.methods   = methods;
        entry.handler   = std::move(shim);
        entry.is_prefix = false;
        regex_routes_.push_back(
            {idx.get_url_complete(), std::move(*tier.re), std::move(entry)});
        break;
    }
    }
}

void webserver_impl::update_existing_v2_entry(const std::string& key,
        method_set methods, std::shared_ptr<http_resource> shim) {
    // The tier was fixed at first registration. For the exact tier a
    // direct map lookup suffices; for the regex tier walk the vector
    // and match by shim identity (regex patterns are not repeated
    // keys; pointer identity is the cheapest and most reliable
    // discriminator).
    auto merge_into = [&](detail::route_entry& target) {
        target.methods = target.methods | methods;
        target.handler = shim;
        target.is_prefix = false;
    };
    auto exact_it = exact_routes_.find(key);
    if (exact_it != exact_routes_.end()) {
        merge_into(exact_it->second);
        return;
    }
    for (auto& rr : regex_routes_) {
        auto* sp = std::get_if<std::shared_ptr<http_resource>>(&rr.entry.handler);
        if (sp && *sp == shim) {
            merge_into(rr.entry);
            return;
        }
    }
}

void webserver_impl::upsert_v2_table_entry(const detail::http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim, bool fresh) {
    // TASK-027: mirror into the v2 3-tier table. We store the
    // lambda_resource shim via the shared_ptr arm so dispatch is
    // identical to class-resource registration. The methods bitmask
    // accumulates across calls when fresh==false.
    std::unique_lock table_lock(route_table_mutex_);
    const std::string& key = idx.get_url_complete();
    if (!idx.get_url_pars().empty()) {
        upsert_v2_param_route(key, methods, std::move(shim));
    } else if (fresh) {
        insert_fresh_v2_entry(idx, methods, std::move(shim));
    } else {
        update_existing_v2_entry(key, methods, std::move(shim));
    }
}

}  // namespace detail

void webserver::on_methods_(method_set methods,
                            const std::string& path,
                            std::function<http_response(const http_request&)> handler) {
    if (methods.empty()) {
        throw std::invalid_argument(
            "route(method_set, ...) requires at least one method bit set");
    }
    if (!handler) {
        throw std::invalid_argument(
            "The handler function passed to on_*/route must be non-empty");
    }
    // Same single-resource constraint as register_path: only "" or "/"
    // is acceptable, and the matching mode must be exact (which on_*/
    // route are).
    if (single_resource && path != "" && path != "/") {
        throw std::invalid_argument(
            "When using a single_resource server, on_*/route requires "
            "the path to be '' or '/'");
    }

    detail::http_endpoint idx(path, /*family=*/false,
                              /*registration=*/true, regex_checking);

    bool fresh = false;
    std::shared_ptr<detail::lambda_resource> shim;
    {
        std::unique_lock registered_resources_lock(impl_->registered_resources_mutex);
        shim = impl_->prepare_or_create_lambda_shim(idx, methods, fresh);
        impl_->commit_handlers_to_shim(*shim, methods, handler);
        if (fresh) impl_->insert_fresh_v1_entries(idx, shim);
    }

    impl_->upsert_v2_table_entry(idx, methods, shim, fresh);
    impl_->invalidate_route_cache();
}

// The seven named forwarders below are the only place that maps the
// method name to its http_method enum constant. Each is a thin alias
// for on_methods_; all validation and insertion logic lives there.
// on_delete uses http_method::del because `delete` is a C++ keyword;
// the wire token is "DELETE" (see http_method::to_string).
void webserver::on_get(const std::string& path,
                       std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::get), path, std::move(handler));
}

void webserver::on_post(const std::string& path,
                        std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::post), path, std::move(handler));
}

void webserver::on_put(const std::string& path,
                       std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::put), path, std::move(handler));
}

void webserver::on_delete(const std::string& path,
                          std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::del), path, std::move(handler));
}

void webserver::on_patch(const std::string& path,
                         std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::patch), path, std::move(handler));
}

void webserver::on_options(const std::string& path,
                           std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::options), path, std::move(handler));
}

void webserver::on_head(const std::string& path,
                        std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::head), path, std::move(handler));
}

// TASK-026: generic table-driven entry points. The single-method form
// rejects http_method::count_ explicitly because the public route()
// overload accepts a runtime value (and so the sentinel is reachable);
// the on_* forwarders never pass count_, so the on_methods_ helper
// itself does not guard against it.
void webserver::route(http_method m,
                      const std::string& path,
                      std::function<http_response(const http_request&)> handler) {
    if (m == http_method::count_) {
        throw std::invalid_argument(
            "http_method::count_ is a sentinel and may not be "
            "registered as a route");
    }
    on_methods_(method_set{}.set(m), path, std::move(handler));
}

void webserver::route(method_set methods,
                      const std::string& path,
                      std::function<http_response(const http_request&)> handler) {
    on_methods_(methods, path, std::move(handler));
}

// TASK-035: canonical smart-pointer overload. The templated unique_ptr
// shim in webserver.hpp constructs a shared_ptr from the unique_ptr and
// forwards here, so this is the single funnel for both ownership shapes.
void webserver::register_ws_resource(const std::string& resource,
                                     std::shared_ptr<websocket_handler> handler) {
#ifdef HAVE_WEBSOCKET
    if (!handler) {
        throw std::invalid_argument("The websocket_handler pointer cannot be null");
    }
    std::string key = http_utils::standardize_url(resource);
    std::unique_lock lock(impl_->registered_resources_mutex);
    auto result = impl_->registered_ws_handlers.emplace(std::move(key),
                                                        std::move(handler));
    if (!result.second) {
        // Mirror TASK-023's throw-on-duplicate. v1's operator[]-based
        // insert silently overwrote; v2.0 surfaces the collision.
        throw std::invalid_argument(
            "A websocket_handler is already registered at this path");
    }
#else
    // TASK-034 §7: WebSocket compiled out -- fail loudly at the public
    // entry point so callers can catch feature_unavailable.
    (void)resource;
    (void)handler;
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
#endif
}

void webserver::unregister_ws_resource(const std::string& resource) {
#ifdef HAVE_WEBSOCKET
    std::unique_lock lock(impl_->registered_resources_mutex);
    impl_->registered_ws_handlers.erase(http_utils::standardize_url(resource));
#else
    (void)resource;
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
#endif
}

namespace detail {

// Wrap MHD_OptionItem aggregate-init so each push reads uniformly
// across the option-array builders below. Replaces the local
// struct-with-operator() that used to live inside webserver::start().
static MHD_OptionItem make_option(enum MHD_OPTION opt, intptr_t val,
                                  void* ptr = nullptr) {
    MHD_OptionItem x = {opt, val, ptr};
    return x;
}

void webserver_impl::add_base_mhd_options(std::vector<MHD_OptionItem>& iov) const {
    iov.push_back(make_option(MHD_OPTION_NOTIFY_COMPLETED,
                              (intptr_t) &webserver_impl::request_completed, nullptr));
    // TASK-016: per-connection arena anchor. MHD_OPTION_NOTIFY_CONNECTION
    // hands us a per-connection void** (socket_context) on STARTED, where
    // we new a detail::connection_state (which owns the arena), and on
    // CLOSED, where we delete it. This makes the arena's lifetime equal
    // to the MHD_Connection's lifetime; request_completed reuses the
    // arena across keep-alive request boundaries via arena_.release().
    iov.push_back(make_option(MHD_OPTION_NOTIFY_CONNECTION,
                              (intptr_t) &webserver_impl::connection_notify, nullptr));
    iov.push_back(make_option(MHD_OPTION_URI_LOG_CALLBACK,
                              (intptr_t) &webserver_impl::uri_log, parent));
    iov.push_back(make_option(MHD_OPTION_EXTERNAL_LOGGER,
                              (intptr_t) &webserver_impl::error_log, parent));
    iov.push_back(make_option(MHD_OPTION_UNESCAPE_CALLBACK,
                              (intptr_t) &webserver_impl::unescaper_func, parent));
    iov.push_back(make_option(MHD_OPTION_CONNECTION_TIMEOUT, parent->connection_timeout));
    if (bind_socket != 0) {
        iov.push_back(make_option(MHD_OPTION_LISTEN_SOCKET, bind_socket));
    }
    if (parent->max_threads != 0) {
        iov.push_back(make_option(MHD_OPTION_THREAD_POOL_SIZE, parent->max_threads));
    }
    if (parent->max_connections != 0) {
        iov.push_back(make_option(MHD_OPTION_CONNECTION_LIMIT, parent->max_connections));
    }
    if (parent->memory_limit != 0) {
        iov.push_back(make_option(MHD_OPTION_CONNECTION_MEMORY_LIMIT, parent->memory_limit));
    }
    if (parent->per_IP_connection_limit != 0) {
        iov.push_back(make_option(MHD_OPTION_PER_IP_CONNECTION_LIMIT, parent->per_IP_connection_limit));
    }
    if (parent->max_thread_stack_size != 0) {
        iov.push_back(make_option(MHD_OPTION_THREAD_STACK_SIZE, parent->max_thread_stack_size));
    }
#ifdef HAVE_DAUTH
    if (parent->nonce_nc_size != 0) {
        iov.push_back(make_option(MHD_OPTION_NONCE_NC_SIZE, parent->nonce_nc_size));
    }
#endif  // HAVE_DAUTH
}

void webserver_impl::add_tls_mhd_options(std::vector<MHD_OptionItem>& iov) const {
    if (parent->use_ssl) {
        // const_cast respects the MHD C interface, which takes a void*
        // even though the data is read-only at the library boundary.
        iov.push_back(make_option(MHD_OPTION_HTTPS_MEM_KEY, 0,
                                  reinterpret_cast<void*>(const_cast<char*>(parent->https_mem_key.c_str()))));
        iov.push_back(make_option(MHD_OPTION_HTTPS_MEM_CERT, 0,
                                  reinterpret_cast<void*>(const_cast<char*>(parent->https_mem_cert.c_str()))));
        if (!parent->https_mem_trust.empty()) {
            iov.push_back(make_option(MHD_OPTION_HTTPS_MEM_TRUST, 0,
                                      reinterpret_cast<void*>(const_cast<char*>(parent->https_mem_trust.c_str()))));
        }
        if (!parent->https_priorities.empty()) {
            iov.push_back(make_option(MHD_OPTION_HTTPS_PRIORITIES, 0,
                                      reinterpret_cast<void*>(const_cast<char*>(parent->https_priorities.c_str()))));
        }
    }
#ifdef HAVE_DAUTH
    if (parent->digest_auth_random != "") {
        iov.push_back(make_option(MHD_OPTION_DIGEST_AUTH_RANDOM,
                                  parent->digest_auth_random.size(),
                                  const_cast<char*>(parent->digest_auth_random.c_str())));
    }
#endif  // HAVE_DAUTH
}

void webserver_impl::add_gnutls_mhd_options(std::vector<MHD_OptionItem>& iov) const {
#ifdef HAVE_GNUTLS
    if (parent->cred_type != http_utils::NONE) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_CRED_TYPE, parent->cred_type));
    }
    if (parent->psk_cred_handler != nullptr && parent->use_ssl) {
        iov.push_back(make_option(MHD_OPTION_GNUTLS_PSK_CRED_HANDLER,
                                  (intptr_t)&webserver_impl::psk_cred_handler_func, parent));
    }
#ifdef MHD_OPTION_HTTPS_CERT_CALLBACK
    if (parent->sni_callback != nullptr && parent->use_ssl) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_CERT_CALLBACK,
                                  (intptr_t)&webserver_impl::sni_cert_callback_func, parent));
    }
#endif  // MHD_OPTION_HTTPS_CERT_CALLBACK
#else   // HAVE_GNUTLS
    (void)iov;
#endif  // HAVE_GNUTLS
}

void webserver_impl::add_extended_mhd_options(std::vector<MHD_OptionItem>& iov) const {
    if (parent->listen_backlog > 0) {
        iov.push_back(make_option(MHD_OPTION_LISTEN_BACKLOG_SIZE, parent->listen_backlog));
    }
    if (parent->address_reuse != 0) {
        iov.push_back(make_option(MHD_OPTION_LISTENING_ADDRESS_REUSE, parent->address_reuse));
    }
    if (parent->connection_memory_increment > 0) {
        iov.push_back(make_option(MHD_OPTION_CONNECTION_MEMORY_INCREMENT,
                                  parent->connection_memory_increment));
    }
    if (parent->tcp_fastopen_queue_size > 0) {
        iov.push_back(make_option(MHD_OPTION_TCP_FASTOPEN_QUEUE_SIZE,
                                  parent->tcp_fastopen_queue_size));
    }
    if (parent->sigpipe_handled_by_app) {
        iov.push_back(make_option(MHD_OPTION_SIGPIPE_HANDLED_BY_APP, 1));
    }
    if (parent->no_alpn) {
        iov.push_back(make_option(MHD_OPTION_TLS_NO_ALPN, 1));
    }
    if (parent->client_discipline_level >= 0) {
        iov.push_back(make_option(MHD_OPTION_CLIENT_DISCIPLINE_LVL, parent->client_discipline_level));
    }
}

void webserver_impl::add_https_extra_options(std::vector<MHD_OptionItem>& iov) const {
    if (!parent->https_mem_dhparams.empty()) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_MEM_DHPARAMS, 0,
                                  const_cast<char*>(parent->https_mem_dhparams.c_str())));
    }
    if (!parent->https_key_password.empty()) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_KEY_PASSWORD, 0,
                                  const_cast<char*>(parent->https_key_password.c_str())));
    }
    if (!parent->https_priorities_append.empty()) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_PRIORITIES_APPEND, 0,
                                  const_cast<char*>(parent->https_priorities_append.c_str())));
    }
}

void webserver_impl::build_mhd_option_array(std::vector<MHD_OptionItem>& iov) const {
    add_base_mhd_options(iov);
    add_tls_mhd_options(iov);
    add_gnutls_mhd_options(iov);
    add_extended_mhd_options(iov);
    add_https_extra_options(iov);
    iov.push_back(make_option(MHD_OPTION_END, 0, nullptr));
}

int webserver_impl::compose_transport_flags() const {
    int flags = 0;
    if (parent->use_ssl) flags |= MHD_USE_SSL;
    if (parent->use_ipv6) flags |= MHD_USE_IPv6;
    if (parent->use_dual_stack) flags |= MHD_USE_DUAL_STACK;
    return flags;
}

int webserver_impl::compose_runtime_flags() const {
    int flags = 0;
    if (parent->debug) flags |= MHD_USE_DEBUG;
    if (parent->pedantic) flags |= MHD_USE_PEDANTIC_CHECKS;
    if (parent->deferred_enabled) flags |= MHD_USE_SUSPEND_RESUME;
    if (parent->no_listen_socket) flags |= MHD_USE_NO_LISTEN_SOCKET;
    if (parent->no_thread_safety) flags |= MHD_USE_NO_THREAD_SAFETY;
    if (parent->turbo) flags |= MHD_USE_TURBO;
    if (parent->suppress_date_header) flags |= MHD_USE_SUPPRESS_DATE_NO_CLOCK;
#ifdef HAVE_WEBSOCKET
    if (!registered_ws_handlers.empty()) flags |= MHD_ALLOW_UPGRADE;
#endif  // HAVE_WEBSOCKET
    return flags;
}

int webserver_impl::compose_start_flags() const {
    int flags = parent->start_method;
    flags |= compose_transport_flags();
    flags |= compose_runtime_flags();
#ifdef USE_FASTOPEN
    flags |= MHD_USE_TCP_FASTOPEN;
#endif
    return flags;
}

}  // namespace detail

bool webserver::start(bool blocking) {
    if (start_method == http_utils::THREAD_PER_CONNECTION
            && (max_threads != 0 || max_thread_stack_size != 0)) {
        throw std::invalid_argument(
            "Cannot specify maximum number of threads when using a thread per connection");
    }

    vector<struct MHD_OptionItem> iov;
    impl_->build_mhd_option_array(iov);
    const int start_conf = impl_->compose_start_flags();

    impl_->daemon = nullptr;
    if (bind_address == nullptr) {
        impl_->daemon = MHD_start_daemon(start_conf, port, &detail::webserver_impl::policy_callback, this,
                &detail::webserver_impl::answer_to_connection, impl_.get(), MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_END);
    } else {
        impl_->daemon = MHD_start_daemon(start_conf, 1, &detail::webserver_impl::policy_callback, this,
                &detail::webserver_impl::answer_to_connection, impl_.get(), MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_SOCK_ADDR, bind_address, MHD_OPTION_END);
    }

    if (impl_->daemon == nullptr) {
        throw std::invalid_argument("Unable to connect daemon to port: " + std::to_string(port));
    }

    impl_->running = true;

    if (blocking) {
        pthread_mutex_lock(&impl_->mutexwait);
        while (impl_->running) {
            pthread_cond_wait(&impl_->mutexcond, &impl_->mutexwait);
        }
        pthread_mutex_unlock(&impl_->mutexwait);
        return true;
    }
    return false;
}

bool webserver::is_running() {
    return impl_->running;
}

bool webserver::stop() {
    if (!impl_->running) return false;

    pthread_mutex_lock(&impl_->mutexwait);
    impl_->running = false;
    pthread_cond_signal(&impl_->mutexcond);
    pthread_mutex_unlock(&impl_->mutexwait);

    MHD_stop_daemon(impl_->daemon);

    shutdown(impl_->bind_socket, 2);

    return true;
}

int webserver::quiesce() {
    if (impl_->daemon == nullptr) return -1;
    MHD_socket fd = MHD_quiesce_daemon(impl_->daemon);
    return static_cast<int>(fd);
}

int webserver::get_listen_fd() const {
    if (impl_->daemon == nullptr) return -1;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(impl_->daemon, MHD_DAEMON_INFO_LISTEN_FD);
    if (info == nullptr) return -1;
    return static_cast<int>(info->listen_fd);
}

unsigned int webserver::get_active_connections() const {
    if (impl_->daemon == nullptr) return 0;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(impl_->daemon, MHD_DAEMON_INFO_CURRENT_CONNECTIONS);
    if (info == nullptr) return 0;
    return info->num_connections;
}

uint16_t webserver::get_bound_port() const {
    if (impl_->daemon == nullptr) return 0;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(impl_->daemon, MHD_DAEMON_INFO_BIND_PORT);
    if (info == nullptr) return 0;
    return info->port;
}

bool webserver::run() {
    if (impl_->daemon == nullptr) return false;
    return MHD_run(impl_->daemon) == MHD_YES;
}

bool webserver::run_wait(int32_t millisec) {
    if (impl_->daemon == nullptr) return false;
    return MHD_run_wait(impl_->daemon, millisec) == MHD_YES;
}

bool webserver::get_fdset(void* read_fd_set, void* write_fd_set, void* except_fd_set, int* max_fd) {
    // TASK-020: the public signature accepts `void*` so the public header
    // does not need <sys/select.h>. Callers pass real `fd_set*` pointers
    // (the implicit conversion to `void*` is well-defined in C++); cast
    // back here, where <sys/select.h> is reachable transitively via
    // <microhttpd.h>.
    if (impl_->daemon == nullptr) return false;
    MHD_socket mhd_max_fd = 0;
    if (MHD_get_fdset(impl_->daemon,
                      static_cast<fd_set*>(read_fd_set),
                      static_cast<fd_set*>(write_fd_set),
                      static_cast<fd_set*>(except_fd_set),
                      &mhd_max_fd) != MHD_YES) {
        return false;
    }
    *max_fd = static_cast<int>(mhd_max_fd);
    return true;
}

bool webserver::get_timeout(uint64_t* timeout) {
    if (impl_->daemon == nullptr) return false;
    MHD_UNSIGNED_LONG_LONG mhd_timeout = 0;
    if (MHD_get_timeout(impl_->daemon, &mhd_timeout) != MHD_YES) {
        return false;
    }
    *timeout = static_cast<uint64_t>(mhd_timeout);
    return true;
}

bool webserver::add_connection(int client_socket, const struct sockaddr* addr, unsigned int addrlen) {
    // TASK-020: the public signature accepts `unsigned int` instead of
    // `socklen_t` so the public header does not need <sys/socket.h>.
    // POSIX guarantees `socklen_t` is an unsigned integer of at least 32
    // bits; `unsigned int` matches on every supported platform. The
    // static_assert below pins that contract.
    static_assert(sizeof(unsigned int) >= sizeof(socklen_t),
                  "unsigned int is narrower than socklen_t on this platform; "
                  "webserver::add_connection's public signature must be widened.");
    if (impl_->daemon == nullptr) return false;
    return MHD_add_connection(impl_->daemon, client_socket, addr,
                              static_cast<socklen_t>(addrlen)) == MHD_YES;
}

// TASK-024: erase a single registration of the requested kind (family).
// Each kind keeps a distinct http_endpoint key (the family flag is part
// of the endpoint's identity), so we must build the key with the right
// flag or the erase silently misses. Caches are invalidated under the
// registered_resources lock to prevent any thread from reading a
// dangling resource pointer after the maps drop their refs.
void webserver::unregister_impl_(const string& resource, bool family) {
    detail::http_endpoint he(resource, family, true, regex_checking);
    std::unique_lock registered_resources_lock(impl_->registered_resources_mutex);

    {
        std::lock_guard<std::mutex> cache_lock(impl_->route_cache_mutex);
        impl_->route_cache_list.clear();
        impl_->route_cache_map.clear();
    }

    impl_->registered_resources.erase(he);
    impl_->registered_resources_regex.erase(he);
    // The string-keyed fast-path map only ever holds exact (non-family)
    // entries (see register_impl_). A prefix unregister has nothing to
    // do here.
    if (!family) {
        impl_->registered_resources_str.erase(he.get_url_complete());
    }

    // TASK-027: mirror the erasure into the v2 3-tier table. Lock order:
    // we already hold registered_resources_lock (v1 table); take
    // route_table_mutex_ next, then route_cache_mutex_ via the
    // route_cache::clear() helper. The discipline (table BEFORE cache)
    // is consistent with register_impl_ and the documented invariant.
    {
        std::unique_lock table_lock(impl_->route_table_mutex_);
        const std::string& key = he.get_url_complete();
        if (family) {
            impl_->param_and_prefix_routes_.remove(key, /*is_prefix=*/true);
        } else if (!he.get_url_pars().empty()) {
            impl_->param_and_prefix_routes_.remove(key, /*is_prefix=*/false);
        } else {
            // Erase from exact tier; also sweep regex tier (url_complete key).
            impl_->exact_routes_.erase(key);
            impl_->regex_routes_.erase(
                std::remove_if(impl_->regex_routes_.begin(),
                               impl_->regex_routes_.end(),
                               [&key](const detail::webserver_impl::regex_route& rr) {
                                   return rr.url_complete == key;
                               }),
                impl_->regex_routes_.end());
        }
    }
    impl_->route_cache_v2.clear();
}

void webserver::unregister_path(const string& path) {
    unregister_impl_(path, /*family=*/false);
}

void webserver::unregister_prefix(const string& path) {
    unregister_impl_(path, /*family=*/true);
}

void webserver::unregister_resource(const string& resource) {
    // Build both endpoint keys before acquiring any lock.
    detail::http_endpoint he_exact(resource, /*family=*/false, true, regex_checking);
    detail::http_endpoint he_prefix(resource, /*family=*/true,  true, regex_checking);

    // Hold a single write-lock across both erasures so no request thread can
    // observe a partially-unregistered state (CWE-367 TOCTOU fix: the exact
    // entry and the prefix entry are removed atomically).
    std::unique_lock registered_resources_lock(impl_->registered_resources_mutex);

    {
        std::lock_guard<std::mutex> cache_lock(impl_->route_cache_mutex);
        impl_->route_cache_list.clear();
        impl_->route_cache_map.clear();
    }

    impl_->registered_resources.erase(he_exact);
    impl_->registered_resources.erase(he_prefix);
    impl_->registered_resources_regex.erase(he_exact);
    impl_->registered_resources_regex.erase(he_prefix);
    // The string-keyed fast-path map only holds exact (non-family) entries.
    impl_->registered_resources_str.erase(he_exact.get_url_complete());

    // TASK-027: mirror into the v2 3-tier table. Erase under both
    // classifications so a prior register_path AND register_prefix on
    // the same path are both cleared atomically.
    {
        std::unique_lock table_lock(impl_->route_table_mutex_);
        const std::string& key = he_exact.get_url_complete();
        impl_->exact_routes_.erase(key);
        impl_->param_and_prefix_routes_.remove(key, /*is_prefix=*/false);
        impl_->param_and_prefix_routes_.remove(key, /*is_prefix=*/true);
        // Also sweep the regex tier by url_complete.
        impl_->regex_routes_.erase(
            std::remove_if(impl_->regex_routes_.begin(),
                           impl_->regex_routes_.end(),
                           [&key](const detail::webserver_impl::regex_route& rr) {
                               return rr.url_complete == key;
                           }),
            impl_->regex_routes_.end());
    }
    impl_->route_cache_v2.clear();
}

// TASK-029: The v2.0 public IP-control API is the pair block_ip / unblock_ip.
// The historical ban_ip / unban_ip / allow_ip / disallow_ip quartet was
// collapsed to a single name pair operating on the deny list. The internal
// allowances set and the allow-list branch in policy_callback remain in
// place so default_policy(REJECT) keeps working at the daemon level, but
// they are no longer reachable from the public API.
void webserver::block_ip(std::string_view ip) {
    std::unique_lock bans_lock(impl_->bans_mutex);
    ip_representation t_ip{std::string{ip}};
    auto it = impl_->bans.find(t_ip);
    if (it != impl_->bans.end() && (t_ip.weight() < it->weight())) {
        impl_->bans.erase(it);
        impl_->bans.insert(t_ip);
    } else {
        impl_->bans.insert(t_ip);
    }
}

void webserver::unblock_ip(std::string_view ip) {
    std::unique_lock bans_lock(impl_->bans_mutex);
    impl_->bans.erase(ip_representation{std::string{ip}});
}

namespace detail {

void webserver_impl::request_completed(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) {
    // These parameters are passed to respect the MHD interface, but are not needed here.
    std::ignore = cls;
    std::ignore = toe;

    // (1) Destroy the modded_request first. This runs ~http_request,
    //     which calls the arena_deleter on the impl's unique_ptr (a
    //     destructor-only call: monotonic_buffer_resource never
    //     deallocates per-object), running every PMR string/vector/map
    //     destructor before we reset the arena.
    delete static_cast<detail::modded_request*>(*con_cls);
    *con_cls = nullptr;

    // (2) Now that no live object inside the arena's storage remains,
    //     rewind the bump pointer AND zero the initial buffer so that
    //     credentials from the completed request do not linger in the
    //     reused memory (security-reviewer-iter1-3). reset_arena() does
    //     both atomically. The next request on this keep-alive connection
    //     reuses the same memory (verified by http_request_arena unit test).
    //
    // MHD ordering guarantee: NOTIFY_COMPLETED always fires before
    // NOTIFY_CLOSED for the same connection (MHD documentation, section
    // "Thread model guarantees"). Therefore the connection_state pointer
    // accessed here is guaranteed live. The NOTIFY_CLOSED handler
    // (connection_notify) must NOT be called concurrently on a different
    // thread for the same connection while this callback is executing.
    // (security-reviewer-iter1-4: thread-safety ordering invariant.)
    if (connection != nullptr) {
        const MHD_ConnectionInfo* ci = MHD_get_connection_info(
            connection, MHD_CONNECTION_INFO_SOCKET_CONTEXT);
        if (ci != nullptr && ci->socket_context != nullptr) {
            auto* cs = static_cast<detail::connection_state*>(ci->socket_context);
            cs->reset_arena();
        }
    }
}

void webserver_impl::connection_notify(void* cls, struct MHD_Connection* connection,
                                       void** socket_context,
                                       enum MHD_ConnectionNotificationCode toe) {
    std::ignore = cls;
    std::ignore = connection;

    switch (toe) {
        case MHD_CONNECTION_NOTIFY_STARTED:
            // Allocate the per-connection state (and its embedded arena)
            // on connection start. The new is the only heap allocation
            // tied to a connection's lifetime; afterwards every request
            // on this connection draws its impl out of the arena.
            *socket_context = new detail::connection_state();
            break;
        case MHD_CONNECTION_NOTIFY_CLOSED:
            // MHD ordering guarantee: NOTIFY_COMPLETED fires before
            // NOTIFY_CLOSED for the same connection. By the time we reach
            // this branch, request_completed has already called reset_arena()
            // and the modded_request has already been deleted -- so the
            // connection_state is no longer referenced by any live object.
            // (security-reviewer-iter1-4: documents the invariant that
            // prevents the concurrent request_completed + NOTIFY_CLOSED
            // race described in CWE-362.)
            delete static_cast<detail::connection_state*>(*socket_context);
            *socket_context = nullptr;
            break;
    }
}

#ifdef HAVE_GNUTLS
// MHD_PskServerCredentialsCallback signature:
// The 'cls' parameter is our webserver pointer (passed via MHD_OPTION)
// Returns 0 on success, -1 on error
// The psk output should be allocated with malloc() - MHD will free it
int webserver_impl::psk_cred_handler_func(void* cls,
                                      struct MHD_Connection* connection,
                                      const char* username,
                                      void** psk,
                                      size_t* psk_size) {
    std::ignore = connection;  // Not needed - we get context from cls

    webserver* ws = static_cast<webserver*>(cls);

    // Initialize output to safe values
    *psk = nullptr;
    *psk_size = 0;

    if (ws == nullptr || ws->psk_cred_handler == nullptr) {
        return -1;
    }

    std::string psk_hex = ws->psk_cred_handler(std::string(username));
    if (psk_hex.empty()) {
        return -1;
    }

    // Validate hex string before allocating memory
    size_t psk_len = psk_hex.size() / 2;
    if (psk_len == 0 || (psk_hex.size() % 2 != 0) ||
        !string_utilities::is_valid_hex(psk_hex)) {
        return -1;
    }

    // Allocate with malloc - MHD will free this
    unsigned char* psk_data = static_cast<unsigned char*>(malloc(psk_len));
    if (psk_data == nullptr) {
        return -1;
    }

    // Convert hex string to binary
    for (size_t i = 0; i < psk_len; i++) {
        psk_data[i] = static_cast<unsigned char>(
            (string_utilities::hex_char_to_val(psk_hex[i * 2]) << 4) |
             string_utilities::hex_char_to_val(psk_hex[i * 2 + 1]));
    }

    *psk = psk_data;
    *psk_size = psk_len;
    return 0;
}

#ifdef MHD_OPTION_HTTPS_CERT_CALLBACK
// SNI callback for selecting certificates based on server name
// Returns 0 on success, -1 on failure
int webserver_impl::sni_cert_callback_func(void* cls,
                                       struct MHD_Connection* connection,
                                       const char* server_name,
                                       gnutls_certificate_credentials_t* creds) {
    std::ignore = connection;

    webserver* ws = static_cast<webserver*>(cls);
    if (ws == nullptr || ws->sni_callback == nullptr || server_name == nullptr) {
        return -1;
    }

    webserver_impl* impl = ws->impl_.get();

    std::string name(server_name);

    // Check if we have cached credentials for this server name
    {
        std::shared_lock lock(impl->sni_credentials_mutex);
        auto it = impl->sni_credentials_cache.find(name);
        if (it != impl->sni_credentials_cache.end()) {
            *creds = it->second;
            return 0;
        }
    }

    // Call user's callback to get cert/key pair
    auto [cert_pem, key_pem] = ws->sni_callback(name);
    if (cert_pem.empty() || key_pem.empty()) {
        return -1;  // Use default certificate
    }

    // Create new credentials for this server name
    gnutls_certificate_credentials_t new_creds;
    if (gnutls_certificate_allocate_credentials(&new_creds) != GNUTLS_E_SUCCESS) {
        return -1;
    }

    gnutls_datum_t cert_data = {
        reinterpret_cast<unsigned char*>(const_cast<char*>(cert_pem.data())),
        static_cast<unsigned int>(cert_pem.size())
    };
    gnutls_datum_t key_data = {
        reinterpret_cast<unsigned char*>(const_cast<char*>(key_pem.data())),
        static_cast<unsigned int>(key_pem.size())
    };

    int ret = gnutls_certificate_set_x509_key_mem(new_creds, &cert_data, &key_data, GNUTLS_X509_FMT_PEM);
    if (ret != GNUTLS_E_SUCCESS) {
        gnutls_certificate_free_credentials(new_creds);
        return -1;
    }

    // Cache the credentials with double-check to avoid race condition
    {
        std::unique_lock lock(impl->sni_credentials_mutex);
        // Re-check after acquiring exclusive lock - another thread may have inserted
        auto it = impl->sni_credentials_cache.find(name);
        if (it != impl->sni_credentials_cache.end()) {
            // Another thread already cached credentials, use theirs and free ours
            gnutls_certificate_free_credentials(new_creds);
            *creds = it->second;
            return 0;
        }
        impl->sni_credentials_cache[name] = new_creds;
    }

    *creds = new_creds;
    return 0;
}
#endif  // MHD_OPTION_HTTPS_CERT_CALLBACK
#endif  // HAVE_GNUTLS

}  // namespace detail

namespace detail {

MHD_Result webserver_impl::policy_callback(void *cls, const struct sockaddr* addr, socklen_t addrlen) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = addrlen;

    const auto ws = static_cast<webserver*>(cls);

    if (!ws->ban_system_enabled) return MHD_YES;

    auto* impl = ws->impl_.get();
    std::shared_lock bans_lock(impl->bans_mutex);
    std::shared_lock allowances_lock(impl->allowances_mutex);
    const bool is_banned = impl->bans.count(ip_representation(addr));
    const bool is_allowed = impl->allowances.count(ip_representation(addr));

    if ((ws->default_policy == http_utils::ACCEPT && is_banned && !is_allowed) ||
        (ws->default_policy == http_utils::REJECT && (!is_allowed || is_banned))) {
        return MHD_NO;
    }

    return MHD_YES;
}

void* webserver_impl::uri_log(void* cls, const char* uri, struct MHD_Connection *con) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;
    std::ignore = con;

    auto mr = std::make_unique<detail::modded_request>();
    // MHD may invoke this callback with a null uri before the request line
    // has been parsed (e.g. port scans, half-open connections, or non-HTTP
    // traffic on the listening port). Treat that as an empty URI so the
    // std::string assignment does not throw std::logic_error and abort the
    // process via std::terminate. See issue #371.
    mr->complete_uri = (uri != nullptr) ? uri : "";
    return reinterpret_cast<void*>(mr.release());
}

void webserver_impl::error_log(void* cls, const char* fmt, va_list ap) {
    webserver* dws = static_cast<webserver*>(cls);

    std::string msg;
    msg.resize(80);  // Asssume one line will be enought most of the time.

    va_list va;
    va_copy(va, ap);  // Stash a copy in case we need to try again.

    size_t r = vsnprintf(&*msg.begin(), msg.size(), fmt, ap);
    va_end(ap);

    if (msg.size() < r) {
      msg.resize(r);
      r = vsnprintf(&*msg.begin(), msg.size(), fmt, va);
    }
    va_end(va);
    msg.resize(r);

    if (dws->log_error != nullptr) dws->log_error(msg);
}

void webserver_impl::access_log(webserver* dws, const string& uri) {
    if (dws->log_access != nullptr) dws->log_access(uri);
}

size_t webserver_impl::unescaper_func(void * cls, struct MHD_Connection *c, char *s) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;
    std::ignore = c;

    // THIS IS USED TO AVOID AN UNESCAPING OF URL BEFORE THE ANSWER.
    // IT IS DUE TO A BOGUS ON libmicrohttpd (V0.99) THAT PRODUCING A
    // STRING CONTAINING '\0' AFTER AN UNESCAPING, IS UNABLE TO PARSE
    // ARGS WITH get_connection_values FUNC OR lookup FUNC.
    if (s == nullptr) return 0;
    return std::char_traits<char>::length(s);
}

}  // namespace detail

namespace detail {

MHD_Result webserver_impl::handle_post_form_arg(detail::modded_request* mr,
        const char* key, const char* data, size_t size, uint64_t off) {
    // No file: set the arg key/value and return. A non-zero @p off
    // means MHD is feeding us a continuation chunk of a previously-
    // started value, so append rather than replace.
    if (off > 0) {
        mr->dhr->grow_last_arg(key, std::string(data, size));
    } else {
        mr->dhr->set_arg(key, std::string(data, size));
    }
    return MHD_YES;
}

bool webserver_impl::setup_new_upload_file_info(http::file_info& file,
        const char* filename, const char* content_type,
        const char* transfer_encoding) const {
    // First chunk for this (key, filename) pair: choose the on-disk
    // destination path (random if generate_random_filename_on_upload,
    // otherwise sanitize the client-supplied filename) and prime the
    // file_info with content_type / transfer_encoding when MHD gave
    // them to us.
    if (parent->generate_random_filename_on_upload) {
        file.set_file_system_file_name(
            http_utils::generate_random_upload_filename(parent->file_upload_dir));
    } else {
        std::string safe_name = http_utils::sanitize_upload_filename(filename);
        if (safe_name.empty()) return false;
        file.set_file_system_file_name(parent->file_upload_dir + "/" + safe_name);
    }
    // Avoid appending to a leftover file from a previous request.
    unlink(file.get_file_system_file_name().c_str());
    if (content_type != nullptr) file.set_content_type(content_type);
    if (transfer_encoding != nullptr) file.set_transfer_encoding(transfer_encoding);
    return true;
}

void webserver_impl::manage_upload_stream(detail::modded_request* mr,
        const char* filename, const char* key, http::file_info& file) {
    // If MHD switches us to a different (filename, key) pair, close the
    // previous output stream. The four-way OR covers fresh state (both
    // tracking strings empty) and either coordinate changing.
    if (mr->upload_filename.empty()
            || mr->upload_key.empty()
            || strcmp(filename, mr->upload_filename.c_str()) != 0
            || strcmp(key, mr->upload_key.c_str()) != 0) {
        if (mr->upload_ostrm != nullptr) mr->upload_ostrm->close();
    }
    // Open a stream when we don't already have one (first chunk, or
    // just-closed above).
    if (mr->upload_ostrm == nullptr || !mr->upload_ostrm->is_open()) {
        mr->upload_key = key;
        mr->upload_filename = filename;
        mr->upload_ostrm = std::make_unique<std::ofstream>();
        mr->upload_ostrm->open(file.get_file_system_file_name(),
                               std::ios::binary | std::ios::app);
    }
}

MHD_Result webserver_impl::process_file_upload(detail::modded_request* mr,
        const char* key, const char* filename, const char* content_type,
        const char* transfer_encoding, const char* data, size_t size) const {
    http::file_info& file = mr->dhr->get_or_create_file_info(key, filename);
    if (file.get_file_system_file_name().empty()) {
        if (!setup_new_upload_file_info(file, filename, content_type, transfer_encoding)) {
            return MHD_NO;
        }
    }
    manage_upload_stream(mr, filename, key, file);
    if (size > 0) {
        mr->upload_ostrm->write(data, size);
        if (!mr->upload_ostrm->good()) return MHD_NO;
    }
    file.grow_file_size(size);
    return MHD_YES;
}

MHD_Result webserver_impl::post_iterator(void *cls, enum MHD_ValueKind kind,
        const char *key, const char *filename, const char *content_type,
        const char *transfer_encoding, const char *data, uint64_t off, size_t size) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = kind;
    auto* mr = static_cast<detail::modded_request*>(cls);

    if (!filename) {
        return handle_post_form_arg(mr, key, data, size, off);
    }

    try {
        if (mr->ws->file_upload_target != FILE_UPLOAD_DISK_ONLY) {
            mr->dhr->set_arg_flat(key,
                std::string(mr->dhr->get_arg(key)) + std::string(data, size));
        }
        if (*filename != '\0' && mr->ws->file_upload_target != FILE_UPLOAD_MEMORY_ONLY) {
            MHD_Result r = mr->ws->impl_->process_file_upload(
                mr, key, filename, content_type, transfer_encoding, data, size);
            if (r != MHD_YES) return r;
        }
        return MHD_YES;
    } catch (const http::generateFilenameException&) {
        return MHD_NO;
    }
}

}  // namespace detail

#ifdef HAVE_WEBSOCKET
namespace {

// RFC 6455 §5.5.1: a CLOSE frame's payload starts with a 2-byte
// status code (default 1000 "normal closure") followed by an optional
// UTF-8 reason. Pulled out of dispatch_websocket_frame so the switch
// stays under the CCN bar.
void handle_close_frame(websocket_handler* handler, websocket_session& session,
                        const char* frame_data, size_t frame_len) {
    uint16_t close_code = 1000;
    std::string close_reason;
    if (frame_len >= 2) {
        close_code = static_cast<uint16_t>(
            (static_cast<unsigned char>(frame_data[0]) << 8) |
             static_cast<unsigned char>(frame_data[1]));
        if (frame_len > 2) {
            close_reason.assign(frame_data + 2, frame_len - 2);
        }
    }
    handler->on_close(session, close_code, close_reason);
    // Echo the close back and end the loop.
    session.close(close_code, close_reason);
}

void dispatch_websocket_frame(int status, struct MHD_WebSocketStream* ws_stream,
                              websocket_handler* handler,
                              websocket_session& session,
                              char* frame_data, size_t frame_len) {
    switch (status) {
        case MHD_WEBSOCKET_STATUS_TEXT_FRAME:
            handler->on_message(session, std::string_view(frame_data, frame_len));
            MHD_websocket_free(ws_stream, frame_data);
            break;
        case MHD_WEBSOCKET_STATUS_BINARY_FRAME:
            handler->on_binary(session, frame_data, frame_len);
            MHD_websocket_free(ws_stream, frame_data);
            break;
        case MHD_WEBSOCKET_STATUS_PING_FRAME:
            handler->on_ping(session, std::string_view(frame_data, frame_len));
            MHD_websocket_free(ws_stream, frame_data);
            break;
        case MHD_WEBSOCKET_STATUS_CLOSE_FRAME:
            handle_close_frame(handler, session, frame_data, frame_len);
            MHD_websocket_free(ws_stream, frame_data);
            break;
        case MHD_WEBSOCKET_STATUS_OK:
            // Need more data - go back to recv.
            if (frame_data != nullptr) MHD_websocket_free(ws_stream, frame_data);
            break;
        default:
            // Protocol error or unknown frame.
            if (frame_data != nullptr) MHD_websocket_free(ws_stream, frame_data);
            session.close(1002, "Protocol error");
            break;
    }
}

}  // namespace

static void decode_websocket_buffer(struct MHD_WebSocketStream* ws_stream,
                                    websocket_handler* handler,
                                    websocket_session& session,
                                    const char* buf, size_t buf_len) {
    size_t offset = 0;
    while (offset < buf_len && session.is_valid()) {
        char* frame_data = nullptr;
        size_t frame_len = 0;
        size_t step = 0;
        int status = MHD_websocket_decode(ws_stream,
                                           buf + offset,
                                           buf_len - offset,
                                           &step,
                                           &frame_data,
                                           &frame_len);
        offset += step;
        dispatch_websocket_frame(status, ws_stream, handler, session,
                                 frame_data, frame_len);
        // If decode consumed no bytes, we need more data.
        if (step == 0) break;
    }
}

namespace detail {

void webserver_impl::upgrade_handler(void *cls, struct MHD_Connection* connection,
                                void *req_cls, const char *extra_in,
                                size_t extra_in_size, MHD_socket sock,
                                struct MHD_UpgradeResponseHandle *urh) {
    std::ignore = connection;
    std::ignore = req_cls;

    // TASK-035: own ws_upgrade_data via unique_ptr for the duration of
    // the session. The shared_ptr<websocket_handler> inside `data`
    // keeps the handler alive across this upgrade callback even if a
    // concurrent unregister_ws_resource drops the registration in the
    // owning webserver.
    std::unique_ptr<ws_upgrade_data> data(static_cast<ws_upgrade_data*>(cls));
    websocket_handler* handler = data->handler.get();

    // Create a WebSocket stream for this connection
    struct MHD_WebSocketStream* ws_stream = nullptr;
    int ws_result = MHD_websocket_stream_init(&ws_stream,
                                               MHD_WEBSOCKET_FLAG_SERVER | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS,
                                               0);
    if (ws_result != MHD_WEBSOCKET_STATUS_OK || ws_stream == nullptr) {
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        return;
    }

    websocket_session session(sock, urh, ws_stream);
    handler->on_open(session);

    // Process any initial data that MHD may have buffered
    if (extra_in != nullptr && extra_in_size > 0) {
        decode_websocket_buffer(ws_stream, handler, session, extra_in, extra_in_size);
    }

    // Receive loop
    char buf[4096];
    while (session.is_valid()) {
        ssize_t got = recv(sock, buf, sizeof(buf), 0);
        if (got <= 0) break;

        decode_websocket_buffer(ws_stream, handler, session,
                                buf, static_cast<size_t>(got));
    }

    // Session destructor will free ws_stream and close urh.
    // `data` (and its shared_ptr handler reference) goes out of scope here.
}

}  // namespace detail
#endif  // HAVE_WEBSOCKET

namespace detail {

http_response webserver_impl::not_found_page(detail::modded_request* mr) const {
    if (parent->not_found_handler != nullptr) {
        return parent->not_found_handler(*mr->dhr);
    }
    return http_response::string(std::string{constants::NOT_FOUND_ERROR})
        .with_status(http_utils::http_not_found);
}

http_response webserver_impl::method_not_allowed_page(detail::modded_request* mr) const {
    if (parent->method_not_allowed_handler != nullptr) {
        return parent->method_not_allowed_handler(*mr->dhr);
    }
    return http_response::string(std::string{constants::METHOD_ERROR})
        .with_status(http_utils::http_method_not_allowed);
}

http_response webserver_impl::internal_error_page(
    detail::modded_request* mr,
    std::string_view msg,
    bool force_our) const {
    // TASK-031 / DR-009 §5.2 point 4: the double-fault fallback. Used when
    // the user-supplied internal_error_handler itself threw or when the
    // belt-and-suspenders site after get_raw_response_with_fallback fires.
    // The body is intentionally empty and the message is intentionally
    // ignored.
    if (force_our) {
        return http_response::empty()
            .with_status(http_utils::http_internal_server_error);
    }
    // §5.2 point 2/3: invoke the user handler with the originating message.
    if (parent->internal_error_handler != nullptr) {
        return parent->internal_error_handler(*mr->dhr, msg);
    }
    // No handler configured: surface the message in the default body so
    // the unset-handler path is informative for debugging. Operators who
    // need a fixed body can wire a constant-returning handler.
    return http_response::string(std::string{msg})
        .with_status(http_utils::http_internal_server_error);
}

void webserver_impl::log_dispatch_error(std::string_view msg) const {
    if (parent->log_error == nullptr) {
        return;
    }
    // A misbehaving user logger must not poison the catch from inside the
    // catch. Swallow any exception it throws; we have no recovery beyond
    // dropping the log line.
    try {
        parent->log_error(std::string(msg));
    } catch (...) {
        // Intentionally suppressed.
    }
}

http_response
webserver_impl::run_internal_error_handler_safely(
    detail::modded_request* mr,
    std::string_view msg) const {
    try {
        return internal_error_page(mr, msg, /*force_our=*/false);
    } catch (...) {
        // §5.2 point 4: the user handler itself threw. Log generically
        // and return an empty-body 500. No exception escapes from here.
        log_dispatch_error("internal_error_handler threw; "
                           "sending hardcoded empty-body 500");
        return internal_error_page(mr, "", /*force_our=*/true);
    }
}

void webserver_impl::invalidate_route_cache() {
    // Clear both the v1 and v2 caches. v1's cache is keyed on
    // standardized_url only; v2's is keyed on (method, path). A
    // registration may invalidate both, so clear both atomically.
    {
        std::lock_guard<std::mutex> lock(route_cache_mutex);
        route_cache_list.clear();
        route_cache_map.clear();
    }
    route_cache_v2.clear();
}

// TASK-027: 3-tier route lookup. Pipeline:
//   1. cache lookup (cache mutex only) — return on hit, promoting LRU.
//   2. on miss, take a shared_lock on route_table_mutex_:
//      a. exact_routes_ (hash, O(1))
//      b. param_and_prefix_routes_ (segment-trie)
//      c. regex_routes_ (linear scan)
//   3. on hit at any tier, drop the table lock and install into the
//      cache (lock acquisition order: table BEFORE cache; we never hold
//      both simultaneously — the table lock is released before the cache
//      lock is taken).
//
// The method-set check (does the entry serve `method`?) lives at the
// dispatch site, NOT here, because the existing 405 + Allow: header
// path needs to see the entry even when no method bit matches.
// Canonicalize a lookup path the same way http_endpoint canonicalizes a
// registration path: strip a trailing '/' (unless the path IS just "/"),
// prepend '/' if missing. Registration stores keys under url_complete,
// which is produced by this same normalization (see http_endpoint.cpp
// ll. 60-67) — so lookup must canonicalize too or "/foo" and "/foo/"
// would never share an entry. Matches the v1 dispatch path, which
// constructs a non-registration http_endpoint at lookup time and so
// gets the same normalization for free.
static std::string canonicalize_lookup_path(const std::string& path) {
    if (path.empty()) {
        return "/";
    }
    std::string out = path;
    if (out.front() != '/') {
        out.insert(out.begin(), '/');
    }
    if (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

webserver_impl::lookup_result
webserver_impl::lookup_v2(http_method method, const std::string& path) {
    lookup_result result;

    const std::string lookup_path = canonicalize_lookup_path(path);

    // Step 1: cache. Cache under the canonical key so /foo and /foo/
    // share an entry.
    cache_key key{method, lookup_path};
    cache_value cached;
    if (route_cache_v2.find(key, cached)) {
        result.found = true;
        result.tier = tier_hit::cache;
        result.entry = std::move(cached.entry);
        result.captured_params = std::move(cached.captured_params);
        return result;
    }

    // Step 2: walk the tiers under a shared lock.
    {
        std::shared_lock table_lock(route_table_mutex_);

        // 2a. Exact tier — single hash probe.
        auto exact_it = exact_routes_.find(lookup_path);
        if (exact_it != exact_routes_.end()) {
            result.found = true;
            result.tier = tier_hit::exact;
            result.entry = exact_it->second;
            // exact tier carries no parameters by definition.
        }

        // 2b. Radix tier — segment-trie walk.
        if (!result.found) {
            radix_match<route_entry> rm;
            if (param_and_prefix_routes_.find(lookup_path, rm) && rm.entry) {
                result.found = true;
                result.tier = tier_hit::radix;
                result.entry = *rm.entry;
                result.captured_params = std::move(rm.captures);
            }
        }

        // 2c. Regex tier — linear scan over pre-compiled std::regex objects.
        // Patterns were compiled once at registration time (in register_impl_
        // and on_methods_), so no compilation cost is paid per lookup.
        if (!result.found) {
            for (const auto& rr : regex_routes_) {
                if (std::regex_match(lookup_path, rr.compiled_re)) {
                    result.found = true;
                    result.tier = tier_hit::regex;
                    result.entry = rr.entry;
                    break;
                }
            }
        }
    }  // table_lock released.

    // Step 3: install into cache (cache mutex only).
    if (result.found) {
        cache_value v;
        v.entry = result.entry;
        v.captured_params = result.captured_params;
        route_cache_v2.insert(key, std::move(v));
    }

    return result;
}

}  // namespace detail

static std::string normalize_path(const std::string& path) {
    std::vector<std::string> segments;
    std::string::size_type start = 0;
    // Skip leading slash
    if (!path.empty() && path[0] == '/') {
        start = 1;
    }
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string seg = path.substr(start, end - start);
        if (seg == "..") {
            if (!segments.empty()) segments.pop_back();
        } else if (!seg.empty() && seg != ".") {
            segments.push_back(seg);
        }
        start = end + 1;
    }
    std::string normalized = "/";
    for (size_t i = 0; i < segments.size(); i++) {
        if (i > 0) normalized += "/";
        normalized += segments[i];
    }
    return normalized;
}

namespace detail {

bool webserver_impl::should_skip_auth(const std::string& path) const {
    std::string normalized = normalize_path(path);

    for (const auto& skip_path : parent->auth_skip_paths) {
        if (skip_path == normalized) return true;
        // Support wildcard suffix (e.g., "/public/*")
        if (skip_path.size() > 2 && skip_path.back() == '*' &&
            skip_path[skip_path.size() - 2] == '/') {
            std::string prefix = skip_path.substr(0, skip_path.size() - 1);
            if (normalized.compare(0, prefix.size(), prefix) == 0) return true;
        }
    }
    return false;
}

MHD_Result webserver_impl::requests_answer_first_step(MHD_Connection* connection, struct detail::modded_request* mr) {
    mr->dhr.reset(new http_request(connection, parent->unescaper));
    mr->dhr->set_file_cleanup_callback(parent->file_cleanup_callback);

    if (!mr->has_body) {
        return MHD_YES;
    }

    mr->dhr->set_content_size_limit(parent->content_size_limit);
    const char *encoding = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, http_utils::http_header_content_type);

    if (parent->post_process_enabled &&
        (nullptr != encoding &&
            ((0 == strncasecmp(http_utils::http_post_encoding_form_urlencoded, encoding, strlen(http_utils::http_post_encoding_form_urlencoded))) ||
             (0 == strncasecmp(http_utils::http_post_encoding_multipart_formdata, encoding, strlen(http_utils::http_post_encoding_multipart_formdata)))))) {
        const size_t post_memory_limit(32 * 1024);  // Same as #MHD_POOL_SIZE_DEFAULT
        mr->pp = MHD_create_post_processor(connection, post_memory_limit, &webserver_impl::post_iterator, mr);
    } else {
        mr->pp = nullptr;
    }
    return MHD_YES;
}

MHD_Result webserver_impl::requests_answer_second_step(MHD_Connection* connection, const char* method,
        const char* version, const char* upload_data,
        size_t* upload_data_size, struct detail::modded_request* mr) {
    if (0 == *upload_data_size) return complete_request(connection, mr, version, method);

    if (mr->has_body) {
#ifdef DEBUG
        std::cout << "Writing content: " << std::string(upload_data, *upload_data_size) << std::endl;
#endif  // DEBUG
        // The post iterator is only created from the libmicrohttpd for content of type
        // multipart/form-data and application/x-www-form-urlencoded
        // all other content (which is indicated by mr-pp == nullptr)
        // has to be put to the content even if put_processed_data_to_content is set to false
        if (mr->pp == nullptr || parent->put_processed_data_to_content) {
            mr->dhr->grow_content(upload_data, *upload_data_size);
        }

        if (mr->pp != nullptr) {
            mr->ws = parent;
            MHD_post_process(mr->pp, upload_data, *upload_data_size);
            if (mr->upload_ostrm != nullptr && mr->upload_ostrm->is_open()) {
                mr->upload_ostrm->close();
            }
        }
    }

    *upload_data_size = 0;
    return MHD_YES;
}

// TASK-013: dispatch helpers replacing the v1 `get_raw_response`,
// `decorate_response`, and `enqueue_response` virtuals on http_response.
// Now that http_response is a final value type and the v1 polymorphic
// subclass hierarchy is gone, the wire-construction logic lives here in
// the dispatch path. webserver_impl is a friend of http_response (declared
// in http_response.hpp) so it can reach body_ directly.
//
// materialize_response: ask the body to produce a fresh MHD_Response
// with no headers/footers/cookies attached.
//
// decorate_mhd_response: walk the response's header/footer/cookie maps
// and attach each to the materialized MHD_Response.
MHD_Response* webserver_impl::materialize_response(http_response* resp) {
    if (resp == nullptr || resp->body_ == nullptr) {
        return nullptr;
    }
    return resp->body_->materialize();
}

void webserver_impl::decorate_mhd_response(MHD_Response* response,
                                      const http_response& resp) {
    for (const auto& [k, v] : resp.get_headers()) {
        MHD_add_response_header(response, k.c_str(), v.c_str());
    }
    for (const auto& [k, v] : resp.get_footers()) {
        MHD_add_response_footer(response, k.c_str(), v.c_str());
    }
    for (const auto& [k, v] : resp.get_cookies()) {
        MHD_add_response_header(response, "Set-Cookie",
                                (k + "=" + v).c_str());
    }
}

struct MHD_Response* webserver_impl::get_raw_response_with_fallback(detail::modded_request* mr) {
    // TASK-036 / DR-010: every assignment into mr->response_ uses
    // emplace(std::move(...)); the optional owns the value and the
    // deferred-body trampoline keeps a pointer into it for the lifetime
    // of the modded_request.
    auto materialize_current = [&]() -> struct MHD_Response* {
        return materialize_response(mr->response_ ? &*mr->response_ : nullptr);
    };
    try {
        struct MHD_Response* raw = materialize_current();
        if (raw == nullptr) {
            // TASK-031: no exception was thrown, but the body materializer
            // returned null. Route through the safe internal-error path so
            // a misbehaving user handler can't escape.
            mr->response_.emplace(run_internal_error_handler_safely(
                mr, "materialize_response returned null"));
            raw = materialize_current();
        }
        return raw;
    } catch(const std::invalid_argument&) {
        try {
            mr->response_.emplace(not_found_page(mr));
            return materialize_current();
        } catch(...) {
            return nullptr;
        }
    } catch(const std::exception& e) {
        log_dispatch_error(std::string("materialize threw: ") + e.what());
        try {
            mr->response_.emplace(run_internal_error_handler_safely(mr, e.what()));
            return materialize_current();
        } catch(...) {
            return nullptr;
        }
    } catch(...) {
        log_dispatch_error("materialize threw unknown exception");
        try {
            mr->response_.emplace(run_internal_error_handler_safely(mr,
                                                         "unknown exception"));
            return materialize_current();
        } catch(...) {
            return nullptr;
        }
    }
}

#ifdef HAVE_WEBSOCKET
std::optional<const char*>
webserver_impl::validate_websocket_handshake(MHD_Connection* connection) {
    const char* connection_header = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                                MHD_HTTP_HEADER_CONNECTION);
    const char* ws_version = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                         "Sec-WebSocket-Version");
    const char* ws_key = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                     "Sec-WebSocket-Key");
    if (connection_header == nullptr || strcasestr(connection_header, "Upgrade") == nullptr) {
        return std::nullopt;
    }
    if (ws_version == nullptr || strcmp(ws_version, "13") != 0) {
        return std::nullopt;
    }
    if (ws_key == nullptr || ws_key[0] == '\0') {
        return std::nullopt;
    }
    return ws_key;
}

std::optional<MHD_Result>
webserver_impl::complete_websocket_upgrade(MHD_Connection* connection,
                                           detail::modded_request* mr,
                                           const char* ws_key) {
    std::shared_lock lock(registered_resources_mutex);
    auto ws_it = registered_ws_handlers.find(mr->standardized_url);
    if (ws_it == registered_ws_handlers.end()) {
        return std::nullopt;
    }
    // TASK-035: take a shared_ptr copy under the shared lock so the
    // handler is kept alive across the MHD upgrade callback even if
    // unregister_ws_resource erases the slot mid-upgrade.
    std::shared_ptr<websocket_handler> handler_sp = ws_it->second;
    lock.unlock();

    auto* data = new ws_upgrade_data{this, std::move(handler_sp)};
    struct MHD_Response* response = MHD_create_response_for_upgrade(
        &webserver_impl::upgrade_handler, data);
    if (response == nullptr) {
        delete data;
        return std::nullopt;
    }
    MHD_add_response_header(response, MHD_HTTP_HEADER_UPGRADE, "websocket");

    // Compute Sec-WebSocket-Accept from client's key (RFC 6455 §4.2.2).
    // Base64 of SHA-1 = 28 chars + null.
    char accept_header[29];
    if (MHD_websocket_create_accept_header(ws_key, accept_header) == MHD_WEBSOCKET_STATUS_OK) {
        MHD_add_response_header(response, "Sec-WebSocket-Accept", accept_header);
    }
    MHD_Result to_ret = (MHD_Result) MHD_queue_response(connection,
                                                       MHD_HTTP_SWITCHING_PROTOCOLS,
                                                       response);
    MHD_destroy_response(response);
    return to_ret;
}
#endif  // HAVE_WEBSOCKET

std::optional<MHD_Result>
webserver_impl::try_handle_websocket_upgrade(MHD_Connection* connection,
                                             detail::modded_request* mr) {
#ifdef HAVE_WEBSOCKET
    const char* upgrade_header = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                             MHD_HTTP_HEADER_UPGRADE);
    if (upgrade_header == nullptr || strcasecmp(upgrade_header, "websocket") != 0) {
        return std::nullopt;
    }
    auto ws_key = validate_websocket_handshake(connection);
    if (!ws_key) {
        // RFC 6455 §4.2.1: required handshake header missing or malformed.
        struct MHD_Response* bad_response = MHD_create_response_from_buffer(0, nullptr,
                                                                            MHD_RESPMEM_PERSISTENT);
        MHD_Result ret = (MHD_Result) MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST,
                                                         bad_response);
        MHD_destroy_response(bad_response);
        return ret;
    }
    return complete_websocket_upgrade(connection, mr, *ws_key);
#else
    (void)connection;
    (void)mr;
    return std::nullopt;
#endif  // HAVE_WEBSOCKET
}

std::optional<webserver_impl::regex_route_lookup>
webserver_impl::lookup_route_cache(const std::string& key) {
    std::lock_guard<std::mutex> cache_lock(route_cache_mutex);
    auto cache_it = route_cache_map.find(key);
    if (cache_it == route_cache_map.end()) {
        return std::nullopt;
    }
    // Cache hit — promote to LRU front and copy the match data while
    // still under the cache lock, so a concurrent invalidation can't
    // free the cached endpoint out from under us.
    route_cache_list.splice(route_cache_list.begin(), route_cache_list, cache_it->second);
    const route_cache_entry& cached = cache_it->second->second;
    return regex_route_lookup{
        cached.resource,
        cached.matched_endpoint.get_url_pars(),
        cached.matched_endpoint.get_chunk_positions(),
    };
}

std::optional<webserver_impl::regex_route_scan_hit>
webserver_impl::scan_regex_routes(const detail::http_endpoint& target) {
    // Longest-match-wins tie-breaking: prefer the endpoint with more
    // url-pieces; among equals, prefer the longer url_complete string.
    bool found = false;
    size_t len = 0;
    size_t tot_len = 0;
    map<detail::http_endpoint, std::shared_ptr<http_resource>>::iterator found_endpoint;
    for (auto it = registered_resources_regex.begin();
            it != registered_resources_regex.end(); ++it) {
        size_t endpoint_pieces_len = it->first.get_url_pieces().size();
        size_t endpoint_tot_len = it->first.get_url_complete().size();
        if (found && endpoint_pieces_len <= len
                && !(endpoint_pieces_len == len && endpoint_tot_len > tot_len)) {
            continue;
        }
        if (!it->first.match(target)) continue;
        found = true;
        len = endpoint_pieces_len;
        tot_len = endpoint_tot_len;
        found_endpoint = it;
    }
    if (!found) return std::nullopt;
    return regex_route_scan_hit{found_endpoint->first, found_endpoint->second};
}

void webserver_impl::store_route_cache(const std::string& key,
                                       const detail::http_endpoint& matched,
                                       std::shared_ptr<http_resource> hrm) {
    std::lock_guard<std::mutex> cache_lock(route_cache_mutex);
    route_cache_list.emplace_front(key, route_cache_entry{matched, std::move(hrm)});
    route_cache_map[key] = route_cache_list.begin();
    if (route_cache_map.size() > ROUTE_CACHE_MAX_SIZE) {
        route_cache_map.erase(route_cache_list.back().first);
        route_cache_list.pop_back();
    }
}

void webserver_impl::apply_extracted_params(detail::modded_request* mr,
        const detail::http_endpoint& target,
        const std::vector<std::string>& url_pars,
        const std::vector<int>& chunks) {
    const auto& url_pieces = target.get_url_pieces();
    for (unsigned int i = 0; i < url_pars.size(); i++) {
        if (chunks[i] < 0 || static_cast<size_t>(chunks[i]) >= url_pieces.size()) continue;
        mr->dhr->set_arg(url_pars[i], url_pieces[chunks[i]]);
    }
}

bool webserver_impl::resolve_resource_for_request(detail::modded_request* mr,
        std::shared_ptr<http_resource>& hrm) {
    std::shared_lock registered_resources_lock(registered_resources_mutex);

    if (parent->single_resource) {
        hrm = registered_resources.begin()->second;
        return true;
    }

    auto fe = registered_resources_str.find(mr->standardized_url.c_str());
    if (fe != registered_resources_str.end()) {
        hrm = fe->second;
        return true;
    }

    if (!parent->regex_checking) return false;

    detail::http_endpoint endpoint(mr->standardized_url.c_str(), false, false, false);

    if (auto cached = lookup_route_cache(mr->standardized_url)) {
        hrm = cached->hrm;
        apply_extracted_params(mr, endpoint, cached->url_pars, cached->chunks);
        return true;
    }

    auto scan_hit = scan_regex_routes(endpoint);
    if (!scan_hit) return false;

    hrm = scan_hit->hrm;
    store_route_cache(mr->standardized_url, scan_hit->endpoint, hrm);
    apply_extracted_params(mr, endpoint, scan_hit->endpoint.get_url_pars(),
                           scan_hit->endpoint.get_chunk_positions());
    return true;
}

bool webserver_impl::apply_auth_short_circuit(detail::modded_request* mr) {
    if (parent->auth_handler == nullptr) return false;
    std::string path(mr->dhr->get_path());
    if (should_skip_auth(path)) return false;
    // TASK-036 boundary: auth_handler_ptr is intentionally NOT touched
    // by this task (PRD-AUTH out of scope). It still returns a
    // shared_ptr<http_response>; when set it acts as a poor-man's
    // optional. Move the pointee into the by-value slot so the
    // dispatch path is uniform from here on. The shared_ptr drops at
    // end of scope.
    std::shared_ptr<http_response> auth_response = parent->auth_handler(*mr->dhr);
    if (auth_response == nullptr) return false;
    mr->response_.emplace(std::move(*auth_response));
    return true;
}

std::string webserver_impl::serialize_allow_methods(method_set allowed) const {
    // TASK-021: enum-declaration order (GET, HEAD, POST, PUT, DELETE,
    // CONNECT, OPTIONS, TRACE, PATCH). v1 used std::map iteration order
    // (alphabetical); the only existing assertion in-tree is
    // "HEAD, POST" which is preserved by enum order.
    std::string header_value;
    for (std::uint8_t i = 0;
            i < static_cast<std::uint8_t>(http_method::count_); ++i) {
        auto m = static_cast<http_method>(i);
        if (!allowed.contains(m)) continue;
        if (!header_value.empty()) header_value += ", ";
        header_value += std::string(to_string(m));
    }
    return header_value;
}

void webserver_impl::dispatch_resource_handler(detail::modded_request* mr,
        const std::shared_ptr<http_resource>& hrm) {
    try {
        if (mr->pp != nullptr) {
            MHD_destroy_post_processor(mr->pp);
            mr->pp = nullptr;
        }
        if (hrm->is_allowed(mr->method_enum)) {
            // TASK-036: pointer-to-member dispatch returns http_response
            // by value (DR-004); the prvalue is moved into the
            // per-connection optional anchor. shared_ptr has no
            // operator->*, so call as ((*ptr).*pmf)(...).
            mr->response_.emplace(((*hrm).*(mr->callback))(*mr->dhr));
            if (mr->response_->get_status() == -1) {
                // TASK-031: no exception was thrown, but the handler
                // returned the default-sentinel response. Route through
                // the safe internal-error path so a misbehaving user
                // handler can't escape into libmicrohttpd. (The "null
                // response" arm from v1 is now structurally impossible:
                // the optional always holds a value at this point.)
                mr->response_.emplace(run_internal_error_handler_safely(
                    mr, "handler returned null response"));
            }
            return;
        }
        // Method not allowed: serialize the allow-mask into a header.
        mr->response_.emplace(method_not_allowed_page(mr));
        std::string header_value = serialize_allow_methods(hrm->get_allowed_methods());
        if (!header_value.empty()) {
            mr->response_->with_header(http_utils::http_header_allow, header_value);
        }
    } catch (const std::exception& e) {
        // TASK-031 / DR-009 §5.2 point 2: handler threw std::exception.
        // Log via error_logger, forward e.what() to internal_error_handler.
        // run_internal_error_handler_safely contains a possible re-throw
        // from the user handler (point 4).
        log_dispatch_error(std::string("dispatch: handler threw "
                                       "std::exception: ") + e.what());
        mr->response_.emplace(run_internal_error_handler_safely(mr, e.what()));
    } catch (...) {
        // §5.2 point 3: handler threw non-std::exception. Same flow as
        // the std::exception arm but with the sentinel message.
        log_dispatch_error("dispatch: handler threw unknown exception");
        mr->response_.emplace(run_internal_error_handler_safely(mr, "unknown exception"));
    }
}

MHD_Result webserver_impl::materialize_and_queue_response(MHD_Connection* connection,
                                                          detail::modded_request* mr) {
    struct MHD_Response* raw_response = get_raw_response_with_fallback(mr);
    if (raw_response == nullptr) {
        // Belt-and-suspenders: even get_raw_response_with_fallback's
        // own try/catch couldn't produce a response. Force the
        // empty-body 500 fallback so MHD always has something to queue.
        mr->response_.emplace(internal_error_page(mr, "", /*force_our=*/true));
        raw_response = materialize_response(&*mr->response_);
    }
    decorate_mhd_response(raw_response, *mr->response_);
    int to_ret = MHD_queue_response(connection, mr->response_->get_status(), raw_response);
    MHD_destroy_response(raw_response);
    return (MHD_Result) to_ret;
}

MHD_Result webserver_impl::finalize_answer(MHD_Connection* connection,
                                           struct detail::modded_request* mr) {
    if (auto ws_result = try_handle_websocket_upgrade(connection, mr)) {
        return *ws_result;
    }

    // TASK-023: hold a shared_ptr copy across dispatch. If a concurrent
    // unregister_resource erases the route mid-call, the resource stays
    // alive until our local shared_ptr drops at the end of
    // finalize_answer.
    std::shared_ptr<http_resource> hrm;
    bool found = resolve_resource_for_request(mr, hrm);

    if (found && apply_auth_short_circuit(mr)) {
        found = false;  // Skip resource rendering, go directly to response.
    }

    if (found) {
        dispatch_resource_handler(mr, hrm);
    } else if (!mr->response_) {
        mr->response_.emplace(not_found_page(mr));
    }

    return materialize_and_queue_response(connection, mr);
}

MHD_Result webserver_impl::complete_request(MHD_Connection* connection, struct detail::modded_request* mr, const char* version, const char* method) {
    mr->ws = parent;

    mr->dhr->set_path(mr->standardized_url);
    mr->dhr->set_method(method);
    mr->dhr->set_version(version);

    return finalize_answer(connection, mr);
}

void webserver_impl::resolve_method_callback(const char* method,
                                              detail::modded_request* mr) {
    // Case-sensitive per RFC 7230 §3.1.1: HTTP method is case-sensitive.
    // TASK-021: also record the enum form once so finalize_answer can
    // call hrm->is_allowed without re-scanning the wire string.
    // Unrecognised methods leave mr->method_enum at the default
    // (count_), so is_allowed(count_) returns false and the request
    // takes the 405 path. Pre-existing latent bug: mr->callback may
    // also be left un-set here; see TASK-027 for the dispatch redesign.
    if (0 == strcmp(method, http_utils::http_method_get)) {
        mr->callback = &http_resource::render_get;
        mr->method_enum = http_method::get;
    } else if (0 == strcmp(method, http_utils::http_method_post)) {
        mr->callback = &http_resource::render_post;
        mr->method_enum = http_method::post;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_put)) {
        mr->callback = &http_resource::render_put;
        mr->method_enum = http_method::put;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_delete)) {
        mr->callback = &http_resource::render_delete;
        mr->method_enum = http_method::del;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_patch)) {
        mr->callback = &http_resource::render_patch;
        mr->method_enum = http_method::patch;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_head)) {
        mr->callback = &http_resource::render_head;
        mr->method_enum = http_method::head;
    } else if (0 == strcmp(method, http_utils::http_method_connect)) {
        mr->callback = &http_resource::render_connect;
        mr->method_enum = http_method::connect;
    } else if (0 == strcmp(method, http_utils::http_method_trace)) {
        mr->callback = &http_resource::render_trace;
        mr->method_enum = http_method::trace;
    } else if (0 == strcmp(method, http_utils::http_method_options)) {
        mr->callback = &http_resource::render_options;
        mr->method_enum = http_method::options;
    }
}

MHD_Result webserver_impl::answer_to_connection(void* cls, MHD_Connection* connection, const char* url, const char* method,
        const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls) {
    auto* mr = static_cast<detail::modded_request*>(*con_cls);
    auto* impl = static_cast<webserver_impl*>(cls);

    if (mr->dhr) {
        return impl->requests_answer_second_step(connection, method, version,
                                                  upload_data, upload_data_size, mr);
    }

    const MHD_ConnectionInfo* conninfo =
        MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CONNECTION_FD);
    if (conninfo != nullptr && impl->parent->tcp_nodelay) {
        int yes = 1;
        setsockopt(conninfo->connect_fd, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<char*>(&yes), sizeof(int));
    }

    std::string t_url = url;
    base_unescaper(&t_url, impl->parent->unescaper);
    mr->standardized_url = http_utils::standardize_url(t_url);
    mr->has_body = false;

    webserver_impl::access_log(impl->parent, mr->complete_uri + " METHOD: " + method);
    resolve_method_callback(method, mr);

    return impl->requests_answer_first_step(connection, mr);
}

}  // namespace detail

}  // namespace httpserver
