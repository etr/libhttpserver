/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "tls_psk_attempt.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_PSK_ATTEMPT_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_PSK_ATTEMPT_HPP_
#include <memory>
#include <httpserver/detail/tls_psk.hpp>
namespace httpserver::detail {
// Bounded per-attempt coalescing only; destroying the attempt wipes its keys.
class tls_psk_attempt final {
 public:
    tls_psk_attempt(tls_psk_config config, psk_handshake_context context);
    ~tls_psk_attempt();
    psk_lookup_result lookup(psk_tls_version version, std::span<const std::byte> identity, std::size_t capacity);
 private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_PSK_ATTEMPT_HPP_
