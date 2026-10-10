/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <memory>
#include <stdexcept>
#include <utility>
#include "config.h"
#include <httpserver/server/tls_credentials.hpp>
#include <httpserver/detail/tls_credentials_access.hpp>
#include <httpserver/detail/tls_psk.hpp>
#ifdef NATIVE_V3_TLS
#include <httpserver/detail/tls_psk_runtime.hpp>
#endif
namespace {
#ifdef NATIVE_V3_TLS
thread_local const httpserver::server::tls_psk_runtime* current_lookup = nullptr;
struct lookup_scope {
    const httpserver::server::tls_psk_runtime* previous;
    explicit lookup_scope(const httpserver::server::tls_psk_runtime* runtime)
        : previous(std::exchange(current_lookup, runtime)) {}
    ~lookup_scope() { current_lookup = previous; }
};
#endif
}  // namespace
namespace httpserver::server {
class tls_psk_key::impl {
 public:
    explicit impl(std::span<const std::byte> bytes) : value(bytes) {}
    httpserver::detail::secure_bytes value;
};
tls_psk_key::tls_psk_key() noexcept = default;
tls_psk_key::tls_psk_key(std::span<const std::byte> bytes) : impl_(std::make_unique<impl>(bytes)) {}
tls_psk_key::~tls_psk_key() = default;
tls_psk_key::tls_psk_key(tls_psk_key&&) noexcept = default;
tls_psk_key& tls_psk_key::operator=(tls_psk_key&&) noexcept = default;
std::span<const std::byte> tls_psk_key::bytes() const noexcept { return impl_ ? impl_->value.bytes() : std::span<const std::byte>{}; }
void tls_psk_key::clear() noexcept { if (impl_) impl_->value.clear(); }
class tls_psk_runtime::impl {
 public:
#ifdef NATIVE_V3_TLS
    std::shared_ptr<httpserver::detail::tls_psk_runtime> runtime;
#endif
};
tls_psk_runtime::tls_psk_runtime() : impl_(std::make_shared<impl>()) {}
tls_psk_runtime::~tls_psk_runtime() { request_stop(); }
http::outcome tls_psk_runtime::create(tls_psk_runtime_options options, std::shared_ptr<tls_psk_runtime>& output) {
#ifdef NATIVE_V3_TLS
    if (options.lookup_workers == 0 || options.lookup_workers > 64 || options.lookup_queue > 65536) {
        return {http::outcome_code::invalid_argument, "TLS lookup runtime invalid"};
    }
    try {
        httpserver::detail::tls_psk_runtime_options limits;
        limits.lookup_workers = options.lookup_workers;
        limits.lookup_queue = options.lookup_queue;
        auto value = std::shared_ptr<tls_psk_runtime>(new tls_psk_runtime());
        value->impl_->runtime = std::make_shared<httpserver::detail::tls_psk_runtime>(limits);
        output = std::move(value);
        return {};
    } catch (const std::exception&) {
        return {http::outcome_code::limit_exceeded, "TLS lookup runtime unavailable"};
    }
#else
    (void)options; (void)output;
    return {http::outcome_code::not_supported, "TLS lookup runtime unavailable"};
#endif
}
void tls_psk_runtime::request_stop() noexcept {
#ifdef NATIVE_V3_TLS
    if (impl_->runtime) impl_->runtime->stop();
#endif
}
http::outcome tls_psk_runtime::wait_retired(std::chrono::steady_clock::time_point deadline) {
#ifdef NATIVE_V3_TLS
    if (current_lookup == this) return {http::outcome_code::would_deadlock, "TLS callback cannot wait for its own retirement"};
    if (impl_->runtime->drain(deadline)) return {};
    return {http::outcome_code::timeout, "TLS callback retirement incomplete"};
#else
    (void)deadline;
    return {http::outcome_code::not_supported, "TLS lookup runtime unavailable"};
#endif
}
}  // namespace httpserver::server
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
            case server::tls_psk_status::accepted: result.status = psk_lookup_status::accepted; break;
            case server::tls_psk_status::rejected: result.status = psk_lookup_status::rejected; break;
            case server::tls_psk_status::provider_failure: result.status = psk_lookup_status::provider_failure; break;
            case server::tls_psk_status::timeout: result.status = psk_lookup_status::timeout; break;
            case server::tls_psk_status::cancelled: result.status = psk_lookup_status::cancelled; break;
            case server::tls_psk_status::limit_exceeded: result.status = psk_lookup_status::limit_exceeded; break;
            default: result.status = psk_lookup_status::provider_failure;
        }
        if (found.key.impl_) result.key = std::move(found.key.impl_->value);
        return result;
        };
    }
    return result;
}
}  // namespace httpserver::detail
#endif
