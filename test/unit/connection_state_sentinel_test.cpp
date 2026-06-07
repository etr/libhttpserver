/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-068 unit test: pins that connection_state::reset_arena() zeros the
// entire initial_buffer_ after a sentinel-prefill.
//
// CWE-226 mitigation: writing the per-request sentinel pattern across the
// 8 KiB arena buffer and then calling reset_arena() must leave the entire
// buffer zeroed -- not just the prefix that was actually allocated, since
// monotonic_buffer_resource::release() does not clear bytes by itself.

// HTTPSERVER_COMPILATION supplied by test/Makefile.am AM_CPPFLAGS.
#include "httpserver/detail/connection_state.hpp"

#include <cstddef>
#include <cstring>

#include "./littletest.hpp"

LT_BEGIN_SUITE(connection_state_sentinel_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(connection_state_sentinel_suite)

// Prefill the buffer with a known sentinel pattern (0xDE) and verify that
// reset_arena() leaves every byte at 0.
LT_BEGIN_AUTO_TEST(connection_state_sentinel_suite, reset_arena_zeros_full_buffer)
    httpserver::detail::connection_state cs;

    // Prefill every byte with 0xDE so a no-op would be detected.
    std::memset(cs.initial_buffer_.data(), 0xDE,
                httpserver::detail::connection_state::ARENA_INITIAL_BYTES);
    // Sanity check: the prefill landed.
    LT_CHECK_EQ(static_cast<unsigned>(
                    static_cast<unsigned char>(cs.initial_buffer_[0])),
                0xDEu);
    LT_CHECK_EQ(static_cast<unsigned>(
                    static_cast<unsigned char>(
                        cs.initial_buffer_[
                            httpserver::detail::connection_state::
                                ARENA_INITIAL_BYTES - 1])),
                0xDEu);

    cs.reset_arena();

    // Every byte must be zero after reset_arena().
    for (std::size_t i = 0;
         i < httpserver::detail::connection_state::ARENA_INITIAL_BYTES; ++i) {
        const auto v = static_cast<unsigned>(
                          static_cast<unsigned char>(cs.initial_buffer_[i]));
        if (v != 0u) {
            LT_FAIL("reset_arena left non-zero byte in initial_buffer_");
            break;
        }
    }
LT_END_AUTO_TEST(reset_arena_zeros_full_buffer)

// After an arena allocation that writes a sentinel pattern, reset_arena()
// must rewind the bump pointer AND zero the buffer so the next allocation
// observes zero bytes (not the prior sentinel).
LT_BEGIN_AUTO_TEST(connection_state_sentinel_suite,
                   reset_arena_zeros_post_allocation_residue)
    httpserver::detail::connection_state cs;

    // Allocate a chunk and write a sentinel into it.
    constexpr std::size_t kChunk = 512;
    void* p1 = cs.arena_.allocate(kChunk, alignof(std::max_align_t));
    LT_CHECK(p1 != nullptr);
    std::memset(p1, 0xDE, kChunk);

    // Reset.
    cs.reset_arena();

    // Same-size allocation lands at the same address (monotonic_buffer
    // bump-pointer semantics), and the bytes must now be zero.
    void* p2 = cs.arena_.allocate(kChunk, alignof(std::max_align_t));
    LT_CHECK_EQ(p2, p1);
    auto* bytes = static_cast<const unsigned char*>(p2);
    for (std::size_t i = 0; i < kChunk; ++i) {
        if (bytes[i] != 0u) {
            LT_FAIL("reset_arena left residue from a prior allocation");
            break;
        }
    }
LT_END_AUTO_TEST(reset_arena_zeros_post_allocation_residue)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
