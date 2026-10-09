/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "./http3_fixture.hpp"
#include <cstdlib>
#include <new>
#include <limits>
#include "./littletest.hpp"
namespace {
int allocation_countdown = -1;
}  // namespace
void* operator new(std::size_t size) {
    if (allocation_countdown == 0) {
        allocation_countdown = -1; throw std::bad_alloc();
    }
    if (allocation_countdown > 0) --allocation_countdown;
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
using namespace h3test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(http3_qpack_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(http3_qpack_suite)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, ancestor_table_charge_survives_pool_handle)
    auto root = budget();
    hs::budget_limits limits; limits.set(hs::resource::qpack_table_bytes, 32);
    hs::resource_budget child; LT_ASSERT(root.child(limits, child).ok());
    hs::reservation charge;
    hd::quic_storage_lease lease;
    {
        hd::quic_storage_pool pool(100, 100, child); lease = pool.data();
        LT_CHECK(!lease.ancestors.reserve(hs::resource::qpack_table_bytes, 33, charge).ok());
    }
    LT_CHECK_EQ(root.in_use(hs::resource::quic_reassembly_bytes), 200U);
    lease = {};
    LT_CHECK_EQ(root.in_use(hs::resource::quic_reassembly_bytes), 0U);
LT_END_AUTO_TEST(ancestor_table_charge_survives_pool_handle)
namespace {
hd::http3_limits dynamic_limits() {
    hd::http3_limits limits; limits.qpack_capacity = 128; limits.qpack_blocked_streams = 2;
    limits.headers = {100, 100, 4}; return limits;
}
}  // namespace
LT_BEGIN_AUTO_TEST(http3_qpack_suite, reordered_headers_unblock_lower_requirement_and_retain_bytes)
    auto root = budget(); hd::quic_storage_pool pool(10000, 30000, root);
    auto limits = dynamic_limits(); limits.qpack_retained_bytes = 9;
    hd::http3_connection c(pool.data(), pool.critical(), limits);
    c.attach_stream(0); c.attach_stream(4); c.attach_stream(8); c.attach_stream(2);
    LT_CHECK(!feed(c, 0, bytes({1, 3, 3, 0, 0x80})).error);
    LT_CHECK(!feed(c, 4, bytes({1, 3, 2, 0, 0x80})).error);
    LT_CHECK(!c.event(0) && !c.event(4));
    LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 2U);
    LT_CHECK_EQ(feed(c, 4, bytes({0, 0})).consumed, 0U);
    LT_CHECK(!feed(c, 8, bytes({1, 3, 0, 0, 0xd1})).error); LT_ASSERT(c.event(8));
    auto insert = bytes({2, 0x3f, 0x61, 0x41, 'x', 1, 'a'});
    for (auto byte : insert) LT_CHECK(!feed(c, 2, std::span(&byte, 1)).error);
    LT_ASSERT(c.event(4)); LT_CHECK(!c.event(0)); LT_CHECK_EQ(c.event(4)->fields[0].value, "a");
    LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 1U);
    LT_CHECK(!feed(c, 2, bytes({0})).error); LT_ASSERT(c.event(0));
    LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 0U);
    LT_CHECK_EQ(c.event(0)->fields[0].name, "x");
    auto before = pool.data().budget.in_use(hs::resource::quic_reassembly_bytes);
    c.release_event(4); LT_CHECK(pool.data().budget.in_use(hs::resource::quic_reassembly_bytes) < before);
LT_END_AUTO_TEST(reordered_headers_unblock_lower_requirement_and_retain_bytes)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, fragmented_instructions_at_every_split_and_invalid_indexes)
    auto wire = bytes({2, 0x3f, 0x61, 0x41, 'x', 1, 'a', 0});
    for (std::size_t split = 1; split <= wire.size(); ++split) {
        hd::quic_storage_pool pool(10000, 30000, budget());
        hd::http3_connection c(pool.data(), pool.critical(), dynamic_limits()); c.attach_stream(0); c.attach_stream(2);
        LT_CHECK(!feed(c, 0, bytes({1, 3, 3, 0, 0x80})).error);
        LT_CHECK(!feed(c, 2, std::span(wire).first(split)).error);
        LT_CHECK(!feed(c, 2, std::span(wire).subspan(split)).error);
        LT_ASSERT(c.event(0)); LT_CHECK_EQ(c.event(0)->fields[0].value, "a");
    }
    for (auto bad : {bytes({2, 0}), bytes({2, 0x3f, 0x62}), bytes({3, 0}), bytes({3, 1}), bytes({3, 0x80})}) {
        hd::quic_storage_pool pool(10000, 30000, budget()); hd::http3_connection c(pool.data(), pool.critical(), dynamic_limits());
        c.attach_stream(2); auto result = feed(c, 2, bad); LT_ASSERT(result.error);
        LT_CHECK_EQ(result.error->wire_code, std::to_integer<unsigned>(bad[0]) == 2 ? 0x201U : 0x202U);
        LT_CHECK_EQ(feed(c, 2, {}).error->wire_code, result.error->wire_code);
    }
