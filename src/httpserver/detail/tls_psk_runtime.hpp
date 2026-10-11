/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "tls_psk_runtime.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_PSK_RUNTIME_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_PSK_RUNTIME_HPP_
#include <chrono>
#include <memory>
#include <functional>
#include <thread>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/detail/tls_psk.hpp>
#include <httpserver/http/outcome.hpp>
namespace httpserver::detail {
struct tls_psk_runtime_options {
    std::size_t handshake_workers = 2;
    std::size_t handshake_queue = 16;
    std::size_t lookup_workers = 2;
    std::size_t lookup_queue = 16;
    std::chrono::milliseconds handshake_timeout{5000};
};
// A fixed provider-only observer owns native handshake worker retirement.
// Application lookup workers stay detached. stop/destruction never join;
// drain preserves its deadline, including native provider TLS teardown.
class tls_psk_runtime final {
 public:
    using thread_start = std::function<std::thread(executor::handler)>;
    explicit tls_psk_runtime(tls_psk_runtime_options options = {}, thread_start start = {});
    ~tls_psk_runtime();
    bool accepting() const;
    std::chrono::milliseconds handshake_timeout() const;
    // Completion runs on the worker after work and admission retirement. The
    // worker stays live through completion, preserving stop/drain obligations.
    http::outcome_code submit_handshake(executor::handler work, executor::handler completed = {});
    psk_lookup_result lookup(psk_lookup callback, std::span<const std::byte> identity, psk_handshake_context context);
    void stop();
    bool drain(std::chrono::steady_clock::time_point deadline);
 private:
    struct impl;
    std::shared_ptr<impl> impl_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_PSK_RUNTIME_HPP_
