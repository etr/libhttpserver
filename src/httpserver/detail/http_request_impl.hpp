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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// TASK-015: http_request PIMPL backing class.
//
// This header is *internal*. It is reachable only when compiling the
// libhttpserver translation units themselves (HTTPSERVER_COMPILATION
// is supplied through src/Makefile.am AM_CPPFLAGS). It is NOT included
// from the public umbrella <httpserver.hpp>, so the gate is the strict
// one-mode form, mirroring detail/webserver_impl.hpp.
#if !defined(HTTPSERVER_COMPILATION)
#error "http_request_impl.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP_REQUEST_IMPL_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP_REQUEST_IMPL_HPP_

#include <microhttpd.h>

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif  // HAVE_GNUTLS

#include <stddef.h>
#include <ctime>
#include <map>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "httpserver/create_webserver.hpp"
#include "httpserver/file_info.hpp"
#include "httpserver/http_arg_value.hpp"
#include "httpserver/http_utils.hpp"

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver::detail {

// http_request_impl: backing object for http_request. Holds every field
// that depends on libmicrohttpd or GnuTLS (the live MHD_Connection*, the
// per-request unescaper, the file table, the lazy parsed-args / cookies
// / cert caches), plus the helpers and MHD trampolines that operate on
// those fields.
//
// Members are deliberately public: http_request just forwards every
// public-API call into a one-line `impl_->...` dispatch. The boundary
// that matters is between the public <httpserver/http_request.hpp>
// header and this internal header -- not between http_request and its
// own impl.
//
// What stays on http_request (the outer):
//   - path / method / content / version (std::string, backend-agnostic)
//   - content_size_limit (size_t)
//   - the impl_ unique_ptr itself
//
// Everything else lives here.
class http_request_impl {
 public:
    // Default constructor: heap-backed (default resource). Used by
    // create_test_request; semantics unchanged from v1.
    http_request_impl()
        : http_request_impl(nullptr, nullptr,
                            std::pmr::polymorphic_allocator<>{
                                std::pmr::get_default_resource()}) {}

    // Two-arg ctor (TASK-015 surface) is preserved for source compatibility
    // with any caller that hasn't been ported to the allocator-taking ctor
    // yet -- it forwards to the three-arg form with the default resource.
    http_request_impl(MHD_Connection* connection, unescaper_ptr unescaper)
        : http_request_impl(connection, unescaper,
                            std::pmr::polymorphic_allocator<>{
                                std::pmr::get_default_resource()}) {}

    // TASK-016: allocator-aware constructor. The PMR-aware containers in
    // this impl propagate `alloc` through their value_types via the
    // standard scoped-allocator semantics built into
    // std::pmr::polymorphic_allocator. Wire this from the dispatch path
    // (webserver_impl::requests_answer_first_step) with the per-connection
    // arena's allocator, and per-request impl_construction stops touching
    // the global heap on the warm path.
    http_request_impl(MHD_Connection* connection, unescaper_ptr unescaper,
                      std::pmr::polymorphic_allocator<> alloc)
        : connection_(connection),
          unescaper_(unescaper),
#ifdef HAVE_BAUTH
          username(alloc),
          password(alloc),
#endif  // HAVE_BAUTH
          querystring(alloc),
          requestor_ip(alloc),
#ifdef HAVE_DAUTH
          digested_user(alloc),
#endif  // HAVE_DAUTH
          unescaped_args(alloc),
          path_pieces(alloc)
#ifdef HAVE_GNUTLS
          , client_cert_dn(alloc),
          client_cert_issuer_dn(alloc),
          client_cert_cn(alloc),
          client_cert_fingerprint_sha256(alloc)
#endif  // HAVE_GNUTLS
    {
    }

    http_request_impl(const http_request_impl&) = delete;
    http_request_impl& operator=(const http_request_impl&) = delete;
    // Moves are left implicitly defined (and unused -- the impl is held
    // through unique_ptr so http_request's defaulted moves operate on
    // the unique_ptr, not on the impl directly).

    // --- per-request backend handles ---
    MHD_Connection* connection_ = nullptr;
    unescaper_ptr unescaper_ = nullptr;
    file_cleanup_callback_ptr file_cleanup_callback_ = nullptr;
    // files_ stays default-allocated. Rationale: file_info owns disk-side
    // state and its destructor (via http_request::~http_request) issues
    // remove() calls. Keeping this map decoupled from the per-connection
    // arena lifecycle simplifies reasoning about when those file removals
    // run; uploads are also a comparatively cold path (no allocations on
    // the warm GET path).
    std::map<std::string, std::map<std::string, http::file_info>> files_;

    // --- test-request local storage ---
    // When connection_ is null (create_test_request path), get_header /
    // get_footer / get_cookie / get_headerlike_values / get_requestor_port /
    // has_tls_session fall back to these instead of calling MHD APIs. These
    // stay default-allocated because the test-request path has no arena
    // and the create_test_request builder hands them in by std::move from
    // its own default-allocated http::header_map.
    http::header_map headers_local;
    http::header_map footers_local;
    http::header_map cookies_local;
    uint16_t requestor_port_local = 0;
#ifdef HAVE_GNUTLS
    bool tls_enabled_local = false;
#endif  // HAVE_GNUTLS

    // --- lazy caches (formerly the http_request_data_cache struct) ---
    // All marked mutable: const accessors lazily populate them. PMR-aware
    // so populations on the warm path (lookups, querystring assembly,
    // unescaped-arg parsing) hit the per-connection arena instead of the
    // global heap.
#ifdef HAVE_BAUTH
    mutable std::pmr::string username;
    mutable std::pmr::string password;
#endif  // HAVE_BAUTH
    mutable std::pmr::string querystring;
    mutable std::pmr::string requestor_ip;
#ifdef HAVE_DAUTH
    mutable std::pmr::string digested_user;
#endif  // HAVE_DAUTH
    mutable std::pmr::map<std::pmr::string, std::pmr::vector<std::pmr::string>,
                          http::arg_comparator> unescaped_args;
    mutable std::pmr::vector<std::pmr::string> path_pieces;
    mutable bool args_populated = false;
    mutable bool path_pieces_cached = false;

#ifdef HAVE_GNUTLS
    mutable bool client_cert_fields_cached = false;
    mutable std::pmr::string client_cert_dn;
    mutable std::pmr::string client_cert_issuer_dn;
    mutable std::pmr::string client_cert_cn;
    mutable std::pmr::string client_cert_fingerprint_sha256;
    mutable std::time_t client_cert_not_before = static_cast<std::time_t>(-1);
    mutable std::time_t client_cert_not_after = static_cast<std::time_t>(-1);
    mutable bool client_cert_verified = false;
#endif  // HAVE_GNUTLS

    // --- helpers (moved out of http_request public header) ---
    std::string_view get_connection_value(std::string_view key, MHD_ValueKind kind) const;
    http::header_view_map get_headerlike_values(MHD_ValueKind kind) const;
    void populate_args() const;
    void ensure_path_pieces_cached(std::string_view path) const;

    void set_arg(const std::string& key, const std::string& value, std::size_t content_size_limit);
    void set_arg(const char* key, const char* value, std::size_t size, std::size_t content_size_limit);
    void set_arg_flat(const std::string& key, const std::string& value, std::size_t content_size_limit);
    void set_args(const std::map<std::string, std::string>& args, std::size_t content_size_limit);
    void grow_last_arg(const std::string& key, const std::string& value);

#ifdef HAVE_BAUTH
    void fetch_user_pass() const;
#endif  // HAVE_BAUTH

#ifdef HAVE_GNUTLS
    bool has_tls_session() const;
    gnutls_session_t get_tls_session() const;
    bool has_client_certificate() const;
    void populate_all_cert_fields() const;
#endif  // HAVE_GNUTLS

    // MHD trampolines. Closure pointer is whatever the caller passes
    // (usually `this`, or a header_view_map* / std::string* sink).
    static MHD_Result build_request_header(void* cls, MHD_ValueKind kind,
                                           const char* key, const char* value);
    static MHD_Result build_request_args(void* cls, MHD_ValueKind kind,
                                         const char* key, const char* value);
    static MHD_Result build_request_querystring(void* cls, MHD_ValueKind kind,
                                                const char* key, const char* value);
};

// Accumulator passed as cls to build_request_args via
// MHD_get_connection_values. Moved to this header (from the anonymous
// namespace in http_request.cpp) so unit tests can drive
// build_request_args directly and verify the DoS guard.
//
// Security limits (security-reviewer-iter1-2):
//   max_args_count: maximum number of distinct argument keys to accept
//     before returning MHD_NO. Prevents arena exhaustion from crafted
//     requests with thousands of unique GET parameters.
//   max_args_bytes: maximum total key+value bytes accumulated before
//     returning MHD_NO. Applies the same protection on a byte basis.
//
// Defaults are deliberately large (64 K / 64 KiB) so existing callers
// that construct the accumulator without setting these fields remain
// compatible. The webserver hot path sets these from connection_state
// or a compile-time constant once the create_webserver API exposes them
// (TODO(M5)).
struct arguments_accumulator {
    unescaper_ptr unescaper = nullptr;
    // TASK-016: points at the impl's pmr-backed map.
    std::pmr::map<std::pmr::string, std::pmr::vector<std::pmr::string>,
                  http::arg_comparator>* arguments = nullptr;
    // Per-request hard limits (security-reviewer-iter1-2).
    static constexpr std::size_t DEFAULT_MAX_ARGS_COUNT = 64;
    static constexpr std::size_t DEFAULT_MAX_ARGS_BYTES = 65536;
    std::size_t max_args_count = DEFAULT_MAX_ARGS_COUNT;
    std::size_t max_args_bytes = DEFAULT_MAX_ARGS_BYTES;
    // Running byte total (key + value lengths) across all calls.
    std::size_t accumulated_bytes = 0;
};

}  // namespace httpserver::detail

#endif  // SRC_HTTPSERVER_DETAIL_HTTP_REQUEST_IMPL_HPP_