LT_END_AUTO_TEST(fragmented_instructions_at_every_split_and_invalid_indexes)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, independent_count_byte_and_ancestor_limits)
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        auto root = budget(); hs::budget_limits bound; bound.set(hs::resource::blocked_headers, scenario == 2 ? 1 : 10);
        hs::resource_budget child; LT_ASSERT(root.child(bound, child).ok());
        hd::quic_storage_pool pool(10000, 30000, child); auto limits = dynamic_limits();
        limits.qpack_blocked_streams = scenario == 0 ? 1 : 2; limits.qpack_retained_bytes = scenario == 1 ? 5 : 6;
        hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(0); c.attach_stream(4);
        LT_CHECK(!feed(c, 0, bytes({1, 3, 2, 0, 0x80})).error);
        LT_CHECK_EQ(feed(c, 4, bytes({1, 3, 2, 0, 0x80})).error->wire_code, 0x107U);
    }
LT_END_AUTO_TEST(independent_count_byte_and_ancestor_limits)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, blocked_fin_reset_and_protected_input)
    auto root = budget();
    {
        hd::quic_storage_pool pool(10000, 30000, root); hd::http3_connection c(pool.data(), pool.critical(), dynamic_limits());
        c.attach_stream(0); c.attach_stream(4); c.attach_stream(2);
        feed(c, 0, bytes({1, 3, 2, 0, 0x80})); feed(c, 4, bytes({1, 3, 2, 0, 0x80}));
        LT_CHECK(!c.terminal(0, {hd::quic_terminal_kind::eof}));
        LT_CHECK(!c.terminal(4, {hd::quic_terminal_kind::reset}));
        LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 1U);
        // Leave room for the already admitted expanded event only.
        hs::reservation saturation; auto used = pool.data().budget.in_use(hs::resource::quic_reassembly_bytes);
        LT_ASSERT(pool.data().budget.reserve(hs::resource::quic_reassembly_bytes, 10000 - used - 100 - 4 * sizeof(hd::qpack_field), saturation).ok());
        LT_CHECK(!feed(c, 2, bytes({2, 0x3f, 0x61, 0x41, 'x', 1, 'a'})).error); LT_ASSERT(c.event(0));
        LT_CHECK(!c.event(4)); LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 0U);
    }
    LT_CHECK_EQ(root.in_use(hs::resource::qpack_table_bytes), 0U);
    LT_CHECK_EQ(root.in_use(hs::resource::quic_reassembly_bytes), 0U);
LT_END_AUTO_TEST(blocked_fin_reset_and_protected_input)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, local_settings_advertise_admitted_limits)
    hd::quic_storage_pool pool(10000, 30000, budget()); hd::http3_connection c(pool.data(), pool.critical(), dynamic_limits());
    hd::quic_cursor cursor(c.local_prefix(hd::http3_role::control));
    LT_CHECK_EQ(cursor.varint(), 0U); LT_CHECK_EQ(cursor.varint(), 4U);
    const auto length = cursor.varint(); LT_CHECK_EQ(length, cursor.remaining());
    std::uint64_t capacity = 0, blocked = 0, connect = 0;
    while (cursor.remaining()) {
        const auto key = cursor.varint(); const auto value = cursor.varint();
        if (key == 1) capacity = value;
        if (key == 7) blocked = value;
        if (key == 8) connect = value;
    }
    LT_CHECK_EQ(capacity, 128U); LT_CHECK_EQ(blocked, 2U); LT_CHECK_EQ(connect, 1U);
