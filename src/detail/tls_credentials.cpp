/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/tls_credentials.hpp>
#include <httpserver/detail/tls_psk_runtime.hpp>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <ctime>

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
namespace httpserver::detail {
namespace {
struct error_scope {
    error_scope() { ERR_clear_error(); }
    ~error_scope() { ERR_clear_error(); }
};
void require(bool valid) {
    if (!valid) {
        throw std::invalid_argument("TLS credentials invalid");
    }
}
bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
void trim(std::string_view& text) {
    while (!text.empty() && whitespace(text.front())) {
        text.remove_prefix(1);
    }
}
void bounded(std::string_view text) {
    require(!text.empty() && text.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
}
// PEM readers otherwise skip arbitrary junk and quietly ignore trailing input.
std::string_view block(std::string_view& text, std::string_view label) {
    trim(text);
    const std::string begin = "-----BEGIN " + std::string(label) + "-----";
    const std::string end = "-----END " + std::string(label) + "-----";
    require(text.starts_with(begin));
    const auto pos = text.find(end, begin.size());
    require(pos != std::string_view::npos);
    const auto result = text.substr(0, pos + end.size());
    text.remove_prefix(result.size());
    trim(text);
    return result;
}
using bio_ptr = std::unique_ptr<BIO, decltype(&BIO_free)>;
bio_ptr bio(std::string_view text) {
    bio_ptr result(BIO_new_mem_buf(text.data(), static_cast<int>(text.size())), BIO_free);
    require(static_cast<bool>(result));
    return result;
}
using cert_ptr = std::unique_ptr<X509, decltype(&X509_free)>;
cert_ptr certificate(std::string_view& text) {
    auto input = bio(block(text, "CERTIFICATE"));
    cert_ptr result(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
    require(static_cast<bool>(result));
    return result;
}
int no_password(char*, int, int, void*) { return 0; }
using key_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
key_ptr private_key(std::string_view text) {
    trim(text);
    std::string_view label = "PRIVATE KEY";
    if (text.starts_with("-----BEGIN RSA PRIVATE KEY-----")) {
        label = "RSA PRIVATE KEY";
    } else if (text.starts_with("-----BEGIN EC PRIVATE KEY-----")) {
        label = "EC PRIVATE KEY";
    }
    auto input = bio(block(text, label));
    require(text.empty());
    key_ptr result(PEM_read_bio_PrivateKey(input.get(), nullptr, no_password, nullptr), EVP_PKEY_free);
    require(static_cast<bool>(result));
    return result;
}
void install_chain(SSL_CTX* ctx, std::string_view text) {
    auto leaf = certificate(text);
    require(SSL_CTX_use_certificate(ctx, leaf.get()) == 1);
    while (!text.empty()) {
        auto intermediate = certificate(text);
        require(SSL_CTX_add1_chain_cert(ctx, intermediate.get()) == 1);
    }
}
void install_roots(SSL_CTX* ctx, std::string_view text) {
    while (!text.empty()) {
        auto root = certificate(text);
        require(X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), root.get()) == 1);
    }
}
bool letter_or_digit(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
void valid_label(std::string_view label) {
    require(!label.empty() && label.size() <= 63);
    require(letter_or_digit(label.front()) && letter_or_digit(label.back()));
    for (char c : label) {
        require(letter_or_digit(c) || c == '-');
    }
}
std::vector<unsigned char> encode_alpn(const std::vector<std::string>& protocols) {
    std::vector<unsigned char> wire;
    std::unordered_set<std::string> unique;
    for (const auto& token : protocols) {
        require(!token.empty() && token.size() <= 255);
        require(unique.insert(token).second);
        require(wire.size() + token.size() + 1 <= 65535);
        wire.push_back(static_cast<unsigned char>(token.size()));
        wire.insert(wire.end(), token.begin(), token.end());
    }
    return wire;
}
X509_EXTENSION* unique_extension(X509* cert, int nid) {
    const auto index = X509_get_ext_by_NID(cert, nid, -1);
    require(index >= 0 && X509_get_ext_by_NID(cert, nid, index) == -1);
    return X509_get_ext(cert, index);
}
void valid_acme_san(X509* cert, const std::string& host) {
    const auto* bytes = X509_EXTENSION_get_data(unique_extension(cert, NID_subject_alt_name));
    const auto* start = ASN1_STRING_get0_data(bytes);
    auto* cursor = start;
    const auto size = ASN1_STRING_length(bytes);
    std::unique_ptr<GENERAL_NAMES, decltype(&GENERAL_NAMES_free)> names(d2i_GENERAL_NAMES(nullptr, &cursor, size), GENERAL_NAMES_free);
    require(names && cursor == start + size && sk_GENERAL_NAME_num(names.get()) == 1);
    const auto* name = sk_GENERAL_NAME_value(names.get(), 0);
    require(name && name->type == GEN_DNS);
    const auto* value = name->d.dNSName;
    require(canonical_tls_host(std::string(reinterpret_cast<const char*>(ASN1_STRING_get0_data(value)), ASN1_STRING_length(value))) == host);
}
void valid_acme_digest(X509* cert, const std::array<std::byte, 32>& digest) {
    std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> object(OBJ_txt2obj("1.3.6.1.5.5.7.1.31", 1), ASN1_OBJECT_free);
    require(static_cast<bool>(object));
    const auto index = X509_get_ext_by_OBJ(cert, object.get(), -1);
    require(index >= 0 && X509_get_ext_by_OBJ(cert, object.get(), index) == -1);
    auto* extension = X509_get_ext(cert, index);
    require(X509_EXTENSION_get_critical(extension) == 1);
    const auto* value = X509_EXTENSION_get_data(extension);
    require(ASN1_STRING_length(value) == 34);
    const auto* bytes = ASN1_STRING_get0_data(value);
    require(bytes[0] == 4 && bytes[1] == 32 && CRYPTO_memcmp(bytes + 2, digest.data(), digest.size()) == 0);
}
void valid_acme_dates(X509* cert, std::chrono::system_clock::time_point deadline) {
    const auto* begin = X509_get0_notBefore(cert);
    const auto* end = X509_get0_notAfter(cert);
    require(ASN1_TIME_check(begin) == 1 && ASN1_TIME_check(end) == 1);
    require(X509_cmp_current_time(begin) < 0 && X509_cmp_current_time(end) > 0);
    require(deadline > std::chrono::system_clock::now());
    const auto seconds = std::chrono::ceil<std::chrono::seconds>(deadline.time_since_epoch()).count();
    std::unique_ptr<ASN1_TIME, decltype(&ASN1_TIME_free)> limit(ASN1_TIME_set(nullptr, static_cast<std::time_t>(seconds)), ASN1_TIME_free);
    require(limit && ASN1_TIME_compare(end, limit.get()) >= 0);
}
void valid_psk_bounds(const tls_psk_config& psk) {
    require(psk.maximum_identity_bytes > 0 && psk.maximum_identity_bytes <= 65535);
    require(psk.maximum_key_bytes > 0 && psk.maximum_key_bytes <= 512);
    require(psk.maximum_attempts > 0 && psk.maximum_attempts <= 65535);
}
void valid_profile(const tls_host_credentials& host) {
    if (host.profile != server::tls_profile::external_psk) {
        require(host.profile == server::tls_profile::certificates || host.profile == server::tls_profile::mutual_tls);
        require(!host.psk);
        return;
    }
    require(host.certificate_chain_pem.empty() && host.private_key_pem.empty() && host.trust_roots_pem.empty());
    require(host.psk && host.psk->lookup && host.psk->runtime && host.psk->runtime->accepting());
    valid_psk_bounds(*host.psk);
    for (const auto& protocol : host.alpn) require(protocol == "http/1.1");
}
}  // namespace
std::string canonical_tls_host(std::string host) {
    for (char& c : host) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    if (!host.empty() && host.back() == '.') host.pop_back();
    require(!host.empty() && host.size() <= 253);
    std::string_view remaining(host);
    while (true) {
        const auto end = remaining.find('.');
        valid_label(remaining.substr(0, end));
        if (end == std::string_view::npos) break;
        remaining.remove_prefix(end + 1);
    }
    return host;
}
std::shared_ptr<tls_context> tls_context::client() {
    error_scope errors;
    auto context = std::make_shared<tls_context>();
    context->native_ = {SSL_CTX_new(TLS_method()), [](void* p) { SSL_CTX_free(static_cast<SSL_CTX*>(p)); }};
    require(static_cast<bool>(context->native_));
    auto* ctx = static_cast<SSL_CTX*>(context->native_.get());
    require(SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) == 1);
    require(SSL_CTX_set_max_early_data(ctx, 0) == 1);
    return context;
}
std::shared_ptr<tls_context> tls_context::server_pem(std::string_view chain, std::string_view key, std::string_view roots, server::tls_client_certificate_mode mode) {
    error_scope errors;
    bounded(chain);
    bounded(key);
    if (!roots.empty()) bounded(roots);
    auto context = client();
    auto* ctx = static_cast<SSL_CTX*>(context->native_.get());
    install_chain(ctx, chain);
    auto parsed_key = private_key(key);
    require(SSL_CTX_use_PrivateKey(ctx, parsed_key.get()) == 1);
    require(SSL_CTX_check_private_key(ctx) == 1);
    install_roots(ctx, roots);
    int verify = SSL_VERIFY_NONE;
    switch (mode) {
        case server::tls_client_certificate_mode::none: break;
        case server::tls_client_certificate_mode::request: verify = SSL_VERIFY_PEER; break;
        case server::tls_client_certificate_mode::require: verify = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT; break;
        default: require(false);
    }
    if (mode != server::tls_client_certificate_mode::none) require(!roots.empty());
    SSL_CTX_set_verify(ctx, verify, nullptr);
    SSL_CTX_set_post_handshake_auth(ctx, 0);
    context->configure_server();
    return context;
}
std::shared_ptr<tls_context> tls_context::server_acme(const tls_acme_challenge& input) {
    error_scope errors;
    bounded(input.certificate_pem);
    bounded(input.private_key_pem);
    auto text = std::string_view(input.certificate_pem);
    auto leaf = certificate(text);
    require(text.empty());
    valid_acme_san(leaf.get(), canonical_tls_host(input.host));
    valid_acme_digest(leaf.get(), input.key_authorization_sha256);
    valid_acme_dates(leaf.get(), input.expires_at);
    auto context = server_pem(input.certificate_pem, input.private_key_pem);
    auto* ctx = static_cast<SSL_CTX*>(context->native_.get());
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET | SSL_OP_NO_RENEGOTIATION);
    require(SSL_CTX_set_num_tickets(ctx, 0) == 1);
    return context;
}
std::shared_ptr<tls_context> tls_context::server_psk(const tls_psk_config& config) {
    auto context = client();
    auto* ctx = static_cast<SSL_CTX*>(context->native_.get());
    require(SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) == 1);
    require(SSL_CTX_set_cipher_list(ctx, "PSK-AES128-GCM-SHA256") == 1);
    require(SSL_CTX_set_ciphersuites(ctx, "TLS_AES_128_GCM_SHA256") == 1);
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET | SSL_OP_NO_RENEGOTIATION);
    require(SSL_CTX_set_num_tickets(ctx, 0) == 1);
    context->psk_ = std::make_shared<const tls_psk_config>(config);
    context->configure_server();
    return context;
}
tls_credentials_selection tls_credentials_snapshot::select(std::size_t host) const {
    return {shared_from_this(), contexts_.at(host)};
}
std::optional<tls_credentials_selection> tls_credentials_snapshot::select_acme(std::string_view host, std::chrono::system_clock::time_point now) const {
    const auto found = challenges_.find(host);
    if (found == challenges_.end() || now >= found->second.expires_at) return std::nullopt;
    return tls_credentials_selection{shared_from_this(), found->second.context};
}
http::outcome tls_credentials_registry::publish_locked(std::shared_ptr<tls_credentials_snapshot> candidate,
    std::shared_ptr<const tls_credentials_snapshot>& retired) {
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        return {http::outcome_code::limit_exceeded, "TLS generation limit exceeded"};
    }
    candidate->generation_ = ++generation_;
    retired = std::atomic_exchange_explicit(&active_, std::shared_ptr<const tls_credentials_snapshot>(std::move(candidate)), std::memory_order_acq_rel);
    return {};
}
http::outcome tls_credentials_registry::publish_acme(const tls_acme_challenge& input) {
    error_scope errors;
    std::shared_ptr<const tls_credentials_snapshot> retired;
    try {
        auto name = canonical_tls_host(input.host);
        auto context = tls_context::server_acme(input);
        std::lock_guard lock(publication_);
        const auto current = acquire();
        if (!current) return {http::outcome_code::invalid_state, "TLS credentials unavailable"};
        require(input.expires_at > std::chrono::system_clock::now());
        auto candidate = std::make_shared<tls_credentials_snapshot>(*current);
        candidate->challenges_.insert_or_assign(std::move(name), tls_credentials_snapshot::acme_entry{std::move(context), input.expires_at});
        return publish_locked(std::move(candidate), retired);
    } catch (const std::invalid_argument&) {
        return {http::outcome_code::invalid_argument, "TLS credentials invalid"};
    }
}
http::outcome tls_credentials_registry::remove_acme(std::string_view host) {
    error_scope errors;
    std::shared_ptr<const tls_credentials_snapshot> retired;
    try {
        const auto name = canonical_tls_host(std::string(host));
        std::lock_guard lock(publication_);
        const auto current = acquire();
        if (!current || !current->challenges_.contains(name)) return {};
        auto candidate = std::make_shared<tls_credentials_snapshot>(*current);
        candidate->challenges_.erase(name);
        return publish_locked(std::move(candidate), retired);
    } catch (const std::invalid_argument&) {
        return {http::outcome_code::invalid_argument, "TLS credentials invalid"};
    }
}
http::outcome tls_credentials_registry::replace(const tls_credentials_config& config) {
    auto candidate = std::make_shared<tls_credentials_snapshot>();
    try {
        require(!config.hosts.empty() && config.default_host < config.hosts.size());
        candidate->default_host_ = config.default_host;
        std::unordered_set<std::string> names;
        for (const auto& host : config.hosts) {
            valid_profile(host);
            if (const auto policy = server::detail::check_client_auth(host.profile, host.client_auth); !policy.ok()) {
                return {policy.code(), "TLS credentials invalid"};
            }
            const auto mode = server::detail::resolved_client_certificate_mode(host.profile, host.client_auth);
            if (mode != server::tls_client_certificate_mode::none) require(!host.trust_roots_pem.empty());
            auto name = canonical_tls_host(host.host);
            require(names.insert(name).second);
            auto wire = encode_alpn(host.alpn);
            candidate->contexts_.push_back(host.psk ? tls_context::server_psk(*host.psk) :
                tls_context::server_pem(host.certificate_chain_pem, host.private_key_pem, host.trust_roots_pem, mode));
            candidate->hosts_.push_back({std::move(name), host.alpn, std::move(wire), host.profile, mode});
        }
    } catch (const std::invalid_argument&) {
        return {http::outcome_code::invalid_argument, "TLS credentials invalid"};
    }
    std::shared_ptr<const tls_credentials_snapshot> retired;
    {
        std::lock_guard lock(publication_);
        if (const auto current = acquire()) candidate->challenges_ = current->challenges_;
        return publish_locked(std::move(candidate), retired);
    }
}
}  // namespace httpserver::detail
