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

#include <cstddef>
#include <cstdint>
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

    // First request: grows the arena (consumes some of the 8 KiB).
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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
