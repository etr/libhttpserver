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

// TASK-114: a consumer including
// <httpserver/auth/basic_auth.hpp> WITHOUT HTTPSERVER_COMPILATION
// (or any other build/TLS configuration macro) must compile and link
// cleanly. The policy's special members live in the library, so the
// empty LDADD of this target proves the header surface needs no
// library linkage to consume; every check is a static_assert over
// unevaluated expressions only -- no object whose members live in
// the library is ever constructed.
//
// TASK-115: the same consumer contract for
// <httpserver/auth/digest_auth.hpp>: the Digest policy vocabulary is
// header-only to consume (the parser, nonce, ledger, and response
// computation live in the library's private detail/), so the digest
// surface joins this sentinel with the same static_assert-only shape.

#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include <httpserver/auth/basic_auth.hpp>
#include <httpserver/auth/digest_auth.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/routes.hpp>

namespace {

using httpserver::auth::basic_auth_policy;
using httpserver::auth::basic_auth_result;
using httpserver::auth::basic_auth_validator;
using httpserver::auth::basic_auth_verdict;
using httpserver::auth::make_basic_guard;
namespace http = httpserver::http;
namespace srv = httpserver::server;

// The classification taxonomy: four distinct values packed in one
// byte.
static_assert(basic_auth_result::authenticated
              != basic_auth_result::no_credentials);
static_assert(basic_auth_result::no_credentials
              != basic_auth_result::malformed_credentials);
static_assert(basic_auth_result::malformed_credentials
              != basic_auth_result::credentials_rejected);
static_assert(std::is_same_v<std::underlying_type_t<basic_auth_result>,
                             std::uint8_t>);

// The verdict is a plain value type carrying the classification and
// the decoded pair; its helpers are const-correct.
static_assert(std::is_default_constructible_v<basic_auth_verdict>);
static_assert(std::is_move_constructible_v<basic_auth_verdict>);
static_assert(std::is_copy_constructible_v<basic_auth_verdict>);
static_assert(std::is_same_v<
    decltype(std::declval<const basic_auth_verdict&>().allowed()),
    bool>);
static_assert(std::is_same_v<
    decltype(std::declval<const basic_auth_verdict&>()
                 .challenge_status()),
    http::status>);
static_assert(std::is_same_v<
    decltype(std::declval<const basic_auth_verdict&>()
                 .challenge_fields()),
    http::fields>);
static_assert(std::is_same_v<
    decltype(std::declval<const basic_auth_verdict&>().result),
    basic_auth_result>);

// The policy: move-only (its validator is a unique_function), with
// the two factory forms overloads and a const check(). Disambiguated
// through explicit function-pointer aliases (the &X::create
// overloads are otherwise ambiguous in decltype).
static_assert(!std::is_copy_constructible_v<basic_auth_policy>);
static_assert(std::is_move_constructible_v<basic_auth_policy>);
static_assert(std::is_nothrow_move_assignable_v<basic_auth_policy>);
using fixed_create = http::outcome (*)(std::string, std::string,
                                       std::string, basic_auth_policy&);
using validator_create = http::outcome (*)(std::string,
                                           basic_auth_validator,
                                           basic_auth_policy&);
static_assert(std::is_same_v<decltype(static_cast<fixed_create>(
                                     &basic_auth_policy::create)),
                             fixed_create>);
static_assert(std::is_same_v<decltype(static_cast<validator_create>(
                                     &basic_auth_policy::create)),
                             validator_create>);
static_assert(std::is_same_v<
    decltype(std::declval<const basic_auth_policy&>().check(
        std::declval<const http::request_head&>())),
    basic_auth_verdict>);
static_assert(std::is_same_v<
    decltype(std::declval<const basic_auth_policy&>().realm()),
    const std::string&>);
static_assert(noexcept(std::declval<const basic_auth_policy&>().realm()));

// The guard adapter wraps a route handler into a route handler.
static_assert(std::is_same_v<decltype(make_basic_guard),
                             srv::route_handler(basic_auth_policy,
                                                srv::route_handler)>);

// The Digest taxonomy: seven distinct values packed in one byte.
using httpserver::auth::digest_algorithm;
using httpserver::auth::digest_auth_options;
using httpserver::auth::digest_auth_policy;
using httpserver::auth::digest_auth_result;
using httpserver::auth::digest_auth_verdict;
using httpserver::auth::digest_ha1_source;
using httpserver::auth::make_digest_guard;
static_assert(digest_auth_result::authenticated
              != digest_auth_result::no_credentials);
static_assert(digest_auth_result::no_credentials
              != digest_auth_result::malformed_credentials);
static_assert(digest_auth_result::malformed_credentials
              != digest_auth_result::credentials_rejected);
static_assert(digest_auth_result::credentials_rejected
              != digest_auth_result::replayed_nonce);
static_assert(digest_auth_result::replayed_nonce
              != digest_auth_result::stale_nonce);
static_assert(digest_auth_result::stale_nonce
              != digest_auth_result::nonce_unavailable);
static_assert(std::is_same_v<std::underlying_type_t<digest_auth_result>,
                             std::uint8_t>);

// The verdict is a plain value type whose helpers are const-correct.
static_assert(std::is_default_constructible_v<digest_auth_verdict>);
static_assert(std::is_move_constructible_v<digest_auth_verdict>);
static_assert(std::is_copy_constructible_v<digest_auth_verdict>);
static_assert(std::is_same_v<
    decltype(std::declval<const digest_auth_verdict&>().allowed()),
    bool>);
static_assert(std::is_same_v<
    decltype(std::declval<const digest_auth_verdict&>().stale()),
    bool>);
static_assert(std::is_same_v<
    decltype(std::declval<const digest_auth_verdict&>()
                 .challenge_status()),
    http::status>);
static_assert(std::is_same_v<
    decltype(std::declval<const digest_auth_verdict&>()
                 .challenge_fields()),
    http::fields>);

// The options default to the v2 posture: MD5, a 300-second nonce TTL,
// an unbounded 1024-slot ledger.
static_assert(std::is_same_v<
    decltype(digest_auth_options{}.algorithm),
    digest_algorithm>);
static_assert(std::is_same_v<
    decltype(digest_auth_options{}.nonce_ttl),
    std::chrono::seconds>);
static_assert(digest_auth_options{}.nonce_ttl
              == std::chrono::seconds{300});
static_assert(digest_auth_options{}.max_nc == 0);
static_assert(digest_auth_options{}.ledger_capacity == 1024);
static_assert(digest_algorithm::md5 != digest_algorithm::sha_256);

// The HA1 source is the unique_function callback shape; the v2
// check_digest_auth_digest analogue.
static_assert(!std::is_copy_constructible_v<digest_ha1_source>);
static_assert(std::is_move_constructible_v<digest_ha1_source>);

// The policy: move-only (its HA1 source is a unique_function), with
// the two factory-form overloads and a const check(). Disambiguated
// through explicit function-pointer aliases.
static_assert(!std::is_copy_constructible_v<digest_auth_policy>);
static_assert(std::is_move_constructible_v<digest_auth_policy>);
static_assert(std::is_nothrow_move_assignable_v<digest_auth_policy>);
static_assert(std::is_nothrow_move_constructible_v<digest_auth_policy>);
using digest_fixed_create = http::outcome (*)(std::string, std::string,
                                              std::string,
                                              digest_auth_options,
                                              digest_auth_policy&);
using digest_source_create = http::outcome (*)(
    std::string, digest_ha1_source, digest_auth_options,
    digest_auth_policy&);
static_assert(std::is_same_v<
    decltype(static_cast<digest_fixed_create>(
        &digest_auth_policy::create)),
    digest_fixed_create>);
static_assert(std::is_same_v<
    decltype(static_cast<digest_source_create>(
        &digest_auth_policy::create)),
    digest_source_create>);
static_assert(std::is_same_v<
    decltype(std::declval<const digest_auth_policy&>().check(
        std::declval<const http::request_head&>())),
    digest_auth_verdict>);
static_assert(std::is_same_v<
    decltype(std::declval<const digest_auth_policy&>().realm()),
    const std::string&>);
static_assert(noexcept(std::declval<const digest_auth_policy&>().realm()));
static_assert(std::is_same_v<
    decltype(std::declval<const digest_auth_policy&>().algorithm()),
    digest_algorithm>);
static_assert(noexcept(
    std::declval<const digest_auth_policy&>().algorithm()));

// The Digest guard adapter wraps a route handler into a route handler.
static_assert(std::is_same_v<decltype(make_digest_guard),
                             srv::route_handler(digest_auth_policy,
                                                srv::route_handler)>);

}  // namespace

int main() {
    // Nothing runs: compiling and linking is the contract.
    return 0;
}
