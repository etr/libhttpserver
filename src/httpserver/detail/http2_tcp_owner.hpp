/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_tcp_owner.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_TCP_OWNER_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_TCP_OWNER_HPP_
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <httpserver/detail/http2_dispatch.hpp>
#include <httpserver/detail/http2_request_engine.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_socket_backend.hpp>
namespace httpserver::detail {
struct connection_engine_config;
class http2_tcp_owner final : public std::enable_shared_from_this<http2_tcp_owner> {
 public:
    http2_tcp_owner(io_socket_backend& backend, executor& workers, const server::route_registry& routes,
                    const server::resource_budget& budget, const connection_engine_config& config,
                    std::uint64_t id, net::peer_address peer, const server::peer_policy* peers,
                    const server::hook_bus& hooks, std::shared_ptr<const server::tls_peer_metadata> tls,
                    executor::handler stopped);
    void start();
    void stop(http::outcome_code reason);
    void drain(http2_connection::time_point deadline);

 private:
    void notify();
    void pump();
    void close(http::outcome_code reason);
    void loop_finished();
    void finalize_if_retired();
    std::optional<http2_connection::time_point> deadline();
    static task<void> reader(std::shared_ptr<http2_tcp_owner> self);
    static task<void> writer(std::shared_ptr<http2_tcp_owner> self);
    static task<void> watchdog(std::shared_ptr<http2_tcp_owner> self);
    io_socket_backend& backend_;
    executor& workers_;
    const server::route_registry& routes_;
    server::resource_budget budget_;
    std::shared_ptr<http2_dispatch> wire_;
    io_connection_owner io_;
    std::unique_ptr<http2_request_engine> engine_;
    http2_request_limits limits_;
    http2_native_context native_;
    request_dispatch_context dispatch_;
    std::uint64_t id_;
    net::peer_address peer_;
    executor::handler stopped_;
    std::array<std::byte, 16384> input_;
    server::reservation storage_, timer_charge_;
    std::shared_ptr<op_state> timer_;
    std::optional<http2_connection::time_point> timer_deadline_, write_anchor_;
    std::atomic<bool> notified_{false};
    std::mutex control_;
    std::optional<http2_connection::time_point> drain_requested_;
    http::outcome_code stop_requested_ = http::outcome_code::ok;
    std::size_t loops_ = 0;
    bool closed_ = false, finalized_ = false, draining_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_TCP_OWNER_HPP_
