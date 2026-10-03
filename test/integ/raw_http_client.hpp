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

// TASK-108 step 9: the raw loopback client of the native HTTP/1
// end-to-end suite. Deliberately independent of the library's engine
// path: a nonblocking 127.0.0.1 stream driven through the pollsys
// shims (the one platform-divergence point) with deadline-bounded
// sends and receives -- every wait is bounded, a pass condition is
// always observed bytes or an observed close, never a sleep. Response
// bytes parse through the parity response-frame parser, so the wire
// (status line, framing, body) is asserted, not the engine's objects.

#ifndef TEST_INTEG_RAW_HTTP_CLIENT_HPP_
#define TEST_INTEG_RAW_HTTP_CLIENT_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "httpserver/detail/io_poll_sys.hpp"
#include "parity/response_frame.hpp"

namespace raw_http {

namespace pollsys = httpserver::detail::pollsys;

using parity::observed_response;
using parity::response_frame_parser;

// Deadline budget for one client exchange. A healthy loopback answers
// in milliseconds; this only bounds a broken scenario so the binary
// can never hang.
constexpr std::chrono::milliseconds kExchangeBudget{5000};

// One blocking-dial, nonblocking-I/O HTTP client connection.
class connection {
 public:
    connection() : socket_(pollsys::open_stream()) { }

    ~connection() { close(); }

    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;

    bool valid() const noexcept { return socket_ != pollsys::k_invalid_socket; }

    // True once the peer closed and the parser drained everything.
    bool peer_closed() const noexcept { return peer_closed_; }

    // TASK-118: marks this client's requests as HEAD so the response
    // parser completes headers-only responses (a HEAD response never
    // carries body bytes however the head frames them).
    void set_head_only(bool head_only) { parser_.set_head_only(head_only); }

    // Connects to 127.0.0.1:@p port (the dial itself blocks; retries
    // briefly while the server settles its listener), then flips the
    // stream to nonblocking for the deadline-bounded exchanges. True on
    // success.
    bool connect(std::uint16_t port) {
        return dial_until(&pollsys::connect_loopback, port);
    }

    // TASK-119: the [::1] twin of connect(), for endpoints served on
    // the IPv6 loopback.
    bool connect_v6(std::uint16_t port) {
        return dial_until(&pollsys::connect_loopback_v6, port);
    }

    // Sends every byte of @p bytes before @p budget elapses.
    bool send(std::string_view bytes,
              std::chrono::milliseconds budget = kExchangeBudget) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            const std::byte* cursor =
                reinterpret_cast<const std::byte*>(bytes.data()) + sent;
            const pollsys::sys_result r = pollsys::write_some(
                socket_, cursor, bytes.size() - sent);
            if (r.status == pollsys::sys_status::ok) {
                sent += r.transferred;
                continue;
            }
            if (r.status != pollsys::sys_status::would_block) return false;
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        return true;
    }

    // Receives until the parser completed @p want responses, the peer
    // closed, or the budget elapsed; parsed responses are appended to
    // @p seen. True when @p want responses arrived (the peer may or may
    // not have closed yet).
    bool receive(std::size_t want, std::deque<observed_response>& seen,
                 std::chrono::milliseconds budget = kExchangeBudget) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::byte buffer[8192];
        while (seen.size() < want && !parser_.failed()) {
            const pollsys::sys_result r =
                pollsys::read_some(socket_, buffer, sizeof buffer);
            if (r.status == pollsys::sys_status::ok && r.transferred > 0) {
                for (observed_response& done : parser_.feed(std::string_view(
                         reinterpret_cast<const char*>(buffer),
                         r.transferred))) {
                    seen.push_back(std::move(done));
                }
                continue;
            }
            if (r.status == pollsys::sys_status::closed_reset) {
                peer_closed_ = true;
                for (observed_response& done : parser_.finish()) {
                    seen.push_back(std::move(done));
                }
                break;
            }
            if (r.status != pollsys::sys_status::would_block) {
                peer_closed_ = true;
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) break;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        return seen.size() >= want;
    }

    // Receives until the peer closes (budget-bounded); responses found
    // on the way are appended. True when the close was observed.
    bool receive_close(std::deque<observed_response>& seen,
                       std::chrono::milliseconds budget = kExchangeBudget) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::byte buffer[8192];
        while (!peer_closed_ && !parser_.failed()) {
            const pollsys::sys_result r =
                pollsys::read_some(socket_, buffer, sizeof buffer);
            if (r.status == pollsys::sys_status::ok && r.transferred > 0) {
                for (observed_response& done : parser_.feed(std::string_view(
                         reinterpret_cast<const char*>(buffer),
                         r.transferred))) {
                    seen.push_back(std::move(done));
                }
                continue;
            }
            if (r.status != pollsys::sys_status::would_block) {
                peer_closed_ = true;
                for (observed_response& done : parser_.finish()) {
                    seen.push_back(std::move(done));
                }
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) break;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        return peer_closed_;
    }

    // Deadline-bounded absence probe (TASK-109): true when NOT one byte
    // -- nor a close -- arrives within @p window. A byte or FIN fails
    // it immediately.
    bool quiet_for(std::chrono::milliseconds window) {
        std::byte buffer[64];
        const auto deadline = std::chrono::steady_clock::now() + window;
        for (;;) {
            const pollsys::sys_result r =
                pollsys::read_some(socket_, buffer, sizeof buffer);
            if (r.status == pollsys::sys_status::would_block) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    return true;
                }
                std::this_thread::sleep_for(
                    std::chrono::microseconds(200));
                continue;
            }
            return false;   // data arrived, or the peer closed
        }
    }

    void close() {
        if (socket_ != pollsys::k_invalid_socket) {
            pollsys::close_socket(socket_);
        }
    }

 private:
    // The shared retry-dial loop of connect()/connect_v6(): blocks on
    // @p attempt until it lands or the exchange budget elapses, then
    // flips the stream to nonblocking.
    bool dial_until(bool (*attempt)(pollsys::native_socket_t,
                                    std::uint16_t),
                    std::uint16_t port) {
        const auto deadline = std::chrono::steady_clock::now()
            + kExchangeBudget;
        for (;;) {
            if (attempt(socket_, port)) {
                pollsys::set_nonblocking(socket_, true);
                return true;
            }
            // A fresh listener may not have reached the backlog yet.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (std::chrono::steady_clock::now() >= deadline) return false;
        }
    }

    pollsys::native_socket_t socket_;
    response_frame_parser parser_;
    bool peer_closed_ = false;
};

}  // namespace raw_http

#endif  // TEST_INTEG_RAW_HTTP_CLIENT_HPP_
