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
    http_request_impl() = default;
    http_request_impl(MHD_Connection* connection, unescaper_ptr unescaper)
        : connection_(connection), unescaper_(unescaper) {}

    http_request_impl(const http_request_impl&) = delete;
    http_request_impl& operator=(const http_request_impl&) = delete;
    // Moves are left implicitly defined (and unused -- the impl is held
    // through unique_ptr so http_request's defaulted moves operate on
    // the unique_ptr, not on the impl directly).

    // --- per-request backend handles ---
    MHD_Connection* connection_ = nullptr;
    unescaper_ptr unescaper_ = nullptr;
    file_cleanup_callback_ptr file_cleanup_callback_ = nullptr;
    std::map<std::string, std::map<std::string, http::file_info>> files_;

    // --- test-request local storage ---
    // When connection_ is null (create_test_request path), get_header /
    // get_footer / get_cookie / get_headerlike_values / get_requestor_port /
    // has_tls_session fall back to these instead of calling MHD APIs.
    http::header_map headers_local;
    http::header_map footers_local;
    http::header_map cookies_local;
    uint16_t requestor_port_local = 0;
#ifdef HAVE_GNUTLS
    bool tls_enabled_local = false;
#endif  // HAVE_GNUTLS

    // --- lazy caches (formerly the http_request_data_cache struct) ---
    // All marked mutable: const accessors lazily populate them.
#ifdef HAVE_BAUTH
    mutable std::string username;
    mutable std::string password;
#endif  // HAVE_BAUTH
    mutable std::string querystring;
    mutable std::string requestor_ip;
#ifdef HAVE_DAUTH
    mutable std::string digested_user;
#endif  // HAVE_DAUTH
    mutable std::map<std::string, std::vector<std::string>, http::arg_comparator> unescaped_args;
    mutable std::vector<std::string> path_pieces;
    mutable bool args_populated = false;
    mutable bool path_pieces_cached = false;

#ifdef HAVE_GNUTLS
    mutable bool client_cert_fields_cached = false;
    mutable std::string client_cert_dn;
    mutable std::string client_cert_issuer_dn;
    mutable std::string client_cert_cn;
    mutable std::string client_cert_fingerprint_sha256;
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

}  // namespace httpserver::detail

#endif  // SRC_HTTPSERVER_DETAIL_HTTP_REQUEST_IMPL_HPP_
