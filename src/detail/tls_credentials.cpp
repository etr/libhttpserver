/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/tls_credentials.hpp>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

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
void valid_profile(const tls_host_credentials& host) {
    require(host.profile == server::tls_profile::certificates || host.profile == server::tls_profile::mutual_tls);
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
tls_credentials_selection tls_credentials_snapshot::select(std::size_t host) const {
    return {shared_from_this(), contexts_.at(host)};
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
            candidate->contexts_.push_back(tls_context::server_pem(host.certificate_chain_pem, host.private_key_pem, host.trust_roots_pem, mode));
            candidate->hosts_.push_back({std::move(name), host.alpn, std::move(wire), host.profile, mode});
        }
    } catch (const std::invalid_argument&) {
        return {http::outcome_code::invalid_argument, "TLS credentials invalid"};
    }
    std::shared_ptr<const tls_credentials_snapshot> retired;
    {
        std::lock_guard lock(publication_);
        if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
            return {http::outcome_code::limit_exceeded, "TLS generation limit exceeded"};
        }
        candidate->generation_ = ++generation_;
        retired = std::atomic_exchange_explicit(&active_, std::shared_ptr<const tls_credentials_snapshot>(std::move(candidate)), std::memory_order_acq_rel);
    }
    return {};
}
}  // namespace httpserver::detail
