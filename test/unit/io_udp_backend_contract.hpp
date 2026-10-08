/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_IO_UDP_BACKEND_CONTRACT_HPP_
#define TEST_UNIT_IO_UDP_BACKEND_CONTRACT_HPP_
#include <chrono>
#include <thread>
#include <vector>
#include <memory>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/io_kqueue_backend.hpp>
#include <httpserver/detail/io_epoll_backend.hpp>
#include <httpserver/detail/io_iocp_backend.hpp>

namespace io_udp_contract {
namespace hd = httpserver::detail;
namespace ps = hd::pollsys;
namespace hh = httpserver::http;
inline ps::native_socket_t udp_socket(int family = AF_INET, bool wildcard = false) {
#if defined(_WIN32)
    ps::ensure_winsock();
#endif
    auto socket = ::socket(family, SOCK_DGRAM, 0);
    sockaddr_storage address{};
    ps::fill_listener_address(family == AF_INET ? (wildcard ? "0.0.0.0" : "127.0.0.1")
        : (wildcard ? "::" : "::1"), 0, address);
    if (::bind(socket, reinterpret_cast<sockaddr*>(&address),
        family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6)) != 0) {
        ps::close_socket(socket);
        return ps::k_invalid_socket;
    }
    ps::set_nonblocking(socket, true);
    return socket;
}
inline hd::datagram_endpoint endpoint(ps::native_socket_t socket) {
    sockaddr_storage address{};
#if defined(_WIN32)
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    ::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length);
    hd::datagram_endpoint result;
    ps::fill_peer(address, result.peer);
    return result;
}
template<class Backend>
bool udp_contract(int family = AF_INET) {
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    Backend backend;
    auto a = udp_socket(family), b = udp_socket(family);
    if (a == ps::k_invalid_socket || b == ps::k_invalid_socket) return false;
    const auto peer = endpoint(b), local = endpoint(a);
    backend.adopt_datagram(1, a);
    backend.adopt_datagram(2, b);
    auto wait = [&](const auto& state) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!state->applied() && std::chrono::steady_clock::now() < deadline) {
            ex.run_pending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return state->applied();
    };
    bool okay = true;
    std::vector<std::byte> bytes(3, std::byte{42});
    for (auto size : {std::size_t{3}, std::size_t{0}, std::size_t{3}}) {
        std::shared_ptr<hd::op_state> received;
        {
            hd::udp_receive_operation receive(owner, 2, 3);
            received = receive.state();
            receive.submit(backend);
        }
        {
            hd::udp_send_operation send(owner, 1, std::span(bytes).first(size), peer);
            bytes[0] = std::byte{99};
            send.submit(backend);
            bytes[0] = std::byte{42};
            okay &= wait(send.state()) && send.state()->stored_result().code == hh::outcome_code::ok;
        }
        okay &= wait(received);
        if (!received->applied()) break;
        auto result = received->stored_result();
        okay &= result.code == hh::outcome_code::ok && result.transferred == size;
        okay &= result.datagram && result.datagram->bytes.size() == size;
        if (result.datagram) {
            if (size != 0) okay &= result.datagram->bytes.front() == std::byte{42};
            okay &= result.datagram->peer.peer == local.peer && result.datagram->socket_id == 2;
            okay &= result.datagram->local && result.datagram->local->peer == peer.peer;
            okay &= result.datagram->received_at != std::chrono::steady_clock::time_point{};
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__linux__)
            okay &= result.datagram->interface_index.has_value();
#endif
        }
    }
    hd::udp_receive_operation zero_capacity(owner, 2, 0);
    hd::udp_send_operation zero_send(owner, 1, {}, peer);
    zero_capacity.submit(backend);
    zero_send.submit(backend);
    okay &= wait(zero_capacity.state()) && zero_capacity.state()->stored_result().code == hh::outcome_code::ok;
    hd::udp_receive_operation zero_truncated(owner, 2, 0);
    hd::udp_send_operation nonempty_send(owner, 1, std::span(bytes).first(1), peer);
    zero_truncated.submit(backend);
    nonempty_send.submit(backend);
    okay &= wait(zero_truncated.state()) && zero_truncated.state()->stored_result().code == hh::outcome_code::limit_exceeded;
    hd::udp_receive_operation first(owner, 2, 3), second(owner, 2, 3);
    first.submit(backend);
    second.submit(backend);
    hd::udp_send_operation first_send(owner, 1, std::span(bytes).first(2), peer);
    hd::udp_send_operation second_send(owner, 1, std::span(bytes).first(1), peer);
    first_send.submit(backend);
    second_send.submit(backend);
    okay &= wait(first.state()) && wait(second.state());
    okay &= first.state()->stored_result().transferred == 2 && second.state()->stored_result().transferred == 1;
    hd::udp_receive_operation huge(owner, 2, hd::k_max_datagram_bytes + 1);
    huge.submit(backend);
    okay &= wait(huge.state()) && huge.state()->stored_result().code == hh::outcome_code::limit_exceeded;
    hd::udp_receive_operation truncated(owner, 2, 2);
    truncated.submit(backend);
    hd::udp_send_operation oversized(owner, 1, bytes, peer);
    oversized.submit(backend);
    okay &= wait(truncated.state()) && truncated.state()->stored_result().code == hh::outcome_code::limit_exceeded;
    hd::udp_receive_operation cancel(owner, 2, 3);
    cancel.submit(backend);
    okay &= backend.request_cancel(*cancel.state()) == hh::outcome_code::ok;
    okay &= wait(cancel.state()) && cancel.state()->stored_result().code == hh::outcome_code::cancelled;
    hd::read_operation wrong(owner, 2, bytes);
    wrong.submit(backend);
    okay &= wait(wrong.state()) && wrong.state()->stored_result().code == hh::outcome_code::invalid_state;
    hd::udp_receive_operation closed(owner, 2, 3);
    closed.submit(backend);
    backend.release_connection(2);
    okay &= wait(closed.state()) && closed.state()->stored_result().code == hh::outcome_code::connection_closed;
    backend.close();
    ex.run_pending();
    return okay;
}
}  // namespace io_udp_contract
#endif  // TEST_UNIT_IO_UDP_BACKEND_CONTRACT_HPP_
