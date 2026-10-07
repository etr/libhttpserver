/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "io_epoll_backend.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_EPOLL_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_EPOLL_BACKEND_HPP_
#if defined(__linux__)
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <httpserver/detail/io_socket_backend.hpp>

namespace httpserver {
namespace detail {

// One managed driver, persistent ET/one-shot registrations. Socket syscalls
// are nonblocking and serialized with cancel/release under mu_: cancellation
// cannot return while the driver still accesses a caller-owned buffer. Selected
// operations remain in pending_ until the terminal claim is decided. Delivery
// to owners always occurs after releasing mu_. Tokens identify incarnations,
// never fds or pointers; late events cannot address a replacement registration.
class io_epoll_backend final : public io_socket_backend {
 public:
    io_epoll_backend();
    ~io_epoll_backend() override;
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

 private:
    struct registration {
        registration(pollsys::native_socket_t fd, std::uint64_t identity, bool is_listener)
            : socket(fd), token(identity), listener(is_listener) { }
        ~registration() { pollsys::close_socket(socket); }
        pollsys::native_socket_t socket;
        std::uint64_t token;
        bool listener;
        bool installed = false;
        bool armed = false;
        std::uint32_t mask = 0;
    };
    using completion = std::pair<std::shared_ptr<op_state>, io_result>;
    using completions = std::vector<completion>;
    static void deliver(const completions& done);
    void finish_locked(const std::shared_ptr<op_state>& op, io_result result,
                       completions& done);
    void retire_locked(std::uint64_t id, completions& done);
    void adopt_locked(std::uint64_t id, pollsys::native_socket_t socket, bool listener);
    void notify_locked();
    std::optional<std::chrono::steady_clock::time_point> reconcile_locked();
    void rearm_locked(registration& record, std::uint32_t mask);
    std::optional<std::chrono::steady_clock::time_point> scan_interest_locked(
        std::unordered_map<std::uint64_t, std::uint32_t>& masks) const;
    static bool wants_event(const op_state& op, std::uint32_t events);
    std::vector<std::shared_ptr<op_state>> collect_event_locked(
        std::uint64_t id, std::uint32_t events) const;
    void dispatch_event(std::uint64_t token, std::uint32_t events);
    void dispatch_batch(std::uint64_t token,
                        const std::vector<std::shared_ptr<op_state>>& batch);
    bool accept_locked(const std::shared_ptr<op_state>& op,
                       const std::shared_ptr<registration>& lease, completions& done);
    static std::pair<std::size_t, bool> transfer_stream(
        pollsys::native_socket_t socket, const op_state& op);
    // Returns false when the head would-block; the rest retain registry ownership.
    bool step_locked(const std::shared_ptr<op_state>& op,
                     const std::shared_ptr<registration>& lease, completions& done);
    std::size_t shutdown();
    void run_loop();
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
    int epoll_ = -1;
    std::thread thread_;
};

}  // namespace detail
}  // namespace httpserver
#endif  // defined(__linux__)
#endif  // SRC_HTTPSERVER_DETAIL_IO_EPOLL_BACKEND_HPP_
