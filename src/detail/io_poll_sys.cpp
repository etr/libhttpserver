/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/io_poll_sys.hpp>
#include <string>

namespace httpserver::detail::pollsys {

int poll_call(poll_slot* slots, std::size_t count, int timeout_ms) {
#if defined(_WIN32)
    return ::WSAPoll(slots, static_cast<ULONG>(count), timeout_ms);
#else
    return ::poll(slots, static_cast<nfds_t>(count), timeout_ms);
#endif
}

native_socket_t open_stream() {
#if defined(_WIN32)
    return ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    return ::socket(AF_INET, SOCK_STREAM, 0);
#endif
}

native_socket_t open_stream_v6() {
#if defined(_WIN32)
    return ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
#else
    return ::socket(AF_INET6, SOCK_STREAM, 0);
#endif
}

bool close_socket(native_socket_t& socket) {
    if (socket == k_invalid_socket) {
        return true;
    }
    const native_socket_t closing = socket;
    socket = k_invalid_socket;
#if defined(_WIN32)
    return ::closesocket(closing) == 0;
#else
    return ::close(closing) == 0;
#endif
}

bool set_nonblocking(native_socket_t socket, bool nonblocking) {
#if defined(_WIN32)
    u_long mode = nonblocking ? 1u : 0u;
    return ::ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = ::fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const int updated = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return ::fcntl(socket, F_SETFL, updated) == 0;
#endif
}

void prepare_stream_socket(native_socket_t socket) {
#if defined(_WIN32)
    BOOL nodelay = TRUE;
    ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
#else
    int nodelay = 1;
    ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
#if defined(SO_NOSIGPIPE)
    int nosigpipe = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
#endif
#endif
}

#if defined(_WIN32)
std::mutex& winsock_mutex() {
    static std::mutex mu;
    return mu;
}
#endif

#if defined(_WIN32)
int& winsock_refcount() {
    static int count = 0;
    return count;
}
#endif

#if defined(_WIN32)
bool ensure_winsock() {
    std::lock_guard<std::mutex> lock(winsock_mutex());
    int& count = winsock_refcount();
    if (count == 0) {
        WSADATA data;
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return false;
        }
    }
    ++count;
    return true;
}
#endif

#if defined(_WIN32)
void release_winsock() {
    std::lock_guard<std::mutex> lock(winsock_mutex());
    int& count = winsock_refcount();
    if (count > 0 && --count == 0) {
        ::WSACleanup();
    }
}
#endif

#if !defined(_WIN32)
sys_result posix_status(ssize_t seen) {
    if (seen > 0) {
        return sys_result{sys_status::ok, static_cast<std::size_t>(seen)};
    }
    if (seen == 0) {
        return sys_result{sys_status::closed_reset, 0};
    }
    switch (errno) {
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
        case ENOBUFS:
            return sys_result{sys_status::would_block, 0};
        case EPIPE:
        case ECONNRESET:
            return sys_result{sys_status::closed_reset, 0};
        default:
            return sys_result{sys_status::error, 0};
    }
}
#endif

#if defined(_WIN32)
sys_result wsa_error_status(int error) {
    switch (error) {
        case WSAEWOULDBLOCK:
        case WSAEINPROGRESS:
            return sys_result{sys_status::would_block, 0};
        case WSAECONNRESET:
        case WSAECONNABORTED:
        case WSAESHUTDOWN:
        case WSAENETRESET:
            return sys_result{sys_status::closed_reset, 0};
        default:
            return sys_result{sys_status::error, 0};
    }
}
#endif

sys_result read_some(native_socket_t socket, std::byte* data, std::size_t size) {
    if (size == 0) {
        return sys_result{sys_status::ok, 0};
    }
#if defined(_WIN32)
    const int seen = ::recv(socket, reinterpret_cast<char*>(data), static_cast<int>(size), 0);
    if (seen > 0) {
        return sys_result{sys_status::ok, static_cast<std::size_t>(seen)};
    }
    if (seen == 0) {
        return sys_result{sys_status::closed_reset, 0};
    }
    return wsa_error_status(::WSAGetLastError());
#else
    ssize_t seen = 0;
    do {
#if defined(MSG_NOSIGNAL)
        seen = ::recv(socket, data, size, MSG_NOSIGNAL);
#else
        seen = ::recv(socket, data, size, 0);
#endif
    } while (seen < 0 && errno == EINTR);
    return posix_status(seen);
#endif
}

