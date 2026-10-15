/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/tls_credentials_access.hpp>
#include <httpserver/detail/tls_credentials_state.hpp>
#include <memory>
namespace httpserver::detail {
http::outcome tls_credentials_access::bind(const server::tls_credentials& credentials, tls_credentials_binding policy, std::shared_ptr<const tls_credentials_binding>& lease) {
#ifdef NATIVE_V3_TLS
    return credentials.impl_->registry.bind(policy, lease);
#else
    (void)credentials;
    (void)policy;
    (void)lease;
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
