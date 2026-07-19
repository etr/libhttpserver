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
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/detail/upload_pipeline.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"
#include "httpserver/detail/body.hpp"
#include "httpserver/detail/connection_state.hpp"

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

    // Fire request_completed BEFORE the modded_request is
    // destroyed so the ctx pointers remain backed by live storage. The
    // gate-and-fire helper reads any_hooks_[request_completed] and
    // builds the ctx from mr->request, mr->response, and the MHD
    // termination code. The fire site is the very first thing this
    // callback does, while mr is still untouched.
    auto* mr = static_cast<detail::modded_request*>(*con_cls);
    if (mr != nullptr) {
        // mr->ws is the parent webserver -- set in answer_to_connection
        // (hoisted there). For paths where
        // answer_to_connection never ran (e.g., very early MHD failures),
        // mr->ws may be null; skip the fire site in that degenerate case.
        if (mr->ws != nullptr && mr->ws->impl_ != nullptr) {
            mr->ws->impl_->hooks_dispatch_.fire_request_completed_gated(mr, toe);
        }
    }

    // (1) Destroy the modded_request first. This runs ~http_request,
    //     which calls the arena_deleter on the impl's unique_ptr (a
    //     destructor-only call: monotonic_buffer_resource never
    //     deallocates per-object), running every PMR string/vector/map
    //     destructor before we reset the arena.
    delete static_cast<detail::modded_request*>(*con_cls);
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
    // the modded_request (and thus all arena-backed objects) before this
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
// modded_request); the file branch routes through the owning webserver's
// upload_ instance.
MHD_Result webserver_impl::post_iterator(void *cls, enum MHD_ValueKind kind,
        const char *key, const char *filename, const char *content_type,
        const char *transfer_encoding, const char *data, uint64_t off, size_t size) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = kind;
    auto* mr = static_cast<detail::modded_request*>(cls);

    if (!filename) {
        return upload_pipeline::handle_post_form_arg(mr, key, data, size, off);
    }
    return mr->ws->impl_->upload_.iterate_file(
        mr, key, filename, content_type, transfer_encoding, data, size);
}

}  // namespace detail

}  // namespace httpserver
