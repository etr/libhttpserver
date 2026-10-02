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
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

// Basic authentication policy above semantic request heads (TASK-114,
// RFC 7617, v2 behavior parity per PRD-V3N-REQ-038/002,
// DR-V3-001). The policy is vocabulary only: parsing, strict Base64
// decoding, constant-time credential matching, and challenge
// construction live in the library's private implementation, and the
// two factory forms validate their inputs (a realm carrying CR, LF,
// or NUL is refused -- CWE-113) before anything is observable.
//
// The verdict a check() returns is self-contained: it classifies the
// presented credentials with an enum, carries the decoded user and
// password when a Basic token parsed (the v2 get_user()/get_pass()
// surface), and holds the pre-built challenge the policy would answer
// with, so an application can commit the 401 (challenge_status() +
// challenge_fields()) without keeping the policy alive. The challenge
// always carries an explicit Content-Length: 0 alongside
// WWW-Authenticate: on HTTP/1.1 an unframed response is
// engine-selected chunked, and the v2 parity shape is the empty
// length-framed body.
//
// Diagnostics exclude credentials by construction: factory failures
// report the violated rule, never the rejected value, and check()
// produces no diagnostic strings at all.

#ifndef SRC_HTTPSERVER_AUTH_BASIC_AUTH_HPP_
#define SRC_HTTPSERVER_AUTH_BASIC_AUTH_HPP_

#include <cstdint>
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

// Classification of one presented Authorization header. The taxonomy
// is observable; which secret was presented is not part of it.
enum class basic_auth_result : std::uint8_t {
    authenticated,          // credentials presented and accepted
    no_credentials,         // no Authorization field on the request
    malformed_credentials,  // present but not a parsable Basic token
    credentials_rejected,   // parsable but not accepted
};

// The outcome of one policy check. user/password hold the decoded
// octets when a Basic token parsed (empty otherwise; a colon-less
// token keeps an empty password -- v2/MHD parity). challenge holds the
// pre-escaped `Basic realm="..."` value the policy answered from.
struct basic_auth_verdict {
    basic_auth_result result = basic_auth_result::no_credentials;
    std::string user;
    std::string password;
    std::string challenge;

    // True iff the presented credentials were accepted.
    bool allowed() const noexcept;

    // The status every non-authenticated verdict answers with: 401.
    http::status challenge_status() const noexcept;

    // The response fields for the 401: WWW-Authenticate with the
    // pre-built challenge, plus an explicit Content-Length: 0 (the v2
    // parity framing for the empty challenge body).
    http::fields challenge_fields() const;
};

// Application-supplied credential decision (the form the parity
// fixture's central auth_handler takes). Receives the decoded pair.
using basic_auth_validator = concurrency::unique_function<bool(
    const std::string& user, const std::string& password)>;

// An immutable, reusable Basic authentication policy. Two factory
// forms: fixed credentials (matched through a constant-time
// comparison) or an application validator. Construction validates and
// pre-escapes the realm; a realm containing CR, LF, or NUL is refused
// with invalid_argument and the output policy is left untouched, as is
// an empty validator. The challenge is built once here so check()
// never re-derives it per request.
class basic_auth_policy {
 public:
    // The empty policy: empty realm, no credentials, no validator.
    // Every check classifies (absent -> no_credentials, anything
    // presented -> rejected) and its challenge names the empty realm.
    basic_auth_policy() noexcept = default;

    basic_auth_policy(basic_auth_policy&& other) noexcept;
    basic_auth_policy& operator=(basic_auth_policy&& other) noexcept;
    ~basic_auth_policy();

    basic_auth_policy(const basic_auth_policy&) = delete;
    basic_auth_policy& operator=(const basic_auth_policy&) = delete;

    // Fixed-credential form: presented credentials authenticate iff
    // they equal @p user / @p password (length-gated constant-time
    // comparison).
    static http::outcome create(std::string realm, std::string user,
                                std::string password,
                                basic_auth_policy& out);

    // Validator form: the decoded pair is handed to @p validate.
    static http::outcome create(std::string realm,
                                basic_auth_validator validate,
                                basic_auth_policy& out);

    // Classifies one request head. Parse and decode failures classify
    // as malformed_credentials and answer the same challenge as an
    // absent header (v2 never answered 400 for bad credentials); no
    // domain exception escapes.
    basic_auth_verdict check(const http::request_head& head) const;

    const std::string& realm() const noexcept;

 private:
    std::string realm_;
    // Pre-escaped: Basic realm="<realm>". The default constructor
    // names the empty realm rather than leaving an empty challenge
    // value.
    std::string challenge_ = "Basic realm=\"\"";
    std::string user_;       // fixed form (validator form: empty)
    std::string password_;   // fixed form (validator form: empty)
    basic_auth_validator validate_;  // empty iff fixed form
    // True only for the two factory forms. The default-constructed
    // policy has no credentials to match, so its fixed-form branch
    // must reject everything presented (an empty user and password
    // pair would otherwise authenticate `Basic Og==`).
    bool configured_ = false;
};

// Wraps one route handler with the policy: unauthenticated requests
// are answered with the verdict's 401 and the wrapped handler never
// runs; authenticated requests flow through unchanged. The policy (and
// handler) are moved into the returned guard.
server::route_handler make_basic_guard(basic_auth_policy policy,
                                         server::route_handler handler);

}  // namespace auth

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_AUTH_BASIC_AUTH_HPP_
