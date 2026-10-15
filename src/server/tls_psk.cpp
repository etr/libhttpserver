/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <httpserver/detail/tls_credentials_access.hpp>
#include <httpserver/detail/tls_psk_state.hpp>
#include <httpserver/server/tls_credentials.hpp>
#include <memory>
#include <stdexcept>
#include <utility>

#include "config.h"
#ifdef NATIVE_V3_TLS
#include <httpserver/detail/tls_psk_runtime.hpp>
#endif
namespace httpserver::server {
tls_psk_key::tls_psk_key() noexcept = default;
tls_psk_key::tls_psk_key(std::span<const std::byte> bytes) : impl_(std::make_unique<impl>(bytes)) {}
tls_psk_key::~tls_psk_key() = default;
tls_psk_key::tls_psk_key(tls_psk_key&&) noexcept = default;
tls_psk_key& tls_psk_key::operator=(tls_psk_key&&) noexcept = default;
std::span<const std::byte> tls_psk_key::bytes() const noexcept { return impl_ ? impl_->value.bytes() : std::span<const std::byte>{}; }
void tls_psk_key::clear() noexcept {
    if (impl_) impl_->value.clear();
}
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
    (void)options;
    (void)output;
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
    if (httpserver::detail::current_psk_lookup == this) return {http::outcome_code::would_deadlock, "TLS callback cannot wait for its own retirement"};
    if (impl_->runtime->drain(deadline)) return {};
    return {http::outcome_code::timeout, "TLS callback retirement incomplete"};
#else
    (void)deadline;
    return {http::outcome_code::not_supported, "TLS lookup runtime unavailable"};
#endif
}
}  // namespace httpserver::server
