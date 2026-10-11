/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_udp_listener.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_UDP_LISTENER_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_UDP_LISTENER_HPP_
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/http2_dispatch.hpp>
#include <httpserver/detail/io_socket_backend.hpp>
#include <httpserver/detail/quic_network_connection.hpp>
#include <httpserver/detail/tls_credentials_access.hpp>
namespace httpserver::detail {
class quic_udp_listener final : public std::enable_shared_from_this<quic_udp_listener> {
 public:
    quic_udp_listener(io_socket_backend& backend, executor& workers, const server::route_registry& routes,
        const server::hook_bus& hooks, const server::peer_policy& peers, server::resource_budget budget, connection_engine_config config);
    http::outcome listen(const server::listener_options& endpoint, std::size_t index, std::shared_ptr<const tls_credentials_binding> binding);
    void activate();
    void request_stop(http::outcome_code reason = http::outcome_code::connection_closed) noexcept;
    void quiesce(std::chrono::steady_clock::time_point deadline);
    std::uint16_t bound_port() const { return port_; }
    bool retired() const { return retired_.load(); }
    void await_progress(std::chrono::milliseconds maximum);

 private:
    struct record;
    static task<void> reader(std::shared_ptr<quic_udp_listener>);
    static task<void> writer(std::shared_ptr<quic_udp_listener>);
    static task<void> watchdog(std::shared_ptr<quic_udp_listener>);
    void notify();
    void pump();
    void update_connections(std::chrono::steady_clock::time_point, std::optional<std::chrono::steady_clock::time_point>);
    void finish_admission(quic_server_admission::pending_handle, const std::shared_ptr<record>&, bool accepted);
    struct pending_output {
        std::shared_ptr<quic_admission_reply> reply;
        std::shared_ptr<quic_network_connection> owner;
        std::optional<quic_transmission> packet;
    };
    pending_output next_output();
    void receive(std::shared_ptr<const io_datagram>);
    void admit(quic_server_admission::pending_handle handle);
    void loop_finished();
    std::optional<std::chrono::steady_clock::time_point> deadline() const;
    io_socket_backend& backend_;
    executor& workers_;
    const server::route_registry& routes_;
    const server::hook_bus& hooks_;
    const server::peer_policy& peers_;
    server::resource_budget budget_;
    connection_engine_config config_;
    std::shared_ptr<http2_dispatch> wire_;
    io_connection_owner io_;
    std::unique_ptr<quic_server_admission> admission_;
    server::listener_options endpoint_;
    std::shared_ptr<const tls_credentials_binding> binding_;
    std::uint64_t id_ = 0, next_connection_ = 0, sending_id_ = 0;
    std::uint16_t port_ = 0;
    struct record {
        std::shared_ptr<quic_network_connection> connection;
        server::resource_budget budget;
        server::reservation seat, timer;
        std::optional<drain_scope::unit> handshake;
    };
    std::map<std::uint64_t, std::shared_ptr<record>> connections_;
    std::deque<std::shared_ptr<quic_admission_reply>> replies_;
    std::shared_ptr<op_state> timer_;
    std::optional<std::chrono::steady_clock::time_point> timer_deadline_;
    server::reservation storage_, timer_charge_;
    std::atomic<bool> stop_requested_{false}, retired_{false}, notified_{false};
    std::atomic<unsigned> admissions_{0};
    unsigned loops_ = 0;
    bool activated_ = false, transport_released_ = false;
    mutable std::mutex control_;
    std::condition_variable changed_;
    std::optional<std::chrono::steady_clock::time_point> draining_;
    std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();
    std::uint64_t seconds() const;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_UDP_LISTENER_HPP_