sys_result write_some(native_socket_t socket, const std::byte* data, std::size_t size) {
    if (size == 0) {
        return sys_result{sys_status::ok, 0};
    }
#if defined(_WIN32)
    const int sent = ::send(socket, reinterpret_cast<const char*>(data), static_cast<int>(size), 0);
    if (sent >= 0) {
        return sys_result{sys_status::ok, static_cast<std::size_t>(sent)};
    }
    return wsa_error_status(::WSAGetLastError());
#else
    ssize_t sent = 0;
    do {
#if defined(MSG_NOSIGNAL)
        sent = ::send(socket, data, size, MSG_NOSIGNAL);
#else
        sent = ::send(socket, data, size, 0);
#endif
    } while (sent < 0 && errno == EINTR);
    return posix_status(sent);
#endif
}

void fill_peer(const sockaddr_storage& storage, net::peer_address& peer) noexcept {
    const auto* const in4 = reinterpret_cast<const sockaddr_in*>(&storage);
    const auto* const in6 = reinterpret_cast<const sockaddr_in6*>(&storage);
    peer = net::peer_address{};
    if (storage.ss_family == AF_INET) {
        static_assert(sizeof(in4->sin_addr.s_addr) == 4);
        peer.address = net::detail::address_from_bytes(net::address_family::ipv4, reinterpret_cast<const std::byte*>(&in4->sin_addr.s_addr));
        peer.port = ntohs(in4->sin_port);
        return;
    }
    if (storage.ss_family == AF_INET6) {
        peer.address = net::detail::address_from_bytes(net::address_family::ipv6, reinterpret_cast<const std::byte*>(&in6->sin6_addr));
        peer.port = ntohs(in6->sin6_port);
    }
}

sys_result accept_one(native_socket_t listener, native_socket_t* out, net::peer_address* peer) {
    sockaddr_storage storage;
#if defined(_WIN32)
    int length = sizeof(storage);
    const native_socket_t fresh = ::accept(listener, reinterpret_cast<sockaddr*>(&storage), &length);
    if (fresh == INVALID_SOCKET) {
        switch (::WSAGetLastError()) {
            case WSAEWOULDBLOCK:
            case WSAEINPROGRESS:
                return sys_result{sys_status::would_block, 0};
            default:
                return sys_result{sys_status::error, 0};
        }
    }
#else
    socklen_t length = sizeof(storage);
    const native_socket_t fresh = ::accept(listener, reinterpret_cast<sockaddr*>(&storage), &length);
    if (fresh < 0) {
        switch (errno) {
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                return sys_result{sys_status::would_block, 0};
            case ECONNABORTED:
                return sys_result{sys_status::closed_reset, 0};
            default:
                return sys_result{sys_status::error, 0};
        }
    }
#endif
    set_nonblocking(fresh, true);
    prepare_stream_socket(fresh);
    if (peer != nullptr) fill_peer(storage, *peer);
    *out = fresh;
    return sys_result{sys_status::ok, 0};
}

native_socket_t listener_failure(native_socket_t socket) {
    close_socket(socket);
#if defined(_WIN32)
    release_winsock();
#endif
    return k_invalid_socket;
}

bool fill_listener_address(std::string_view address, std::uint16_t port, sockaddr_storage& storage) {
    auto* const in4 = reinterpret_cast<sockaddr_in*>(&storage);
    if (address.empty() || address == "*") {
        in4->sin_family = AF_INET;
        in4->sin_port = htons(port);
        in4->sin_addr.s_addr = INADDR_ANY;
        return true;
    }
    if (address.size() < 16 && ::inet_pton(AF_INET, std::string(address).c_str(), &in4->sin_addr) == 1) {
        in4->sin_family = AF_INET;
        in4->sin_port = htons(port);
        return true;
    }
    auto* const in6 = reinterpret_cast<sockaddr_in6*>(&storage);
    if (address.size() >= 64 || ::inet_pton(AF_INET6, std::string(address).c_str(), &in6->sin6_addr) != 1) {
        return false;
    }
    in6->sin6_family = AF_INET6;
    in6->sin6_port = htons(port);
    return true;
}

