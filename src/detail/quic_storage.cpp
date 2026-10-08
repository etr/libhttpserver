/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <utility>
#include <stdexcept>
#include <httpserver/detail/quic_storage.hpp>
namespace httpserver::detail {
struct quic_storage_state {
    server::reservation envelope;
    server::resource_budget data, critical;
};
quic_storage_pool::quic_storage_pool(std::size_t data_bytes, std::size_t critical_bytes, server::resource_budget ancestors) {
    const auto maximum = server::max_capacity(server::resource::quic_reassembly_bytes);
    if (data_bytes > maximum || critical_bytes > maximum - data_bytes || !critical_bytes)
        throw std::invalid_argument("Invalid QUIC storage envelope");
    server::reservation envelope;
    if (!ancestors.reserve(server::resource::quic_reassembly_bytes, data_bytes + critical_bytes, envelope).ok()) throw std::bad_alloc();
    auto state = std::make_shared<quic_storage_state>();
    server::budget_limits data_limits, critical_limits;
    data_limits.set(server::resource::quic_reassembly_bytes, data_bytes);
    critical_limits.set(server::resource::quic_reassembly_bytes, critical_bytes);
    // Independent internal roots subdivide already charged storage, so actual
    // ancestors see exactly one envelope charge and data cannot spend its reserve.
    state->data = server::resource_budget::root(data_limits);
    state->critical = server::resource_budget::root(critical_limits);
    state->envelope = std::move(envelope);
    state_ = std::move(state);
}
quic_storage_lease quic_storage_pool::data() const { return {state_, state_->data}; }
quic_storage_lease quic_storage_pool::critical() const { return {state_, state_->critical}; }
}  // namespace httpserver::detail
