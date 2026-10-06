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

// Standalone consumer: this public header must supply its own dependencies.
#include <httpserver/server/readiness.hpp>

#include <type_traits>  // NOLINT(build/include_order): readiness must be first.
#include <vector>  // NOLINT(build/include_order): readiness must be first.

namespace srv = httpserver::server;
using clock_type = std::chrono::steady_clock;

static_assert(std::is_same_v<srv::registration_generation, std::uint64_t>);
static_assert(!std::is_convertible_v<std::uint64_t, srv::socket_key>);
static_assert(std::has_virtual_destructor_v<srv::readiness_driver>);
static_assert(std::is_abstract_v<srv::readiness_driver>);
static_assert(std::is_same_v<decltype(&srv::readiness_driver::interests),
              srv::interest_snapshot(srv::readiness_driver::*)() const>);
static_assert(std::is_same_v<decltype(&srv::readiness_driver::dispatch),
              httpserver::http::outcome(srv::readiness_driver::*)(
                  std::span<const srv::readiness_event>, clock_type::time_point)>);

// Fixture only; it demonstrates calls through the port, not engine behavior.
class fixture_driver final : public srv::readiness_driver {
 public:
    srv::interest_snapshot interests() const override {
        srv::interest_snapshot snapshot;
        snapshot.sockets.push_back({srv::socket_key{1}, 4,
            {0, srv::native_handle_kind::posix_descriptor, true}, true, false});
        snapshot.wake = srv::socket_interest{srv::socket_key{2}, 1,
            {3, srv::native_handle_kind::posix_descriptor, true}, true, false};
        snapshot.next_deadline = clock_type::time_point{};
        return snapshot;
    }

    httpserver::http::outcome dispatch(std::span<const srv::readiness_event>,
                                     clock_type::time_point) override {
        return {};
    }
};

int main() {
    fixture_driver fixture;
    srv::readiness_driver& driver = fixture;
    auto snapshot = driver.interests();
    // A host saves tokens with its registrations and translates native flags.
    const auto& registration = snapshot.sockets.front();
    const std::vector<srv::readiness_event> events{{registration.key,
        registration.generation, true, false, false, false}};
    const auto result = driver.dispatch(events, clock_type::now());
    if (!result.ok()) return 1;
    snapshot = driver.interests();
    return snapshot.wake && snapshot.next_deadline ? 0 : 1;
}
