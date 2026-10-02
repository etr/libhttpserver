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
     License along with this library; if not, see the file LICENSE in
     the distribution; if not, write to the Free Software Foundation,
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/

// Digest authentication policy above semantic request heads (TASK-115,
// RFC 7616, v2 behavior parity per PRD-V3N-REQ-002/038, DR-V3-001).
// The policy is vocabulary only: bounded parsing, nonce
// minting/classification, the replay ledger, and the
// algorithm-parameterized response computation live in the library's
// private implementation; the two factory forms validate their inputs
// (a realm carrying CR, LF, or NUL is refused -- CWE-113) before
// anything is observable.
//
// Scope (documented algorithm decisions, TASK-115 plan section 2):
// the two non-session algorithms the v2 surface exposed -- MD5 (the
// default; a REQUIRED part of RFC 7616) and SHA-256 -- with qop=auth
// and the RFC 2617 legacy no-qop chain; qop=auth-int is rejected
// (v2 rejected it at the factory). SHA-512-256, the -sess variants,
// userhash and username* are not offered: nothing pins them and the
// in-tree surface has no SHA-512.
//
// The verdict a check() returns is self-contained: it classifies the
// presented credentials, names the username when a Digest credential
// parsed, and carries a FRESH challenge (a nonce minted for this
// check) so an application can commit the 401 -- or the one 503 the
// taxonomy has, nonce_unavailable -- without keeping the policy
// alive. stale_nonce is the only verdict whose challenge appends
// stale=TRUE: the nonce was ours and is now expired or used up, so
// the client should retry with the new challenge rather than
// re-prompt; a replayed nc or a forged nonce gets no stale hint.
//
// Diagnostics exclude credentials by construction: factory failures
// report the violated rule, never the rejected value, and check()
// produces no diagnostic strings at all.

#ifndef SRC_HTTPSERVER_AUTH_DIGEST_AUTH_HPP_
#define SRC_HTTPSERVER_AUTH_DIGEST_AUTH_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/routes.hpp>

namespace httpserver {

namespace auth {

// The two documented Digest algorithms (TASK-115 plan section 2).
enum class digest_algorithm : std::uint8_t {
    md5,      // RFC 7616 REQUIRED; the v2 default
    sha_256,  // RFC 7616 section 6; the v2 SHA-256 surface
};

// Classification of one presented Authorization field. The taxonomy
// is observable; which secret was presented is not part of it.
enum class digest_auth_result : std::uint8_t {
    authenticated,          // response verified; username resolved
    no_credentials,         // no Authorization field on the request
    malformed_credentials,  // present but not a parsable Digest field
    credentials_rejected,   // forged/unknown nonce, bad response,
                            // wrong user/realm/uri/method/algorithm
    replayed_nonce,         // nc not strictly increasing: 401, NO stale
    stale_nonce,            // our nonce, expired or nc-exhausted:
                            // 401 WITH stale=TRUE
    nonce_unavailable,      // entropy failure: 503, no challenge
};

// The outcome of one policy check. challenge holds the complete
// WWW-Authenticate value with a fresh nonce (empty only for
// nonce_unavailable); challenge_body is the policy's configured body
// ("" gives Content-Length: 0, the v2 parity framing).
struct digest_auth_verdict {
    digest_auth_result result = digest_auth_result::no_credentials;
    std::string username;
    std::string challenge;
    std::string challenge_body;

    // True iff the presented credentials were accepted.
    bool allowed() const noexcept;

    // True iff the challenge carries the stale=TRUE retry hint.
    bool stale() const noexcept;

    // 401 for every non-authenticated result except
    // nonce_unavailable, which answers 503.
    http::status challenge_status() const noexcept;

    // The response fields for the challenge: WWW-Authenticate with
    // the pre-built value plus an explicit Content-Length matching
    // the body (the pinned HTTP/1.1 framing; unframed would select
    // chunked).
    http::fields challenge_fields() const;
};

// Per-policy tuning. Defaults mirror the v2 surface: MD5, a
// 300-second nonce TTL, an unbounded nc ledger of 1024 nonces, and a
// random per-policy opaque.
struct digest_auth_options {
    digest_algorithm algorithm = digest_algorithm::md5;
    std::chrono::seconds nonce_ttl{300};
    std::uint32_t max_nc = 0;  // 0 = unbounded use count per nonce
    std::size_t ledger_capacity = 1024;
    std::string opaque;             // empty -> per-policy 32-hex random
    std::string challenge_body;     // "" -> Content-Length: 0
};

// Application-supplied HA1 source (the v2 check_digest_auth_digest
// surface): resolves a username to its precomputed HA1 hex
// (H(username:realm:password) under the policy's algorithm) without
// the cleartext ever reaching the library. Returning false classifies
// the username as unknown.
using digest_ha1_source = concurrency::unique_function<bool(
    const std::string& username, std::string& ha1_hex)>;

// An immutable, thread-safe Digest authentication policy. Two
// factory forms: fixed credentials (HA1 precomputed at construction
// and the cleartext scrubbed; the username matched through a
// constant-time comparison) or an application HA1 source.
// Construction validates and pre-escapes the realm and opaque; the
// policy holds its own nonce MAC key, opaque value, and replay
// ledger, moved with it and shared by copy-free check()s. The
// default-constructed policy is the unconfigured one: empty realm,
// nothing authenticates, and its challenge names the empty realm.
class digest_auth_policy {
 public:
    digest_auth_policy() noexcept;

