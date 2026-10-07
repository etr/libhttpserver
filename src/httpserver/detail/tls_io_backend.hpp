/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#if !defined(HTTPSERVER_COMPILATION)
#error "tls_io_backend.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_IO_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_IO_BACKEND_HPP_
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <httpserver/detail/io_operation.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/tls_peer_metadata.hpp>
namespace httpserver::detail {
// Contexts are immutable after construction. No provider types escape this seam.
enum class tls_negotiated_protocol { unknown, none, http1, h2, other };
enum class tls_transport { unknown, tcp, quic };
// Trusted listener metadata; unknown transport/port cannot serve challenges.
struct tls_handshake_context {
    tls_transport transport = tls_transport::unknown;
    std::uint16_t local_port = 0;
};
struct tls_credentials_selection;
struct tls_psk_config;
struct tls_acme_challenge;
class tls_context {
 public:
    static std::shared_ptr<tls_context> client();
    static std::shared_ptr<tls_context> server_acme(const tls_acme_challenge& input);
    static std::shared_ptr<tls_context> server_psk(const tls_psk_config& config);
    static std::shared_ptr<tls_context> server_pem(std::string_view certificate, std::string_view key, std::string_view roots = {},
        server::tls_client_certificate_mode mode = server::tls_client_certificate_mode::none);

 private:
    friend class tls_session;
    std::shared_ptr<const tls_psk_config> psk_;
    void configure_server();
    std::array<unsigned char, 32> session_namespace_{};
    std::shared_ptr<void> native_;
};
class tls_handshake_operation final : public op_handle {
 public:
    tls_handshake_operation(io_connection_owner& owner, std::uint64_t connection, std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max())
        : op_handle(std::make_shared<op_state>(io_op_kind::tls_handshake, &owner, connection, timer_payload{deadline})) {}
};
class tls_shutdown_operation final : public op_handle {
 public:
    tls_shutdown_operation(io_connection_owner& owner, std::uint64_t connection, std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max())
        : op_handle(std::make_shared<op_state>(io_op_kind::tls_shutdown, &owner, connection, timer_payload{deadline})) {}
};
// raw backend and executor outlive this adapter AND its retiring child completions.
// One adapter owns one connection. Cancellation/timeout abort the entire session.
class tls_io_backend final : public io_backend {
 public:
    tls_io_backend(io_backend& raw, executor& ex, std::uint64_t connection, std::shared_ptr<const tls_context> context, bool server, tls_handshake_context handshake = {});
    tls_io_backend(io_backend& raw, executor& ex, std::uint64_t connection, tls_credentials_selection selection, bool server, tls_handshake_context handshake = {});
    ~tls_io_backend() override;
    void submit(op_state& op) override;
    // ok accepts a cancellation event; the operation result decides races.
    // Publication waits for the pump to stop touching borrowed storage.
    http::outcome_code request_cancel(op_state& op) override;
    void close();
    // Thread-safe immutable publication, visible before handshake completion.
    // A retained value survives adapter teardown and credential replacement.
    std::shared_ptr<const server::tls_peer_metadata> peer_metadata() const;
    // Published before successful completion; unknown on a failed handshake.
    tls_negotiated_protocol negotiated_protocol() const;

 private:
    struct core;
    std::shared_ptr<core> core_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_IO_BACKEND_HPP_
