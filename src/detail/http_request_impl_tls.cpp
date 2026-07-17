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

// detail::http_request_impl method bodies — TLS / client-certificate
// section. Compiled only when HAVE_GNUTLS is defined; on builds without
// GnuTLS this TU contributes nothing to the library (the public
// accessors short-circuit on the HAVE_GNUTLS-off path before reaching
// these symbols, so they don't need to exist).

#include "httpserver/http_request.hpp"

#ifdef HAVE_GNUTLS

#include <stdio.h>

#include <gnutls/gnutls.h>  // NOLINT(build/include_order)
#include <gnutls/x509.h>  // NOLINT(build/include_order)
#include <microhttpd.h>  // NOLINT(build/include_order)

#include <string>

#include "httpserver/detail/http_request_impl.hpp"

namespace {

// RAII wrapper for gnutls_x509_crt_t to ensure proper cleanup.
class scoped_x509_cert {
 public:
    scoped_x509_cert() : cert_(nullptr), valid_(false) {}

    ~scoped_x509_cert() {
        if (cert_ != nullptr) {
            gnutls_x509_crt_deinit(cert_);
        }
    }

    // Initialize from a TLS session's peer certificate
    // Returns true if certificate was successfully loaded
    bool init_from_session(gnutls_session_t session) {
        unsigned int list_size = 0;
        const gnutls_datum_t* cert_list = gnutls_certificate_get_peers(session, &list_size);

        if (cert_list == nullptr || list_size == 0) {
            return false;
        }

        if (gnutls_x509_crt_init(&cert_) != GNUTLS_E_SUCCESS) {
            cert_ = nullptr;
            return false;
        }

        if (gnutls_x509_crt_import(cert_, &cert_list[0], GNUTLS_X509_FMT_DER) != GNUTLS_E_SUCCESS) {
            gnutls_x509_crt_deinit(cert_);
            cert_ = nullptr;
            return false;
        }

        valid_ = true;
        return true;
    }

    bool is_valid() const { return valid_; }
    gnutls_x509_crt_t get() const { return cert_; }

    // Movable
    scoped_x509_cert(scoped_x509_cert&& other) noexcept
        : cert_(other.cert_), valid_(other.valid_) {
        other.cert_ = nullptr;
        other.valid_ = false;
    }
    scoped_x509_cert& operator=(scoped_x509_cert&& other) noexcept {
        if (this != &other) {
            if (cert_ != nullptr) gnutls_x509_crt_deinit(cert_);
            cert_ = other.cert_;
            valid_ = other.valid_;
            other.cert_ = nullptr;
            other.valid_ = false;
        }
        return *this;
    }

    // Non-copyable
    scoped_x509_cert(const scoped_x509_cert&) = delete;
    scoped_x509_cert& operator=(const scoped_x509_cert&) = delete;

 private:
    gnutls_x509_crt_t cert_;
    bool valid_;
};

}  // namespace