    digest_auth_policy(digest_auth_policy&& other) noexcept;
    digest_auth_policy& operator=(digest_auth_policy&& other) noexcept;
    ~digest_auth_policy();

    digest_auth_policy(const digest_auth_policy&) = delete;
    digest_auth_policy& operator=(const digest_auth_policy&) = delete;

    // Fixed-credential form: authenticates iff the presented username
    // equals @p username (constant-time) and the response verifies
    // against H(username:realm:password).
    static http::outcome create(std::string realm, std::string username,
                                std::string password,
                                digest_auth_options options,
                                digest_auth_policy& out);

    // HA1-source form: @p source resolves the username to its HA1.
    static http::outcome create(std::string realm,
                                digest_ha1_source source,
                                digest_auth_options options,
                                digest_auth_policy& out);

    // Classifies one request head. Parse failures classify as
    // malformed_credentials and answer the same challenge as an
    // absent header (v2 never answered 400 for bad credentials);
    // every non-authenticated verdict except nonce_unavailable
    // carries a fresh challenge; no domain exception escapes.
    digest_auth_verdict check(const http::request_head& head) const;

    const std::string& realm() const noexcept;
    digest_algorithm algorithm() const noexcept;

 private:
    struct state;
    std::string realm_;
    std::string username_;      // fixed form (source form: empty)
    std::string ha1_hex_;       // fixed form: precomputed, scrubbed
    digest_ha1_source source_;  // empty iff fixed form
    std::shared_ptr<const state> state_;
    digest_algorithm algorithm_ = digest_algorithm::md5;

    // Builds the shared state: the MAC key and (when unset) the
    // opaque drawn from OS entropy; a failed draw yields null.
    static std::shared_ptr<const state> make_state(
        const digest_auth_options& options);

    // The entropy draw the challenge mint uses, threaded as an
    // internal-only parameter (the type detail::digest::entropy_fill
    // spells): nullptr -- what every production call site passes --
    // resolves to the OS entropy source, so default behavior is
    // unchanged; tests inject a failing draw to drive the
    // nonce_unavailable mapping through the real settle() path.
    using entropy_fill = http::outcome (*)(std::span<std::byte>);

    // The freshly minted WWW-Authenticate value (empty string when
    // the configured policy's entropy draw failed; the unconfigured
    // policy's degenerate empty-nonce form otherwise).
    std::string minted_challenge(bool stale,
                                 entropy_fill fill = nullptr) const;

    // Records one classification and attaches the matching challenge;
    // a configured policy that cannot mint maps to nonce_unavailable.
    void settle(digest_auth_verdict& verdict,
                digest_auth_result result,
                entropy_fill fill = nullptr) const;

    digest_auth_verdict verify(std::string_view authorization,
                               const http::request_head& head,
                               digest_auth_verdict verdict) const;

#if defined(HTTPSERVER_COMPILATION)
    // Test-only bridge to the entropy seam above (the
    // webserver_test_access pattern): gated on HTTPSERVER_COMPILATION
    // so it never appears in installed-header compilations.
    friend struct digest_auth_test_access;
#endif
};

#if defined(HTTPSERVER_COMPILATION)
// White-box bridge for the Digest entropy seam: unit tests compiled
// with -DHTTPSERVER_COMPILATION (test/Makefile.am AM_CPPFLAGS) drive
// the REAL settle() with an injected entropy draw -- a permanently
// failing one pins the nonce_unavailable/503 mapping without
// fabricating the verdict. Matches webserver_test_access.
struct digest_auth_test_access {
    static void settle(const digest_auth_policy& policy,
                       digest_auth_verdict& verdict,
                       digest_auth_result result,
                       digest_auth_policy::entropy_fill fill) {
        policy.settle(verdict, result, fill);
    }
};
#endif

// Wraps one route handler with the policy: unauthenticated requests
// are answered with the verdict's challenge (401, or the 503 of
// nonce_unavailable) and the wrapped handler never runs;
// authenticated requests flow through unchanged. A configured
// challenge body is written with explicit Content-Length framing.
server::route_handler make_digest_guard(digest_auth_policy policy,
                                        server::route_handler handler);

}  // namespace auth

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_AUTH_DIGEST_AUTH_HPP_
