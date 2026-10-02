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

// TASK-114: the Basic authentication policy implementation (RFC 7617,
// v2 parity per PRD-V3N-REQ-038). Parsing rules mirror the v2/MHD
// posture the parity corpus pins:
//   - the Authorization value is `Basic 1*(SP/HTAB) token68` with the
//     scheme matched case-insensitively and '=' only as trailing
//     token68 padding;
//   - the token is strict-canonical RFC 4648 base64 (the in-tree
//     codec); any parse or decode failure classifies as
//     malformed_credentials and answers the same challenge as an
//     absent header (v2 never answered 400 for bad credentials);
//   - the decoded octets split at the FIRST colon; a colon-less token
//     keeps the octets as the user with an empty password;
//   - fixed credentials compare through the constant-time equality
//     (length early-out, branch-free fold);
//   - decoded staging buffers are scrubbed with secure_zero before
//     every return (CWE-14/CWE-312: no credential bytes linger in
//     freed heap).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/auth/basic_auth.hpp>
#include <httpserver/detail/auth_text.hpp>
#include <httpserver/detail/base64.hpp>
#include <httpserver/detail/secure_compare.hpp>
#include <httpserver/detail/secure_zero.hpp>
#include <httpserver/exchange.hpp>

namespace httpserver {

namespace auth {

namespace {

constexpr std::string_view k_basic_scheme = "Basic";
constexpr std::string_view k_authorization_field = "Authorization";
constexpr std::string_view k_forbidden_realm_chars("\r\n\0", 3);
constexpr std::size_t k_not_present = static_cast<std::size_t>(-1);

using detail::auth_text::ascii_iequal;

bool is_separator(char c) noexcept {
    return c == ' ' || c == '\t';
}

// The token68 alphabet of RFC 7235 (base64 URL-unfriendly specials
// included); '=' is handled separately as trailing padding only.
constexpr std::string_view k_token68_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~+/";

bool is_token68_char(char c) noexcept {
    return k_token68_chars.find(c) != std::string_view::npos;
}

// Consumes the case-insensitive Basic scheme and its 1*(SP/HTAB)
// separator; returns the offset where the token68 starts, or
// k_not_present when the value opens with anything else.
std::size_t basic_scheme_prefix(
    std::string_view credentials) noexcept {
    std::size_t i = 0;
    while (i < credentials.size()
           && !is_separator(credentials[i])) {
        ++i;
    }
    if (!ascii_iequal(credentials.substr(0, i), k_basic_scheme)) {
        return k_not_present;
    }
    if (i == credentials.size()) return k_not_present;
    while (i < credentials.size() && is_separator(credentials[i])) {
        ++i;
    }
    return i;
}

// Measures the token68 run (padding only after the alphabet run);
// returns its length, or k_not_present on an out-of-place character.
std::size_t token68_length(std::string_view rest) noexcept {
    std::size_t i = 0;
    bool padding = false;
    while (i < rest.size()) {
        const char c = rest[i];
        if (c == '=') {
            padding = true;
        } else if (is_separator(c)) {
            break;
        } else if (padding || !is_token68_char(c)) {
            return k_not_present;
        }
        ++i;
    }
    return i;
}

// Extracts the token68 from `Basic 1*(SP/HTAB) token68 [OWS]`.
bool extract_basic_token(std::string_view credentials,
                         std::string_view& token) noexcept {
    const std::size_t start = basic_scheme_prefix(credentials);
    if (start == k_not_present || start == credentials.size()) {
        return false;
    }
    const std::size_t length =
        token68_length(credentials.substr(start));
    if (length == k_not_present || length == 0) return false;
    for (std::size_t i = start + length; i < credentials.size(); ++i) {
        if (!is_separator(credentials[i])) return false;
    }
    token = credentials.substr(start, length);
    return true;
}

// v2 unauthorized() parity: CR, LF, NUL refused (CWE-113), backslash
// and double-quote escaped per the RFC 7235 quoted-pair rule
// (CWE-116). The fast path appends directly when nothing needs
// escaping, keeping the canonical `Basic realm="myrealm"`
// byte-for-byte.
std::string build_challenge(std::string_view realm) {
    std::string challenge;
    challenge.reserve(k_basic_scheme.size() + realm.size() + 10);
    challenge.append(k_basic_scheme);
    challenge.append(" realm=\"");
    if (realm.find_first_of("\\\"") == std::string_view::npos) {
        challenge.append(realm);
    } else {
        for (const char c : realm) {
            if (c == '\\' || c == '"') challenge.push_back('\\');
            challenge.push_back(c);
        }
    }
    challenge.push_back('"');
    return challenge;
}

// Assigns the decoded user/password views over @p decoded, splitting
// at the first colon (no colon: password stays empty).
void split_credentials(std::string_view decoded, std::string_view& user,
                       std::string_view& password) noexcept {
    const std::size_t colon = decoded.find(':');
    if (colon == std::string_view::npos) {
        user = decoded;
        password = std::string_view();
        return;
    }
    user = decoded.substr(0, colon);
    password = decoded.substr(colon + 1);
}

}  // namespace

bool basic_auth_verdict::allowed() const noexcept {
    return result == basic_auth_result::authenticated;
}

http::status basic_auth_verdict::challenge_status() const noexcept {
    return http::status::from_code(401);
}

http::fields basic_auth_verdict::challenge_fields() const {
    http::fields fields;
    fields.append("WWW-Authenticate", challenge);
    fields.append("Content-Length", "0");
    return fields;
}

basic_auth_policy::basic_auth_policy(basic_auth_policy&&) noexcept = default;

basic_auth_policy& basic_auth_policy::operator=(
    basic_auth_policy&&) noexcept = default;

basic_auth_policy::~basic_auth_policy() = default;

http::outcome basic_auth_policy::create(std::string realm,
                                        std::string user,
                                        std::string password,
                                        basic_auth_policy& out) {
    if (realm.find_first_of(k_forbidden_realm_chars)
        != std::string::npos) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "basic_auth_policy: realm contains a forbidden control "
            "character (CR, LF, or NUL)");
    }
    basic_auth_policy built;
    built.realm_ = std::move(realm);
    built.challenge_ = build_challenge(built.realm_);
    built.user_ = std::move(user);
    built.password_ = std::move(password);
    built.configured_ = true;
    out = std::move(built);
    return http::outcome::okay();
}

