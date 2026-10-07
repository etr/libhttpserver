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

#include <httpserver/server/readiness.hpp>

#include <cstdint>
#include <limits>
#include <type_traits>

#include "./littletest.hpp"

namespace srv = httpserver::server;
using clock_type = std::chrono::steady_clock;

static_assert(std::is_same_v<decltype(srv::native_handle::value), std::uintptr_t>);
static_assert(!std::is_convertible_v<srv::socket_key, std::uint64_t>);
static_assert(std::is_same_v<decltype(srv::interest_snapshot::next_deadline),
              std::optional<clock_type::time_point>>);

LT_BEGIN_SUITE(readiness_contract_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(readiness_contract_suite)

LT_BEGIN_AUTO_TEST(readiness_contract_suite, registration_identity)
    const srv::native_handle handle{7, srv::native_handle_kind::posix_descriptor, true};
    const srv::socket_interest first{srv::socket_key{1}, 10, handle, true, false};
    const srv::socket_interest other{srv::socket_key{2}, 10, handle, false, true};
    const srv::readiness_event replacement{first.key, 11, false, true, true, true};
    LT_CHECK(first.key == srv::socket_key{1});
    LT_CHECK(first.key != other.key);
    LT_CHECK_EQ(first.handle.value, other.handle.value);
    LT_CHECK(replacement.key == first.key);
    LT_CHECK(replacement.generation != first.generation);
    LT_CHECK(first.readable && !first.writable);
    LT_CHECK(!other.readable && other.writable);
    LT_CHECK(!replacement.readable && replacement.writable);
    LT_CHECK(replacement.closed && replacement.error);
LT_END_AUTO_TEST(registration_identity)

LT_BEGIN_AUTO_TEST(readiness_contract_suite, native_handle_width_and_validity)
    const srv::native_handle absent;
    const srv::native_handle zero{0, srv::native_handle_kind::posix_descriptor, true};
    const auto wide = static_cast<std::uintptr_t>(std::numeric_limits<int>::max()) + 1;
    const srv::native_handle windows{wide, srv::native_handle_kind::winsock_socket, true};
    const srv::native_handle maximum{std::numeric_limits<std::uintptr_t>::max(),
                                    srv::native_handle_kind::winsock_socket, false};
    LT_CHECK(!absent.valid);
    LT_CHECK(zero.valid);
    LT_CHECK_EQ(zero.value, std::uintptr_t{0});
    LT_CHECK(windows.valid);
    LT_CHECK_EQ(windows.value, wide);
    LT_CHECK(windows.kind == srv::native_handle_kind::winsock_socket);
    LT_CHECK(!maximum.valid);
    LT_CHECK_EQ(maximum.value, std::numeric_limits<std::uintptr_t>::max());
LT_END_AUTO_TEST(native_handle_width_and_validity)

LT_BEGIN_AUTO_TEST(readiness_contract_suite, default_snapshot_and_events)
    const srv::interest_snapshot empty;
    const srv::readiness_event event;
    LT_CHECK(empty.sockets.empty());
    LT_CHECK(!empty.wake);
    LT_CHECK(!empty.next_deadline);
    LT_CHECK(!event.readable && !event.writable && !event.closed && !event.error);
LT_END_AUTO_TEST(default_snapshot_and_events)

LT_BEGIN_AUTO_TEST(readiness_contract_suite, owned_snapshot_and_monotonic_deadline)
    srv::interest_snapshot saved;
    const auto deadline = clock_type::time_point{} + std::chrono::seconds{3};
    {
        srv::interest_snapshot source;
        source.sockets.push_back({srv::socket_key{42}, 9,
            {5, srv::native_handle_kind::posix_descriptor, true}, true, true});
        source.wake = srv::socket_interest{srv::socket_key{43}, 6,
            {6, srv::native_handle_kind::posix_descriptor, true}, true, false};
        source.next_deadline = deadline;
        saved = source;
        source.sockets.front().generation = 20;
        source.sockets.clear();
        source.wake.reset();
        source.next_deadline.reset();
    }
    LT_CHECK_EQ(saved.sockets.size(), std::size_t{1});
    LT_CHECK(saved.sockets.front().key == srv::socket_key{42});
    LT_CHECK_EQ(saved.sockets.front().generation, srv::registration_generation{9});
    LT_CHECK(saved.wake.has_value());
    LT_CHECK(saved.wake->key == srv::socket_key{43});
    LT_CHECK(saved.wake->readable && !saved.wake->writable);
    LT_CHECK(saved.next_deadline.has_value());
    LT_CHECK(*saved.next_deadline == deadline);
    LT_CHECK(*saved.next_deadline > clock_type::time_point{});
LT_END_AUTO_TEST(owned_snapshot_and_monotonic_deadline)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
