/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <memory>
#include <utility>
#include "config.h"
#include <httpserver/server/tls_credentials.hpp>
#include <httpserver/detail/tls_credentials_access.hpp>
#ifdef NATIVE_V3_TLS
#include <httpserver/detail/tls_credentials.hpp>
#endif

namespace httpserver::server {
class tls_credentials::impl {
 public:
#ifdef NATIVE_V3_TLS
    httpserver::detail::tls_credentials_registry registry;
#endif
};
tls_credentials::tls_credentials() : impl_(std::make_shared<impl>()) {}
http::outcome tls_credentials::replace(const tls_credentials_config& config) {
#ifdef NATIVE_V3_TLS
    httpserver::detail::tls_credentials_config candidate;
    candidate.default_host = config.default_host;
    candidate.hosts.reserve(config.hosts.size());
    for (const auto& host : config.hosts) {
        for (const auto& protocol : host.alpn) {
            if (protocol != "http/1.1" && protocol != "h2") {
                return {http::outcome_code::invalid_argument, "TLS certificate store supports HTTP/1.1 and HTTP/2 ALPN"};
            }
        }
        httpserver::detail::tls_host_credentials value;
        value.host = host.host;
        value.certificate_chain_pem = host.certificate_chain_pem;
        value.private_key_pem = host.private_key_pem;
        value.alpn = host.alpn;
        value.trust_roots_pem = host.trust_roots_pem;
        value.profile = host.profile;
        value.client_auth = host.client_auth;
        if (host.psk) value.psk = httpserver::detail::tls_credentials_access::psk(*host.psk);
        candidate.hosts.push_back(std::move(value));
    }
    return impl_->registry.replace(candidate);
#else
    (void)config;
    return {http::outcome_code::not_supported, "TLS credentials unavailable"};
#endif
}
http::outcome tls_credentials::publish_acme(const tls_acme_challenge& challenge) {
#ifdef NATIVE_V3_TLS
    httpserver::detail::tls_acme_challenge value{challenge.host, challenge.certificate_pem,
        challenge.private_key_pem, challenge.key_authorization_sha256, challenge.expires_at};
    return impl_->registry.publish_acme(value);
#else
    (void)challenge;
    return {http::outcome_code::not_supported, "TLS credentials unavailable"};
#endif
}
http::outcome tls_credentials::remove_acme(std::string_view host) {
#ifdef NATIVE_V3_TLS
    return impl_->registry.remove_acme(host);
#else
    (void)host;
    return {http::outcome_code::not_supported, "TLS credentials unavailable"};
#endif
}
}  // namespace httpserver::server
namespace httpserver::detail {
http::outcome tls_credentials_access::bind(const server::tls_credentials& credentials, tls_credentials_binding policy,
                                         std::shared_ptr<const tls_credentials_binding>& lease) {
#ifdef NATIVE_V3_TLS
    return credentials.impl_->registry.bind(policy, lease);
#else
    (void)credentials; (void)policy; (void)lease;
    return {http::outcome_code::not_supported, "TLS credentials unavailable"};
#endif
}
std::shared_ptr<const tls_credentials_snapshot> tls_credentials_access::acquire(const server::tls_credentials& credentials) {
#ifdef NATIVE_V3_TLS
    return credentials.impl_->registry.acquire();
#else
    (void)credentials;
    return {};
#endif
}
}  // namespace httpserver::detail
