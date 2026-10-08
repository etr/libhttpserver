/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_FUZZ_HTTP2_ENGINE_FUZZ_HPP_
#define TEST_FUZZ_HTTP2_ENGINE_FUZZ_HPP_
#include <cstdint>
#include <span>
void http2_engine_fuzz_input(std::span<const std::uint8_t> bytes);
#endif  // TEST_FUZZ_HTTP2_ENGINE_FUZZ_HPP_
