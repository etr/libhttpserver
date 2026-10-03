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
// TASK-119 adds the fill_peer battery (the accept-time peer capture)
// over synthesized transport address storages, cross-checked against
// net::parse_address so the byte path and the text path of one host
// can never disagree; that cross-check needs the shared byte decode
// in v3core, so the suite links libhttpserver.la (the default LDADD).

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include <httpserver/detail/io_poll_sys.hpp>

#include "./littletest.hpp"

namespace {

namespace pollsys = httpserver::detail::pollsys;
namespace net = httpserver::net;

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

// ---- accept-time peer capture (TASK-119: fill_peer) ------------------------

// A synthesized AF_INET storage: family ipv4, the address bytes
// right-aligned, the port converted to host order -- and the captured
// address equal to the parsed spelling of the same literal (the
// drift pin between the byte path and the text path).
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, fill_peer_af_inet_fill_and_port)
    sockaddr_in in4{};
    in4.sin_family = AF_INET;
    in4.sin_port = htons(41000);
    LT_CHECK(::inet_pton(AF_INET, "192.0.2.1", &in4.sin_addr) == 1);
    sockaddr_storage storage{};
    std::memcpy(&storage, &in4, sizeof in4);
    net::peer_address peer;
    pollsys::fill_peer(storage, peer);
    LT_CHECK(peer.address.family == net::address_family::ipv4);
    LT_CHECK_EQ(peer.port, std::uint16_t{41000});
    const std::optional<net::address> parsed =
        net::parse_address("192.0.2.1");
    LT_CHECK(parsed.has_value());
    if (parsed.has_value()) LT_CHECK(peer.address == *parsed);
    LT_CHECK(peer.address.to_string() == "192.0.2.1");
LT_END_AUTO_TEST(fill_peer_af_inet_fill_and_port)

// A plain AF_INET6 storage: family ipv6, all sixteen bytes carried,
// the port converted to host order.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, fill_peer_plain_v6)
    sockaddr_in6 in6{};
    in6.sin6_family = AF_INET6;
    in6.sin6_port = htons(61000);
    LT_CHECK(::inet_pton(AF_INET6, "2001:db8::1", &in6.sin6_addr) == 1);
    sockaddr_storage storage{};
    std::memcpy(&storage, &in6, sizeof in6);
    net::peer_address peer;
    pollsys::fill_peer(storage, peer);
    LT_CHECK(peer.address.family == net::address_family::ipv6);
    LT_CHECK_EQ(peer.port, std::uint16_t{61000});
    const std::optional<net::address> parsed =
        net::parse_address("2001:db8::1");
    LT_CHECK(parsed.has_value());
    if (parsed.has_value()) LT_CHECK(peer.address == *parsed);
    LT_CHECK(peer.address.to_string() == "2001:db8::1");
LT_END_AUTO_TEST(fill_peer_plain_v6)

// A true v4-mapped arrival (::ffff:127.0.0.1 -- exactly what a
// dual-stack listener hands the capture for every IPv4 client):
// family ipv4 and the plain parsed literal's value, so a policy
// spelling "127.0.0.1" matches the dual-stack arrival.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, fill_peer_v4_mapped_normalizes)
    sockaddr_in6 in6{};
    in6.sin6_family = AF_INET6;
    in6.sin6_port = htons(80);
    LT_CHECK(::inet_pton(AF_INET6, "::ffff:127.0.0.1",
                         &in6.sin6_addr) == 1);
    sockaddr_storage storage{};
    std::memcpy(&storage, &in6, sizeof in6);
    net::peer_address peer;
    pollsys::fill_peer(storage, peer);
    LT_CHECK(peer.address.family == net::address_family::ipv4);
    LT_CHECK_EQ(peer.port, std::uint16_t{80});
    const std::optional<net::address> parsed =
        net::parse_address("127.0.0.1");
    LT_CHECK(parsed.has_value());
    if (parsed.has_value()) LT_CHECK(peer.address == *parsed);
    LT_CHECK(peer.address.to_string() == "127.0.0.1");
LT_END_AUTO_TEST(fill_peer_v4_mapped_normalizes)

// The regression twin: a genuine IPv6 address carrying 0xffff at
// bytes[10..11] with nonzero high bytes is NOT v4-mapped -- the
// marker is the full ::ffff:0:0/96 prefix, not just the two 0xff
// bytes. The compressed spelling must keep ffff in the sixth hextet:
// 2001:db8::ffff:c000:201 expands to bytes 2001:0db8:0:0:0:ffff:
// c000:0201, so the capture-side weak-predicate regression rewrites
// this peer to the IPv4 address 192.0.2.1 (an allow-list spoof) and
// the correct rule keeps family ipv6 with the address unchanged.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, fill_peer_non_mapped_stays_v6)
    sockaddr_in6 in6{};
    in6.sin6_family = AF_INET6;
    in6.sin6_port = htons(443);
    LT_CHECK(::inet_pton(AF_INET6, "2001:db8::ffff:c000:201",
                         &in6.sin6_addr) == 1);
    sockaddr_storage storage{};
    std::memcpy(&storage, &in6, sizeof in6);
    net::peer_address peer;
    pollsys::fill_peer(storage, peer);
    LT_CHECK(peer.address.family == net::address_family::ipv6);
    LT_CHECK_EQ(peer.port, std::uint16_t{443});
    const std::optional<net::address> parsed =
        net::parse_address("2001:db8::ffff:c000:201");
    LT_CHECK(parsed.has_value());
    if (parsed.has_value()) LT_CHECK(peer.address == *parsed);
    LT_CHECK(peer.address.to_string() == "2001:db8::ffff:c000:201");
    // The high bytes survive: the capture never collapses a genuine
    // IPv6 peer onto the v4-mapped tail.
    LT_CHECK(peer.address.bytes[0] == std::byte{0x20});
    LT_CHECK(peer.address.bytes[1] == std::byte{0x01});
LT_END_AUTO_TEST(fill_peer_non_mapped_stays_v6)

// Any other family reports the unspec snapshot: the capture resets a
// seeded out-param, leaving no address and no port.
LT_BEGIN_AUTO_TEST(io_poll_sys_listener_suite, fill_peer_other_family_unspec)
    sockaddr_storage storage{};
    storage.ss_family = AF_UNSPEC;
    net::peer_address seeded;
    seeded.address.family = net::address_family::ipv4;
    seeded.port = 9999;
    pollsys::fill_peer(storage, seeded);
    LT_CHECK(seeded.address.family == net::address_family::unspec);
    LT_CHECK_EQ(seeded.port, std::uint16_t{0});
    LT_CHECK(seeded.address.to_string().empty());
LT_END_AUTO_TEST(fill_peer_other_family_unspec)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
