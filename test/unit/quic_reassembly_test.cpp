/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <httpserver/detail/quic_reassembly.hpp>
#include "./quic_codec_test_support.hpp"
namespace {
bool track_storage = false, refuse_array_allocation = false, refuse_storage_allocation = false;
std::size_t requested_storage = 0;
}  // namespace
void* operator new(std::size_t size) {
    if (refuse_storage_allocation) {
        refuse_storage_allocation = false;
        throw std::bad_alloc();
    }
    if (track_storage) requested_storage += size;
    if (auto* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void* operator new[](std::size_t size) {
    if (refuse_array_allocation) {
        refuse_array_allocation = false;
        throw std::bad_alloc();
    }
    return ::operator new(size);
}
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
namespace {
using httpserver::server::resource;
auto budget(std::size_t bytes = 1048576) {
    httpserver::server::budget_limits limits;
    limits.set(resource::quic_reassembly_bytes, bytes);
    return httpserver::server::resource_budget::root(limits);
}
}  // namespace
LT_BEGIN_SUITE(reassembly_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(reassembly_suite)
LT_BEGIN_AUTO_TEST(reassembly_suite, reordered_overlaps_bridge_and_own_input)
    hd::quic_reassembly r({12, 3, 20}, budget());
    auto tail = octets({5, 6, 7, 8}), head = octets({1, 2, 3});
    LT_ASSERT(r.insert(4, tail));
    tail.assign(4, std::byte{99});
    LT_ASSERT(r.insert(0, head));
    std::array<std::byte, 8> out{};
    LT_CHECK(r.read(std::span(out).first(2)) == 2);
    LT_CHECK(out[0] == std::byte{1} && out[1] == std::byte{2});
    auto bridge = octets({3, 4, 5, 6});
    LT_ASSERT(r.insert(2, bridge));
    LT_CHECK(r.ranges() == 1 && r.buffered_bytes() == 6);
    auto nested = octets({5, 6});
    const auto storage = r.retained_storage();
    LT_CHECK(r.insert(4, nested));
    LT_CHECK(r.retained_storage() == storage);
    LT_CHECK(r.read(out) == 6);
    LT_CHECK(std::equal(out.begin(), out.begin() + 6, octets({3, 4, 5, 6, 7, 8}).begin()));
    LT_CHECK(r.consumed() == 8 && r.buffered_bytes() == 0 && r.ranges() == 0);
    LT_CHECK(r.read(out) == 0);
LT_END_AUTO_TEST(reordered_overlaps_bridge_and_own_input)
LT_BEGIN_AUTO_TEST(reassembly_suite, exact_payload_range_span_and_arithmetic_limits_are_transactional)
    hd::quic_reassembly r({4, 2, 8}, budget());
    auto two = octets({1, 2}), one = octets({3});
    LT_ASSERT(r.insert(0, two));
    LT_ASSERT(r.insert(6, two));
    LT_CHECK(r.insert(3, one).code == hd::quic_stream_code::byte_limit_exceeded);
    LT_CHECK(r.insert(8, one).code == hd::quic_stream_code::gap_limit_exceeded);
    LT_CHECK(r.insert(hd::k_quic_max_integer, one).code == hd::quic_stream_code::frame_encoding_error);
    LT_CHECK(r.insert(hd::k_quic_max_integer + 1, {}).code == hd::quic_stream_code::frame_encoding_error);
    LT_CHECK(r.insert(7, one).code == hd::quic_stream_code::protocol_violation);
    LT_CHECK(r.buffered_bytes() == 4 && r.ranges() == 2 && r.consumed() == 0);
    hd::quic_reassembly gaps({10, 2, 100}, budget());
    LT_ASSERT(gaps.insert(2, one));
    LT_ASSERT(gaps.insert(4, one));
    auto before = gaps.retained_storage();
    LT_CHECK(gaps.insert(6, one).code == hd::quic_stream_code::gap_limit_exceeded);
    LT_CHECK(gaps.retained_storage() == before && gaps.ranges() == 2);
    hd::quic_reassembly span({1, 1, 100}, budget());
    LT_CHECK(span.insert(99, one));
LT_END_AUTO_TEST(exact_payload_range_span_and_arithmetic_limits_are_transactional)
LT_BEGIN_AUTO_TEST(reassembly_suite, empty_and_consumed_prefixes_need_no_storage)
    hd::quic_reassembly r({4, 1, 8}, budget());
    LT_CHECK(r.insert(8, {}));
    LT_CHECK(r.ranges() == 0 && r.retained_storage() == 0);
    auto bytes = octets({1, 2, 3, 4});
    LT_ASSERT(r.insert(0, bytes));
    std::array<std::byte, 3> out{};
    LT_CHECK(r.read(out) == 3);
    auto charged = r.retained_storage();
    LT_CHECK(r.buffered_bytes() == 1 && charged >= 4);
    auto prefix = octets({99, 99, 99});
    LT_CHECK(r.insert(0, prefix));
    LT_CHECK(r.retained_storage() == charged);
    auto overlap = octets({99, 99, 3, 4});
    LT_CHECK(r.insert(0, overlap));
    LT_CHECK(r.read(out) == 1 && out[0] == std::byte{4});
LT_END_AUTO_TEST(empty_and_consumed_prefixes_need_no_storage)
LT_BEGIN_AUTO_TEST(reassembly_suite, ancestor_budget_refusal_releases_staging_and_reset_storage)
    std::size_t descriptor_bytes = 0;
    {
        hd::quic_reassembly probe({8, 1, 20}, budget());
        LT_ASSERT(probe.insert(0, octets({1})));
        descriptor_bytes = probe.retained_storage() - 1;
    }
    auto root = budget(2 * descriptor_bytes + 6);
    httpserver::server::budget_limits limits;
    limits.set(resource::quic_reassembly_bytes, 2 * descriptor_bytes + 6);
    httpserver::server::resource_budget connection;
    LT_ASSERT(root.child(limits, connection).ok());
    {
        hd::quic_reassembly a({8, 1, 20}, connection), b({8, 1, 20}, connection);
        LT_ASSERT(a.insert(0, octets({1, 2, 3, 4})));
        auto before = root.in_use(resource::quic_reassembly_bytes);
        LT_CHECK(b.insert(0, octets({5, 6, 7})).code == hd::quic_stream_code::no_memory);
        LT_CHECK(root.in_use(resource::quic_reassembly_bytes) == before);
        LT_CHECK(b.buffered_bytes() == 0 && b.retained_storage() == 0);
        LT_ASSERT(b.insert(0, octets({5, 6})));
        before = root.in_use(resource::quic_reassembly_bytes);
        LT_CHECK(a.insert(4, octets({5})).code == hd::quic_stream_code::no_memory);
        LT_CHECK(root.in_use(resource::quic_reassembly_bytes) == before);
        LT_CHECK(a.buffered_bytes() == 4 && a.ranges() == 1);
        b.clear();
        LT_CHECK(b.retained_storage() == 0 && b.ranges() == 0);
        LT_ASSERT(a.insert(4, octets({5})));
        LT_CHECK(root.in_use(resource::quic_reassembly_bytes) == a.retained_storage());
    }
    LT_CHECK(root.in_use(resource::quic_reassembly_bytes) == 0);
    LT_CHECK(connection.in_use(resource::quic_reassembly_bytes) == 0);
LT_END_AUTO_TEST(ancestor_budget_refusal_releases_staging_and_reset_storage)
LT_BEGIN_AUTO_TEST(reassembly_suite, retained_capacity_and_configuration_arithmetic_are_bounded)
    hd::quic_reassembly r({4, 2, 20}, budget());
    LT_ASSERT(r.insert(0, octets({1, 2, 3, 4})));
    std::array<std::byte, 3> out{};
    LT_CHECK(r.read(out) == 3);
    auto before = r.retained_storage();
    LT_CHECK(r.insert(5, octets({6})).code == hd::quic_stream_code::byte_limit_exceeded);
    LT_CHECK(r.retained_storage() == before && r.buffered_bytes() == 1);
    LT_CHECK(r.insert(4, octets({5, 6, 7})));
    LT_CHECK(r.buffered_bytes() == 4);
    bool invalid = false;
    try {
        hd::quic_reassembly overflowing({std::numeric_limits<std::size_t>::max(), 1, 20}, budget());
    } catch (const std::invalid_argument&) { invalid = true; }
    LT_CHECK(invalid);
    hd::quic_reassembly disabled({0, 0, 0}, budget());
    LT_CHECK(disabled.insert(0, {}));
    LT_CHECK(disabled.insert(0, octets({1})).code == hd::quic_stream_code::gap_limit_exceeded);
LT_END_AUTO_TEST(retained_capacity_and_configuration_arithmetic_are_bounded)
LT_BEGIN_AUTO_TEST(reassembly_suite, budget_covers_actual_array_requests_and_allocation_failure_rolls_back)
    auto shared = budget();
    hd::quic_reassembly r({8, 2, 20}, shared);
    auto bytes = octets({1, 2, 3});
    requested_storage = 0;
    track_storage = true;
    auto inserted = r.insert(0, bytes);
    track_storage = false;
    LT_ASSERT(inserted);
    LT_CHECK(shared.in_use(resource::quic_reassembly_bytes) >= requested_storage);
    const auto before = r.retained_storage();
    refuse_array_allocation = true;
    LT_CHECK(r.insert(3, bytes).code == hd::quic_stream_code::no_memory);
    LT_CHECK(r.retained_storage() == before && r.buffered_bytes() == 3 && r.ranges() == 1);
    LT_CHECK(shared.in_use(resource::quic_reassembly_bytes) == before);
    hd::quic_reassembly empty({8, 2, 20}, shared);
    refuse_storage_allocation = true;
    LT_CHECK(empty.insert(0, bytes).code == hd::quic_stream_code::no_memory);
    LT_CHECK(empty.retained_storage() == 0 && empty.buffered_bytes() == 0);
    LT_CHECK(shared.in_use(resource::quic_reassembly_bytes) == before);
LT_END_AUTO_TEST(budget_covers_actual_array_requests_and_allocation_failure_rolls_back)
LT_BEGIN_AUTO_TEST(reassembly_suite, large_offsets_and_many_merge_orders_preserve_bytes)
    hd::quic_reassembly sparse({2, 1, hd::k_quic_max_integer}, budget());
    LT_ASSERT(sparse.insert(hd::k_quic_max_integer - 1, octets({7})));
    LT_CHECK(sparse.buffered_bytes() == 1 && sparse.ranges() == 1);
    std::array<std::byte, 16> out{};
    LT_CHECK(sparse.read(out) == 0);
    const auto expected = octets({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
    for (unsigned rotation = 0; rotation < 8; ++rotation) {
        hd::quic_reassembly r({16, 8, 16}, budget());
        for (unsigned i = 0; i < 8; ++i) {
            const unsigned start = 2 * ((i * 3 + rotation) % 8);
            LT_ASSERT(r.insert(start, std::span(expected).subspan(start, 2)));
        }
        LT_CHECK(r.ranges() == 1 && r.buffered_bytes() == 16);
        LT_CHECK(r.insert(1, std::span(expected).subspan(1, 14)));
        LT_CHECK(r.read(out) == 16);
        LT_CHECK(std::equal(out.begin(), out.end(), expected.begin()));
    }
LT_END_AUTO_TEST(large_offsets_and_many_merge_orders_preserve_bytes)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
