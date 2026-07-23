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
#ifdef HAVE_WEBSOCKET
#include <microhttpd_ws.h>
#endif  // HAVE_WEBSOCKET
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/feature_unavailable.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/websocket_handler.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/lambda_resource.hpp"
#include "httpserver/detail/connection_context.hpp"
#include "httpserver/detail/upload_pipeline.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"
#include "httpserver/detail/response_body.hpp"
#include "httpserver/detail/connection_state.hpp"
#include "httpserver/detail/path_normalize.hpp"
#include "httpserver/detail/resource_hook_table.hpp"

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#endif  // HAVE_GNUTLS

using std::string;
using std::pair;
using std::vector;
using std::map;
using std::set;

namespace httpserver {

using httpserver::http::http_utils;
using httpserver::http::ip_representation;
using httpserver::http::base_unescaper;

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

void webserver_impl::request_completed(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) {
    // These parameters are passed to respect the MHD interface, but are not needed here.
    std::ignore = cls;

    // Fire request_completed BEFORE the connection_context is
    // destroyed so the ctx pointers remain backed by live storage. The
    // gate-and-fire helper reads any_hooks_[request_completed] and
    // builds the ctx from conn->request, conn->response, and the MHD
    // termination code. The fire site is the very first thing this
    // callback does, while conn is still untouched.
    auto* conn = static_cast<detail::connection_context*>(*con_cls);
    if (conn != nullptr) {
        // conn->ws is the parent webserver -- set in answer_to_connection
        // (hoisted there). For paths where
        // answer_to_connection never ran (e.g., very early MHD failures),
        // conn->ws may be null; skip the fire site in that degenerate case.
        if (conn->ws != nullptr && conn->ws->impl_ != nullptr) {
            conn->ws->impl_->hooks_dispatch_.fire_request_completed_gated(conn, toe);
        }
    }

    // (1) Destroy the connection_context first. This runs ~http_request,
    //     which calls the arena_deleter on the impl's unique_ptr (a
    //     destructor-only call: monotonic_buffer_resource never
    //     deallocates per-object), running every PMR string/vector/map
    //     destructor before we reset the arena.
    delete static_cast<detail::connection_context*>(*con_cls);
    *con_cls = nullptr;

    // (2) Now that no live object inside the arena's storage remains,
    //     rewind the bump pointer AND secure-zero the initial buffer so
    //     credentials from the completed request do not linger in the
    //     reused memory (CWE-226 / CWE-14).
    //     reset_arena() does release + non-elidable zero atomically; see
    //     connection_state::reset_arena() docs and
    //     httpserver/detail/secure_zero.hpp for the platform-specific
    //     dispatch. The next request on this keep-alive
    //     connection reuses the same memory (verified by
    //     http_request_arena and connection_state_sentinel unit tests).
    //
    // Unconditional release is correct regardless of the `toe`
    // (MHD_RequestTerminationCode) value: step (1) above always destroys
    // the connection_context (and thus all arena-backed objects) before this
    // point, so the arena holds no live objects for any termination code,
    // including MHD_REQUEST_TERMINATED_WITH_ERROR. Resetting unconditionally
    // is therefore both safe and necessary to prepare the arena for the next
    // keep-alive request.
    //
    // MHD ordering guarantee: NOTIFY_COMPLETED always fires before
    // NOTIFY_CLOSED for the same connection (MHD documentation, section
    // "Thread model guarantees"). Therefore the connection_state pointer
    // accessed here is guaranteed live. The NOTIFY_CLOSED handler
    // (connection_notify) must NOT be called concurrently on a different
    // thread for the same connection while this callback is executing.
    // (Thread-safety ordering invariant.)
    if (connection != nullptr) {
        const MHD_ConnectionInfo* ci = MHD_get_connection_info(
            connection, MHD_CONNECTION_INFO_SOCKET_CONTEXT);
        if (ci != nullptr && ci->socket_context != nullptr) {
            auto* cs = static_cast<detail::connection_state*>(ci->socket_context);
            cs->reset_arena();
        }
    }
}

// connection_notify (NOTIFY_STARTED / NOTIFY_CLOSED) and policy_callback:
// the per-connection lifecycle trampolines. Both fire lifecycle hooks and
// reach into <sys/socket.h> via the peer-address adapter in the anonymous
// namespace above. (Previously carved into webserver_callbacks_lifecycle.cpp
// for the FILE_LOC_MAX gate; folded back once the SLOC metric stopped
// counting single-character lines brought this TU comfortably under the
// ceiling.)

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
                ws_impl->hooks_dispatch_.fire_connection_opened(ctx);
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
                ws_impl->hooks_dispatch_.fire_connection_closed(ctx);
            }
            // MHD ordering guarantee: NOTIFY_COMPLETED fires before
            // NOTIFY_CLOSED for the same connection. By the time we reach
            // this branch, request_completed has already called reset_arena()
            // and the connection_context has already been deleted -- so the
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
        impl->hooks_dispatch_.fire_accept_decision(ctx);
    }

    return decision;
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

    if (ws == nullptr || ws->config.psk_cred_handler == nullptr) {
        return -1;
    }

    std::string psk_hex = ws->config.psk_cred_handler(std::string(username));
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
    if (ws == nullptr || ws->config.sni_callback == nullptr || server_name == nullptr) {
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
    auto [cert_pem, key_pem] = ws->config.sni_callback(name);
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

void* webserver_impl::uri_log(void* cls, const char* uri, struct MHD_Connection *con) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;
    std::ignore = con;

    auto conn = std::make_unique<detail::connection_context>();
    // MHD may invoke this callback with a null uri before the request line
    // has been parsed (e.g. port scans, half-open connections, or non-HTTP
    // traffic on the listening port). Treat that as an empty URI so the
    // std::string assignment does not throw std::logic_error and abort the
    // process via std::terminate. See issue #371.
    conn->complete_uri = (uri != nullptr) ? uri : "";
    return reinterpret_cast<void*>(conn.release());
}