http::outcome basic_auth_policy::create(std::string realm,
                                        basic_auth_validator validate,
                                        basic_auth_policy& out) {
    if (!validate) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "basic_auth_policy: the validator is empty");
    }
    basic_auth_policy built;
    built.realm_ = std::move(realm);
    built.challenge_ = build_challenge(built.realm_);
    built.validate_ = std::move(validate);
    built.configured_ = true;
    out = std::move(built);
    return http::outcome::okay();
}

basic_auth_verdict basic_auth_policy::check(
    const http::request_head& head) const {
    basic_auth_verdict verdict;
    verdict.challenge = challenge_;

    const std::optional<std::string_view> credentials =
        head.head_fields.first(k_authorization_field);
    if (!credentials.has_value()) {
        verdict.result = basic_auth_result::no_credentials;
        return verdict;
    }

    std::string_view token;
    if (!extract_basic_token(*credentials, token)) {
        verdict.result = basic_auth_result::malformed_credentials;
        return verdict;
    }

    std::optional<std::vector<std::byte>> decoded =
        detail::base64_decode(token);
    if (!decoded.has_value()) {
        verdict.result = basic_auth_result::malformed_credentials;
        return verdict;
    }

    const std::string_view staging(reinterpret_cast<const char*>(
                                        decoded->data()),
                                    decoded->size());
    std::string_view user;
    std::string_view password;
    split_credentials(staging, user, password);
    verdict.user.assign(user);
    verdict.password.assign(password);

    bool accepted = false;
    if (validate_) {
        accepted = validate_(verdict.user, verdict.password);
    } else if (configured_) {
        // The default-constructed policy is not configured: nothing
        // presented can authenticate (empty fixed credentials would
        // otherwise match the empty pair of `Basic Og==`).
        accepted = detail::constant_time_equal(verdict.user, user_)
                   && detail::constant_time_equal(verdict.password,
                                                  password_);
    }
    verdict.result = accepted ? basic_auth_result::authenticated
                              : basic_auth_result::credentials_rejected;

    // Scrub the decoded staging copy before it frees; the verdict's
    // own user/password are the caller's delivered surface.
    detail::secure_zero(decoded->data(), decoded->size());
    return verdict;
}

const std::string& basic_auth_policy::realm() const noexcept {
    return realm_;
}

server::route_handler make_basic_guard(basic_auth_policy policy,
                                         server::route_handler handler) {
    return [guard = std::move(policy), next = std::move(handler)](
               exchange& x) -> task<void> {
        const basic_auth_verdict verdict = guard.check(x.head());
        if (!verdict.allowed()) {
            static_cast<void>(x.respond(verdict.challenge_status(),
                                        verdict.challenge_fields()));
            co_return;
        }
        co_await next(x);
    };
}

}  // namespace auth

}  // namespace httpserver