namespace httpserver {

namespace detail {

bool http_request_impl::has_tls_session() const {
    // Test-request path: connection_ is null, return the local flag.
    if (connection_ == nullptr) {
        return tls_enabled_local;
    }
    const MHD_ConnectionInfo* conninfo = MHD_get_connection_info(connection_, MHD_CONNECTION_INFO_GNUTLS_SESSION);
    return (conninfo != nullptr);
}

gnutls_session_t http_request_impl::get_tls_session() const {
    // The test-request path (connection_ == nullptr) has no
    // live MHD connection to query; return null so callers fall back
    // to the no-cert sentinels rather than UB-dereferencing in MHD.
    if (connection_ == nullptr) {
        return nullptr;
    }
    const MHD_ConnectionInfo* conninfo = MHD_get_connection_info(connection_, MHD_CONNECTION_INFO_GNUTLS_SESSION);

    if (conninfo == nullptr) {
        return nullptr;
    }

    return static_cast<gnutls_session_t>(conninfo->tls_session);
}

bool http_request_impl::has_client_certificate() const {
    if (!has_tls_session()) {
        return false;
    }

    // Even when the test-request flag advertises a TLS
    // session, there is no live MHD connection from which to extract
    // peer certs. Bail out before calling into GnuTLS so the public
    // accessors honour the "no live cert means false / empty / -1"
    // sentinel contract.
    gnutls_session_t session = get_tls_session();
    if (session == nullptr) {
        return false;
    }
    unsigned int list_size = 0;
    const gnutls_datum_t* cert_list = gnutls_certificate_get_peers(session, &list_size);

    return (cert_list != nullptr && list_size > 0);
}

namespace {

// Two-pass GnuTLS string extractor: first call discovers the length,
// second call fills the buffer. The DN APIs (get_dn / get_issuer_dn)
// share this exact shape, so parameterising on the function pointer
// dedupes the wrapping and keeps the surrounding ctor under the CCN
// bar.
using x509_string_getter = int (*)(gnutls_x509_crt_t, char*, size_t*);

std::string extract_x509_string(gnutls_x509_crt_t cert,
                                x509_string_getter getter) {
    size_t size = 0;
    getter(cert, nullptr, &size);
    std::string buf(size, '\0');
    if (getter(cert, buf.data(), &size) != GNUTLS_E_SUCCESS) return {};
    if (!buf.empty() && buf.back() == '\0') buf.pop_back();
    return buf;
}

std::string extract_x509_common_name(gnutls_x509_crt_t cert) {
    size_t cn_size = 0;
    gnutls_x509_crt_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME, 0, 0,
                                  nullptr, &cn_size);
    if (cn_size == 0) return {};
    std::string cn(cn_size, '\0');
    if (gnutls_x509_crt_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME, 0, 0,
                                      cn.data(), &cn_size) != GNUTLS_E_SUCCESS) {
        return {};
    }
    if (!cn.empty() && cn.back() == '\0') cn.pop_back();
    return cn;
}

// SHA-256 produces 32 bytes (256 bits). Named constant avoids the magic
// number in the fingerprint buffer declaration and in the hex-length
// computation below.
static constexpr size_t SHA256_DIGEST_BYTES = 32;

std::string extract_x509_fingerprint_sha256(gnutls_x509_crt_t cert) {
    unsigned char fingerprint[SHA256_DIGEST_BYTES];
    size_t fingerprint_size = sizeof(fingerprint);
    if (gnutls_x509_crt_get_fingerprint(cert, GNUTLS_DIG_SHA256, fingerprint,
                                        &fingerprint_size) != GNUTLS_E_SUCCESS) {
        return {};
    }
    std::string hex;
    hex.reserve(fingerprint_size * 2);
    for (size_t i = 0; i < fingerprint_size; ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", fingerprint[i]);
        hex += buf;
    }
    return hex;
}

bool verify_peer_certificate(gnutls_session_t session) {
    unsigned int status = 0;
    if (gnutls_certificate_verify_peers2(session, &status) != GNUTLS_E_SUCCESS) {
        return false;
    }
    return status == 0;
}

}  // namespace

void http_request_impl::populate_all_cert_fields() const {
    if (client_cert_fields_cached) return;
    client_cert_fields_cached = true;

    // get_tls_session() already handles the null-connection case and the
    // no-TLS-session case (returns nullptr for both). A separate
    // has_tls_session() call is redundant; calling get_tls_session()
    // once is sufficient and halves the MHD_get_connection_info calls.
    gnutls_session_t session = get_tls_session();

    scoped_x509_cert cert;
    if (session != nullptr) cert.init_from_session(session);

    if (!cert.is_valid()) {
        // Default values (empty strings and -1) are already set by the
        // impl member initializers; client_cert_verified defaults to false.
        return;
    }

    client_cert_verified = verify_peer_certificate(session);

    // pmr::string has no cross-allocator copy-assign, so we route every
    // assignment through .assign(ptr, len).
    auto subject_dn = extract_x509_string(cert.get(), gnutls_x509_crt_get_dn);
    client_cert_dn.assign(subject_dn.data(), subject_dn.size());

    auto issuer_dn = extract_x509_string(cert.get(), gnutls_x509_crt_get_issuer_dn);
    client_cert_issuer_dn.assign(issuer_dn.data(), issuer_dn.size());

    auto cn = extract_x509_common_name(cert.get());
    client_cert_cn.assign(cn.data(), cn.size());

    auto fp = extract_x509_fingerprint_sha256(cert.get());
    client_cert_fingerprint_sha256.assign(fp.data(), fp.size());

    // Validity times. The GnuTLS API returns std::time_t; we cast to
    // int64_t so the public accessors can return a fixed-width type
    // regardless of how the local platform sizes time_t. ((time_t)-1
    // round-trips cleanly because both width and signedness are
    // preserved by the static_cast.)
    client_cert_not_before = static_cast<std::int64_t>(
        gnutls_x509_crt_get_activation_time(cert.get()));
    client_cert_not_after = static_cast<std::int64_t>(
        gnutls_x509_crt_get_expiration_time(cert.get()));
}

}  // namespace detail
}  // namespace httpserver

#endif  // HAVE_GNUTLS
