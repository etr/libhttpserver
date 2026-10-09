/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_FUZZ_QUIC_TLS_CALLBACKS_FUZZ_HPP_
#define TEST_FUZZ_QUIC_TLS_CALLBACKS_FUZZ_HPP_
#include "fuzz/quic_event_fuzz.hpp"
quic_fuzz::report quic_tls_callbacks_fuzz_input(std::span<const std::uint8_t> input);
#endif  // TEST_FUZZ_QUIC_TLS_CALLBACKS_FUZZ_HPP_
