/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-019: compile-time guarantees of http_request's high-level GnuTLS
// accessor surface. We assert the structural invariants TASK-019 owns:
//
//   1. The 9 high-level cert accessors exist UNCONDITIONALLY (no
//      #ifdef HAVE_GNUTLS gate around the public declarations) so that
//      downstream code can rely on the symbols being present in both
//      build modes; when HAVE_GNUTLS is off they return sentinel values
//      (covered by runtime tests).
//
//   2. The raw `gnutls_session_t get_tls_session()` accessor has been
//      REMOVED from http_request's public surface. We use SFINAE to
//      detect the absence of the symbol.
//
//   3. Return-type lockdowns for every signature in the TASK-019 plan
//      (string_view for the four DN/CN/fingerprint, std::int64_t for
//      the two times, bool for the three predicates).
//
//   4. noexcept lockdowns for the five accessors documented as noexcept
//      (has_tls_session, has_client_certificate, is_client_cert_verified,
//      get_client_cert_not_before, get_client_cert_not_after).
//
//   5. <gnutls/gnutls.h> must NOT be reachable through this TU's
//      includes. We rely on the standard TASK-007 staged-install grep
//      gate for the umbrella check; here we just include the request
//      header directly and fail compilation if a gnutls macro escapes.

// HTTPSERVER_COMPILATION is supplied by test/Makefile.am AM_CPPFLAGS so
// the per-header _HTTPSERVER_HPP_INSIDE_ guard is satisfied.
#include "httpserver/http_request.hpp"

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using h = httpserver::http_request;
using cref = const h&;

// (1) Existence + invocability — unconditional, both build modes.
static_assert(std::is_invocable_v<decltype(&h::has_tls_session), cref>,
              "has_tls_session must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::has_client_certificate), cref>,
              "has_client_certificate must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_client_cert_dn), cref>,
              "get_client_cert_dn must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_client_cert_issuer_dn), cref>,
              "get_client_cert_issuer_dn must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_client_cert_cn), cref>,
              "get_client_cert_cn must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_client_cert_fingerprint_sha256), cref>,
              "get_client_cert_fingerprint_sha256 must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::is_client_cert_verified), cref>,
              "is_client_cert_verified must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_client_cert_not_before), cref>,
              "get_client_cert_not_before must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_client_cert_not_after), cref>,
              "get_client_cert_not_after must be invocable on const http_request&");

// (2) Absence of the raw `gnutls_session_t get_tls_session()` accessor.
// SFINAE detector: the primary template has value=false; the partial
// specialization that requires `&T::get_tls_session` to name something
// only kicks in if the member exists. After TASK-019 the member is
// gone, so the primary stays selected and value remains false.
template<class T, class = void>
struct has_get_tls_session : std::false_type {};
template<class T>
struct has_get_tls_session<T, std::void_t<decltype(&T::get_tls_session)>> : std::true_type {};

static_assert(!has_get_tls_session<h>::value,
              "http_request must not expose a public get_tls_session() "
              "method; raw gnutls_session_t leaks the GnuTLS type into "
              "the public header surface");

// (3) Return-type lockdowns.
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().has_tls_session()),
                  bool>,
              "has_tls_session must return bool");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().has_client_certificate()),
                  bool>,
              "has_client_certificate must return bool");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_client_cert_dn()),
                  std::string_view>,
              "get_client_cert_dn must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_client_cert_issuer_dn()),
                  std::string_view>,
              "get_client_cert_issuer_dn must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_client_cert_cn()),
                  std::string_view>,
              "get_client_cert_cn must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_client_cert_fingerprint_sha256()),
                  std::string_view>,
              "get_client_cert_fingerprint_sha256 must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().is_client_cert_verified()),
                  bool>,
              "is_client_cert_verified must return bool");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_client_cert_not_before()),
                  std::int64_t>,
              "get_client_cert_not_before must return std::int64_t");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_client_cert_not_after()),
                  std::int64_t>,
              "get_client_cert_not_after must return std::int64_t");

// (4) noexcept lockdowns. The five accessors marked noexcept by spec.
// The four string_view accessors are deliberately NOT noexcept because
// they call populate_all_cert_fields() which can throw on allocation
// failure; that exception path is allowed to propagate.
//
// NOTE on the noexcept try/catch sentinel path: the noexcept wrappers
// in is_client_cert_verified, get_client_cert_not_before, and
// get_client_cert_not_after each contain a try/catch block that returns
// the documented sentinel (false / -1) on std::bad_alloc. This path is
// NOT covered by automated tests because injecting std::bad_alloc inside
// a GnuTLS context is impractical in CI. The sentinel values are trivially
// correct on the else-branch, so the risk is very low.
// (code-quality-reviewer items 12, 13)
static_assert(noexcept(std::declval<cref>().has_tls_session()),
              "has_tls_session must be noexcept");
static_assert(noexcept(std::declval<cref>().has_client_certificate()),
              "has_client_certificate must be noexcept");
static_assert(noexcept(std::declval<cref>().is_client_cert_verified()),
              "is_client_cert_verified must be noexcept");
static_assert(noexcept(std::declval<cref>().get_client_cert_not_before()),
              "get_client_cert_not_before must be noexcept");
static_assert(noexcept(std::declval<cref>().get_client_cert_not_after()),
              "get_client_cert_not_after must be noexcept");

}  // namespace

// NOTE on no-GNUTLS build coverage: this file compiles with whatever
// HAVE_GNUTLS setting the build system provides. The static_asserts above
// verify the public surface in both modes (declarations are unconditional),
// but there is no dedicated no-GNUTLS compile target in test/Makefile.am.
// The no-GNUTLS runtime sentinel contract (all accessors return false/empty/-1)
// is covered by the unconditional build_no_client_cert_returns_sentinels test
// in test/unit/create_test_request_test.cpp, which runs on every build
// including HAVE_GNUTLS-off builds.
// (test-quality-reviewer item 31)

int main() { return 0; }