void webserver_impl::error_log(void* cls, const char* fmt, va_list ap) {
    webserver* dws = static_cast<webserver*>(cls);

    std::string msg;
    msg.resize(80);  // Assume one line will be enough most of the time.

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

    if (dws->config.log_error != nullptr) dws->config.log_error(msg);
}

size_t webserver_impl::unescaper_func(void * cls, struct MHD_Connection *c, char *s) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;
    std::ignore = c;

    // No-op unescaper: returns the input length and does not mutate `s`,
    // so MHD ships raw percent-encoded bytes to our get_connection_values
    // callbacks. Decoding is performed by libhttpserver itself:
    //   - request URL: base_unescaper() in webserver_request.cpp
    //     (answer_to_connection, line ~418)
    //   - GET args: unescape_in_arena() in http_request_impl.cpp
    // This is required so we can honour a user-registered unescaper hook
    // (create_webserver::unescaper(...)) and route GET-arg decoding through
    // the per-connection arena. Per microhttpd.h, registering a custom
    // MHD_OPTION_UNESCAPE_CALLBACK suppresses MHD's internal "%HH" decode,
    // so this no-op is what guarantees MHD does not pre-decode behind our
    // back (which would otherwise cause double-decoding of `?key=%2F`).
    //
    // Historical note (verified against upstream libmicrohttpd
    // ChangeLog): this callback originally also worked around a v0.99-era
    // MHD bug where the internal unescape could produce strings containing
    // embedded NULs (e.g. from `%00`), which then broke
    // MHD_get_connection_values / MHD_lookup_connection_value lookups
    // downstream. Upstream resolved that by adding explicit
    // binary-zero-aware key/value storage and the size-carrying
    // MHD_KeyValueIteratorN callback in libmicrohttpd 0.9.64 (released
    // 2019-06-09; see ChangeLog entries dated 2019-03-20, 2019-05-01,
    // 2019-05-03; https://git.gnunet.org/libmicrohttpd.git/log/?qt=grep&q=0.9.64).
    // configure.ac requires libmicrohttpd >= 1.0.0 (released 2024-02-01),
    // so the original v0.99 bug is no longer reachable; the no-op stays
    // for the architectural reasons above.
    if (s == nullptr) return 0;
    return std::char_traits<char>::length(s);
}

