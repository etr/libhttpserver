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

// http_request public-API forwarders for the auth/credentials surface:
// get_user / get_pass / get_digested_user, check_digest_auth /
// check_digest_auth_digest, and the high-level TLS / client-certificate
// accessors. The declarations live in httpserver/http_request_auth.hpp
// (included by httpserver/http_request.hpp via the in-class-body
// include pattern).

#include "httpserver/http_request.hpp"

#include <stdint.h>

#include <string>
#include <string_view>

#include <microhttpd.h>

#include "httpserver/detail/http_request_impl.hpp"
#include "httpserver/http_utils.hpp"

namespace httpserver {

std::string_view http_request::get_user() const {
#ifdef HAVE_BAUTH
    // Use user_pass_fetched instead of !username.empty() as the cache
    // guard: an empty username string after a successful MHD call is a
    // valid result (no auth header), but would cause a redundant re-fetch
    // on every subsequent get_user() call without the boolean.
    // (code-simplifier-iter1 finding #6 / major review item)
    if (!impl_->user_pass_fetched) {
        impl_->fetch_user_pass();
        // fetch_user_pass() sets user_pass_fetched = true on the live path.
        // On the test-request path (connection_ == nullptr) it returns early
        // without setting the flag; set it here so future calls are skipped.
        impl_->user_pass_fetched = true;
    }
    return impl_->username;
#else
    // TASK-034 §7 sentinel: BAUTH disabled at build time -> empty view.
    return std::string_view{};
#endif
}

std::string_view http_request::get_pass() const {
#ifdef HAVE_BAUTH
    // Mirror the get_user() boolean guard (code-simplifier-iter1 #6).
    if (!impl_->user_pass_fetched) {
        impl_->fetch_user_pass();
        impl_->user_pass_fetched = true;
    }
    return impl_->password;
#else
    return std::string_view{};
#endif
}

std::string_view http_request::get_digested_user() const {
#ifdef HAVE_DAUTH
    if (!impl_->digested_user.empty()) {
        return impl_->digested_user;
    }

    // Test-request path: connection_ is null, digested_user already set.
    if (impl_->connection_ == nullptr) {
        return impl_->digested_user;
    }

    struct MHD_DigestAuthUsernameInfo* info = MHD_digest_auth_get_username3(impl_->connection_);

    impl_->digested_user = EMPTY;
    if (info != nullptr) {
        if (info->username != nullptr) {
            impl_->digested_user.assign(info->username, info->username_len);
        }
        MHD_free(info);
    }

    return impl_->digested_user;
#else
    // TASK-034 §7 sentinel: DAUTH disabled at build time -> empty view.
    return std::string_view{};
#endif
}

http::http_utils::digest_auth_result http_request::check_digest_auth(
    const std::string& realm,
    const std::string& password,
    unsigned int nonce_timeout,
    uint32_t max_nc,
    http::http_utils::digest_algorithm algo) const {
#ifdef HAVE_DAUTH
    // CWE-476 guard: test-request path sets connection_ to nullptr.
    // Passing nullptr to MHD_digest_auth_check3 is undefined behaviour;
    // return the same WRONG_HEADER sentinel used on the HAVE_DAUTH-off
    // path and by get_digested_user() when connection_ is null.
    if (impl_->connection_ == nullptr) {
        return http::http_utils::digest_auth_result::WRONG_HEADER;
    }

    std::string_view digested_user = get_digested_user();

    enum MHD_DigestAuthResult result = MHD_digest_auth_check3(
        impl_->connection_,
        realm.c_str(),
        digested_user.data(),
        password.c_str(),
        nonce_timeout,
        max_nc,
        MHD_DIGEST_AUTH_MULT_QOP_ANY_NON_INT,
        static_cast<MHD_DigestAuthMultiAlgo3>(algo));

    return static_cast<http::http_utils::digest_auth_result>(result);
#else
    // TASK-034 §7 sentinel: DAUTH disabled at build time -> the
    // call is documented to "return a sentinel result". WRONG_HEADER
    // is the most explicit "this request was never authenticated"
    // terminal value of the digest_auth_result enum.
    (void)realm;
    (void)password;
    (void)nonce_timeout;
    (void)max_nc;
    (void)algo;
    return http::http_utils::digest_auth_result::WRONG_HEADER;
#endif
}

http::http_utils::digest_auth_result http_request::check_digest_auth_digest(
    const std::string& realm,
    const void* userdigest,
    size_t userdigest_size,
    unsigned int nonce_timeout,
    uint32_t max_nc,
    http::http_utils::digest_algorithm algo) const {
#ifdef HAVE_DAUTH
    // CWE-476 guard: same null-connection guard as check_digest_auth.
    if (impl_->connection_ == nullptr) {
        return http::http_utils::digest_auth_result::WRONG_HEADER;
    }

    std::string_view digested_user = get_digested_user();

    enum MHD_DigestAuthResult result = MHD_digest_auth_check_digest3(
        impl_->connection_,
        realm.c_str(),
        digested_user.data(),
        userdigest,
        userdigest_size,
        nonce_timeout,
        max_nc,
        MHD_DIGEST_AUTH_MULT_QOP_ANY_NON_INT,
        static_cast<MHD_DigestAuthMultiAlgo3>(algo));

    return static_cast<http::http_utils::digest_auth_result>(result);
#else
    (void)realm;
    (void)userdigest;
    (void)userdigest_size;
    (void)nonce_timeout;
    (void)max_nc;
    (void)algo;
    return http::http_utils::digest_auth_result::WRONG_HEADER;
#endif
}

// ----------------------------------------------------------------------
// TASK-019: high-level GnuTLS accessors. Public surface is unconditional
// (same symbols visible whether HAVE_GNUTLS is on or off at build time);
// only the body of each method dispatches on HAVE_GNUTLS. Sentinel
// returns when GnuTLS is disabled match the architecture spec §7:
// empty for the four string_view accessors, false for the three
// predicates, -1 for the two time accessors.
//
// The five accessors documented as `noexcept` (the three predicates and
// the two times) wrap populate_all_cert_fields() in try/catch so that a
// hypothetical std::bad_alloc from the cache-fill path doesn't violate
// the noexcept commitment; on throw we return the documented sentinel.
// The four string_view accessors deliberately omit `noexcept` so the
// allocator failure mode is observable (and they don't need a
// try/catch).
// ----------------------------------------------------------------------
bool http_request::has_tls_session() const noexcept {
#ifdef HAVE_GNUTLS
    return impl_->has_tls_session();
#else
    return false;
#endif
}

bool http_request::has_client_certificate() const noexcept {
#ifdef HAVE_GNUTLS
    return impl_->has_client_certificate();
#else
    return false;
#endif
}

std::string_view http_request::get_client_cert_dn() const {
#ifdef HAVE_GNUTLS
    impl_->populate_all_cert_fields();
    return std::string_view(impl_->client_cert_dn.data(),
                            impl_->client_cert_dn.size());
#else
    return std::string_view{};
#endif
}

std::string_view http_request::get_client_cert_issuer_dn() const {
#ifdef HAVE_GNUTLS
    impl_->populate_all_cert_fields();
    return std::string_view(impl_->client_cert_issuer_dn.data(),
                            impl_->client_cert_issuer_dn.size());
#else
    return std::string_view{};
#endif
}

std::string_view http_request::get_client_cert_cn() const {
#ifdef HAVE_GNUTLS
    impl_->populate_all_cert_fields();
    return std::string_view(impl_->client_cert_cn.data(),
                            impl_->client_cert_cn.size());
#else
    return std::string_view{};
#endif
}

std::string_view http_request::get_client_cert_fingerprint_sha256() const {
#ifdef HAVE_GNUTLS
    impl_->populate_all_cert_fields();
    return std::string_view(impl_->client_cert_fingerprint_sha256.data(),
                            impl_->client_cert_fingerprint_sha256.size());
#else
    return std::string_view{};
#endif
}

bool http_request::is_client_cert_verified() const noexcept {
#ifdef HAVE_GNUTLS
    try {
        impl_->populate_all_cert_fields();
        return impl_->client_cert_verified;
    } catch (...) {
        return false;
    }
#else
    return false;
#endif
}

std::int64_t http_request::get_client_cert_not_before() const noexcept {
#ifdef HAVE_GNUTLS
    try {
        impl_->populate_all_cert_fields();
        return impl_->client_cert_not_before;
    } catch (...) {
        return -1;
    }
#else
    return -1;
#endif
}

std::int64_t http_request::get_client_cert_not_after() const noexcept {
#ifdef HAVE_GNUTLS
    try {
        impl_->populate_all_cert_fields();
        return impl_->client_cert_not_after;
    } catch (...) {
        return -1;
    }
#else
    return -1;
#endif
}

}  // namespace httpserver
