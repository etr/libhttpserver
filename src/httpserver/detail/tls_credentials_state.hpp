/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "tls_credentials_state.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_STATE_HPP_
#include "config.h"
#include <httpserver/server/tls_credentials.hpp>
#ifdef NATIVE_V3_TLS
#include <httpserver/detail/tls_credentials.hpp>
#endif
namespace httpserver::server {
class tls_credentials::impl {
 public:
#ifdef NATIVE_V3_TLS
    httpserver::detail::tls_credentials_registry registry;
#endif
};
}  // namespace httpserver::server
#endif  // SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_STATE_HPP_
