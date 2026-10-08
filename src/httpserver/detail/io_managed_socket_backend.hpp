/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "io_managed_socket_backend.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_MANAGED_SOCKET_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_MANAGED_SOCKET_BACKEND_HPP_

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <httpserver/detail/io_socket_backend.hpp>
#include <httpserver/detail/io_udp_backend.hpp>

namespace httpserver {
namespace detail {

// Registry ownership and nonblocking socket steps shared by managed drivers.
// mu_ holds the socket/buffer lease across each syscall; terminal delivery is
// always outside that lock. Selected operations stay cancellable in pending_.
// Kernel registrations remain the responsibility of each concrete driver.
class io_managed_socket_backend : public io_socket_backend {
 public:
    ~io_managed_socket_backend() override;
    void adopt_datagram(std::uint64_t id, pollsys::native_socket_t socket) override;
    void adopt_listener(std::uint64_t id, pollsys::native_socket_t socket) override;
    void adopt_connection(std::uint64_t id, pollsys::native_socket_t socket) override;
    void release_connection(std::uint64_t id) override;
    pollsys::native_socket_t native_handle(std::uint64_t id) const override;
    http::outcome ready() const override;
    std::size_t close() override;
    std::size_t wake() override;
    std::size_t pending_count() const;
    std::uint64_t poll_iterations() const;
    void submit(op_state& op) override;
    http::outcome_code request_cancel(op_state& target) override;
    void activate_external() override { }
    server::interest_snapshot interests() const override { return {}; }
    http::outcome dispatch(std::span<const server::readiness_event>,
                           std::chrono::steady_clock::time_point) override;

 protected:
    struct registration {
        registration(pollsys::native_socket_t fd, std::uint64_t identity, bool is_listener)
            : socket(fd), token(identity), listener(is_listener) { }
        ~registration() { pollsys::close_socket(socket); }
        pollsys::native_socket_t socket;
        std::uint64_t token;
        bool listener;
        std::uint32_t installed = 0;
        std::uint32_t armed = 0;
        std::uint32_t mask = 0;
        bool datagram = false;
        udp_admission udp;
        bool write_closed = false;
    };
    using completion = std::pair<std::shared_ptr<op_state>, io_result>;
    using completions = std::vector<completion>;
    static void deliver(const completions& done);
    void finish_locked(const std::shared_ptr<op_state>& op, io_result result, completions& done);
    void retire_locked(std::uint64_t id, completions& done);
    void adopt_locked(std::uint64_t id, pollsys::native_socket_t socket, bool listener, bool datagram = false);
    void notify_locked();
    virtual std::optional<std::chrono::steady_clock::time_point> reconcile_locked() = 0;
    virtual void run_loop() = 0;
    void run_managed();
    int prepare_wait();
    void finish_wait();
    void expire_timers();
    void dispatch_ready(std::uint64_t token, std::uint32_t directions, std::uint32_t disabled);
    std::optional<std::chrono::steady_clock::time_point> scan_interest_locked(
        std::unordered_map<std::uint64_t, std::uint32_t>& masks) const;
    static bool wants_event(const op_state& op, std::uint32_t events);
    std::vector<std::shared_ptr<op_state>> collect_event_locked(std::uint64_t id, std::uint32_t events) const;
    void dispatch_batch(std::uint64_t token, const std::vector<std::shared_ptr<op_state>>& batch);
    bool accept_locked(const std::shared_ptr<op_state>& op,
                       const std::shared_ptr<registration>& lease, completions& done);
    std::pair<std::size_t, bool> transfer_stream(pollsys::native_socket_t socket, const op_state& op);
    bool step_locked(const std::shared_ptr<op_state>& op,
                     const std::shared_ptr<registration>& lease, completions& done);
    void complete_stream_locked(const std::shared_ptr<op_state>& op,
        const std::shared_ptr<registration>& lease, std::pair<std::size_t, bool> result, completions& done);
    std::size_t shutdown();
    static thread_local io_managed_socket_backend* current_driver_;
    std::mutex join_mu_;
    mutable std::mutex mu_;
    std::unordered_map<op_state*, std::shared_ptr<op_state>> pending_;
    std::unordered_map<std::uint64_t, std::shared_ptr<registration>> connections_;
    std::unordered_map<std::uint64_t, std::uint64_t> tokens_;
    std::uint64_t next_token_ = 2;  // wake registration uses token 1
    std::uint64_t next_connection_ = 1;
    std::uint64_t next_sequence_ = 1;
    std::uint64_t iterations_ = 0;
    bool closed_ = false;
    bool wake_pending_ = false;
    bool wake_failed_ = false;
    pollsys::wake_source wake_;
    std::thread thread_;
    using read_call = pollsys::sys_result (*)(pollsys::native_socket_t, std::byte*, std::size_t);
    read_call read_ = &pollsys::read_some;
};

}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_IO_MANAGED_SOCKET_BACKEND_HPP_
