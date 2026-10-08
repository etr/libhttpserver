/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "io_udp_backend.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_UDP_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_UDP_BACKEND_HPP_
#include <memory>
#include <optional>
#include <vector>
#include <httpserver/detail/io_operation.hpp>
#include <httpserver/detail/io_poll_sys.hpp>
#if defined(_WIN32)
#include <mswsock.h>
#endif
namespace httpserver {
namespace detail {
// Guarded by the backend registry mutex. Weak records include detached poll
// batches; terminal entries release their reservation exactly once at pruning.
class udp_admission {
 public:
    http::outcome_code admit(const std::shared_ptr<op_state>& op);
 private:
    std::vector<std::weak_ptr<op_state>> admitted_;
};
void prepare_datagram_socket(pollsys::native_socket_t socket);
bool encode_datagram_endpoint(const datagram_endpoint& endpoint, sockaddr_storage& address, int& length);
void capture_datagram_metadata(pollsys::native_socket_t socket, io_datagram& packet,
                               const sockaddr_storage& peer, std::uint64_t id);
#if defined(_WIN32)
LPFN_WSARECVMSG datagram_receive_api(pollsys::native_socket_t socket);
void capture_datagram_control(io_datagram& packet, WSAMSG& message, std::uint16_t port);
#endif
// nullopt means would-block. Every other result concerns precisely one packet.
std::optional<io_result> datagram_step(pollsys::native_socket_t socket, const op_state& op);
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_IO_UDP_BACKEND_HPP_
