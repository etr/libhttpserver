/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "tls_socket_backend.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_SOCKET_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_SOCKET_BACKEND_HPP_

#include <atomic>
#include <cstdint>
#include <memory>
#include <httpserver/detail/io_socket_backend.hpp>
#include <httpserver/detail/tls_io_backend.hpp>

namespace httpserver::detail {

// One accepted TCP connection's encrypted socket-owner view. Complete the
// explicit TLS handshake before starting the protocol engine. Byte I/O and TLS
// controls belong to tls_io_backend; wake/timer readiness belongs to raw.
// Cancellation follows the target's backend, so watchdog rearming never aborts
// the TLS session. Closing releases only this connection, never a shared driver.
// raw and ex outlive this view AND the TLS pump's retiring child completions.
class tls_socket_backend final : public io_socket_backend {
 public:
    tls_socket_backend(io_socket_backend& raw, executor& ex, std::uint64_t connection,
                       std::shared_ptr<const tls_context> context, tls_handshake_context handshake = {});
    ~tls_socket_backend() override;
    void submit(op_state& op) override;
    http::outcome_code request_cancel(op_state& op) override;
    http::outcome ready() const override;
    void activate_external() override;
    void adopt_connection(std::uint64_t id, pollsys::native_socket_t socket) override;
    void adopt_datagram(std::uint64_t id, pollsys::native_socket_t socket) override;
    void adopt_listener(std::uint64_t id, pollsys::native_socket_t socket) override;
    pollsys::native_socket_t native_handle(std::uint64_t id) const override;
    void release_connection(std::uint64_t id) override;
    std::size_t wake() override;
    std::size_t close() override;
    server::interest_snapshot interests() const override;
    http::outcome dispatch(std::span<const server::readiness_event> events,
                           std::chrono::steady_clock::time_point now) override;
    std::shared_ptr<const server::tls_peer_metadata> peer_metadata() const;
    tls_negotiated_protocol negotiated_protocol() const;

 private:
    io_backend& operation_backend(const op_state& op);
    io_socket_backend& raw_;
    const std::uint64_t connection_;
    tls_io_backend tls_;
    std::atomic<bool> released_{false};
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_SOCKET_BACKEND_HPP_
