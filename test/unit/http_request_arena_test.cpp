/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-016: per-connection arena for http_request_impl.
//
// Two cycles in this TU:
//   1. arena_release_resets_bump_pointer  -- structural anchor: a
//      connection_state owns a std::pmr::monotonic_buffer_resource whose
//      release() rewinds the bump pointer so a second allocation lands at
//      the same address as the first.
//   2. warm_path_zero_upstream_allocs     -- the headline acceptance
//      criterion: an http_request_impl constructed against an arena (with a
//      generously-sized initial buffer) does NOT touch the upstream resource
//      on the warm path -- after the first request grew the arena, the
//      second request's upstream alloc count stays flat.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <memory_resource>

// HTTPSERVER_COMPILATION supplied by test/Makefile.am AM_CPPFLAGS.
#include "httpserver/detail/http_request_impl.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include "./littletest.hpp"

// Counting upstream resource. Wraps new_delete_resource and bumps a
// counter on every do_allocate. Used to assert that the warm-path
// http_request_impl construction does not spill out of the arena.
class assert_no_upstream_resource final : public std::pmr::memory_resource {
 public:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        ++upstream_alloc_count_;
        return std::pmr::new_delete_resource()->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }
    bool do_is_equal(const std::pmr::memory_resource& o) const noexcept override {
        return this == &o;
    }
    std::size_t upstream_alloc_count() const { return upstream_alloc_count_; }

 private:
    std::size_t upstream_alloc_count_ = 0;
};

LT_BEGIN_SUITE(http_request_arena_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(http_request_arena_suite)

// (1) connection_state must own a std::pmr::monotonic_buffer_resource named
//     arena_, and arena_.release() must rewind the bump pointer so a second
//     allocation lands at the same address as the first.
//
//     Note: impl_address_reuse_after_release (test 3) extends this contract
//     to http_request_impl construction, so the split between these two
//     tests is intentional rather than redundant. (test-quality-reviewer-iter1-5)
LT_BEGIN_AUTO_TEST(http_request_arena_suite, arena_release_resets_bump_pointer)
    httpserver::detail::connection_state cs;

    // Two byte allocations of the same size+align; release between them.
    void* p1 = cs.arena_.allocate(64, alignof(std::max_align_t));
    LT_CHECK(p1 != nullptr);

    cs.arena_.release();
    void* p2 = cs.arena_.allocate(64, alignof(std::max_align_t));
    LT_CHECK(p2 != nullptr);

    LT_CHECK_EQ(reinterpret_cast<std::uintptr_t>(p1),
                reinterpret_cast<std::uintptr_t>(p2));
LT_END_AUTO_TEST(arena_release_resets_bump_pointer)

// (2) Constructing an http_request_impl against an arena consumes only the
//     arena's initial buffer on the warm path (after the first request has
//     grown the arena and the arena has been released). The upstream
//     allocation counter stays flat across the warm-path construction.
LT_BEGIN_AUTO_TEST(http_request_arena_suite, warm_path_zero_upstream_allocs)
    assert_no_upstream_resource upstream;

    // 8 KiB initial buffer; sized so a typical http_request_impl fits with
    // headroom even before any PMR-aware container starts spilling into it.
    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(), &upstream);

    using httpserver::detail::http_request_impl;
    using impl_alloc_t = std::pmr::polymorphic_allocator<http_request_impl>;

    // First request (cold path): grows the arena (consumes some of the 8 KiB).
    // The baseline is sampled AFTER the cold cycle so the warm-path assertion
    // below measures only warm-path allocations. If the 8 KiB buffer is too
    // small for the cold cycle, the cold cycle will spill to upstream but
    // baseline will be non-zero, and the warm-path assertion would become
    // vacuous. The warm_path_zero_upstream_allocs_with_containers test (7)
    // asserts baseline == 0 there to guard against this. (test-quality-iter1-6)
    {
        impl_alloc_t alloc(&arena);
        auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);
        alloc.delete_object(p);
    }
    arena.release();
    const std::size_t baseline = upstream.upstream_alloc_count();

    // Warm-path request: no new upstream allocations expected.
    {
        impl_alloc_t alloc(&arena);
        auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);
        alloc.delete_object(p);
    }

    LT_CHECK_EQ(upstream.upstream_alloc_count(), baseline);
