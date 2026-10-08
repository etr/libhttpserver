/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <memory>
#include <cstdlib>
#include <new>
#include <httpserver/detail/quic_flow_control.hpp>
#include <httpserver/detail/quic_recovery.hpp>
#include "../littletest.hpp"
namespace {
int allocation_countdown = -1;
}
void* operator new(std::size_t size) {
    if (allocation_countdown == 0) {
        allocation_countdown = -1;
        throw std::bad_alloc();
    }
    if (allocation_countdown > 0) --allocation_countdown;
    if (auto* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
constexpr auto resource = hs::resource::quic_reassembly_bytes;
LT_BEGIN_SUITE(flow_storage_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(flow_storage_suite)
LT_BEGIN_AUTO_TEST(flow_storage_suite, envelope_protects_real_ancestor_capacity_and_outlives_pool_handle)
    auto root = hs::resource_budget::root({});
    hd::quic_recovery_config config;
    config.max_information = 3;
    config.critical_information = 2;
    config.max_retained_bytes = 16;
    config.critical_retained_bytes = 8;
    const auto data_capacity = 16U;
    const auto critical_capacity = hd::quic_recovery::storage_capacity(config) + 16;
    hs::budget_limits limits;
    limits.set(resource, data_capacity + critical_capacity);
    hs::resource_budget connection;
    LT_ASSERT(root.child(limits, connection).ok());
    std::unique_ptr<hd::quic_recovery> recovery;
    {
        hd::quic_storage_pool pool(data_capacity, critical_capacity, connection);
        LT_CHECK(root.in_use(resource) == data_capacity + critical_capacity);
        hs::reservation steal;
        LT_CHECK(!connection.reserve(resource, 1, steal).ok());
        recovery = std::make_unique<hd::quic_recovery>(config, pool.data(), pool.critical());
        std::array<std::byte, 8> payload{};
        LT_ASSERT(recovery->retain_stream({1, 0, payload}));
        LT_CHECK(pool.data().budget.in_use(resource) == 16);
        LT_CHECK(!pool.data().budget.reserve(resource, 1, steal).ok());
        LT_ASSERT(recovery->retain_crypto(hd::quic_pn_space::initial, 0, payload));
        LT_ASSERT(recovery->retain_flow({hd::quic_flow_kind::max_data, 16}));
        LT_CHECK(root.in_use(resource) == data_capacity + critical_capacity);
    }
    LT_CHECK(root.in_use(resource) == data_capacity + critical_capacity);
    std::array<std::byte, 64> output{};
    auto plan = recovery->prepare_packet(hd::quic_pn_space::initial, output, {});
    LT_ASSERT(plan);
    LT_ASSERT(recovery->abandon_packet(plan.token));
    recovery.reset();
    LT_CHECK(root.in_use(resource) == 0);
LT_END_AUTO_TEST(envelope_protects_real_ancestor_capacity_and_outlives_pool_handle)
LT_BEGIN_AUTO_TEST(flow_storage_suite, bounded_data_reassembly_cannot_consume_crypto_capacity)
    auto root = hs::resource_budget::root({});
    hd::quic_stream_limits limits{8, 2, 100};
    const auto capacity = hd::quic_reassembly::storage_capacity(limits);
    LT_CHECK(capacity > 16);
    {
        hd::quic_storage_pool pool(capacity, capacity, root);
        hd::quic_reassembly data(limits, pool.data());
        hd::quic_reassembly crypto(limits, pool.critical());
        std::array<std::byte, 8> bytes{};
        LT_ASSERT(data.insert(0, bytes));
        LT_CHECK(data.insert(8, bytes).code == hd::quic_stream_code::byte_limit_exceeded);
        hs::reservation rest;
        const auto remaining = capacity - pool.data().budget.in_use(resource);
        LT_ASSERT(pool.data().budget.reserve(resource, remaining, rest).ok());
        LT_ASSERT(crypto.insert(0, bytes));
        LT_CHECK(root.in_use(resource) == 2 * capacity);
    }
    LT_CHECK(root.in_use(resource) == 0);
LT_END_AUTO_TEST(bounded_data_reassembly_cannot_consume_crypto_capacity)
LT_BEGIN_AUTO_TEST(flow_storage_suite, flow_reassembly_and_recovery_compose_under_saturated_data_storage)
    hd::quic_transport_parameters parameters;
    parameters.initial_max_data = 16;
    parameters.initial_max_stream_data_bidi_remote = parameters.initial_max_stream_data_bidi_local = 8;
    parameters.initial_max_streams_bidi = 2;
    hd::quic_stream_limits receive{8, 2, 100};
    hd::quic_recovery_config config;
    config.max_information = 5;
    config.critical_information = 3;
    config.max_retained_bytes = 24;
    config.critical_retained_bytes = 8;
    auto root = hs::resource_budget::root({});
    const auto data_capacity = 2 * hd::quic_reassembly::storage_capacity(receive) + 32;
    const auto critical_capacity = hd::quic_flow_control::storage_capacity(8) + hd::quic_recovery::storage_capacity(config) +
        16 + hd::quic_reassembly::storage_capacity(receive);
    {
        hd::quic_storage_pool pool(data_capacity, critical_capacity, root);
        hd::quic_flow_control flow(hd::quic_endpoint_role::server, parameters, parameters, 8, pool.critical());
        LT_ASSERT(flow.observe_peer(4));
        hd::quic_stream_state a(0, hd::quic_endpoint_role::server, flow.ids(), receive, pool.data());
        hd::quic_stream_state b(4, hd::quic_endpoint_role::server, flow.ids(), receive, pool.data());
        std::array<std::byte, 8> payload{};
        LT_ASSERT(flow.receive(a, hd::quic_stream_frame{0, 0, payload}));
        LT_ASSERT(flow.receive(b, hd::quic_stream_frame{4, 0, payload}));
        std::array<std::byte, 8> staging{};
        LT_CHECK(b.read(staging).bytes == 8);
        LT_CHECK(!flow.pending_credit());
        LT_ASSERT(flow.consume_body(b, 4));
        LT_ASSERT(flow.receive(b, hd::quic_stream_frame{4, 8, std::span(payload).first(4)}));
        LT_CHECK(a.buffered_bytes() == 8 && b.buffered_bytes() == 4 && flow.received() == 20);
        LT_ASSERT(flow.open_local(false));
        hd::quic_recovery recovery(config, pool.data(), pool.critical());
        auto kept = recovery.retain_stream({1, 0, payload});
        LT_ASSERT(kept);
        LT_ASSERT(recovery.retain_stream({5, 0, payload}));
        hs::reservation data_saturation;
        const auto remaining = data_capacity - pool.data().budget.in_use(resource);
        LT_ASSERT(pool.data().budget.reserve(resource, remaining, data_saturation).ok());
        hd::quic_reassembly crypto(receive, pool.critical());
        LT_ASSERT(crypto.insert(0, payload));
        LT_ASSERT(recovery.retain_crypto(hd::quic_pn_space::application, 0, payload));
        LT_ASSERT(recovery.retain_flow(*flow.pending_credit()));
        LT_ASSERT(recovery.retain_reset({1, 9, 0}));
        std::array<std::byte, 64> output{};
        auto plan = recovery.prepare_packet(hd::quic_pn_space::application, output, {}, flow);
        LT_ASSERT(plan && !plan.stream && !plan.reset && !plan.flow);
        LT_ASSERT(recovery.commit_sent(plan.token, {}, 100, true, true));
        plan = recovery.prepare_packet(hd::quic_pn_space::application, output, {}, flow);
        LT_ASSERT(plan && plan.flow);
        LT_CHECK(plan.flow->limit == 20);
        LT_ASSERT(recovery.abandon_packet(plan.token));
        LT_CHECK(root.in_use(resource) == data_capacity + critical_capacity);
        LT_ASSERT(recovery.cancel_information(kept.id));
        LT_CHECK(root.in_use(resource) == data_capacity + critical_capacity);
    }
    LT_CHECK(root.in_use(resource) == 0);
LT_END_AUTO_TEST(flow_reassembly_and_recovery_compose_under_saturated_data_storage)
LT_BEGIN_AUTO_TEST(flow_storage_suite, envelope_allocation_and_ancestor_refusal_release_all_charges)
    auto root = hs::resource_budget::root({});
    allocation_countdown = 0;
    bool allocation_refused = false;
    try {
        hd::quic_storage_pool pool(8, 8, root);
    } catch (const std::bad_alloc&) {
        allocation_refused = true;
    }
    LT_CHECK(allocation_refused && root.in_use(resource) == 0);
    hs::budget_limits limits;
    limits.set(resource, 15);
    hs::resource_budget child;
    LT_ASSERT(root.child(limits, child).ok());
    bool ancestor_refused = false;
    try {
        hd::quic_storage_pool pool(8, 8, child);
    } catch (const std::bad_alloc&) {
        ancestor_refused = true;
    }
    LT_CHECK(ancestor_refused && child.in_use(resource) == 0 && root.in_use(resource) == 0);
    for (int step = 0; step < 3; ++step) {
        allocation_countdown = step;
        bool refused = false;
        try {
            hd::quic_storage_pool pool(8, 8, root);
        } catch (const std::bad_alloc&) {
            refused = true;
        }
        allocation_countdown = -1;
        LT_CHECK(refused && root.in_use(resource) == 0);
    }
LT_END_AUTO_TEST(envelope_allocation_and_ancestor_refusal_release_all_charges)
LT_BEGIN_AUTO_TEST(flow_storage_suite, scheduled_emission_is_allocation_free_and_returns_scheduler_budget)
    auto root = hs::resource_budget::root({});
    {
        hd::quic_recovery recovery({}, root);
        hd::quic_transport_parameters parameters;
        hd::quic_flow_control flow(hd::quic_endpoint_role::server, parameters, parameters, 4, root);
        std::array<std::byte, 1200> output{};
        LT_ASSERT(recovery.retain_crypto(hd::quic_pn_space::initial, 0, std::span(output).first(10)));
        auto plan = recovery.prepare_scheduled_packet(hd::quic_pn_space::initial, output, {}, {1200, 40}, flow);
        LT_ASSERT(plan);
        allocation_countdown = 0;
        bool emitted = false;
        try {
            emitted = static_cast<bool>(recovery.commit_sent(plan.token, {}, 1200, true, true));
        } catch (const std::bad_alloc&) {
            emitted = false;
        }
        allocation_countdown = -1;
        LT_CHECK(emitted);
    }
    LT_CHECK(root.in_use(resource) == 0);
LT_END_AUTO_TEST(scheduled_emission_is_allocation_free_and_returns_scheduler_budget)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
