/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-018: compile-time guarantees of http_request's per-key and
// always-present getters.
//
// We assert the structural invariants TASK-018 owns:
//   1. Every per-key getter (get_header / get_cookie / get_footer /
//      get_arg / get_arg_flat) is callable on `const http_request&` with
//      a `std::string_view` key.
//   2. Every always-present getter (get_path / get_method / get_version
//      / get_content / get_querystring) is callable on `const
//      http_request&` and returns `std::string_view`.
//   3. The five always-present getters above are `noexcept`. The
//      string_view-of-std::string conversion is itself noexcept since
//      C++17, so every getter that just hands back a member's string can
//      lock that contract in.
//   4. Return-type lockdowns: every per-key getter returns
//      `std::string_view`, except `get_arg` which deliberately returns
//      `httpserver::http_arg_value` to preserve multi-value semantics
//      (see TASK-018 plan section 4 for the rationale).
//
// The intent of these static_asserts is to catch silent regressions:
// e.g. if a future refactor narrows `get_arg`'s signature without
// adjusting the multi-value tests, or if someone strips `noexcept` off
// `get_path`, the build breaks at compile time rather than at the next
// integration-test failure.

// HTTPSERVER_COMPILATION is supplied by test/Makefile.am AM_CPPFLAGS.
#include "httpserver/http_request.hpp"
#include "httpserver/http_arg_value.hpp"

#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using h = httpserver::http_request;
using cref = const h&;

// (1) Const-callable invocability for every per-key getter.
static_assert(std::is_invocable_v<decltype(&h::get_header),   cref, std::string_view>,
              "get_header must be invocable on const http_request& with string_view");
static_assert(std::is_invocable_v<decltype(&h::get_cookie),   cref, std::string_view>,
              "get_cookie must be invocable on const http_request& with string_view");
static_assert(std::is_invocable_v<decltype(&h::get_footer),   cref, std::string_view>,
              "get_footer must be invocable on const http_request& with string_view");
static_assert(std::is_invocable_v<decltype(&h::get_arg),      cref, std::string_view>,
              "get_arg must be invocable on const http_request& with string_view");
static_assert(std::is_invocable_v<decltype(&h::get_arg_flat), cref, std::string_view>,
              "get_arg_flat must be invocable on const http_request& with string_view");

// (2) Const-callable invocability for every always-present getter.
static_assert(std::is_invocable_v<decltype(&h::get_path),        cref>,
              "get_path must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_method),      cref>,
              "get_method must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_version),     cref>,
              "get_version must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_content),     cref>,
              "get_content must be invocable on const http_request&");
static_assert(std::is_invocable_v<decltype(&h::get_querystring), cref>,
              "get_querystring must be invocable on const http_request&");

// (3) `noexcept` lockdowns for the five always-present getters.
static_assert(noexcept(std::declval<cref>().get_path()),
              "get_path must be noexcept");
static_assert(noexcept(std::declval<cref>().get_method()),
              "get_method must be noexcept");
static_assert(noexcept(std::declval<cref>().get_version()),
              "get_version must be noexcept");
static_assert(noexcept(std::declval<cref>().get_content()),
              "get_content must be noexcept");
static_assert(noexcept(std::declval<cref>().get_querystring()),
              "get_querystring must be noexcept");

// (4) Return-type lockdowns: per-key getters narrow to string_view,
// except get_arg which preserves multi-value semantics via http_arg_value.
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_header(std::string_view{})),
                  std::string_view>,
              "get_header must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_cookie(std::string_view{})),
                  std::string_view>,
              "get_cookie must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_footer(std::string_view{})),
                  std::string_view>,
              "get_footer must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_arg_flat(std::string_view{})),
                  std::string_view>,
              "get_arg_flat must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_arg(std::string_view{})),
                  httpserver::http_arg_value>,
              "get_arg must return http_arg_value (multi-value semantics)");

// Always-present getters all return string_view.
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_path()),
                  std::string_view>,
              "get_path must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_method()),
                  std::string_view>,
              "get_method must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_version()),
                  std::string_view>,
              "get_version must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_content()),
                  std::string_view>,
              "get_content must return std::string_view");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_querystring()),
                  std::string_view>,
              "get_querystring must return std::string_view");

// (5) Const-qualifier lockdowns via method-pointer type matching.
//     Item 28 (test-quality-reviewer): confirms that each per-key getter is
//     declared `const` at the type-system level, not just invocable on a
//     const& (which would also accept non-const overloads). Using
//     std::is_same_v<decltype(&h::method), ReturnType (h::*)(ArgType) const>
//     pins the const-qualifier into the type signature.
static_assert(
    std::is_same_v<decltype(&h::get_header),
                   std::string_view (h::*)(std::string_view) const>,
    "get_header must be a const member function returning std::string_view");
static_assert(
    std::is_same_v<decltype(&h::get_cookie),
                   std::string_view (h::*)(std::string_view) const>,
    "get_cookie must be a const member function returning std::string_view");
static_assert(
    std::is_same_v<decltype(&h::get_footer),
                   std::string_view (h::*)(std::string_view) const>,
    "get_footer must be a const member function returning std::string_view");
static_assert(
    std::is_same_v<decltype(&h::get_arg_flat),
                   std::string_view (h::*)(std::string_view) const>,
    "get_arg_flat must be a const member function returning std::string_view");
static_assert(
    std::is_same_v<decltype(&h::get_arg),
                   httpserver::http_arg_value (h::*)(std::string_view) const>,
    "get_arg must be a const member function returning http_arg_value");

}  // namespace

int main() { return 0; }