LT_END_AUTO_TEST(warm_path_zero_upstream_allocs)

// (3) Companion to (1): allocating an http_request_impl, releasing the
//     arena, and allocating a second http_request_impl produces the same
//     address. This is the strict statement the TASK-016 acceptance
//     criterion asks for ("MHD_RequestTerminationCode callback resets the
//     arena -- verified by a test that observes arena memory reuse").
LT_BEGIN_AUTO_TEST(http_request_arena_suite, impl_address_reuse_after_release)
    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(),
                                              std::pmr::new_delete_resource());

    using httpserver::detail::http_request_impl;
    using impl_alloc_t = std::pmr::polymorphic_allocator<http_request_impl>;

    impl_alloc_t alloc(&arena);

    auto* p1 = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);
    const std::uintptr_t a1 = reinterpret_cast<std::uintptr_t>(p1);
    alloc.delete_object(p1);
    arena.release();

    auto* p2 = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);
    const std::uintptr_t a2 = reinterpret_cast<std::uintptr_t>(p2);
    alloc.delete_object(p2);

    LT_CHECK_EQ(a1, a2);
LT_END_AUTO_TEST(impl_address_reuse_after_release)

// (4) build_request_args must honour the max_args_count limit: once the
//     accumulator has collected max_args_count unique keys, subsequent
//     calls must return MHD_NO (so MHD stops iterating) and must NOT
//     insert additional entries into the map. This prevents a DoS where
//     a crafted request with thousands of unique GET arguments exhausts
//     the per-connection arena and the heap upstream.
//     (security-reviewer-iter1-2)
LT_BEGIN_AUTO_TEST(http_request_arena_suite, build_request_args_respects_max_args_count)
    using httpserver::detail::http_request_impl;
    using impl_alloc_t = std::pmr::polymorphic_allocator<http_request_impl>;

    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(),
                                              std::pmr::new_delete_resource());
    impl_alloc_t alloc(&arena);
    auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);

    // Directly drive build_request_args via the arguments_accumulator.
    // The accumulator is set up with max_args_count = 2.
    httpserver::detail::arguments_accumulator aa;
    aa.unescaper = nullptr;
    aa.arguments = &p->unescaped_args;
    aa.max_args_count = 2;
    aa.max_args_bytes = 4096;

    // First two entries: must be accepted (MHD_YES).
    MHD_Result r1 = http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k1", "v1");
    LT_CHECK_EQ(r1, MHD_YES);
    MHD_Result r2 = http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k2", "v2");
    LT_CHECK_EQ(r2, MHD_YES);
    LT_CHECK_EQ(p->unescaped_args.size(), std::size_t{2});

    // Third entry: must be rejected (MHD_NO) and map stays at 2.
    MHD_Result r3 = http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k3", "v3");
    LT_CHECK_EQ(r3, MHD_NO);
    LT_CHECK_EQ(p->unescaped_args.size(), std::size_t{2});

    alloc.delete_object(p);
LT_END_AUTO_TEST(build_request_args_respects_max_args_count)

// (4b) build_request_args must honour the max_args_bytes limit: once
//      total accumulated key+value bytes would exceed max_args_bytes,
//      subsequent calls must return MHD_NO.
LT_BEGIN_AUTO_TEST(http_request_arena_suite, build_request_args_respects_max_args_bytes)
    using httpserver::detail::http_request_impl;
    using impl_alloc_t = std::pmr::polymorphic_allocator<http_request_impl>;

    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(),
                                              std::pmr::new_delete_resource());
    impl_alloc_t alloc(&arena);
    auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);

    httpserver::detail::arguments_accumulator aa;
    aa.unescaper = nullptr;
    aa.arguments = &p->unescaped_args;
    aa.max_args_count = 100;   // high count limit; bytes limit is the active one
    aa.max_args_bytes = 10;    // only 10 bytes total

    // "key=val" = 3+3 = 6 bytes <= 10; should be accepted.
    MHD_Result r1 = http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "key", "val");
    LT_CHECK_EQ(r1, MHD_YES);

    // "key2=val2" would push total to 6+4+4 = 14 > 10; must be rejected.
    MHD_Result r2 = http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "key2", "val2");
    LT_CHECK_EQ(r2, MHD_NO);
    LT_CHECK_EQ(p->unescaped_args.size(), std::size_t{1});

    alloc.delete_object(p);
