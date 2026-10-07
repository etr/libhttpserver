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
#include <stop_token>
#include <httpserver/detail/tls_io_backend.hpp>
#include <httpserver/detail/tls_psk_runtime.hpp>
namespace httpserver::detail {
// Bounded BIO pair. The pump transfers exclusive access to a handshake worker;
// no SSL/BIO access is permitted on the owner until that step retires.
class tls_session final {
 public:
    enum class progress { complete, input, output, eof, failed };
    struct result {
        progress state;
        std::size_t bytes = 0;
        http::outcome_code failure = http::outcome_code::protocol_error;
    };
    tls_session(std::shared_ptr<const tls_context> context, bool server, tls_handshake_context handshake = {});
    tls_session(tls_credentials_selection selection, bool server, tls_handshake_context handshake = {});
    ~tls_session();
    // Serialized pump access only. Null before success or after failed auth.
    std::shared_ptr<const server::tls_peer_metadata> peer_metadata() const;
    // Unknown until a successful authenticated handshake; owned protocol value.
    tls_negotiated_protocol negotiated_protocol() const;
    result handshake();
    std::shared_ptr<tls_psk_runtime> handshake_runtime() const;
    void handshake_limits(std::chrono::steady_clock::time_point deadline, std::stop_token cancellation);
    result read(std::span<std::byte> buffer);
    result write(std::span<const std::byte> bytes);
    result shutdown();
    std::size_t input_capacity() const;
    bool feed(std::span<const std::byte> bytes);
    std::size_t drain(std::span<std::byte> bytes);
    bool output_pending() const;

 private:
    friend class tls_context;
    struct impl;
    std::unique_ptr<impl> impl_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_SESSION_HPP_
