/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-068 unit test: pins that httpserver::detail::secure_zero() is not
// dead-code-eliminated by the optimizer.
//
// The header centralizes a portable secure-zero primitive (CWE-14
// mitigation) that the per-connection arena cleanup uses. The test calls
// secure_zero() against a small stack buffer declared `volatile` so the
// compiler cannot substitute pre-zeroing values for post-zeroing reads.
// The TU is compiled at -O2 -DNDEBUG so the release-mode optimizer is
// given the fullest opportunity to elide writes.
//
// Why buf must be volatile (not just the sink):
//   Reading a non-volatile buf[i] into a volatile sink lets the
//   compiler propagate the known-constant value of buf[i] directly into
//   the volatile store, legally skipping the secure_zero writes entirely.
//   Declaring buf as `volatile unsigned char[]` makes every element
//   access an observable side effect, so the optimizer must round-trip
//   through memory both before and after secure_zero.
//
// Note: the load-bearing argument is in the header itself
// (`asm __volatile__("" ::: "memory")` + volatile pointer writes in the
// fallback). This test is a regression sentinel: if a future toolchain
// elides the writes, the post-write reads here will observe non-zero
// bytes and the assertion fires.

#include "httpserver/detail/secure_zero.hpp"

#include <cstddef>

#include "./littletest.hpp"

LT_BEGIN_SUITE(secure_zero_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(secure_zero_suite)

LT_BEGIN_AUTO_TEST(secure_zero_suite, writes_zero_bytes_to_buffer)
    constexpr std::size_t kSize = 256;
    // buf is volatile so that every element read is an observable side
    // effect. Without volatile the optimizer may propagate the pre-
    // zeroing value (0xA5) directly into the post-zeroing read,
    // eliminating the secure_zero call without violating the as-if rule.
    // With volatile every access must round-trip through memory.
    volatile unsigned char buf[kSize];
    // Prefill with a non-zero sentinel so a no-op secure_zero would be
    // detected as a non-zero residual.
    for (std::size_t i = 0; i < kSize; ++i) {
        buf[i] = static_cast<unsigned char>(0xA5);
    }

    // secure_zero expects a non-volatile pointer; pass via const_cast to
    // strip the volatile qualifier. buf already decays implicitly from
    // `volatile unsigned char[kSize]` to `volatile unsigned char*`, so no
    // intermediate static_cast is needed. The volatile declaration on buf
    // already ensures the compiler cannot cache element values across the
    // call boundary.
    httpserver::detail::secure_zero(const_cast<unsigned char*>(buf), kSize);

    // Each load from buf[i] is an observable side effect (buf is
    // volatile). The compiler must read from memory and cannot substitute
    // the pre-zeroing constant. Stop at the first mismatch and report its
    // index rather than emitting up to kSize identical failures.
    for (std::size_t i = 0; i < kSize; ++i) {
        if (buf[i] != 0) {
            LT_FAIL("secure_zero left non-zero byte at index " + std::to_string(i));
            break;
        }
    }
LT_END_AUTO_TEST(writes_zero_bytes_to_buffer)

LT_BEGIN_AUTO_TEST(secure_zero_suite, zero_sized_input_is_safe)
    // buf is volatile so the post-call read-back below is a genuine memory
    // round-trip rather than a value the compiler could constant-fold away.
    volatile unsigned char buf[1] = {0x55};
    httpserver::detail::secure_zero(const_cast<unsigned char*>(buf), 0);
    LT_CHECK_EQ(static_cast<unsigned>(buf[0]), 0x55u);
LT_END_AUTO_TEST(zero_sized_input_is_safe)

LT_BEGIN_AUTO_TEST(secure_zero_suite, nullptr_with_zero_size_is_safe)
    // Documented contract: secure_zero(nullptr, 0) is a no-op. This test
    // verifies both that the call completes without crashing or invoking
    // undefined behaviour, AND that adjacent memory is left unmolested (see
    // the verification loop below) -- not "successful completion" alone.
    unsigned char adjacent[4] = {0xCC, 0xCC, 0xCC, 0xCC};
    httpserver::detail::secure_zero(nullptr, 0);
    // If secure_zero(nullptr,0) wrote anything at all, the adjacent
    // sentinel would be overwritten. Verify it is unmolested.
    for (std::size_t i = 0; i < 4; ++i) {
        LT_CHECK_EQ(static_cast<unsigned>(adjacent[i]), 0xCCu);
    }
LT_END_AUTO_TEST(nullptr_with_zero_size_is_safe)

// TASK-068 finding-34: pin that secure_zero() zeroes the FULL requested
// length, not just a prefix. A buggy implementation that stops early (e.g.
// zeroes only the first N/2 bytes) would pass the other tests in this file
// (which prefill uniformly) but fail here, where the two halves start with
// different sentinel values.
LT_BEGIN_AUTO_TEST(secure_zero_suite, zeroes_full_length_not_just_prefix)
    constexpr std::size_t kSize = 256;
    constexpr std::size_t kHalf = kSize / 2;
    volatile unsigned char buf[kSize];
    for (std::size_t i = 0; i < kHalf; ++i) {
        buf[i] = static_cast<unsigned char>(0xA5);
    }
    for (std::size_t i = kHalf; i < kSize; ++i) {
        buf[i] = static_cast<unsigned char>(0x5A);
    }

    httpserver::detail::secure_zero(const_cast<unsigned char*>(buf), kSize);

    for (std::size_t i = 0; i < kSize; ++i) {
        if (buf[i] != 0) {
            LT_FAIL("secure_zero left non-zero byte at index " + std::to_string(i));
            break;
        }
    }
LT_END_AUTO_TEST(zeroes_full_length_not_just_prefix)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