LT_END_AUTO_TEST(build_request_args_respects_max_args_bytes)

// (4c) connection_state::reset_arena() must zero the initial buffer after
//      releasing the arena bump pointer. This prevents credentials written
//      by a previous request from remaining readable in the reused buffer
//      memory until overwritten by the next request's population.
//      (security-reviewer-iter1-3)
LT_BEGIN_AUTO_TEST(http_request_arena_suite, reset_arena_clears_initial_buffer)
    httpserver::detail::connection_state cs;

    // Write a recognisable sentinel pattern into the arena via allocation.
    constexpr std::size_t sentinel_size = 16;
    void* raw = cs.arena_.allocate(sentinel_size, alignof(std::max_align_t));
    LT_CHECK(raw != nullptr);

    // Confirm the allocation is within the initial_buffer_.
    const auto* buf_start = reinterpret_cast<const std::byte*>(
        cs.initial_buffer_.data());
    const auto* buf_end = buf_start + httpserver::detail::connection_state::ARENA_INITIAL_BYTES;
    const auto* alloc_ptr = reinterpret_cast<const std::byte*>(raw);
    LT_CHECK(alloc_ptr >= buf_start);
    LT_CHECK(alloc_ptr < buf_end);

    // Write known non-zero bytes (simulate a credential string).
    std::memset(raw, 0xAB, sentinel_size);

    // reset_arena() must call release() AND zero the initial buffer.
    cs.reset_arena();

    // After reset, the bytes at that location must be zero.
    // (test-quality-reviewer-iter1-4: use std::all_of for a cleaner assertion)
    LT_CHECK(std::all_of(alloc_ptr, alloc_ptr + sentinel_size,
                         [](std::byte b) { return b == std::byte{0}; }));

    // Verify the arena is also released (bump pointer rewound): a new
    // allocation of the same size must land at the same address.
    void* raw2 = cs.arena_.allocate(sentinel_size, alignof(std::max_align_t));
    LT_CHECK_EQ(reinterpret_cast<std::uintptr_t>(raw),
                reinterpret_cast<std::uintptr_t>(raw2));
LT_END_AUTO_TEST(reset_arena_clears_initial_buffer)