// MHD post-iterator trampoline. Registered with MHD_create_post_processor
// (webserver_body_pipeline.cpp); its address is taken, so it stays a static
// webserver_impl member. The upload logic moved to the upload_pipeline
// behavior service (DR-014 §4.11). The no-file form-arg branch uses the
// static upload_pipeline::handle_post_form_arg so it is reachable without an
// owning webserver (post_iterator_null_key_test feeds a null-ws
// connection_context); the file branch routes through the owning webserver's
// upload_ instance.
MHD_Result webserver_impl::post_iterator(void *cls, enum MHD_ValueKind kind,
        const char *key, const char *filename, const char *content_type,
        const char *transfer_encoding, const char *data, uint64_t off, size_t size) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = kind;
    auto* conn = static_cast<detail::connection_context*>(cls);

    if (!filename) {
        return upload_pipeline::handle_post_form_arg(conn, key, data, size, off);
    }
    return conn->ws->impl_->upload_.iterate_file(
        conn, key, filename, content_type, transfer_encoding, data, size);
}

}  // namespace detail



// ===== webserver_request.cpp (answer_to_connection + dispatch
// helpers: resolve_method_callback / should_skip_auth / normalize_path)
// ============================================================



namespace detail {

namespace {

// NOTE: the caller (should_skip_auth) must receive an already-unescaped
// path (i.e., no %XX sequences remain). libhttpserver's base_unescaper()
// (called in answer_to_connection) runs before should_skip_auth, so this
// invariant is satisfied on the dispatch path. Double slashes (//) and
// trailing slashes are collapsed automatically: empty segments between
// consecutive '/' separators are skipped, producing the same result as a
// single '/'.
//
// Path-normalization chain (per request, in order):
//   1. http_utils::standardize_url (answer_to_connection) collapses
//      duplicate '/' runs and strips a trailing '/'.
//   2. normalize_path (below), applied to the standardized URL,
//      resolves dot-segments ("." / "..") into a canonical absolute
//      path; the result is stored as conn->standardized_url and is the
//      single path the rest of dispatch sees.
//   3. canonicalize_lookup_path, inside lookup_v2
//      (webserver_dispatch.cpp), canonicalizes slashes on the lookup
//      key (leading '/' ensured, trailing '/' stripped) so lookups hit
//      the same keys registration stored.
// should_skip_auth re-runs normalize_path on its input (idempotent on
// the already-normalized dispatch path), so the auth-skip decision and
// the route lookup always agree on the same canonical path. A past
// auth-bypass fix (dot-segment mismatch between auth and routing,
// commit a3e53f3) depends on this agreement -- do not let the two
// views diverge.
//
// Single pass, no per-segment heap allocation: each retained segment is
// appended straight into the output buffer, and a stack of segment start
// offsets lets ".." pop the previous segment by truncating the buffer
// back to that offset. This runs on every request (the auth-bypass
// canonicalisation, commit a3e53f3), so it avoids the vector<std::string>
// of owning segments the earlier tokenize-and-rebuild form allocated.
std::string normalize_path(std::string_view path) {
    std::string out;
    out.reserve(path.size() + 1);
    out.push_back('/');
    // Offsets into `out` where each retained segment begins, recorded
    // just BEFORE its leading separator so ".." can drop the whole "/seg"
    // by resizing back to the recorded offset.
    std::vector<std::string::size_type> seg_marks;
    std::string::size_type start = 0;
    if (!path.empty() && path[0] == '/') start = 1;
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string_view seg = path.substr(start, end - start);
        start = end + 1;
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (!seg_marks.empty()) {
                out.resize(seg_marks.back());
                seg_marks.pop_back();
            }
            continue;
        }
        seg_marks.push_back(out.size());
        if (out.size() > 1) out.push_back('/');
        out.append(seg.data(), seg.size());
    }
    return out;
}

}  // namespace

