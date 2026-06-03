/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

// http_request_auth.hpp — auth/credentials surface of class http_request.
//
// This header carries member-function DECLARATIONS only; it is meant to
// be included from WITHIN the body of `class http_request` defined in
// httpserver/http_request.hpp. Including it in any other context
// produces a compile error (see the inner gate below). The included
// declarations cover three related concerns kept in one place:
//
//   1. Basic-auth credentials (get_user / get_pass / get_digested_user)
//   2. TLS session and client-certificate accessors
//   3. Digest-auth verification entrypoints
//
// Keeping the surface in its own file lets the http_request.hpp class
// definition stay under the project-wide per-file line-count ceiling
// (FILE_LOC_MAX in scripts/check-file-size.sh) without splitting the
// public class across translation units.
#ifndef SRC_HTTPSERVER_HTTP_REQUEST_AUTH_HPP_
#define SRC_HTTPSERVER_HTTP_REQUEST_AUTH_HPP_

#ifndef SRC_HTTPSERVER_HTTP_REQUEST_HPP_INSIDE_CLASS_
#error "httpserver/http_request_auth.hpp must be included from inside the http_request class body in <httpserver/http_request.hpp>."
#endif

// -------------------------------------------------------------------
// Basic-auth credentials. The string_view returns alias arena-backed
// storage that lives for the handler call frame only — see the
// class-level lifetime contract in http_request.hpp.
//
// PRD-FLG-REQ-001 contract: every public symbol declared here is
// unconditional. http_request_auth.cpp returns documented sentinels
// (empty std::string_view for the BAUTH getters / get_digested_user;
// digest_auth_result::WRONG_HEADER for the two check_digest_auth
// overloads) on HAVE_BAUTH-off / HAVE_DAUTH-off builds. Any future
// addition to this header must preserve that property.
// -------------------------------------------------------------------

/**
 * Method used to get the username eventually passed through basic authentication.
 * @return string representation of the username.
 * @note The returned view is only valid within the handler's call frame.
 *       Copy into std::string if the value must outlast the handler.
 * @note (TASK-034 / PRD-FLG-REQ-001) Declared unconditionally.
 *       When HAVE_BAUTH is undefined the implementation returns an
 *       empty std::string_view sentinel (architecture spec §7).
**/
std::string_view get_user() const;

/**
 * Method used to get the username extracted from a digest authentication.
 * @return the username.
 * @note The returned view is only valid within the handler's call frame.
 *       Copy into std::string if the value must outlast the handler.
 * @note (TASK-034 / PRD-FLG-REQ-001) Declared unconditionally.
 *       When HAVE_DAUTH is undefined the implementation returns an
 *       empty std::string_view sentinel (architecture spec §7).
**/
std::string_view get_digested_user() const;

/**
 * Method used to get the password eventually passed through basic authentication.
 * @return string representation of the password.
 * @note The returned view is only valid within the handler's call frame.
 *       Copy into std::string if the value must outlast the handler.
 * @note (TASK-034 / PRD-FLG-REQ-001) Declared unconditionally.
 *       When HAVE_BAUTH is undefined the implementation returns an
 *       empty std::string_view sentinel (architecture spec §7).
**/
std::string_view get_pass() const;

// -------------------------------------------------------------------
// TASK-019: high-level GnuTLS accessors. Declared unconditionally
// (no build-flag preprocessor gate) so the public surface is
// identical in TLS-enabled and TLS-disabled builds. When the
// library is built without GnuTLS the implementations return
// empty / false / -1 sentinels without throwing (architecture
// spec §7).
// -------------------------------------------------------------------

/**
 * Method used to check whether the request was carried over a TLS
 * session.
 * @return true when a live TLS session is present, false otherwise
 *         (including when HAVE_GNUTLS is disabled at build time).
 **/
bool has_tls_session() const noexcept;

/**
 * Method used to check whether the peer presented a client
 * certificate over the TLS session.
 * @return true when a peer certificate is available, false
 *         otherwise (including when HAVE_GNUTLS is disabled at
 *         build time).
 **/
bool has_client_certificate() const noexcept;

/**
 * Subject Distinguished Name from the client certificate.
 * @return string_view over the cached subject DN; empty when no
 *         peer cert is present or HAVE_GNUTLS is disabled.
 * @note The returned view aliases storage owned by this
 *       http_request and is only valid for the lifetime of the
 *       request object (typically the handler invocation). Copy
 *       into std::string to extend the lifetime.
 **/
std::string_view get_client_cert_dn() const;

/**
 * Issuer Distinguished Name from the client certificate.
 * @return string_view over the cached issuer DN; empty when no
 *         peer cert is present or HAVE_GNUTLS is disabled.
 * @note Same lifetime contract as get_client_cert_dn().
 **/
