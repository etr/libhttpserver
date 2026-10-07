/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <httpserver/detail/tls_session.hpp>
#include <httpserver/detail/tls_credentials.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>

#include <algorithm>
#include <memory>
#include <string>
#include <stdexcept>
#include <utility>
namespace httpserver::detail {
struct tls_session::impl {
    tls_credentials_selection selection;
    SSL* ssl = nullptr;
    bool accepted_name = false;
    std::size_t selected_host = 0;
    static int index() {
        static const int value = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
        return value;
    }
    static impl* state(SSL* ssl) { return static_cast<impl*>(SSL_get_ex_data(ssl, index())); }
    static unsigned u16(const unsigned char* p) { return (static_cast<unsigned>(p[0]) << 8) | p[1]; }
    static std::string hello_name(SSL* ssl) {
        const unsigned char* data = nullptr;
        std::size_t size = 0;
        if (!SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_server_name, &data, &size)) return {};
        if (size < 6 || u16(data) != size - 2 || data[2] != TLSEXT_NAMETYPE_host_name || u16(data + 3) != size - 5) {
            throw std::invalid_argument("TLS server name invalid");
        }
        return canonical_tls_host(std::string(reinterpret_cast<const char*>(data + 5), size - 5));
    }
    int select_hello() {
        if (!selection.snapshot) return SSL_CLIENT_HELLO_SUCCESS;
        const auto name = hello_name(ssl);
        auto host = selection.snapshot->default_host();
        accepted_name = false;
        const auto& hosts = selection.snapshot->hosts();
        for (std::size_t i = 0; i < hosts.size(); ++i) {
            if (hosts[i].host == name) {
                host = i;
                accepted_name = true;
                break;
            }
        }
        selected_host = host;
        selection = selection.snapshot->select(host);
        if (!SSL_set_SSL_CTX(ssl, static_cast<SSL_CTX*>(selection.context->native_.get()))) {
            throw std::runtime_error("TLS selection unavailable");
        }
        // SSL_set_SSL_CTX does not replace the initial ticket/cache owner.
        // Bind lookup to the selected immutable host before resumption runs.
        const auto& id = selection.context->session_namespace_;
        if (SSL_set_session_id_context(ssl, id.data(), id.size()) != 1) {
            throw std::runtime_error("TLS selection unavailable");
        }
        return SSL_CLIENT_HELLO_SUCCESS;
    }
    static int client_hello(SSL* ssl, int* alert, void*) noexcept {
        try {
            auto* self = state(ssl);
            if (!self) throw std::runtime_error("TLS session unavailable");
            const int result = self->select_hello();
            if (!self->allows_absent_alpn()) {
                *alert = SSL_AD_NO_APPLICATION_PROTOCOL;
                return SSL_CLIENT_HELLO_ERROR;
            }
            return result;
        } catch (const std::invalid_argument&) {
            *alert = SSL_AD_DECODE_ERROR;
        } catch (...) {
            *alert = SSL_AD_INTERNAL_ERROR;
        }
        return SSL_CLIENT_HELLO_ERROR;
    }
    static int server_name(SSL* ssl, int*, void*) noexcept {
        auto* self = state(ssl);
        return self && self->accepted_name ? SSL_TLSEXT_ERR_OK : SSL_TLSEXT_ERR_NOACK;
    }
    const tls_host_metadata& metadata() const { return selection.snapshot->hosts()[selected_host]; }
    bool allows_absent_alpn() const {
        if (!selection.snapshot || metadata().alpn.empty()) return true;
        const unsigned char* data = nullptr;
        std::size_t size = 0;
        if (SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_application_layer_protocol_negotiation, &data, &size)) return true;
        const auto& protocols = metadata().alpn;
        return std::find(protocols.begin(), protocols.end(), "http/1.1") != protocols.end();
    }
    static bool http_protocol(std::string_view token) { return token == "h2" || token == "http/1.1"; }
    static const unsigned char* offered(std::string_view token, const unsigned char* input, unsigned size) {
        unsigned pos = 0;
        while (pos < size) {
            const unsigned length = input[pos++];
            if (!length || length > size - pos) return nullptr;
            if (token == std::string_view(reinterpret_cast<const char*>(input + pos), length)) return input + pos;
            pos += length;
        }
        return nullptr;
    }
    static int alpn(SSL* ssl, const unsigned char** out, unsigned char* length, const unsigned char* input, unsigned size, void*) noexcept {
        try {
            const auto* self = state(ssl);
            if (!self) return SSL_TLSEXT_ERR_ALERT_FATAL;
            if (!self->selection.snapshot || self->metadata().alpn.empty()) return SSL_TLSEXT_ERR_NOACK;
            for (const auto& token : self->metadata().alpn) {
                if (!http_protocol(token)) continue;
                if (const auto* match = offered(token, input, size)) {
                    *out = match;
                    *length = static_cast<unsigned char>(token.size());
                    return SSL_TLSEXT_ERR_OK;
                }
            }
        } catch (...) {
            // No C++ exception may cross the provider callback boundary.
        }
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
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
void tls_context::configure_server() {
    auto* ctx = static_cast<SSL_CTX*>(native_.get());
    if (RAND_bytes(session_namespace_.data(), session_namespace_.size()) != 1 ||
        SSL_CTX_set_session_id_context(ctx, session_namespace_.data(), session_namespace_.size()) != 1) {
        throw std::invalid_argument("TLS credentials invalid");
    }
    SSL_CTX_set_alpn_select_cb(ctx, tls_session::impl::alpn, nullptr);
    SSL_CTX_set_client_hello_cb(ctx, tls_session::impl::client_hello, nullptr);
    SSL_CTX_set_tlsext_servername_callback(ctx, tls_session::impl::server_name);
}
tls_session::tls_session(std::shared_ptr<const tls_context> context, bool server) : tls_session(tls_credentials_selection{nullptr, std::move(context)}, server) {}
tls_session::tls_session(tls_credentials_selection selection, bool server) : impl_(std::make_unique<impl>()) {
    if (!selection.context || !selection.context->native_) {
        throw std::invalid_argument("TLS context missing");
    }
    impl_->selection = std::move(selection);
    impl_->ssl = SSL_new(static_cast<SSL_CTX*>(impl_->selection.context->native_.get()));
    BIO* local = nullptr;
    if (!impl_->ssl || BIO_new_bio_pair(&local, 16384, &impl_->wire, 16384) != 1) {
        throw std::runtime_error("TLS session unavailable");
    }
    SSL_set_bio(impl_->ssl, local, local);
    if (impl::index() < 0 || SSL_set_ex_data(impl_->ssl, impl::index(), impl_.get()) != 1) {
        throw std::runtime_error("TLS session unavailable");
    }
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
