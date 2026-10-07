/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <httpserver/detail/tls_session.hpp>
#include <httpserver/detail/tls_credentials.hpp>
#include <httpserver/detail/tls_psk_attempt.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <memory>
#include <string>
#include <stdexcept>
#include <utility>
namespace httpserver::detail {
namespace {
void metadata_check(bool valid) {
    if (!valid) throw std::runtime_error("TLS peer metadata unavailable");
}
std::string distinguished_name(X509_NAME* name) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
    metadata_check(bio && name);
    metadata_check(X509_NAME_print_ex(bio.get(), name, 0, XN_FLAG_RFC2253) >= 0);
    char* data = nullptr;
    const auto size = BIO_get_mem_data(bio.get(), &data);
    metadata_check(size >= 0);
    return size ? std::string(data, static_cast<std::size_t>(size)) : std::string{};
}
std::string common_name(X509_NAME* name) {
    const int index = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    if (index < 0) return {};
    const auto* entry = X509_NAME_get_entry(name, index);
    metadata_check(entry != nullptr);
    unsigned char* data = nullptr;
    const int size = ASN1_STRING_to_UTF8(&data, X509_NAME_ENTRY_get_data(entry));
    const auto free_bytes = [](unsigned char* bytes) { OPENSSL_free(bytes); };
    std::unique_ptr<unsigned char, decltype(free_bytes)> owned(data, free_bytes);
    metadata_check(size >= 0);
    return size ? std::string(reinterpret_cast<const char*>(data), static_cast<std::size_t>(size)) : std::string{};
}
std::string fingerprint(X509* certificate) {
    unsigned char bytes[EVP_MAX_MD_SIZE];
    unsigned size = 0;
    metadata_check(X509_digest(certificate, EVP_sha256(), bytes, &size) == 1 && size == 32);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (unsigned i = 0; i < size; ++i) {
        result.push_back(hex[bytes[i] >> 4]);
        result.push_back(hex[bytes[i] & 15]);
    }
    return result;
}
std::int64_t unix_seconds(const ASN1_TIME* time) {
    std::tm tm{};
    metadata_check(ASN1_TIME_to_tm(time, &tm) == 1);
    using std::chrono::year_month_day;
    using std::chrono::year;
    using std::chrono::month;
    using std::chrono::day;
    using std::chrono::sys_days;
    using std::chrono::seconds;
    using std::chrono::duration_cast;
    const year_month_day date{year{tm.tm_year + 1900}, month{static_cast<unsigned>(tm.tm_mon + 1)}, day{static_cast<unsigned>(tm.tm_mday)}};
    metadata_check(date.ok());
    return duration_cast<seconds>(sys_days{date}.time_since_epoch()).count() +
        tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
}
std::shared_ptr<const server::tls_peer_metadata> copy_peer(SSL* ssl, bool server_side) {
    auto result = std::make_shared<server::tls_peer_metadata>();
    if (!server_side) return result;
    std::unique_ptr<X509, decltype(&X509_free)> leaf(SSL_get1_peer_certificate(ssl), X509_free);
    if (!leaf) return result;
    result->has_client_certificate = true;
    result->client_certificate_verified = (SSL_get_verify_mode(ssl) & SSL_VERIFY_PEER) && SSL_get_verify_result(ssl) == X509_V_OK;
    result->subject_dn = distinguished_name(X509_get_subject_name(leaf.get()));
    result->issuer_dn = distinguished_name(X509_get_issuer_name(leaf.get()));
    result->common_name = common_name(X509_get_subject_name(leaf.get()));
    result->fingerprint_sha256 = fingerprint(leaf.get());
    result->not_before = unix_seconds(X509_get0_notBefore(leaf.get()));
    result->not_after = unix_seconds(X509_get0_notAfter(leaf.get()));
    return result;
}
}  // namespace
struct tls_session::impl {
    tls_credentials_selection selection;
    SSL* ssl = nullptr;
    bool accepted_name = false;
    bool acme = false;
    bool sole_acme_offer = false;
    std::string challenge_name;
    tls_handshake_context handshake_context;
    bool server_side = false;
    bool metadata_failed = false;
    std::shared_ptr<const server::tls_peer_metadata> peer;
    std::size_t selected_host = 0;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    std::stop_token cancellation;
    http::outcome_code psk_failure = http::outcome_code::protocol_error;
    std::unique_ptr<tls_psk_attempt> attempt;
    psk_lookup_result lookup(psk_tls_version version, std::span<const std::byte> identity, std::size_t capacity) {
        const auto config = selection.context->psk_;
        if (!config || identity.empty() || identity.size() > config->maximum_identity_bytes) return {};
        psk_handshake_context context{version, selection.snapshot ? selection.snapshot->generation() : 0,
            selection.snapshot ? metadata().host : std::string{}, deadline, cancellation, std::min(capacity, config->maximum_key_bytes)};
        if (!attempt) attempt = std::make_unique<tls_psk_attempt>(*config, std::move(context));
        auto result = attempt->lookup(version, identity, capacity);
        switch (result.status) {
            case psk_lookup_status::timeout: psk_failure = http::outcome_code::timeout; break;
            case psk_lookup_status::cancelled: psk_failure = http::outcome_code::cancelled; break;
            case psk_lookup_status::limit_exceeded: psk_failure = http::outcome_code::limit_exceeded; break;
            default: break;
        }
        return result;
    }
    static unsigned psk12(SSL* ssl, const char* identity, unsigned char* key, unsigned capacity) noexcept {
        try {
            auto* self = state(ssl);
            if (!self || !self->selection.context->psk_ || SSL_version(ssl) >= TLS1_3_VERSION || !identity) return 0;
            const auto maximum = std::min<std::size_t>(256, self->selection.context->psk_->maximum_identity_bytes);
            std::size_t size = 0;
            while (size <= maximum && identity[size] != 0) ++size;
            if (size > maximum) return 0;
            auto result = self->lookup(psk_tls_version::tls12, {reinterpret_cast<const std::byte*>(identity), size}, std::min<unsigned>(capacity, 512));
            if (result.status != psk_lookup_status::accepted) return 0;
            std::memcpy(key, result.key.bytes().data(), result.key.bytes().size());
            return static_cast<unsigned>(result.key.bytes().size());
        } catch (...) { return 0; }
    }
    static SSL_SESSION* external_session(SSL* ssl, const secure_bytes& key) {
        std::unique_ptr<SSL_SESSION, decltype(&SSL_SESSION_free)> owned(SSL_SESSION_new(), SSL_SESSION_free);
        const unsigned char cipher_id[] = {0x13, 0x01};
        const auto* cipher = SSL_CIPHER_find(ssl, cipher_id);
        if (!owned || !cipher || SSL_SESSION_set1_master_key(owned.get(), reinterpret_cast<const unsigned char*>(key.bytes().data()), key.bytes().size()) != 1 ||
            SSL_SESSION_set_cipher(owned.get(), cipher) != 1 || SSL_SESSION_set_protocol_version(owned.get(), TLS1_3_VERSION) != 1 ||
            SSL_SESSION_set_max_early_data(owned.get(), 0) != 1) return nullptr;
        return owned.release();
    }
    static int psk13(SSL* ssl, const unsigned char* identity, std::size_t size, SSL_SESSION** session) noexcept {
        *session = nullptr;
        try {
            auto* self = state(ssl);
            if (!self || !self->selection.context->psk_) return 0;
            auto result = self->lookup(psk_tls_version::tls13, {reinterpret_cast<const std::byte*>(identity), size}, 512);
            if (result.status != psk_lookup_status::accepted) return 0;
            *session = external_session(ssl, result.key);
            return *session != nullptr;
        } catch (...) { return 0; }
    }
    void apply_profile() {
        const bool psk = static_cast<bool>(selection.context->psk_);
        SSL_set_psk_server_callback(ssl, psk ? psk12 : nullptr);
        SSL_set_psk_find_session_callback(ssl, psk ? psk13 : nullptr);
        if (SSL_set_cipher_list(ssl, psk ? "PSK-AES128-GCM-SHA256" : "DEFAULT") != 1 ||
            SSL_set_ciphersuites(ssl, psk ? "TLS_AES_128_GCM_SHA256" : "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256") != 1) {
            throw std::runtime_error("TLS profile unavailable");
        }
        SSL_set_max_early_data(ssl, 0);
        if (psk) {
            SSL_set_options(ssl, SSL_OP_NO_TICKET | SSL_OP_NO_RENEGOTIATION);
            SSL_set_num_tickets(ssl, 0);
        }
    }
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
    static bool hello_acme_offer(SSL* ssl) {
        const unsigned char* data = nullptr;
        std::size_t size = 0;
        if (!SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_application_layer_protocol_negotiation, &data, &size)) return false;
        if (size < 3 || u16(data) != size - 2) throw std::invalid_argument("TLS ALPN invalid");
        std::size_t pos = 2;
        while (pos < size) {
            const auto length = data[pos++];
            if (!length || length > size - pos) throw std::invalid_argument("TLS ALPN invalid");
            pos += length;
        }
        return size == 13 && data[2] == 10 && std::memcmp(data + 3, "acme-tls/1", 10) == 0;
    }
    bool select_acme(const std::string& name) {
        if (!selection.snapshot || !sole_acme_offer || name.empty() ||
            handshake_context.transport != tls_transport::tcp || handshake_context.local_port != 443) return false;
        auto challenge = selection.snapshot->select_acme(name);
        if (!challenge) return false;
        selection = std::move(*challenge);
        acme = true;
        challenge_name = name;
        accepted_name = true;
        return true;
    }
    void apply_acme_isolation() {
        std::array<unsigned char, 32> id{};
        if (RAND_bytes(id.data(), id.size()) != 1 || SSL_set_session_id_context(ssl, id.data(), id.size()) != 1 ||
            SSL_set_num_tickets(ssl, 0) != 1) throw std::runtime_error("TLS challenge unavailable");
        SSL_set_options(ssl, SSL_OP_NO_TICKET | SSL_OP_NO_RENEGOTIATION);
        SSL_set_not_resumable_session_callback(ssl, [](SSL*, int) { return 1; });
    }
    void install_selected_context() {
        if (!SSL_set_SSL_CTX(ssl, static_cast<SSL_CTX*>(selection.context->native_.get()))) {
            throw std::runtime_error("TLS selection unavailable");
        }
        apply_verification();
        apply_profile();
        if (acme) {
            apply_acme_isolation();
            return;
        }
        // SSL_set_SSL_CTX does not replace the initial ticket/cache owner.
        // Bind lookup to the selected immutable host before resumption runs.
        const auto& id = selection.context->session_namespace_;
        if (SSL_set_session_id_context(ssl, id.data(), id.size()) != 1) {
            throw std::runtime_error("TLS selection unavailable");
        }
    }
    void apply_verification() {
        auto* ctx = static_cast<SSL_CTX*>(selection.context->native_.get());
        SSL_set_verify(ssl, SSL_CTX_get_verify_mode(ctx), nullptr);
        SSL_set_verify_depth(ssl, SSL_CTX_get_verify_depth(ctx));
        if (SSL_set1_verify_cert_store(ssl, SSL_CTX_get_cert_store(ctx)) != 1 ||
            SSL_set1_param(ssl, SSL_CTX_get0_param(ctx)) != 1) {
            throw std::runtime_error("TLS verification unavailable");
        }
    }
    int select_hello() {
        const auto name = hello_name(ssl);
        sole_acme_offer = hello_acme_offer(ssl);
        // A HelloRetryRequest continues the same already-selected handshake.
        if (acme) {
            if (name != challenge_name || !sole_acme_offer) throw std::invalid_argument("TLS challenge changed");
            return SSL_CLIENT_HELLO_SUCCESS;
        }
        if (select_acme(name)) {
            install_selected_context();
            return SSL_CLIENT_HELLO_SUCCESS;
        }
        if (!selection.snapshot) return SSL_CLIENT_HELLO_SUCCESS;
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
        install_selected_context();
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
        if (sole_acme_offer) return acme;
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
    static int acme_alpn(const unsigned char** out, unsigned char* length, const unsigned char* input, unsigned size) {
        if (size != 11 || input[0] != 10 || std::memcmp(input + 1, "acme-tls/1", 10) != 0) return SSL_TLSEXT_ERR_ALERT_FATAL;
        *out = input + 1;
        *length = 10;
        return SSL_TLSEXT_ERR_OK;
    }
    static int alpn(SSL* ssl, const unsigned char** out, unsigned char* length, const unsigned char* input, unsigned size, void*) noexcept {
        try {
            const auto* self = state(ssl);
            if (!self) return SSL_TLSEXT_ERR_ALERT_FATAL;
            if (self->acme) return acme_alpn(out, length, input, size);
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
                return {progress::failed, 0, psk_failure};
        }
    }
};
void tls_context::configure_server() {
    auto* ctx = static_cast<SSL_CTX*>(native_.get());
    if (RAND_bytes(session_namespace_.data(), session_namespace_.size()) != 1 ||
        SSL_CTX_set_session_id_context(ctx, session_namespace_.data(), session_namespace_.size()) != 1) {
        throw std::invalid_argument("TLS credentials invalid");
    }
    SSL_CTX_set_psk_server_callback(ctx, psk_ ? tls_session::impl::psk12 : nullptr);
    SSL_CTX_set_psk_find_session_callback(ctx, psk_ ? tls_session::impl::psk13 : nullptr);
    SSL_CTX_set_alpn_select_cb(ctx, tls_session::impl::alpn, nullptr);
    SSL_CTX_set_client_hello_cb(ctx, tls_session::impl::client_hello, nullptr);
    SSL_CTX_set_tlsext_servername_callback(ctx, tls_session::impl::server_name);
}
tls_session::tls_session(std::shared_ptr<const tls_context> context, bool server, tls_handshake_context handshake)
    : tls_session(tls_credentials_selection{nullptr, std::move(context)}, server, handshake) {
}
tls_session::tls_session(tls_credentials_selection selection, bool server, tls_handshake_context handshake) : impl_(std::make_unique<impl>()) {
    if (!selection.context || !selection.context->native_) {
        throw std::invalid_argument("TLS context missing");
    }
    impl_->handshake_context = handshake;
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
        impl_->server_side = true;
        impl_->apply_verification();
        impl_->apply_profile();
        SSL_set_accept_state(impl_->ssl);
    } else {
        SSL_set_connect_state(impl_->ssl);
    }
}
tls_session::~tls_session() = default;
std::shared_ptr<tls_psk_runtime> tls_session::handshake_runtime() const {
    if (impl_->selection.context->psk_) return impl_->selection.context->psk_->runtime;
    if (impl_->selection.snapshot) {
        for (std::size_t i = 0; i < impl_->selection.snapshot->hosts().size(); ++i) {
            const auto context = impl_->selection.snapshot->select(i).context;
            if (context->psk_) return context->psk_->runtime;
        }
    }
    return nullptr;
}
void tls_session::handshake_limits(std::chrono::steady_clock::time_point deadline, std::stop_token cancellation) {
    impl_->deadline = deadline;
    impl_->cancellation = cancellation;
}
std::shared_ptr<const server::tls_peer_metadata> tls_session::peer_metadata() const { return impl_->peer; }
tls_session::result tls_session::handshake() {
    ERR_clear_error();
    if (impl_->metadata_failed) return {progress::failed};
    const auto result = impl_->classify(SSL_do_handshake(impl_->ssl));
    if (result.state == progress::complete && !impl_->peer) {
        try {
            impl_->peer = copy_peer(impl_->ssl, impl_->server_side);
        } catch (...) {
            impl_->metadata_failed = true;
            ERR_clear_error();
            return {progress::failed};
        }
    }
    if (result.state == progress::complete || result.state == progress::failed || result.state == progress::eof) impl_->attempt.reset();
    return result;
}
tls_session::result tls_session::read(std::span<std::byte> buffer) {
    if (impl_->acme) return {progress::failed};
    std::size_t bytes = 0;
    ERR_clear_error();
    const int rc = SSL_read_ex(impl_->ssl, buffer.data(), buffer.size(), &bytes);
    return impl_->classify(rc, bytes);
}
tls_session::result tls_session::write(std::span<const std::byte> buffer) {
    if (impl_->acme) return {progress::failed};
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
