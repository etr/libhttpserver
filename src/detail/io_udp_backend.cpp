/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <algorithm>
#include <cstring>
#include <httpserver/detail/io_udp_backend.hpp>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <net/if_dl.h>
#endif
namespace httpserver {
namespace detail {
http::outcome_code udp_admission::admit(const std::shared_ptr<op_state>& op) {
    const auto& payload = std::get<udp_payload>(op->payload());
    if (!payload.valid) return http::outcome_code::limit_exceeded;
    std::size_t bytes = 0;
    std::erase_if(admitted_, [&](const auto& weak) {
        const auto state = weak.lock();
        if (!state || state->is_terminal()) return true;
        bytes += std::get<udp_payload>(state->payload()).storage_bytes;
        return false;
    });
    if (admitted_.size() >= k_udp_pending_operations
        || payload.storage_bytes > k_udp_pending_bytes - bytes) return http::outcome_code::limit_exceeded;
    admitted_.push_back(op);
    return http::outcome_code::ok;
}
void prepare_datagram_socket(pollsys::native_socket_t socket) {
    pollsys::set_nonblocking(socket, true);
    int yes = 1;
#if defined(IP_PKTINFO) && !defined(_WIN32)
    ::setsockopt(socket, IPPROTO_IP, IP_PKTINFO, &yes, sizeof(yes));
#elif defined(IP_RECVDSTADDR) && !defined(_WIN32)
    ::setsockopt(socket, IPPROTO_IP, IP_RECVDSTADDR, &yes, sizeof(yes));
#endif
#if defined(IP_RECVIF) && (defined(__APPLE__) || defined(__FreeBSD__))
    ::setsockopt(socket, IPPROTO_IP, IP_RECVIF, &yes, sizeof(yes));
#endif
#if defined(IPV6_RECVPKTINFO) && !defined(_WIN32)
    ::setsockopt(socket, IPPROTO_IPV6, IPV6_RECVPKTINFO, &yes, sizeof(yes));
#elif defined(IPV6_2292PKTINFO) && !defined(_WIN32)
    // Darwin exposes RFC2292 pktinfo unless the TU opts into RFC3542.
    ::setsockopt(socket, IPPROTO_IPV6, IPV6_2292PKTINFO, &yes, sizeof(yes));
#endif
#if defined(_WIN32)
    ::setsockopt(socket, IPPROTO_IP, IP_PKTINFO, reinterpret_cast<const char*>(&yes), sizeof(yes));
    ::setsockopt(socket, IPPROTO_IPV6, IPV6_PKTINFO, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif
    (void)yes;
}
bool encode_datagram_endpoint(const datagram_endpoint& endpoint, sockaddr_storage& storage, int& length) {
    storage = {};
    if (endpoint.peer.address.family == net::address_family::ipv4) {
        auto* address = reinterpret_cast<sockaddr_in*>(&storage);
        address->sin_family = AF_INET;
        address->sin_port = htons(endpoint.peer.port);
        std::memcpy(&address->sin_addr, endpoint.peer.address.bytes.data() + 12, 4);
        length = sizeof(*address);
        return true;
    }
    if (endpoint.peer.address.family == net::address_family::ipv6) {
        auto* address = reinterpret_cast<sockaddr_in6*>(&storage);
        address->sin6_family = AF_INET6;
        address->sin6_port = htons(endpoint.peer.port);
        address->sin6_scope_id = endpoint.scope;
        std::memcpy(&address->sin6_addr, endpoint.peer.address.bytes.data(), 16);
        length = sizeof(*address);
        return true;
    }
    return false;
}
void capture_datagram_metadata(pollsys::native_socket_t socket, io_datagram& packet,
                               const sockaddr_storage& peer, std::uint64_t id) {
    pollsys::fill_peer(peer, packet.peer.peer);
    if (peer.ss_family == AF_INET6) packet.peer.scope = reinterpret_cast<const sockaddr_in6*>(&peer)->sin6_scope_id;
    packet.socket_id = id;
    packet.received_at = std::chrono::steady_clock::now();
    sockaddr_storage local{};
#if defined(_WIN32)
    int length = sizeof(local);
#else
    socklen_t length = sizeof(local);
#endif
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&local), &length) != 0) return;
    datagram_endpoint endpoint;
    pollsys::fill_peer(local, endpoint.peer);
    if (local.ss_family == AF_INET6) {
        endpoint.scope = reinterpret_cast<const sockaddr_in6*>(&local)->sin6_scope_id;
    }
    // A wildcard bind is not the packet's destination. Ancillary data below
    // fills it when available; unsupported systems leave it explicitly absent.
    const auto& bytes = endpoint.peer.address.bytes;
    const auto first = endpoint.peer.address.family == net::address_family::ipv4 ? 12 : 0;
    const bool bound_address = std::any_of(bytes.begin() + first, bytes.end(), [](std::byte byte) {
        return byte != std::byte{0};
    });
    if (bound_address) {
        packet.local = endpoint;
    }
}
#if defined(_WIN32)
LPFN_WSARECVMSG datagram_receive_api(pollsys::native_socket_t socket) {
    GUID identity = WSAID_WSARECVMSG;
    LPFN_WSARECVMSG receive = nullptr;
    DWORD bytes = 0;
    if (::WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &identity, sizeof(identity),
            &receive, sizeof(receive), &bytes, nullptr, nullptr) != 0) return nullptr;
    return receive;
}
void capture_datagram_control(io_datagram& packet, WSAMSG& message, std::uint16_t port) {
    for (auto* item = WSA_CMSG_FIRSTHDR(&message); item != nullptr; item = WSA_CMSG_NXTHDR(&message, item)) {
        sockaddr_storage destination{};
        if (item->cmsg_level == IPPROTO_IP && item->cmsg_type == IP_PKTINFO
            && item->cmsg_len >= WSA_CMSG_LEN(sizeof(IN_PKTINFO))) {
            IN_PKTINFO info{};
            std::memcpy(&info, WSA_CMSG_DATA(item), sizeof(info));
            auto* target = reinterpret_cast<sockaddr_in*>(&destination);
            target->sin_family = AF_INET;
            target->sin_addr = info.ipi_addr;
            packet.interface_index = info.ipi_ifindex;
        } else if (item->cmsg_level == IPPROTO_IPV6 && item->cmsg_type == IPV6_PKTINFO
            && item->cmsg_len >= WSA_CMSG_LEN(sizeof(IN6_PKTINFO))) {
            IN6_PKTINFO info{};
            std::memcpy(&info, WSA_CMSG_DATA(item), sizeof(info));
            auto* target = reinterpret_cast<sockaddr_in6*>(&destination);
            target->sin6_family = AF_INET6;
            target->sin6_addr = info.ipi6_addr;
            target->sin6_scope_id = info.ipi6_ifindex;
            packet.interface_index = info.ipi6_ifindex;
        } else {
            continue;
        }
        datagram_endpoint local;
        pollsys::fill_peer(destination, local.peer);
        local.peer.port = port;
        if (destination.ss_family == AF_INET6 && IN6_IS_ADDR_LINKLOCAL(&(reinterpret_cast<sockaddr_in6*>(&destination)->sin6_addr))) {
            local.scope = reinterpret_cast<sockaddr_in6*>(&destination)->sin6_scope_id;
        }
        packet.local = local;
    }
}
#endif
std::optional<io_result> datagram_step(pollsys::native_socket_t socket, const op_state& op) {
    const auto& payload = std::get<udp_payload>(op.payload());
    auto& packet = *payload.packet;
    sockaddr_storage address{};
    int length = 0;
    const bool receive = op.kind() == io_op_kind::udp_receive;
    if (!receive && !encode_datagram_endpoint(packet.peer, address, length)) return io_result{http::outcome_code::invalid_state};
    bool truncated = false;
    // A one-byte probe distinguishes an empty message from truncation at zero
    // capacity even on kernels that omit MSG_TRUNC for a zero-length iovec.
    std::byte zero_capacity_probe{};
    auto* buffer_data = packet.bytes.empty() ? &zero_capacity_probe : packet.bytes.data();
    const auto buffer_size = receive && packet.bytes.empty() ? std::size_t{1} : packet.bytes.size();
#if defined(_WIN32)
    length = sizeof(address);
    WSABUF buffer{static_cast<ULONG>(buffer_size), reinterpret_cast<char*>(buffer_data)};
    alignas(WSACMSGHDR) char control[256]{};
    WSAMSG message{};
    message.name = reinterpret_cast<sockaddr*>(&address);
    message.namelen = sizeof(address);
    message.lpBuffers = &buffer;
    message.dwBufferCount = 1;
    message.Control.buf = control;
    message.Control.len = sizeof(control);
    const auto receive_api = receive ? datagram_receive_api(socket) : nullptr;
    int seen;
    if (receive_api) {
        DWORD transferred = 0;
        const auto status = receive_api(socket, &message, &transferred, nullptr, nullptr);
        seen = status == SOCKET_ERROR ? SOCKET_ERROR : static_cast<int>(transferred);
        truncated = (message.dwFlags & (MSG_TRUNC | MSG_PARTIAL)) != 0;
    } else {
        seen = receive
            ? ::recvfrom(socket, buffer.buf, static_cast<int>(buffer.len), 0,
                         reinterpret_cast<sockaddr*>(&address), &length)
            : ::sendto(socket, buffer.buf, static_cast<int>(buffer.len), 0,
                       reinterpret_cast<sockaddr*>(&address), packet.peer.peer.address.family == net::address_family::ipv4
                           ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
    }
    if (seen == SOCKET_ERROR) {
        const auto error = ::WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINTR) return std::nullopt;
        return io_result{error == WSAEMSGSIZE ? http::outcome_code::limit_exceeded : http::outcome_code::protocol_error};
    }
#else
    iovec vector{buffer_data, buffer_size};
    // Aligned ancillary storage handles IPv4 destination and IPv6 pktinfo.
    alignas(cmsghdr) char control[256]{};
    msghdr message{};
    message.msg_name = &address;
    message.msg_namelen = sizeof(address);
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
    const auto seen = receive ? ::recvmsg(socket, &message, 0)
        : ::sendto(socket, packet.bytes.data(), packet.bytes.size(), flags,
                   reinterpret_cast<sockaddr*>(&address), static_cast<socklen_t>(length));
    if (seen < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return std::nullopt;
        return io_result{errno == EMSGSIZE ? http::outcome_code::limit_exceeded : http::outcome_code::protocol_error};
    }
    truncated = (message.msg_flags & MSG_TRUNC) != 0;
#endif
    if (truncated || static_cast<std::size_t>(seen) > packet.bytes.size()) return io_result{http::outcome_code::limit_exceeded};
    if (!receive) return io_result{static_cast<std::size_t>(seen) == packet.bytes.size()
        ? http::outcome_code::ok : http::outcome_code::protocol_error, static_cast<std::size_t>(seen)};
    capture_datagram_metadata(socket, packet, address, op.connection());
#if defined(_WIN32)
    if (receive_api) capture_datagram_control(packet, message, pollsys::bound_listener_port(socket));
#else
    for (auto* item = CMSG_FIRSTHDR(&message); item != nullptr; item = CMSG_NXTHDR(&message, item)) {
        sockaddr_storage destination{};
        if (item->cmsg_level == IPPROTO_IP) {
#if defined(IP_RECVIF) && (defined(__APPLE__) || defined(__FreeBSD__))
            if (item->cmsg_type == IP_RECVIF) {
                if (item->cmsg_len >= CMSG_LEN(offsetof(sockaddr_dl, sdl_data))) {
                    sockaddr_dl info{};
                    std::memcpy(&info, CMSG_DATA(item), std::min(sizeof(info), item->cmsg_len - CMSG_LEN(0)));
                    packet.interface_index = info.sdl_index;
                }
                continue;
            }
#endif
#if defined(IP_PKTINFO)
            if (item->cmsg_type != IP_PKTINFO || item->cmsg_len < CMSG_LEN(sizeof(in_pktinfo))) continue;
            in_pktinfo info{};
            std::memcpy(&info, CMSG_DATA(item), sizeof(info));
            auto* target = reinterpret_cast<sockaddr_in*>(&destination);
            target->sin_family = AF_INET;
            target->sin_addr = info.ipi_addr;
            packet.interface_index = info.ipi_ifindex;
#elif defined(IP_RECVDSTADDR)
            if (item->cmsg_type != IP_RECVDSTADDR || item->cmsg_len < CMSG_LEN(sizeof(in_addr))) continue;
            auto* target = reinterpret_cast<sockaddr_in*>(&destination);
            target->sin_family = AF_INET;
            std::memcpy(&target->sin_addr, CMSG_DATA(item), sizeof(in_addr));
#else
            continue;
#endif
        } else if (item->cmsg_level == IPPROTO_IPV6) {
#if defined(IPV6_PKTINFO) || defined(IPV6_2292PKTINFO)
#if defined(IPV6_PKTINFO)
            constexpr int pktinfo_type = IPV6_PKTINFO;
#else
            constexpr int pktinfo_type = IPV6_2292PKTINFO;
#endif
            if (item->cmsg_type != pktinfo_type || item->cmsg_len < CMSG_LEN(sizeof(in6_pktinfo))) continue;
            in6_pktinfo info{};
            std::memcpy(&info, CMSG_DATA(item), sizeof(info));
            auto* target = reinterpret_cast<sockaddr_in6*>(&destination);
            target->sin6_family = AF_INET6;
            target->sin6_addr = info.ipi6_addr;
            target->sin6_scope_id = info.ipi6_ifindex;
            packet.interface_index = info.ipi6_ifindex;
#else
            continue;
#endif
        } else {
            continue;
        }
        datagram_endpoint local;
        pollsys::fill_peer(destination, local.peer);
        local.peer.port = pollsys::bound_listener_port(socket);
        // Scope is required only for scoped IPv6 addresses; interface_index
        // retains the receiving interface for global and loopback addresses.
        if (destination.ss_family == AF_INET6 && IN6_IS_ADDR_LINKLOCAL(&(reinterpret_cast<sockaddr_in6*>(&destination)->sin6_addr))) {
            local.scope = reinterpret_cast<sockaddr_in6*>(&destination)->sin6_scope_id;
        }
        packet.local = local;
    }
#endif
    packet.bytes.resize(static_cast<std::size_t>(seen));
    return io_result{http::outcome_code::ok, static_cast<std::size_t>(seen), 0, {}, payload.packet};
}
}  // namespace detail
}  // namespace httpserver
