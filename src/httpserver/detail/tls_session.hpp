/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#if !defined(HTTPSERVER_COMPILATION)
#error "tls_session.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_SESSION_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_SESSION_HPP_
#include <cstddef>
#include <memory>
#include <span>
#include <httpserver/detail/tls_io_backend.hpp>
namespace httpserver::detail {
// Bounded BIO pair; only the adapter's serialized pump may call these methods.
class tls_session final {
 public:
    enum class progress { complete, input, output, eof, failed };
    struct result {
        progress state;
        std::size_t bytes = 0;
    };
    tls_session(std::shared_ptr<tls_context> context, bool server);
    ~tls_session();
    result handshake();
    result read(std::span<std::byte> buffer);
    result write(std::span<const std::byte> bytes);
    result shutdown();
    std::size_t input_capacity() const;
    bool feed(std::span<const std::byte> bytes);
    std::size_t drain(std::span<std::byte> bytes);
    bool output_pending() const;

 private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_SESSION_HPP_