// (5) Populate the PMR-aware lazy caches (querystring, requestor_ip,
//     unescaped_args, path_pieces) inside an http_request_impl and verify
//     that the warm-path (second request after arena release) does not
//     spill to the upstream resource. This closes the gap flagged by
//     performance-reviewer-iter1-5: the previous warm_path_zero_upstream_allocs
//     test only exercised construction with a null connection (no container
//     population), so it did not validate the acceptance criterion for a
//     request that actually populates the arena-backed containers.
//
//     code-quality-reviewer-iter1-3 / spec-alignment-checker-iter1-7:
//     The baseline is asserted to be zero to confirm the cold cycle itself
//     fit within the 8 KiB initial buffer (no spill to upstream). If the
//     buffer is ever too small, the cold-cycle spill would be silently
//     included in the baseline and the warm-path assertion would still
//     pass -- making the test vacuous.
LT_BEGIN_AUTO_TEST(http_request_arena_suite, warm_path_zero_upstream_allocs_with_containers)
    assert_no_upstream_resource upstream;

    // 8 KiB initial buffer; large enough for a typical small GET request
    // with a few args, a querystring, and a short requestor IP.
    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(), &upstream);

    using httpserver::detail::http_request_impl;
    using impl_alloc_t = std::pmr::polymorphic_allocator<http_request_impl>;

    // Helper lambda: construct an impl, populate its lazy-cache containers
    // via the set_arg / path helpers that go through the PMR allocator,
    // then destroy it. Returns nothing; callers check upstream_alloc_count.
    auto one_request_cycle = [&]() {
        impl_alloc_t alloc(&arena);
        auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);

        // Populate the PMR-backed unescaped_args map via the set_arg helper.
        constexpr std::size_t limit = 1024;
        p->set_arg("key1", "value_one", limit);
        p->set_arg("key2", "value_two", limit);
        p->set_arg("key3", "value_three", limit);

        // Populate querystring and requestor_ip (pmr::string members) directly.
        p->querystring = "?key1=value_one&key2=value_two&key3=value_three";
        p->requestor_ip = "192.168.1.100";

        // Populate path_pieces cache via the helper (takes a string_view path).
        p->ensure_path_pieces_cached("/api/v1/resource/item");

        alloc.delete_object(p);
    };

    // Cold cycle: grows the arena (tree nodes, string storage, etc.).
    one_request_cycle();
    arena.release();
    const std::size_t baseline = upstream.upstream_alloc_count();

    // Baseline must be zero: the 8 KiB initial buffer must be sufficient to
    // hold all cold-cycle allocations. If this fails, the buffer is too small
    // and the warm-path assertion below would be vacuous (the "baseline" would
    // absorb cold-cycle spills and the warm path would appear zero-delta even
    // if it also spills). (code-quality-reviewer-iter1-3)
    LT_CHECK_EQ(baseline, std::size_t{0});

    // Warm cycle: reuses the arena's initial buffer -- upstream must stay flat.
    one_request_cycle();
    arena.release();

    LT_CHECK_EQ(upstream.upstream_alloc_count(), baseline);
LT_END_AUTO_TEST(warm_path_zero_upstream_allocs_with_containers)

// (6) Simulate the request_completed trampoline contract: destroy the
//     arena-backed impl (running its destructor), then call reset_arena()
//     on the connection_state, and verify the arena is ready for reuse at
//     the same address. This tests the two-step sequence in
//     webserver_impl::request_completed without requiring a live MHD daemon.
//
//     Acceptance criterion: 'MHD_RequestTerminationCode callback resets
//     the arena (test)'. The structural tests (1) and (3) verify the
//     bump-pointer rewind property; this test verifies the ordering contract
//     between ~http_request_impl (step 1) and reset_arena() (step 2) by
//     running them in the same order request_completed does.
//     (code-quality-reviewer-iter1-2 / test-quality-reviewer-iter1-1)
LT_BEGIN_AUTO_TEST(http_request_arena_suite, request_completed_trampoline_contract)
    using httpserver::detail::http_request_impl;
    using impl_alloc_t = std::pmr::polymorphic_allocator<http_request_impl>;

    httpserver::detail::connection_state cs;

    // Step 1 (mirrors request_completed): allocate and construct an
    // http_request_impl from the connection_state's arena, populate
    // a couple of args to give it live PMR state, then call its
    // destructor via delete_object (mirrors the arena_deleter: destructor
    // only, no deallocation). This must not touch the arena bump pointer.
    impl_alloc_t alloc(&cs.arena_);
    auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);

    constexpr std::size_t limit = 1024;
    p->set_arg("user", "alice", limit);
    p->querystring = "?user=alice";
    p->requestor_ip = "10.0.0.1";

    // Capture the address before destruction so we can verify reuse.
    const void* const first_addr = static_cast<void*>(p);

    // Destroy the impl without deallocating (mirrors destroy_impl_arena).
    p->~http_request_impl();

    // Step 2 (mirrors request_completed): rewind and zero the arena.
    cs.reset_arena();

    // After reset, a new allocation of the same type must land at the same
    // address (bump pointer rewound to the start of the initial buffer).
    auto* p2 = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);
    const void* const second_addr = static_cast<void*>(p2);
    alloc.delete_object(p2);

    LT_CHECK_EQ(first_addr, second_addr);
LT_END_AUTO_TEST(request_completed_trampoline_contract)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
