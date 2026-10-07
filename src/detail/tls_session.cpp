/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <httpserver/detail/tls_session.hpp>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
namespace httpserver::detail {
std::shared_ptr<tls_context> tls_context::client() {
    auto context = std::make_shared<tls_context>();
    context->native_ = {SSL_CTX_new(TLS_method()), [](void* p) { SSL_CTX_free(static_cast<SSL_CTX*>(p)); }};
    if (!context->native_) {
        throw std::runtime_error("TLS context unavailable");
    }
    SSL_CTX_set_min_proto_version(static_cast<SSL_CTX*>(context->native_.get()), TLS1_2_VERSION);
    return context;
}
std::shared_ptr<tls_context> tls_context::server_pem(std::string_view certificate, std::string_view key) {
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (certificate.size() > maximum || key.size() > maximum) {
        throw std::invalid_argument("TLS credentials too large");
    }
    auto context = client();
    using bio_ptr = std::unique_ptr<BIO, decltype(&BIO_free)>;
    bio_ptr cert_bio(BIO_new_mem_buf(certificate.data(), static_cast<int>(certificate.size())), BIO_free);
    bio_ptr key_bio(BIO_new_mem_buf(key.data(), static_cast<int>(key.size())), BIO_free);
    if (!cert_bio || !key_bio) {
        throw std::runtime_error("TLS credentials unavailable");
    }
    std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr), X509_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> private_key(PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    auto* ctx = static_cast<SSL_CTX*>(context->native_.get());
    if (!cert || !private_key || SSL_CTX_use_certificate(ctx, cert.get()) != 1 || SSL_CTX_use_PrivateKey(ctx, private_key.get()) != 1 || SSL_CTX_check_private_key(ctx) != 1) {
        ERR_clear_error();
        throw std::invalid_argument("TLS credentials invalid");
    }
    return context;
}
struct tls_session::impl {
    std::shared_ptr<tls_context> context;
    SSL* ssl = nullptr;
    BIO* wire = nullptr;
    ~impl() {
        SSL_free(ssl);
        BIO_free(wire);
    }
    result classify(int rc, std::size_t bytes = 0) {
        // Must follow the SSL call immediately on this same thread.
        if (rc > 0) {
            return {progress::complete, bytes};
        }
        switch (SSL_get_error(ssl, rc)) {
            case SSL_ERROR_WANT_READ:
                return {progress::input};
            case SSL_ERROR_WANT_WRITE:
                return {progress::output};
            case SSL_ERROR_ZERO_RETURN:
                return {progress::eof};
            default:
                ERR_clear_error();
                return {progress::failed};
        }
    }
};
tls_session::tls_session(std::shared_ptr<tls_context> context, bool server) : impl_(std::make_unique<impl>()) {
    if (!context || !context->native_) {
        throw std::invalid_argument("TLS context missing");
    }
    impl_->context = std::move(context);
    impl_->ssl = SSL_new(static_cast<SSL_CTX*>(impl_->context->native_.get()));
    BIO* local = nullptr;
    if (!impl_->ssl || BIO_new_bio_pair(&local, 16384, &impl_->wire, 16384) != 1) {
        throw std::runtime_error("TLS session unavailable");
    }
    SSL_set_bio(impl_->ssl, local, local);
    if (server) {
        SSL_set_accept_state(impl_->ssl);
    } else {
        SSL_set_connect_state(impl_->ssl);
    }
}
tls_session::~tls_session() = default;
tls_session::result tls_session::handshake() {
    ERR_clear_error();
    return impl_->classify(SSL_do_handshake(impl_->ssl));
}
tls_session::result tls_session::read(std::span<std::byte> buffer) {
    std::size_t bytes = 0;
    ERR_clear_error();
    const int rc = SSL_read_ex(impl_->ssl, buffer.data(), buffer.size(), &bytes);
    return impl_->classify(rc, bytes);
}
tls_session::result tls_session::write(std::span<const std::byte> buffer) {
    std::size_t bytes = 0;
    ERR_clear_error();
    const int rc = SSL_write_ex(impl_->ssl, buffer.data(), buffer.size(), &bytes);
    return impl_->classify(rc, bytes);
}
tls_session::result tls_session::shutdown() {
    ERR_clear_error();
    const int rc = SSL_shutdown(impl_->ssl);
    if (rc == 0) {
        return {progress::input};  // sent close_notify; still waiting for peer
    }
    return impl_->classify(rc);
}
std::size_t tls_session::input_capacity() const {
    return BIO_ctrl_get_write_guarantee(impl_->wire);
}
bool tls_session::feed(std::span<const std::byte> bytes) {
    return BIO_write(impl_->wire, bytes.data(), static_cast<int>(bytes.size())) == static_cast<int>(bytes.size());
}
std::size_t tls_session::drain(std::span<std::byte> bytes) {
    const int rc = BIO_read(impl_->wire, bytes.data(), static_cast<int>(bytes.size()));
    return rc > 0 ? static_cast<std::size_t>(rc) : 0;
}
bool tls_session::output_pending() const {
    return BIO_ctrl_pending(impl_->wire) != 0;
}
}  // namespace httpserver::detail