// Pre-normalize each auth_skip_paths entry once at
// webserver construction time.  Entries ending in "/*" keep their
// wildcard suffix; the prefix before the wildcard is normalized.
// Callers (webserver::webserver) pass the raw config-bag list and
// store the result on the webserver instance as a sibling to the
// original `auth_skip_paths` list.  Without this pre-normalization
// the skip list would be matched verbatim against a normalized
// request path, so non-canonical entries (e.g. "/public/",
// "/a/../b") would silently never match.
//
// Entries containing '%' are rejected with
// std::invalid_argument.  Skip-path entries must be provided in
// decoded form (the same form as the request path after
// libhttpserver's base_unescaper() runs).  A '%'-encoded entry would
// never match a decoded request path and would silently bypass auth
// for no route -- a misconfiguration hazard caught early here.
std::vector<std::string> normalize_auth_skip_paths(
        const std::vector<std::string>& raw) {
    std::vector<std::string> out;
    out.reserve(raw.size());
    for (const auto& entry : raw) {
        // Reject percent-encoded entries: skip-path entries must be
        // provided in decoded form.  A '%' in the entry indicates a
        // URL-encoded sequence that would never match the decoded
        // request path produced by libhttpserver's base_unescaper().
        if (entry.find('%') != std::string::npos) {
            throw std::invalid_argument(
                "auth_skip_paths entry contains a percent-encoded "
                "sequence ('" + entry + "'). "
                "Skip-path entries must be provided in decoded form "
                "(e.g. '/public/test', not '/public%2Ftest').");
        }
        // Wildcard suffix: strip the trailing "/*", normalize the
        // prefix, then re-append "/*".  The special case "/*" (size
        // == 2) means "match every path" and is stored as-is so
        // should_skip_auth can recognise it with the >= 2 guard.
        if (entry.size() >= 2 && entry.back() == '*' &&
            entry[entry.size() - 2] == '/') {
            if (entry.size() == 2) {
                // "/*" -- global wildcard: matches every path.
                out.push_back("/*");
            } else {
                std::string prefix = entry.substr(0, entry.size() - 2);
                std::string normalized_prefix = normalize_path(prefix);
                if (normalized_prefix == "/") {
                    // Prefix collapsed to root -- treat as "/*".
                    out.push_back("/*");
                } else {
                    out.push_back(normalized_prefix + "/*");
                }
            }
            continue;
        }
        out.push_back(normalize_path(entry));
    }
    return out;
}

bool webserver_impl::should_skip_auth(std::string_view path) const {
    // Empty-list early-out.  Servers with no
    // auth_skip_paths configured pay zero normalization cost.  This
    // is the production-typical case for any server whose auth
    // surface either covers every route or has no auth_handler at
    // all.
    if (parent->auth_skip_paths_normalized.empty()) {
        return false;
    }

    // Compare against the pre-normalized list (built
    // once at construction time) instead of re-normalizing skip-list
    // entries on every request.  The per-request normalize_path call
    // on @p path remains -- the inbound URL is per-request data and
    // cannot be pre-normalized.
    std::string normalized = normalize_path(path);

    for (const auto& skip_path : parent->auth_skip_paths_normalized) {
        if (skip_path == normalized) return true;
        // Support wildcard suffix (e.g., "/public/*").
        // Use >= 2 (not > 2) so the global
        // wildcard "/*" (size == 2) is handled.  When skip_path is "/*"
        // the prefix is "/" and every normalized path starts with "/",
        // so we return true immediately for any request.
        if (skip_path.size() >= 2 && skip_path.back() == '*' &&
            skip_path[skip_path.size() - 2] == '/') {
            std::string_view prefix(skip_path.data(), skip_path.size() - 1);
            if (normalized.compare(0, prefix.size(), prefix.data(),
                                   prefix.size()) == 0) {
                return true;
            }
        }
    }
    return false;
}

// requests_answer_first_step and requests_answer_second_step
// live in detail/webserver_body_pipeline.cpp to keep this TU under the
// 500-LOC ceiling (FILE_LOC_MAX in scripts/check-file-size.sh).

// finalize_answer, resolve_resource_for_request, dispatch_resource_handler,
// and fire_route_resolved_gated moved to the request_dispatcher behavior
// service; requests_answer_first_step / requests_answer_second_step /
// complete_request moved to the request_pipeline behavior service (both
// DR-014 §4.11). answer_to_connection (below) stays a webserver_impl static
// MHD trampoline: it does the per-request setup (start_time, standardized_url,
// method callback) and forwards into impl_->pipeline_.

