/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-015: compile-time guarantees of the http_request PIMPL split.
//
// We assert the structural invariants TASK-015 owns:
//   1. http_request is move-only (copy-deleted, move-defaulted). This
//      preserves v1 semantics: modded_request holds unique_ptr<http_request>
//      and reset/move-rebinds it.
//   2. sizeof(http_request) is bounded -- after the split, only the small
//      backend-agnostic fields (path/method/content/version/limit) plus
//      the impl_ unique_ptr remain on the outer. Everything backend-coupled
//      moved into detail/http_request_impl.hpp.
//   3. sizeof(http_request) >= sizeof(void*) (the impl pointer must exist).
//      This is the lower-bound companion that the TASK-014 review record
//      flagged as missing on webserver_pimpl_test.cpp.
//
// Note: the literal "no <microhttpd.h>/<gnutls/gnutls.h> in
// <http_request.hpp>" grep is enforced by the per-task acceptance command
// (see specs/tasks/M3-request/TASK-015.md). We do *not* repeat that as a
// runtime or preprocessor assertion here because <httpserver.hpp> is still
// on the preprocessor side of the umbrella in TASK-015's scope -- scrubbing
// that path is TASK-020's job (the existing XFAIL_TESTS gate).

// HTTPSERVER_COMPILATION is supplied by test/Makefile.am AM_CPPFLAGS.
#include "httpserver/http_request.hpp"
#include "httpserver/detail/http_request_impl.hpp"

#include <map>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// (1) Externally non-constructible: copy is deleted (the dtor removes
//     transient files from disk; copying would double-free) and move
//     ctor/assign are *defaulted but private* -- only the friend dispatch
//     path inside libhttpserver (webserver_impl, modded_request) can move
//     a request. Externally the type therefore appears as both non-copy-
//     and non-move-constructible. That's the contract a downstream
//     consumer sees, and that's what we lock in here.
static_assert(!std::is_copy_constructible_v<httpserver::http_request>,
              "http_request must not be copy-constructible from external scope");
static_assert(!std::is_copy_assignable_v<httpserver::http_request>,
              "http_request must not be copy-assignable from external scope");
static_assert(!std::is_move_constructible_v<httpserver::http_request>,
              "http_request must not be move-constructible from external scope (move is private)");
static_assert(!std::is_move_assignable_v<httpserver::http_request>,
              "http_request must not be move-assignable from external scope (move is private)");

// (2) Conservative size upper bound. After the PIMPL split the outer
//     carries just a handful of small fields (path/method/content/version
//     std::strings, a size_t limit, and the impl_ unique_ptr). 24
//     pointer-widths (192 bytes on LP64) leaves headroom for libstdc++ vs
//     libc++ string layout differences (libstdc++ std::string is 32 B on
//     LP64; libc++ is 24 B) without being so loose that an accidental
//     impl-fold-back goes unnoticed.
static_assert(sizeof(httpserver::http_request) <= 24 * sizeof(void*),
              "http_request size grew unexpectedly after PIMPL split");

// (3) Lower-bound companion: the impl_ pointer must exist. If someone
//     accidentally folds the impl back into the outer, this stays >=
//     sizeof(void*); but if someone deletes the pointer member entirely
//     (no impl indirection), the type can become smaller.
static_assert(sizeof(httpserver::http_request) >= sizeof(void*),
              "http_request must hold at least one pointer (the impl)");

// (4) TASK-017: container getters return `const ContainerType&`.
//
//     The acceptance criterion in specs/tasks/M3-request/TASK-017.md mandates:
//
//         static_assert(std::is_lvalue_reference_v<
//             decltype(std::declval<const http_request&>().get_headers())>);
//
//     We assert the same for the six container getters, plus the exact
//     return-type identity (so a future widening that returns by value
//     again is caught at compile time, not just by perf regression).
using cref = const httpserver::http_request&;

static_assert(std::is_lvalue_reference_v<
                  decltype(std::declval<cref>().get_headers())>,
              "get_headers must return an lvalue reference");
static_assert(std::is_lvalue_reference_v<
                  decltype(std::declval<cref>().get_footers())>,
              "get_footers must return an lvalue reference");
static_assert(std::is_lvalue_reference_v<
                  decltype(std::declval<cref>().get_cookies())>,
              "get_cookies must return an lvalue reference");
static_assert(std::is_lvalue_reference_v<
                  decltype(std::declval<cref>().get_args())>,
              "get_args must return an lvalue reference");
static_assert(std::is_lvalue_reference_v<
                  decltype(std::declval<cref>().get_path_pieces())>,
              "get_path_pieces must return an lvalue reference");
static_assert(std::is_lvalue_reference_v<
                  decltype(std::declval<cref>().get_files())>,
              "get_files must return an lvalue reference");

// Exact-type assertions: each getter returns const Container& by reference
// to a container owned by the request's impl. Locking the precise type also
// catches accidental drift to a non-const reference.
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_headers()),
                  const httpserver::http::header_view_map&>,
              "get_headers must return const http::header_view_map&");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_footers()),
                  const httpserver::http::header_view_map&>,
              "get_footers must return const http::header_view_map&");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_cookies()),
                  const httpserver::http::header_view_map&>,
              "get_cookies must return const http::header_view_map&");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_args()),
                  const httpserver::http::arg_view_map&>,
              "get_args must return const http::arg_view_map&");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_path_pieces()),
                  const std::vector<std::string>&>,
              "get_path_pieces must return const std::vector<std::string>&");
static_assert(std::is_same_v<
                  decltype(std::declval<cref>().get_files()),
                  const std::map<std::string,
                                 std::map<std::string,
                                          httpserver::http::file_info>>&>,
              "get_files must return const std::map<...>&");

// TASK-069: pin the http_request_impl construction surface. After v2
// cutover, the only public constructors are the no-arg default
// (test-request path, delegates to the three-arg form) and the
// allocator-aware three-arg form. The transitional two-arg overload
// (MHD_Connection*, unescaper_ptr) is gone. If it ever returns, the
// negative is_constructible assertion below breaks the build.
static_assert(std::is_constructible_v<httpserver::detail::http_request_impl>,
              "default http_request_impl() must remain (test-request path)");
static_assert(std::is_constructible_v<
                  httpserver::detail::http_request_impl,
                  MHD_Connection*, httpserver::unescaper_ptr,
                  std::pmr::polymorphic_allocator<>>,
              "canonical three-arg http_request_impl(MHD_Connection*, unescaper_ptr, alloc) must exist");
static_assert(!std::is_constructible_v<
                  httpserver::detail::http_request_impl,
                  MHD_Connection*, httpserver::unescaper_ptr>,
              "TASK-069: transitional two-arg http_request_impl ctor must be removed");

int main() { return 0; }