LT_END_AUTO_TEST(local_settings_advertise_admitted_limits)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, instruction_allocation_failure_is_internal_and_does_not_publish)
    for (int stage = 0; stage < 3; ++stage) {
        auto root = budget(); hd::quic_storage_pool pool(10000, 30000, root); auto limits = dynamic_limits(); limits.qpack_capacity = 256;
        hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(2);
        LT_CHECK(!feed(c, 2, bytes({2, 0x3f, 0xe1, 1})).error);
        auto wire = bytes({0x54, 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 20,
                           'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b'});
        LT_CHECK(!feed(c, 2, std::span(wire).first(wire.size() - 1)).error);
        allocation_countdown = stage;
        auto result = feed(c, 2, std::span(wire).last(1)); allocation_countdown = -1;
        LT_ASSERT(result.error); LT_CHECK_EQ(result.error->wire_code, 0x102U);
        LT_CHECK(!c.event(0));
    }
LT_END_AUTO_TEST(instruction_allocation_failure_is_internal_and_does_not_publish)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, encoded_byte_limit_persists_until_event_release)
    for (bool release : {false, true}) {
        hd::quic_storage_pool pool(10000, 30000, budget()); auto limits = dynamic_limits(); limits.qpack_retained_bytes = 3;
        hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(0); c.attach_stream(4); c.attach_stream(2);
        LT_CHECK(!feed(c, 0, bytes({1, 3, 2, 0, 0x80})).error);
        LT_CHECK(!feed(c, 2, bytes({2, 0x3f, 0x61, 0x41, 'x', 1, 'a'})).error); LT_ASSERT(c.event(0));
        if (release) c.release_event(0);
        auto next = feed(c, 4, bytes({1, 3, 0, 0, 0xd1}));
        if (release) {
            LT_CHECK(!next.error); LT_ASSERT(c.event(4));
        } else {
            LT_ASSERT(next.error); LT_CHECK_EQ(next.error->wire_code, 0x107U);
        }
    }
LT_END_AUTO_TEST(encoded_byte_limit_persists_until_event_release)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, limits_overflow_and_table_ancestor_refusal_precede_settings)
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        auto root = budget(); hs::budget_limits bound; bound.set(hs::resource::qpack_table_bytes, scenario == 0 ? 127 : 512);
        hs::resource_budget child; LT_ASSERT(root.child(bound, child).ok()); hd::quic_storage_pool pool(10000, 30000, child);
        auto limits = dynamic_limits();
        if (scenario == 1) limits.qpack_output_bytes = SIZE_MAX;
        if (scenario == 2) limits.qpack_capacity = SIZE_MAX;
        if (scenario == 3) limits.qpack_instruction_bytes = 0;
        hd::http3_connection c(pool.data(), pool.critical(), limits);
        LT_CHECK(c.local_prefix(hd::http3_role::control).empty());
        LT_CHECK_EQ(c.attach_stream(0)->wire_code, 0x107U);
    }
LT_END_AUTO_TEST(limits_overflow_and_table_ancestor_refusal_precede_settings)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, frozen_prefix_cannot_alias_an_entry_after_insert_count_wrap)
    hd::qpack_dynamic_table table(hd::qpack_table_role::decoder); LT_ASSERT(table.initialize(budget(), 128).ok());
    LT_ASSERT(table.set_capacity(128).ok()); hd::qpack_decoder decoder;
    std::array<std::uint8_t, 3> wire{2, 0, 0x80}; const auto frozen = decoder.inspect_prefix(wire, table);
    LT_CHECK_EQ(frozen.required, 1U);
    for (unsigned i = 0; i < 9; ++i) LT_ASSERT(table.insert("x", "a").ok());
    LT_CHECK(decoder.decode_dynamic_allocated(wire, table, {100, 100, 4}, frozen).status.state == hd::qpack_state::malformed);
    LT_CHECK_EQ(decoder.decode_dynamic(wire, table, {100, 100, 4}).required_insert_count, 9U);
LT_END_AUTO_TEST(frozen_prefix_cannot_alias_an_entry_after_insert_count_wrap)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, local_instruction_bound_is_load_refusal_and_incomplete_insert_keeps_blocked_count)
    auto root = budget(); hd::quic_storage_pool pool(10000, 30000, root); auto limits = dynamic_limits(); limits.qpack_instruction_bytes = 3;
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(0); c.attach_stream(2);
    LT_CHECK(!feed(c, 0, bytes({1, 3, 2, 0, 0x80})).error);
    LT_CHECK(!feed(c, 2, bytes({2, 0x3f, 0x61})).error);
    LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 1U); LT_CHECK(!c.event(0));
    auto result = feed(c, 2, bytes({0x43, 'a', 'b', 'c', 1, 'v'})); LT_ASSERT(result.error); LT_CHECK_EQ(result.error->wire_code, 0x107U);