void webserver_impl::resolve_method_callback(const char* method,
                                              detail::connection_context* conn) {
    // Case-sensitive per RFC 7230 §3.1.1: HTTP method is case-sensitive.
    // Also record the enum form once so finalize_answer can call
    // hrm->is_allowed without re-scanning the wire string.
    // Unrecognised methods leave conn->method_enum at the default
    // (count_), so is_allowed(count_) returns false and the request
    // takes the 405 path. conn->callback is left at nullptr (its
    // default-initializer value) for unrecognised methods; the 405 guard
    // in dispatch_resource_handler fires before it is ever invoked.
    //
    // Data-driven lookup table: a new HTTP method requires only one
    // row here (wire string, callback pointer, enum value, has_body
    // flag).
    using render_fn = http_response (http_resource::*)(const http_request&);
    struct method_entry {
        const char*  wire;
        render_fn    callback;
        http_method  enum_val;
        bool         has_body;
    };
    static const method_entry methods[] = {
        { http_utils::http_method_get,     &http_resource::render_get,     http_method::get,     false },
        { http_utils::http_method_post,    &http_resource::render_post,    http_method::post,    true  },
        { http_utils::http_method_put,     &http_resource::render_put,     http_method::put,     true  },
        { http_utils::http_method_delete,  &http_resource::render_delete,  http_method::del,     true  },
        { http_utils::http_method_patch,   &http_resource::render_patch,   http_method::patch,   true  },
        { http_utils::http_method_head,    &http_resource::render_head,    http_method::head,    false },
        { http_utils::http_method_connect, &http_resource::render_connect, http_method::connect, false },
        { http_utils::http_method_trace,   &http_resource::render_trace,   http_method::trace,   false },
        { http_utils::http_method_options, &http_resource::render_options, http_method::options, false },
    };
    for (const auto& e : methods) {
        if (0 == strcmp(method, e.wire)) {
            conn->callback    = e.callback;
            conn->method_enum = e.enum_val;
            if (e.has_body) conn->has_body = true;
            return;
        }
    }
    // Unrecognised method: leave conn->callback == nullptr and
    // conn->method_enum == http_method::count_ (both set by connection_context
    // default initialiser); the 405 guard fires before callback is used.
}

MHD_Result webserver_impl::answer_to_connection(void* cls, MHD_Connection* connection, const char* url, const char* method,
        const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls) {
    auto* conn = static_cast<detail::connection_context*>(*con_cls);
    auto* impl = static_cast<webserver_impl*>(cls);

    if (conn->request) {
        return impl->pipeline_.requests_answer_second_step(connection, method,
            version, upload_data, upload_data_size, conn);
    }

    const MHD_ConnectionInfo* conninfo =
        MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CONNECTION_FD);
    if (conninfo != nullptr && impl->parent->config.tcp_nodelay) {
        int yes = 1;
        setsockopt(conninfo->connect_fd, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<char*>(&yes), sizeof(int));
    }

    // Anchor for response_sent.elapsed and
    // request_completed.duration. Captured here -- the earliest moment
    // for the request inside the dispatch path. uri_log runs earlier
    // but is also invoked on non-HTTP traffic (#371); answer_to_connection
    // is the first point where a real HTTP request is unambiguously
    // in flight.
    conn->start_time = std::chrono::steady_clock::now();
    // Hoist the parent-webserver back-pointer here (rather than in
    // complete_request) so the request_completed firing site
    // can reach impl_->any_hooks_ even on request_received short-circuit
    // paths that may not reach complete_request.
    conn->ws = impl->parent;

    std::string t_url = url;
    base_unescaper(&t_url, impl->parent->config.unescaper);
    // SECURITY: collapse dot-segments ("." / "..") into the canonical
    // path here, at the single point where the routing/auth path is
    // derived. Both the route matcher (segment_trie::find via
    // conn->standardized_url) and should_skip_auth() must interpret the
    // path identically; should_skip_auth() runs the path through
    // normalize_path() (which pops ".."), but standardize_url() only
    // collapses duplicate '/' and a trailing '/'. Without this, a request
    // such as "/admin/../public/x" normalizes to "/public/x" for the
    // auth-skip check (auth skipped) yet the router still descends to the
    // "/admin" prefix/regex handler -- an authentication bypass. Applying
    // normalize_path() to the standardized URL makes the two views agree;
    // it is idempotent w.r.t. the normalize_path() call already in
    // should_skip_auth().
    conn->standardized_url = normalize_path(http_utils::standardize_url(t_url));
    conn->has_body = false;

    // log_access is now a response_sent alias (see webserver_aliases.cpp).
    resolve_method_callback(method, conn);

    return impl->pipeline_.requests_answer_first_step(connection, conn);
}

}  // namespace detail

}  // namespace httpserver
