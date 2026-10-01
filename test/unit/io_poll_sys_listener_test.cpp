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

// TASK-108 step 1: the loopback/any-address listener bind helper
// (pollsys::open_listener). Pins: the three accepted address spellings
// (IPv4 literal, any-address "" and "*", IPv6 literal), the resolved
// port report (0 -> ephemeral, nonzero -> the requested port), the
// SO_REUSEADDR option, nonblocking accept behavior (the poll backend's
// adopt contract), and the typed rejection of a non-literal address.
// Header-only surface: io_poll_sys.hpp compiles straight into this
// program (AM_CPPFLAGS supplies -DHTTPSERVER_COMPILATION), so the LDADD
// stays empty.

#include <cstdint>
#include <string>

#include <httpserver/detail/io_poll_sys.hpp>

#include "./littletest.hpp"

namespace {

namespace pollsys = httpserver::detail::pollsys;

// True iff SO_REUSEADDR is enabled on @p handle (both platforms spell
// the option identically; only the option-value pointer type differs).
bool reuseaddr_enabled(pollsys::native_socket_t handle) {
    int value = 0;
#if defined(_WIN32)
    int length = sizeof(value);
#else
    socklen_t length = sizeof(value);
#endif
    return ::getsockopt(handle, SOL_SOCKET, SO_REUSEADDR,
                        reinterpret_cast<char*>(&value), &length) == 0
        && value != 0;
}

}  // namespace

LT_BEGIN_SUITE(io_poll_sys_listener_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(io_poll_sys_listener_suite)

// An IPv4 literal on an ephemeral port produces a usable listener and
// reports the resolved host-order port.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, ipv4_literal_ephemeral)
    std::uint16_t port = 0;
    pollsys::native_socket_t listener =
        pollsys::open_listener("127.0.0.1", 0, port);
    LT_CHECK(listener != pollsys::k_invalid_socket);
    LT_CHECK(port != 0);
    pollsys::close_socket(listener);
LT_END_AUTO_TEST(ipv4_literal_ephemeral)

// Both any-address spellings bind.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, any_address_spellings)
    for (const std::string& address : {std::string(""), std::string("*")}) {
        std::uint16_t port = 0;
        pollsys::native_socket_t listener =
            pollsys::open_listener(address, 0, port);
        LT_CHECK(listener != pollsys::k_invalid_socket);
        LT_CHECK(port != 0);
        pollsys::close_socket(listener);
    }
LT_END_AUTO_TEST(any_address_spellings)

// An IPv6 literal binds on the v6 family.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, ipv6_literal)
    std::uint16_t port = 0;
    pollsys::native_socket_t listener =
        pollsys::open_listener("::1", 0, port);
    LT_CHECK(listener != pollsys::k_invalid_socket);
    LT_CHECK(port != 0);
    pollsys::close_socket(listener);
LT_END_AUTO_TEST(ipv6_literal)

// A requested nonzero port is bound exactly and reported back.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, requested_port_reported)
    std::uint16_t ephemeral = 0;
    pollsys::native_socket_t probe =
        pollsys::open_listener("127.0.0.1", 0, ephemeral);
    LT_CHECK(probe != pollsys::k_invalid_socket);
    pollsys::close_socket(probe);

    std::uint16_t bound = 0;
    pollsys::native_socket_t listener =
        pollsys::open_listener("127.0.0.1", ephemeral, bound);
    LT_CHECK(listener != pollsys::k_invalid_socket);
    LT_CHECK_EQ(bound, ephemeral);
    pollsys::close_socket(listener);
LT_END_AUTO_TEST(requested_port_reported)

// SO_REUSEADDR is set so a restarted server can rebind immediately.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, reuseaddr_set)
    std::uint16_t port = 0;
    pollsys::native_socket_t listener =
        pollsys::open_listener("127.0.0.1", 0, port);
    LT_CHECK(listener != pollsys::k_invalid_socket);
    LT_CHECK(reuseaddr_enabled(listener));
    pollsys::close_socket(listener);
LT_END_AUTO_TEST(reuseaddr_set)

// Listener handles are nonblocking: accept with nothing pending reports
// would_block instead of stalling (blocking accept here would hang the
// suite -- the failure mode under regression is a hang, not a red).
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, nonblocking_accept)
    std::uint16_t port = 0;
    pollsys::native_socket_t listener =
        pollsys::open_listener("127.0.0.1", 0, port);
    LT_CHECK(listener != pollsys::k_invalid_socket);
    pollsys::native_socket_t accepted = pollsys::k_invalid_socket;
    const pollsys::sys_result none = pollsys::accept_one(listener, &accepted);
    LT_CHECK(none.status == pollsys::sys_status::would_block);
    pollsys::close_socket(listener);
LT_END_AUTO_TEST(nonblocking_accept)

// A host name is not a numeric literal: rejected with the invalid
// handle and nothing bound.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, name_rejected)
    std::uint16_t port = 12345;
    pollsys::native_socket_t listener =
        pollsys::open_listener("localhost.invalid", 0, port);
    LT_CHECK(listener == pollsys::k_invalid_socket);
LT_END_AUTO_TEST(name_rejected)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
