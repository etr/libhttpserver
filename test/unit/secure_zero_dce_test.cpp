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
// secure_zero() against a small stack buffer and then reads every byte
// through a `volatile` sink so the compiler cannot speculate the writes
// away. The TU is compiled at -O2 -DNDEBUG so the release-mode optimizer
// sees the post-write `volatile` reads.
//
// Note: the load-bearing argument is in the header itself
// (`asm __volatile__("" ::: "memory")` + volatile pointer writes in the
// fallback). This test is a regression sentinel: if a future toolchain
// elides the writes, the post-write reads here will observe non-zero
// bytes and the assertion fires.

#include "httpserver/detail/secure_zero.hpp"

#include <cstddef>
#include <cstdint>

#include "./littletest.hpp"

LT_BEGIN_SUITE(secure_zero_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(secure_zero_suite)

LT_BEGIN_AUTO_TEST(secure_zero_suite, writes_zero_bytes_to_buffer)
    constexpr std::size_t kSize = 256;
    unsigned char buf[kSize];
    // Prefill with a non-zero sentinel so a no-op would be detected.
    for (std::size_t i = 0; i < kSize; ++i) {
        buf[i] = static_cast<unsigned char>(0xA5);
    }

    httpserver::detail::secure_zero(buf, kSize);

    // Read every byte through a volatile sink. Each load is a visible
    // side effect, so the compiler must materialize the reads and -- by
    // the as-if rule -- must have materialized the secure_zero writes
    // before them.
    volatile unsigned char sink = 0xFF;
    for (std::size_t i = 0; i < kSize; ++i) {
        sink = buf[i];
        LT_CHECK_EQ(static_cast<unsigned>(sink), 0u);
    }
    // Defeat "unused variable" diagnostics without observable effects.
    (void)sink;
LT_END_AUTO_TEST(writes_zero_bytes_to_buffer)

LT_BEGIN_AUTO_TEST(secure_zero_suite, zero_sized_input_is_safe)
    unsigned char buf[1] = {0x55};
    httpserver::detail::secure_zero(buf, 0);
    LT_CHECK_EQ(static_cast<unsigned>(buf[0]), 0x55u);
LT_END_AUTO_TEST(zero_sized_input_is_safe)

LT_BEGIN_AUTO_TEST(secure_zero_suite, nullptr_with_zero_size_is_safe)
    // Documented contract: secure_zero(nullptr, 0) is a no-op.
    httpserver::detail::secure_zero(nullptr, 0);
    LT_CHECK(true);
LT_END_AUTO_TEST(nullptr_with_zero_size_is_safe)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
