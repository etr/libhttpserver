/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <utility>
#include <stdexcept>
#include <httpserver/detail/quic_storage.hpp>
namespace httpserver::detail {
struct quic_storage_state {
    server::reservation envelope;
    server::resource_budget data, critical, ancestors;
};
quic_storage_pool::quic_storage_pool(std::size_t data_bytes, std::size_t critical_bytes, server::resource_budget ancestors) {
    const auto maximum = server::max_capacity(server::resource::quic_reassembly_bytes);
    if (data_bytes > maximum || critical_bytes > maximum - data_bytes || !critical_bytes)
        throw std::invalid_argument("Invalid QUIC storage envelope");
    server::reservation envelope;
    if (!ancestors.reserve(server::resource::quic_reassembly_bytes, data_bytes + critical_bytes, envelope).ok()) throw std::bad_alloc();
    auto state = std::make_shared<quic_storage_state>();
    auto data_limits = ancestors.node_->limits_;
    auto critical_limits = data_limits;
    data_limits.set(server::resource::quic_reassembly_bytes, data_bytes);
    critical_limits.set(server::resource::quic_reassembly_bytes, critical_bytes);
    // Only storage stops at the prepaid subdivision. Every semantic resource
    // still reaches the configured ancestors and competes with siblings.
    auto data_node = std::make_shared<server::detail::budget_node>(data_limits, ancestors.node_);
    auto critical_node = std::make_shared<server::detail::budget_node>(critical_limits, ancestors.node_);
    data_node->prepaid_quic_storage = critical_node->prepaid_quic_storage = true;
    state->data = server::resource_budget(std::move(data_node));
    state->critical = server::resource_budget(std::move(critical_node));
    state->ancestors = ancestors;
    state->envelope = std::move(envelope);
    state_ = std::move(state);
}
quic_storage_lease quic_storage_pool::data() const { return {state_, state_->data, state_->ancestors}; }
quic_storage_lease quic_storage_pool::critical() const { return {state_, state_->critical, state_->ancestors}; }
}  // namespace httpserver::detail
