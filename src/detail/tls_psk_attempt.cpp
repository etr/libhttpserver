/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/tls_psk_attempt.hpp>
#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>
#include <httpserver/detail/tls_psk_runtime.hpp>
namespace httpserver::detail {
struct tls_psk_attempt::impl {
    struct entry {
        psk_tls_version version;
        std::vector<std::byte> identity;
        psk_lookup_result result;
    };
    tls_psk_config config;
    psk_handshake_context context;
    std::mutex mutex;
    std::vector<entry> cache;
    impl(tls_psk_config policy, psk_handshake_context limits) : config(std::move(policy)), context(std::move(limits)) {
        cache.reserve(config.maximum_attempts);
    }
};
tls_psk_attempt::tls_psk_attempt(tls_psk_config config, psk_handshake_context context)
    : impl_(std::make_unique<impl>(std::move(config), std::move(context))) {}
tls_psk_attempt::~tls_psk_attempt() = default;
psk_lookup_result tls_psk_attempt::lookup(psk_tls_version version, std::span<const std::byte> identity, std::size_t capacity) {
    std::lock_guard lock(impl_->mutex);
    const auto& policy = impl_->config;
    if (impl_->context.cancellation.stop_requested()) return {psk_lookup_status::cancelled, {}};
    if (std::chrono::steady_clock::now() >= impl_->context.deadline) return {psk_lookup_status::timeout, {}};
    if (identity.empty() || identity.size() > policy.maximum_identity_bytes) return {};
    const auto maximum = std::min(capacity, policy.maximum_key_bytes);
    auto found = std::find_if(impl_->cache.begin(), impl_->cache.end(), [&](const auto& entry) {
        return entry.version == version && std::equal(identity.begin(), identity.end(), entry.identity.begin(), entry.identity.end());
    });
    if (found == impl_->cache.end()) {
        if (impl_->cache.size() >= policy.maximum_attempts) return {psk_lookup_status::limit_exceeded, {}};
        auto context = impl_->context;
        context.version = version;
        context.maximum_key_bytes = maximum;
        auto result = policy.runtime->lookup(policy.lookup, identity, std::move(context));
        impl_->cache.push_back({version, {identity.begin(), identity.end()}, std::move(result)});
        found = std::prev(impl_->cache.end());
    }
    if (found->result.status != psk_lookup_status::accepted) return {found->result.status, {}};
    if (found->result.key.bytes().size() > maximum) return {};
    return {psk_lookup_status::accepted, secure_bytes(found->result.key.bytes())};
}
}  // namespace httpserver::detail
