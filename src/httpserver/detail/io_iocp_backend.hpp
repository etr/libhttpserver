/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "io_iocp_backend.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_IOCP_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_IOCP_BACKEND_HPP_

#if defined(_WIN32)
#include <httpserver/detail/io_socket_backend.hpp>
#include <httpserver/detail/io_udp_backend.hpp>
#include <windows.h>
#include <mswsock.h>
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <httpserver/detail/io_completion_storage.hpp>

namespace httpserver {
namespace detail {

// Completion ownership is separate from logical pending ownership. CancelIoEx
// and closesocket do not retire OVERLAPPED memory: only a consumed packet does.
class io_iocp_backend final : public io_socket_backend {
 public:
    io_iocp_backend();
    ~io_iocp_backend() override;
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

 private:
    friend struct io_iocp_test_access;
    struct registration {
        ~registration() { pollsys::close_socket(socket); }
        pollsys::native_socket_t socket = pollsys::k_invalid_socket;
        ULONG_PTR token = 0;
        bool listener = false;
        bool datagram = false;
        bool write_closed = false;
        WSAPROTOCOL_INFOW protocol{};
        LPFN_ACCEPTEX accept = nullptr;
        LPFN_WSARECVMSG receive_datagram = nullptr;
    };
    struct native_request {
        ~native_request() { pollsys::close_socket(candidate); }
        OVERLAPPED overlapped{};
        std::shared_ptr<op_state> op;
        std::shared_ptr<registration> socket;
        std::optional<owned_completion_storage> storage;
        std::shared_ptr<io_datagram> datagram;
        sockaddr_storage datagram_peer{};
        int datagram_peer_length = sizeof(sockaddr_storage);
        std::byte zero_capacity_probe{};
        WSABUF buffer{};
        WSAMSG message{};
        alignas(WSACMSGHDR) std::array<char, 256> control{};
        DWORD flags = 0;
        DWORD bytes = 0;
        std::array<char, 2 * (sizeof(sockaddr_storage) + 16)> addresses{};
        pollsys::native_socket_t candidate = pollsys::k_invalid_socket;
    };
    using completions = std::vector<std::pair<std::shared_ptr<op_state>, io_result>>;
    http::outcome_code admit_udp_locked(const std::shared_ptr<op_state>& op);
    bool adopt_locked(std::uint64_t id, pollsys::native_socket_t socket, bool listener, bool datagram = false);
    void retire_locked(std::uint64_t id, completions& done);
    void notify_locked();
    void finish_locked(const std::shared_ptr<op_state>& op, io_result result, completions& done);
    http::outcome_code cancel_locked(op_state& target, completions& done);
    void deliver(const completions& done);
    void post_locked(const std::shared_ptr<op_state>& op, completions& done);
    void packet_locked(OVERLAPPED* address, ULONG_PTR token, DWORD bytes, DWORD error, completions& done);
    int prepare_locked(completions& done);
    std::size_t shutdown();
    void run_loop();
    static thread_local io_iocp_backend* current_driver_;
    static thread_local io_iocp_backend* current_delivery_;
    mutable std::mutex mu_;
    std::mutex join_mu_;
    std::condition_variable delivery_cv_;
    std::unordered_map<op_state*, std::shared_ptr<op_state>> pending_;
    std::unordered_map<OVERLAPPED*, std::unique_ptr<native_request>> outstanding_;
    std::unordered_map<op_state*, OVERLAPPED*> posted_;
    std::unordered_map<std::uint64_t, std::shared_ptr<registration>> connections_;
    ULONG_PTR next_token_ = 2;  // token 1 is the control doorbell
    std::uint64_t next_connection_ = 1;
    std::uint64_t next_sequence_ = 1;
    std::uint64_t iterations_ = 0;
    std::size_t deliveries_ = 0;
    bool closed_ = false;
    bool failed_ = false;
    bool wake_pending_ = false;
    bool winsock_held_ = false;
    HANDLE port_ = nullptr;
    std::thread thread_;
    // A native wait seam lets private tests gate real dequeued packets without
    // replacing the backend or its lifetime logic.
    decltype(&::GetQueuedCompletionStatus) wait_ = &::GetQueuedCompletionStatus;
};

}  // namespace detail
}  // namespace httpserver
#endif  // defined(_WIN32)
#endif  // SRC_HTTPSERVER_DETAIL_IO_IOCP_BACKEND_HPP_
