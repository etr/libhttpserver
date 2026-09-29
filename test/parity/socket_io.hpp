/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-096: thin POSIX TCP client for transcript runs. Everything is
// loopback-only and bounded by caller-supplied deadlines which serve as
// failure detection, never as pass conditions (determinism rule D3 of
// the parity plan).
//
// POSIX-only by design for now; a Windows port is a later milestone's
// concern.

#ifndef TEST_PARITY_SOCKET_IO_HPP_
#define TEST_PARITY_SOCKET_IO_HPP_

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <string>

namespace parity {

class tcp_client {
 public:
    tcp_client() = default;
    tcp_client(const tcp_client&) = delete;
    tcp_client& operator=(const tcp_client&) = delete;
    ~tcp_client() { close(); }

    // Connect to 127.0.0.1:port within the deadline.
    bool connect(uint16_t port, int timeout_ms) {
        close();
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int flags = ::fcntl(fd_, F_GETFL, 0);
        ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            if (!wait_for_connect(timeout_ms)) {
                close();
                return false;
            }
        }
        ::fcntl(fd_, F_SETFL, flags);  // back to blocking with poll guards
        return true;
    }

    // Write every byte, polling for writability. False on error/timeout.
    bool write_all(const std::string& bytes, int timeout_ms) {
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            pollfd p{fd_, POLLOUT, 0};
            if (::poll(&p, 1, timeout_ms) <= 0) return false;
            ssize_t n = ::send(fd_, bytes.data() + sent, bytes.size() - sent, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Read outcome for read_some().
    enum read_status { READ_DATA = 0, READ_EOF = 1, READ_ERROR = 2 };

    // Append whatever is available before the deadline. READ_EOF means
    // the peer performed an orderly shutdown (read() == 0).
    read_status read_some(std::string& out, int timeout_ms) {
        pollfd p{fd_, POLLIN, 0};
        int pr = ::poll(&p, 1, timeout_ms);
        if (pr == 0) return READ_ERROR;   // deadline: treated as failure
        if (pr < 0) return READ_ERROR;
        char buf[4096];
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n == 0) return READ_EOF;
        if (n < 0) return READ_ERROR;
        out.append(buf, static_cast<std::size_t>(n));
        return READ_DATA;
    }

    void half_close() {
        if (fd_ >= 0) ::shutdown(fd_, SHUT_WR);
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

 private:
    bool wait_for_connect(int timeout_ms) {
        pollfd p{fd_, POLLOUT | POLLERR, 0};
        if (::poll(&p, 1, timeout_ms) <= 0) return false;
        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0) return false;
        return err == 0;
    }

    int fd_ = -1;
};

}  // namespace parity

#endif  // TEST_PARITY_SOCKET_IO_HPP_
