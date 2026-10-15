/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "tls_psk_state.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_PSK_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_PSK_STATE_HPP_
#include <memory>
#include "config.h"
#include <httpserver/server/tls_credentials.hpp>
#include <httpserver/detail/tls_psk.hpp>
#ifdef NATIVE_V3_TLS
#include <httpserver/detail/tls_psk_runtime.hpp>
#endif
namespace httpserver::server {
class tls_psk_key::impl {
 public:
    explicit impl(std::span<const std::byte> bytes) : value(bytes) {}
    httpserver::detail::secure_bytes value;
};
class tls_psk_runtime::impl {
 public:
#ifdef NATIVE_V3_TLS
    std::shared_ptr<httpserver::detail::tls_psk_runtime> runtime;
#endif
};
}  // namespace httpserver::server
#ifdef NATIVE_V3_TLS
namespace httpserver::detail {
extern thread_local const server::tls_psk_runtime* current_psk_lookup;
}  // namespace httpserver::detail
#endif
#endif  // SRC_HTTPSERVER_DETAIL_TLS_PSK_STATE_HPP_