std::string_view get_client_cert_issuer_dn() const;

/**
 * Common Name (CN) from the client certificate subject.
 * @return string_view over the cached CN; empty when no peer cert
 *         is present, the cert subject has no CN attribute, or
 *         HAVE_GNUTLS is disabled.
 * @note Multi-CN subjects: only the first CN is reported.
 * @note Same lifetime contract as get_client_cert_dn().
 **/
std::string_view get_client_cert_cn() const;

/**
 * SHA-256 fingerprint of the client certificate (hex-encoded).
 * @return string_view over the lowercase hex-encoded SHA-256
 *         fingerprint (64 hex chars); empty when no peer cert is
 *         present or HAVE_GNUTLS is disabled.
 * @note Same lifetime contract as get_client_cert_dn().
 **/
std::string_view get_client_cert_fingerprint_sha256() const;

/**
 * Whether the peer certificate chain validated successfully against
 * the configured trust anchors.
 * @return true on successful validation, false otherwise (including
 *         when no cert was presented or HAVE_GNUTLS is disabled).
 **/
bool is_client_cert_verified() const noexcept;

/**
 * Activation time (`Not Before`) of the client certificate, in
 * seconds since the UNIX epoch.
 * @return seconds-since-epoch as a 64-bit signed integer; -1 when
 *         no peer cert is present or HAVE_GNUTLS is disabled.
 **/
std::int64_t get_client_cert_not_before() const noexcept;

/**
 * Expiration time (`Not After`) of the client certificate, in
 * seconds since the UNIX epoch.
 * @return seconds-since-epoch as a 64-bit signed integer; -1 when
 *         no peer cert is present or HAVE_GNUTLS is disabled.
 **/
std::int64_t get_client_cert_not_after() const noexcept;

// -------------------------------------------------------------------
// Digest-auth verification entrypoints. Same TASK-034 contract: the
// declarations are unconditional; the implementations return the
// WRONG_HEADER sentinel when the library is built without HAVE_DAUTH.
// -------------------------------------------------------------------

/**
 * Digest-authenticate the current request against (@p realm, @p password).
 *
 * (TASK-034 / PRD-FLG-REQ-001) Declared unconditionally. When the
 * library was built without HAVE_DAUTH the implementation returns
 * the sentinel `digest_auth_result::WRONG_HEADER` (architecture
 * spec §7 "returns a sentinel result") without touching MHD.
 *
 * @param realm         protection realm advertised in the WWW-Authenticate header.
 * @param password      cleartext password to verify against.
 * @param nonce_timeout nonce lifetime in seconds (0 = backend default).
 * @param max_nc        max accepted nonce-count value (0 = backend default).
 * @param algo          digest hash algorithm; defaults to SHA-256.
 * @return one of the @ref httpserver::http::http_utils::digest_auth_result values.
 **/
http::http_utils::digest_auth_result check_digest_auth(
    const std::string& realm,
    const std::string& password,
    unsigned int nonce_timeout = 0,
    uint32_t max_nc = 0,
    http::http_utils::digest_algorithm algo = http::http_utils::digest_algorithm::SHA256) const;

/**
 * Digest-authenticate using a pre-computed user digest (no cleartext password).
 *
 * Variant of @ref check_digest_auth that takes a raw H(A1) digest
 * instead of a cleartext password. Same feature-flag behaviour:
 * declared unconditionally; returns
 * `digest_auth_result::WRONG_HEADER` on HAVE_DAUTH-off builds.
 *
 * @param realm          protection realm advertised in the WWW-Authenticate header.
 * @param userdigest     pre-computed digest of the username/realm/password.
 * @param userdigest_size size of @p userdigest in bytes.
 * @param nonce_timeout  nonce lifetime in seconds (0 = backend default).
 * @param max_nc         max accepted nonce-count value (0 = backend default).
 * @param algo           digest hash algorithm; defaults to SHA-256.
 * @return one of the @ref httpserver::http::http_utils::digest_auth_result values.
**/
// NOLINTNEXTLINE(build/include_what_you_use) -- this file is included inside
// the http_request class body; transitive <string> lives in
// the parent http_request.hpp.
http::http_utils::digest_auth_result check_digest_auth_digest(
    const std::string& realm,
    const void* userdigest,
    size_t userdigest_size,
    unsigned int nonce_timeout = 0,
    uint32_t max_nc = 0,
    http::http_utils::digest_algorithm algo = http::http_utils::digest_algorithm::SHA256) const;

#endif  // SRC_HTTPSERVER_HTTP_REQUEST_AUTH_HPP_
