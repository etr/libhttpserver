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

// TASK-119 step 1: a consumer including <httpserver/net/address.hpp>
// WITHOUT HTTPSERVER_COMPILATION (or any other build/TLS configuration
// macro) must compile and link cleanly. The parsing and text
// conversion live in the library, so every check here is a
// static_assert over unevaluated expressions (the consumer_v3_hooks
// convention): the empty LDADD of this target proves the vocabulary
// surface itself needs no linkage.

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

#include <httpserver/net/address.hpp>

namespace {

namespace net = httpserver::net;

// The family enum stays one byte and starts at unspec.
static_assert(std::is_same_v<std::underlying_type_t<net::address_family>,
                             std::uint8_t>);
static_assert(net::address_family::unspec < net::address_family::ipv4);
static_assert(net::address_family::ipv4 < net::address_family::ipv6);

// The value types are aggregates with defaulted equality.
static_assert(std::is_aggregate_v<net::address>);
static_assert(std::is_aggregate_v<net::peer_address>);
static_assert(std::is_aggregate_v<net::address_pattern>);
static_assert(std::is_default_constructible_v<net::address>);
static_assert(std::is_same_v<decltype(net::address{}.family),
                             net::address_family>);
static_assert(std::is_same_v<decltype(net::peer_address{}.port),
                             std::uint16_t>);
static_assert(std::is_same_v<decltype(net::address_pattern{}.prefix_bits),
                             unsigned>);
static_assert(std::is_same_v<decltype(std::declval<const net::address&>()
                                          .to_string()),
                             std::string>);
static_assert(std::is_same_v<decltype(
                  std::declval<const net::peer_address&>().to_string()),
                             std::string>);
static_assert(std::is_nothrow_invocable_v<
              decltype(&net::address_pattern::matches),
              const net::address_pattern&, const net::address&>);

// The free functions return optionals of the vocabulary values.
static_assert(std::is_same_v<decltype(net::parse_address("")),
                             std::optional<net::address>>);
static_assert(std::is_same_v<decltype(net::parse_pattern("")),
                             std::optional<net::address_pattern>>);

}  // namespace

int main() {
    // The compile is the contract; nothing runs.
    return 0;
}