LT_END_AUTO_TEST(local_instruction_bound_is_load_refusal_and_incomplete_insert_keeps_blocked_count)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, instructions_yield_exactly_at_turn_boundary)
    hd::quic_storage_pool pool(10000, 30000, budget()); auto limits = dynamic_limits(); limits.frames_per_turn = 1;
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(0); c.attach_stream(2);
    LT_CHECK(!feed(c, 0, bytes({1, 3, 3, 0, 0x80})).error); c.begin_turn();
    auto wire = bytes({2, 0x3f, 0x61, 0x41, 'x', 1, 'a', 0});
    auto result = feed(c, 2, wire); LT_CHECK_EQ(result.consumed, 3U); LT_CHECK(result.progress == hd::http3_progress::yield);
    LT_CHECK_EQ(feed(c, 2, std::span(wire).subspan(3)).consumed, 0U); c.begin_turn();
    result = feed(c, 2, std::span(wire).subspan(3)); LT_CHECK_EQ(result.consumed, 4U); LT_CHECK(!c.event(0));
    c.begin_turn(); LT_CHECK_EQ(feed(c, 2, std::span(wire).last(1)).consumed, 1U); LT_ASSERT(c.event(0));
LT_END_AUTO_TEST(instructions_yield_exactly_at_turn_boundary)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, invalid_blocked_field_is_decompression_error_after_insert)
    hd::quic_storage_pool pool(10000, 30000, budget()); hd::http3_connection c(pool.data(), pool.critical(), dynamic_limits());
    c.attach_stream(0); c.attach_stream(2); LT_CHECK(!feed(c, 0, bytes({1, 3, 2, 0, 0x81})).error);
    auto result = feed(c, 2, bytes({2, 0x3f, 0x61, 0x41, 'x', 1, 'a'})); LT_ASSERT(result.error); LT_CHECK_EQ(result.error->wire_code, 0x200U);
LT_END_AUTO_TEST(invalid_blocked_field_is_decompression_error_after_insert)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, reset_of_incomplete_headers_sends_one_cancellation)
    auto root = budget(); hd::quic_storage_pool pool(10000, 30000, root);
    hd::http3_connection c(pool.data(), pool.critical(), dynamic_limits()); c.attach_stream(0);
    LT_CHECK(!feed(c, 0, bytes({1, 3, 2})).error);
    LT_CHECK_EQ(root.in_use(hs::resource::blocked_headers), 0U);
    LT_CHECK(!c.terminal(0, {hd::quic_terminal_kind::reset}));
    LT_CHECK(!c.terminal(0, {hd::quic_terminal_kind::reset}));
    LT_CHECK_EQ(pool.data().budget.in_use(hs::resource::quic_reassembly_bytes), 0U);
    LT_ASSERT(c.advance_local_prefix(hd::http3_role::qpack_decoder, 1));
    LT_CHECK(std::ranges::equal(c.local_prefix(hd::http3_role::qpack_decoder), bytes({0x40})));
LT_END_AUTO_TEST(reset_of_incomplete_headers_sends_one_cancellation)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, instruction_scratch_includes_decoded_strings)
    hd::quic_storage_pool pool(10000, 16000, budget()); hd::http3_connection c(pool.data(), pool.critical(), dynamic_limits()); c.attach_stream(2);
    auto result = feed(c, 2, bytes({2, 0x3f, 0x61})); LT_ASSERT(result.error); LT_CHECK_EQ(result.error->wire_code, 0x107U);
LT_END_AUTO_TEST(instruction_scratch_includes_decoded_strings)
LT_BEGIN_AUTO_TEST(http3_qpack_suite, outgoing_compressed_storage_arithmetic_is_validated_before_allocation)
    hd::quic_storage_pool pool(10000, 30000, budget()); auto limits = dynamic_limits(); limits.headers.max_compressed_bytes = SIZE_MAX;
    hd::http3_connection c(pool.data(), pool.critical(), limits);
    auto error = c.attach_stream(0); LT_ASSERT(error); LT_CHECK_EQ(error->wire_code, 0x107U);
LT_END_AUTO_TEST(outgoing_compressed_storage_arithmetic_is_validated_before_allocation)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
