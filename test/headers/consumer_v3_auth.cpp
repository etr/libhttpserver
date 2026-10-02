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

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include <httpserver/auth/basic_auth.hpp>
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

}  // namespace

int main() {
    // Nothing runs: compiling and linking is the contract.
    return 0;
}
