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
#include <chrono>
#include <memory>
#include <string_view>
#include <httpserver/detail/io_operation.hpp>
namespace httpserver::detail {
// Contexts are immutable after construction. No provider types escape this seam.
struct tls_credentials_selection;
class tls_context {
 public:
    static std::shared_ptr<tls_context> client();
    static std::shared_ptr<tls_context> server_pem(std::string_view certificate, std::string_view key, std::string_view roots = {});

 private:
    friend class tls_session;
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
    tls_io_backend(io_backend& raw, executor& ex, std::uint64_t connection, std::shared_ptr<const tls_context> context, bool server);
    tls_io_backend(io_backend& raw, executor& ex, std::uint64_t connection, tls_credentials_selection selection, bool server);
    ~tls_io_backend() override;
    void submit(op_state& op) override;
    // ok accepts a cancellation event; the operation result decides races.
    // Publication waits for the pump to stop touching borrowed storage.
    http::outcome_code request_cancel(op_state& op) override;
    void close();

 private:
    struct core;
    std::shared_ptr<core> core_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_IO_BACKEND_HPP_
