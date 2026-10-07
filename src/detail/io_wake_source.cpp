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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#include <httpserver/detail/io_poll_sys.hpp>

namespace httpserver {
namespace detail {
namespace pollsys {

wake_source::wake_source() {
#if defined(_WIN32)
    if (!ensure_winsock()) {
        return;
    }
    winsock_held_ = true;
#endif
    native_socket_t ends[2] = {k_invalid_socket, k_invalid_socket};
    if (!make_loopback_pair(ends)) {
#if defined(_WIN32)
        release_winsock();
        winsock_held_ = false;
#endif
        return;
    }
    read_end_ = ends[0];
    write_end_ = ends[1];
    if (!set_nonblocking(read_end_, true)
            || !set_nonblocking(write_end_, true)) {
        close_socket(read_end_);
        close_socket(write_end_);
        return;
    }
    prepare_stream_socket(write_end_);
}

wake_source::~wake_source() {
    close_socket(read_end_);
    close_socket(write_end_);
#if defined(_WIN32)
    if (winsock_held_) {
        release_winsock();
    }
#endif
}

bool wake_source::signal() noexcept {
    const std::byte token{0};
#if defined(_WIN32)
    sys_result result;
    do {
        result = write_some(write_end_, &token, 1);
    } while (result.status == sys_status::error && ::WSAGetLastError() == WSAEINTR);
#else
    const auto result = write_some(write_end_, &token, 1);
#endif
    if (result.status == sys_status::ok) return result.transferred == 1;
    if (result.status != sys_status::would_block) return false;
#if defined(_WIN32)
    return ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

bool wake_source::acknowledge() noexcept {
    std::byte scratch[64];
    const auto result = read_some(read_end_, scratch, sizeof(scratch));
    return result.status == sys_status::ok
        || result.status == sys_status::would_block;
}

void wake_source::drain() noexcept {
    std::byte scratch[64];
    for (;;) {
        const sys_result r =
            read_some(read_end_, scratch, sizeof(scratch));
        if (r.status != sys_status::ok || r.transferred == 0) {
            return;
        }
    }
}

}  // namespace pollsys
}  // namespace detail
}  // namespace httpserver
