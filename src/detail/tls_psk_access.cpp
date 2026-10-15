/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/tls_credentials_access.hpp>
#include <httpserver/detail/tls_psk_state.hpp>
#include <utility>
namespace httpserver::detail {
#ifdef NATIVE_V3_TLS
thread_local const httpserver::server::tls_psk_runtime* current_psk_lookup = nullptr;
struct lookup_scope {
    const httpserver::server::tls_psk_runtime* previous;
    explicit lookup_scope(const httpserver::server::tls_psk_runtime* runtime) : previous(std::exchange(current_psk_lookup, runtime)) {}
    ~lookup_scope() { current_psk_lookup = previous; }
};
#endif
}  // namespace httpserver::detail
#ifdef NATIVE_V3_TLS
namespace httpserver::detail {
tls_psk_config tls_credentials_access::psk(const server::tls_psk_config& input) {
    tls_psk_config result;
    result.maximum_identity_bytes = input.maximum_identity_bytes;
    result.maximum_key_bytes = input.maximum_key_bytes;
    result.maximum_attempts = input.maximum_attempts;
    if (input.runtime) result.runtime = input.runtime->impl_->runtime;
    if (input.lookup) {
        result.lookup = [lookup = input.lookup, runtime = input.runtime](auto identity, const psk_handshake_context& context) {
            lookup_scope scope(runtime.get());
            server::tls_psk_context value;
            value.version = context.version == psk_tls_version::tls12 ? server::tls_psk_version::tls12 : server::tls_psk_version::tls13;
            value.credential_generation = context.credential_generation;
            value.selected_host = context.selected_host;
            value.deadline = context.deadline;
            value.cancellation = context.cancellation;
            value.maximum_key_bytes = context.maximum_key_bytes;
            auto found = lookup(identity, value);
            psk_lookup_result result;
            switch (found.status) {
                case server::tls_psk_status::accepted:
                    result.status = psk_lookup_status::accepted;
                    break;
                case server::tls_psk_status::rejected:
                    result.status = psk_lookup_status::rejected;
                    break;
                case server::tls_psk_status::provider_failure:
                    result.status = psk_lookup_status::provider_failure;
                    break;
                case server::tls_psk_status::timeout:
                    result.status = psk_lookup_status::timeout;
                    break;
                case server::tls_psk_status::cancelled:
                    result.status = psk_lookup_status::cancelled;
                    break;
                case server::tls_psk_status::limit_exceeded:
                    result.status = psk_lookup_status::limit_exceeded;
                    break;
                default:
                    result.status = psk_lookup_status::provider_failure;
            }
            if (found.key.impl_) result.key = std::move(found.key.impl_->value);
            return result;
        };
    }
    return result;
}
}  // namespace httpserver::detail
#endif