bool bind_listener(native_socket_t socket, const sockaddr_storage& storage) {
    const socklen_t size = static_cast<socklen_t>(storage.ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in));
    return ::bind(socket, reinterpret_cast<const sockaddr*>(&storage), size) == 0 && ::listen(socket, k_listen_backlog) == 0;
}

std::uint16_t bound_listener_port(native_socket_t socket) {
    sockaddr_storage bound{};
#if defined(_WIN32)
    int length = sizeof(bound);
#else
    socklen_t length = sizeof(bound);
#endif
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
        return 0;
    }
    if (bound.ss_family == AF_INET6) {
        auto* const in6 = reinterpret_cast<sockaddr_in6*>(&bound);
        return ntohs(in6->sin6_port);
    }
    auto* const in4 = reinterpret_cast<sockaddr_in*>(&bound);
    return ntohs(in4->sin_port);
}

native_socket_t open_listener(std::string_view address, std::uint16_t port, std::uint16_t& bound_port) {
#if defined(_WIN32)
    if (!ensure_winsock()) {
        return k_invalid_socket;
    }
#endif
    sockaddr_storage storage{};
    if (!fill_listener_address(address, port, storage)) {
        return listener_failure(k_invalid_socket);
    }
#if defined(_WIN32)
    const int family = static_cast<int>(storage.ss_family);
    const native_socket_t socket = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
#else
    const native_socket_t socket = ::socket(storage.ss_family, SOCK_STREAM, 0);
#endif
    if (socket == k_invalid_socket) {
        return listener_failure(socket);
    }
    int reuse = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    if (!bind_listener(socket, storage)) {
        return listener_failure(socket);
    }
    bound_port = bound_listener_port(socket);
    set_nonblocking(socket, true);
    return socket;
}

native_socket_t make_listener(std::uint16_t& port) {
#if defined(_WIN32)
    if (!ensure_winsock()) {
        return k_invalid_socket;
    }
#endif
    native_socket_t socket = open_stream();
    if (socket == k_invalid_socket) {
        return listener_failure(socket);
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    if (::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        return listener_failure(socket);
    }
    if (::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(socket, 16) != 0) {
        return listener_failure(socket);
    }
    sockaddr_in bound{};
#if defined(_WIN32)
    int length = sizeof(bound);
#else
    socklen_t length = sizeof(bound);
#endif
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
        return listener_failure(socket);
    }
    port = ntohs(bound.sin_port);
    set_nonblocking(socket, true);
    return socket;
}

bool connect_loopback(native_socket_t socket, std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        return false;
    }
    return ::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
}

bool connect_loopback_v6(native_socket_t socket, std::uint16_t port) {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(port);
    if (::inet_pton(AF_INET6, "::1", &address.sin6_addr) != 1) {
        return false;
    }
    return ::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
}

bool make_loopback_pair(native_socket_t (&ends)[2]) {
    ends[0] = k_invalid_socket;
    ends[1] = k_invalid_socket;
#if defined(_WIN32)
    if (!ensure_winsock()) {
        return false;
    }
    std::uint16_t port = 0;
    const native_socket_t listener = make_listener(port);
    if (listener == k_invalid_socket) {
        release_winsock();
        return false;
    }
    native_socket_t client = open_stream();
    native_socket_t server = k_invalid_socket;
    if (client != k_invalid_socket && connect_loopback(client, port)) {
        server = ::accept(listener, nullptr, nullptr);
    }
    ::closesocket(listener);
    if (client == k_invalid_socket || server == k_invalid_socket) {
        close_socket(client);
        close_socket(server);
        release_winsock();
        return false;
    }
    ends[0] = server;  // adopted end (driver side / wake read end)
    ends[1] = client;  // peer end (wake write end / test side)
    return true;
#else
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) != 0) {
        ends[0] = k_invalid_socket;
        ends[1] = k_invalid_socket;
        return false;
    }
    return true;
#endif
}

}  // namespace httpserver::detail::pollsys
