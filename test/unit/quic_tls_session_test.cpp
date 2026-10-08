/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/ssl.h>
#include <array>
#include <cstdlib>
#include <new>
#include <memory>
#include <stdexcept>
#include <vector>
#include <httpserver/detail/quic_key_state.hpp>
#include "./quic_codec_test_support.hpp"
#include "../../src/detail/quic_tls_callbacks.hpp"
namespace {
bool refuse_allocation = false;
}
void* operator new(std::size_t size) {
    if (refuse_allocation) {
        refuse_allocation = false;
        throw std::bad_alloc();
    }
    if (auto* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
LT_BEGIN_SUITE(quic_tls_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(quic_tls_suite)
LT_BEGIN_AUTO_TEST(quic_tls_suite, ordered_owned_crypto_and_stable_receive_lease)
    auto budget = httpserver::server::resource_budget::root({});
    hd::quic_key_state keys;
    hd::quic_tls_config config;
    config.output_capacity = 4;
    config.receive_lease_capacity = 4;
    hd::quic_tls_callbacks state(config, budget, keys);
    for (auto level : {hd::quic_crypto_level::initial, hd::quic_crypto_level::handshake, hd::quic_crypto_level::application}) {
        const auto base = 10U * static_cast<unsigned>(level);
        auto tail = octets({base + 3, base + 4}), head = octets({base + 1, base + 2});
        LT_ASSERT(state.receive(level, 2, tail));
        tail.assign(2, std::byte{99});
        LT_ASSERT(state.receive(level, 0, head));
        head.assign(2, std::byte{99});
        LT_ASSERT(state.receive(level, 1, octets({base + 2, base + 3})));
    }
    const unsigned char* lease = nullptr;
    std::size_t size = 0;
    LT_ASSERT(state.recv(&lease, &size));
    LT_CHECK(size == 4 && lease[0] == 1 && lease[3] == 4);
    auto* address = lease;
    LT_ASSERT(state.receive(hd::quic_crypto_level::initial, 4, octets({5, 6})));
    LT_CHECK(address[0] == 1 && address[3] == 4);
    LT_ASSERT(state.release(4));
    LT_ASSERT(state.recv(&lease, &size));
    LT_CHECK(size == 2 && lease[0] == 5 && lease[1] == 6);
    LT_ASSERT(state.release(2));
    LT_ASSERT(state.recv(&lease, &size));
    LT_CHECK(size == 0);
    const std::vector<std::byte> secret(32, std::byte{7});
    LT_ASSERT(state.secret(OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE, 0, 0x1301, secret));
    LT_ASSERT(state.recv(&lease, &size));
    LT_CHECK(size == 4 && lease[0] == 11 && lease[3] == 14);
    LT_ASSERT(state.release(size));
    LT_ASSERT(state.secret(OSSL_RECORD_PROTECTION_LEVEL_APPLICATION, 0, 0x1301, secret));
    LT_ASSERT(state.recv(&lease, &size));
    LT_CHECK(size == 4 && lease[0] == 21 && lease[3] == 24);
    LT_ASSERT(state.release(size));
LT_END_AUTO_TEST(ordered_owned_crypto_and_stable_receive_lease)
LT_BEGIN_AUTO_TEST(quic_tls_suite, output_retries_keep_offset_addressable_owned_prefixes)
    auto budget = httpserver::server::resource_budget::root({});
    hd::quic_key_state keys;
    hd::quic_tls_config config;
    config.output_capacity = 4;
    hd::quic_tls_callbacks state(config, budget, keys);
    auto bytes = octets({1, 2, 3, 4, 5, 6});
    std::size_t accepted = 99;
    LT_ASSERT(state.send(bytes, &accepted));
    LT_CHECK(accepted == 4);
    bytes[0] = std::byte{99};
    std::array<std::byte, 8> out{};
    LT_CHECK(state.copy_output(hd::quic_crypto_level::initial, 0, out).bytes == 4);
    LT_CHECK(out[0] == std::byte{1} && out[3] == std::byte{4});
    LT_CHECK(state.copy_output(hd::quic_crypto_level::handshake, 0, out).bytes == 0);
    LT_ASSERT(state.send(std::span(bytes).subspan(4), &accepted));
    LT_CHECK(accepted == 0);
    LT_CHECK(!state.retire_output_prefix(hd::quic_crypto_level::initial, 5));
    LT_ASSERT(state.retire_output_prefix(hd::quic_crypto_level::initial, 2));
    LT_ASSERT(state.send(std::span(bytes).subspan(4), &accepted));
    LT_CHECK(accepted == 2);
    LT_CHECK(state.copy_output(hd::quic_crypto_level::initial, 2, out).bytes == 4);
    LT_CHECK(out[0] == std::byte{3} && out[3] == std::byte{6});
    LT_CHECK(!state.copy_output(hd::quic_crypto_level::initial, 1, out));
    LT_CHECK(!state.copy_output(hd::quic_crypto_level::initial, hd::k_quic_max_integer + 1, out));
    LT_ASSERT(state.retire_output_prefix(hd::quic_crypto_level::initial, 6));
    const std::vector<std::byte> secret(32, std::byte{7});
    LT_ASSERT(state.secret(OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE, 1, 0x1301, secret));
    LT_ASSERT(state.send(octets({7, 8}), &accepted));
    LT_CHECK(accepted == 2 && state.copy_output(hd::quic_crypto_level::handshake, 0, out).bytes == 2);
    LT_CHECK(out[0] == std::byte{7} && out[1] == std::byte{8});
    LT_ASSERT(state.secret(OSSL_RECORD_PROTECTION_LEVEL_APPLICATION, 1, 0x1301, secret));
    LT_ASSERT(state.send(octets({9}), &accepted));
    LT_CHECK(accepted == 1 && state.copy_output(hd::quic_crypto_level::application, 0, out).bytes == 1);
    LT_CHECK(out[0] == std::byte{9});
LT_END_AUTO_TEST(output_retries_keep_offset_addressable_owned_prefixes)
LT_BEGIN_AUTO_TEST(quic_tls_suite, secret_levels_directions_and_suites_install_transactionally)
    for (unsigned suite : {0x1301, 0x1302, 0x1303}) {
        auto budget = httpserver::server::resource_budget::root({});
        hd::quic_key_state keys;
        LT_ASSERT(keys.install_initial(hd::quic_endpoint_role::server, octets({1, 2})) == hd::quic_crypto_code::ok);
        const auto* initial = keys.keys(hd::quic_key_level::initial, hd::quic_key_direction::read);
        auto original = std::vector<std::byte>(initial->secret.bytes().begin(), initial->secret.bytes().end());
        hd::quic_tls_callbacks state({}, budget, keys);
        auto secret = std::vector<std::byte>(suite == 0x1302 ? 48 : 32, std::byte{7});
        for (unsigned level : {OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE, OSSL_RECORD_PROTECTION_LEVEL_APPLICATION}) {
            for (int direction : {0, 1}) {
                LT_ASSERT(state.secret(level, direction, suite, secret));
                const auto key_level = level == OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE ? hd::quic_key_level::handshake : hd::quic_key_level::application;
                auto* installed = keys.keys(key_level, direction ? hd::quic_key_direction::write : hd::quic_key_direction::read);
                LT_ASSERT(installed);
                LT_CHECK(std::equal(secret.begin(), secret.end(), installed->secret.bytes().begin()));
            }
        }
        LT_CHECK(std::equal(original.begin(), original.end(), initial->secret.bytes().begin()));
    }
    for (unsigned failure = 0; failure < 7; ++failure) {
        hd::quic_key_state keys;
        hd::quic_tls_callbacks state({}, httpserver::server::resource_budget::root({}), keys);
        const std::vector<std::byte> secret(32, std::byte{9});
        unsigned level = OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE, suite = 0x1301;
        int direction = 0;
        std::size_t length = 32;
        if (failure == 0) level = OSSL_RECORD_PROTECTION_LEVEL_EARLY;
        if (failure == 1) level = 999;
        if (failure == 2) direction = 2;
        if (failure == 3) suite = 0xffff;
        if (failure == 4) length = 31;
        if (failure == 5) level = OSSL_RECORD_PROTECTION_LEVEL_APPLICATION;
        if (failure == 6) LT_ASSERT(state.secret(level, direction, suite, secret));
        LT_CHECK(!state.secret(level, direction, suite, std::span(secret).first(length)));
        LT_CHECK(state.failure().code == hd::quic_tls_code::secret_error);
        std::size_t sent = 99;
        LT_CHECK(!state.send(secret, &sent) && sent == 0);
    }
LT_END_AUTO_TEST(secret_levels_directions_and_suites_install_transactionally)
LT_BEGIN_AUTO_TEST(quic_tls_suite, parameters_are_owned_validated_and_budgeted)
    auto budget = httpserver::server::resource_budget::root({});
    hd::quic_key_state keys;
    {
        auto local = octets({15, 1, 8}), expected = octets({7});
        hd::quic_tls_config config;
        config.local_parameters = local;
        config.peer_cids.initial_source = expected;
        config.maximum_peer_parameters = 16;
        hd::quic_tls_callbacks state(config, budget, keys);
        local[2] = std::byte{99};
        expected[0] = std::byte{99};
        LT_CHECK(state.local_parameters()[2] == std::byte{8});
        auto peer = octets({15, 1, 7});
        LT_ASSERT(state.parameters(peer));
        peer[2] = std::byte{99};
        LT_CHECK(state.peer_parameters()[2] == std::byte{7});
        LT_CHECK(budget.in_use(httpserver::server::resource::quic_reassembly_bytes) > 0);
    }
    LT_CHECK(budget.in_use(httpserver::server::resource::quic_reassembly_bytes) == 0);
    for (const auto& bytes : {octets({15, 1, 8}), octets({15, 1, 7, 15, 1, 7}), octets({0, 0}), octets({15, 2, 7})}) {
        hd::quic_tls_config config;
        auto expected = octets({7});
        config.peer_cids.initial_source = expected;
        hd::quic_tls_callbacks state(config, budget, keys);
        LT_CHECK(!state.parameters(bytes));
        LT_CHECK(state.peer_parameters().empty());
        LT_CHECK(state.failure().code == hd::quic_tls_code::transport_parameter_error);
    }
LT_END_AUTO_TEST(parameters_are_owned_validated_and_budgeted)
LT_BEGIN_AUTO_TEST(quic_tls_suite, receive_leases_survive_direction_changes_and_dispatch_is_complete)
    auto budget = httpserver::server::resource_budget::root({});
    hd::quic_key_state keys;
    hd::quic_tls_callbacks state({}, budget, keys);
    const auto* table = hd::quic_tls_callbacks::dispatch();
    for (int id = 2001; id <= 2006; ++id) LT_CHECK(table[id - 2001].function_id == id && table[id - 2001].function);
    LT_CHECK(table[6].function_id == 0 && table[6].function == nullptr);
    const unsigned char* data = nullptr;
    std::size_t size = 0;
    LT_ASSERT(state.receive(hd::quic_crypto_level::initial, 2, octets({3, 4})));
    LT_ASSERT(state.recv(&data, &size));
    LT_CHECK(size == 0);
    LT_ASSERT(state.receive(hd::quic_crypto_level::initial, 0, octets({1, 2})));
    LT_ASSERT(state.receive(hd::quic_crypto_level::handshake, 0, octets({5, 6})));
    LT_ASSERT(state.recv(&data, &size));
    const auto* retained = data;
    const std::vector<std::byte> secret(32, std::byte{7});
    LT_ASSERT(state.secret(OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE, 0, 0x1301, secret));
    LT_ASSERT(state.receive(hd::quic_crypto_level::handshake, 2, octets({7, 8})));
    LT_CHECK(retained[0] == 1 && retained[3] == 4);
    LT_ASSERT(state.release(size));
    LT_ASSERT(state.recv(&data, &size));
    LT_CHECK(size == 4 && data[0] == 5 && data[3] == 8);
    // Destruction deliberately leaves a provider lease outstanding.
LT_END_AUTO_TEST(receive_leases_survive_direction_changes_and_dispatch_is_complete)
LT_BEGIN_AUTO_TEST(quic_tls_suite, storage_refusal_allocation_failure_and_input_limits_are_transactional)
    auto budget = httpserver::server::resource_budget::root({});
    hd::quic_key_state keys;
    hd::quic_tls_config config;
    config.input_limits = {4, 2, 8};
    config.output_capacity = 4;
    config.receive_lease_capacity = 4;
    config.maximum_peer_parameters = 16;
    const auto bytes = octets({1, 2, 3, 4});
    {
        hd::quic_tls_callbacks state(config, budget, keys);
        const auto before = budget.in_use(httpserver::server::resource::quic_reassembly_bytes);
        refuse_allocation = true;
        LT_CHECK(state.receive(hd::quic_crypto_level::initial, 0, bytes).code == hd::quic_tls_code::no_memory);
        LT_CHECK(budget.in_use(httpserver::server::resource::quic_reassembly_bytes) == before);
        httpserver::server::reservation guard;
        LT_ASSERT(budget.reserve(httpserver::server::resource::quic_reassembly_bytes,
                                budget.capacity(httpserver::server::resource::quic_reassembly_bytes) - before, guard).ok());
        LT_CHECK(state.receive(hd::quic_crypto_level::initial, 0, bytes).code == hd::quic_tls_code::no_memory);
        guard.release();
        LT_ASSERT(state.receive(hd::quic_crypto_level::initial, 0, bytes));
        LT_CHECK(!state.receive(hd::quic_crypto_level::initial, 5, octets({5})));
        LT_CHECK(!state.receive(hd::quic_crypto_level::initial, hd::k_quic_max_integer, bytes));
        LT_CHECK(!state.receive(static_cast<hd::quic_crypto_level>(3), 0, bytes));
        const unsigned char* data = nullptr;
        std::size_t size = 0;
        LT_ASSERT(state.recv(&data, &size));
        LT_CHECK(size == 4 && data[0] == 1 && data[3] == 4);
        LT_ASSERT(state.release(4));
    }
    LT_CHECK(budget.in_use(httpserver::server::resource::quic_reassembly_bytes) == 0);
    refuse_allocation = true;
    bool failed = false;
    try {
        hd::quic_tls_callbacks state(config, budget, keys);
    } catch (const std::bad_alloc&) {
        failed = true;
    }
    LT_CHECK(failed && budget.in_use(httpserver::server::resource::quic_reassembly_bytes) == 0);
LT_END_AUTO_TEST(storage_refusal_allocation_failure_and_input_limits_are_transactional)
LT_BEGIN_AUTO_TEST(quic_tls_suite, callback_failures_are_terminal_and_zero_capacity_retries)
    auto budget = httpserver::server::resource_budget::root({});
    const std::vector<std::byte> secret(32, std::byte{7});
    for (unsigned failure = 0; failure < 4; ++failure) {
        hd::quic_key_state keys;
        hd::quic_tls_callbacks state({}, budget, keys);
        const unsigned char* data = nullptr;
        std::size_t size = 0;
        LT_ASSERT(state.receive(hd::quic_crypto_level::initial, 0, secret));
        LT_ASSERT(state.recv(&data, &size));
        if (failure == 0) LT_CHECK(!state.release(size - 1));
        if (failure == 1) LT_CHECK(!state.recv(&data, &size));
        if (failure == 2) LT_CHECK(!state.alert(42));
        if (failure == 3) {
            refuse_allocation = true;
            LT_CHECK(!state.secret(OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE, 0, 0x1301, secret));
            LT_CHECK(!keys.keys(hd::quic_key_level::handshake, hd::quic_key_direction::read));
        }
        LT_CHECK(state.failure().code != hd::quic_tls_code::ok);
        LT_CHECK(!state.receive(hd::quic_crypto_level::initial, 32, secret));
        LT_CHECK(!state.send(secret, &size) && size == 0);
    }
    hd::quic_key_state keys;
    hd::quic_tls_config config;
    config.output_capacity = 0;
    hd::quic_tls_callbacks zero(config, budget, keys);
    std::size_t sent = 999;
    LT_ASSERT(zero.send(secret, &sent));
    LT_CHECK(sent == 0 && zero.failure().code == hd::quic_tls_code::ok);
    LT_ASSERT(zero.secret(OSSL_RECORD_PROTECTION_LEVEL_NONE, 0, 0, {}));
    LT_CHECK(!keys.keys(hd::quic_key_level::initial, hd::quic_key_direction::read));
    config.maximum_peer_parameters = 2;
    hd::quic_tls_callbacks limited(config, budget, keys);
    LT_CHECK(!limited.parameters(octets({15, 1, 7})));
LT_END_AUTO_TEST(callback_failures_are_terminal_and_zero_capacity_retries)
LT_BEGIN_AUTO_TEST(quic_tls_suite, protected_crypto_survives_saturated_data_and_pool_handle_teardown)
    auto root = httpserver::server::resource_budget::root({});
    constexpr auto resource = httpserver::server::resource::quic_reassembly_bytes;
    hd::quic_key_state keys;
    hd::quic_tls_config config;
    config.input_limits = {8, 2, 100};
    config.output_capacity = 8;
    config.receive_lease_capacity = 4;
    config.maximum_peer_parameters = 16;
    const auto capacity = hd::quic_tls_callbacks::storage_capacity(config);
    LT_CHECK(capacity > 44);
    std::unique_ptr<hd::quic_tls_callbacks> tls;
    {
        hd::quic_storage_pool pool(8, capacity, root);
        httpserver::server::reservation data;
        LT_ASSERT(pool.data().budget.reserve(resource, 8, data).ok());
        tls = std::make_unique<hd::quic_tls_callbacks>(config, pool.critical(), keys);
        auto bytes = octets({1, 2, 3, 4, 5, 6, 7, 8});
        LT_ASSERT(tls->receive(hd::quic_crypto_level::initial, 0, bytes));
        std::size_t sent = 0;
        LT_ASSERT(tls->send(bytes, &sent));
        LT_CHECK(sent == 8);
    }
    LT_CHECK(root.in_use(resource) == 8 + capacity);
    const unsigned char* bytes = nullptr;
    std::size_t size = 0;
    LT_ASSERT(tls->recv(&bytes, &size));
    LT_CHECK(size == 4 && bytes[0] == 1 && bytes[3] == 4);
    LT_ASSERT(tls->release(size));
    tls.reset();
    LT_CHECK(root.in_use(resource) == 0);
LT_END_AUTO_TEST(protected_crypto_survives_saturated_data_and_pool_handle_teardown)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
