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

// TASK-119 step 2: a consumer including
// <httpserver/server/peer_policy.hpp> WITHOUT HTTPSERVER_COMPILATION
// (or any other build/TLS configuration macro) must compile and link
// cleanly. The store lives in the library, so every check here is a
// static_assert over unevaluated expressions (the consumer_v3_hooks
// convention): the empty LDADD of this target proves the vocabulary
// surface itself needs no linkage.

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <httpserver/http/outcome.hpp>
#include <httpserver/net/address.hpp>
#include <httpserver/server/peer_policy.hpp>

namespace {

namespace http = httpserver::http;
namespace net = httpserver::net;
namespace srv = httpserver::server;

using srv::peer_policy;
using srv::peer_policy_mode;
using srv::peer_refusal;
using srv::peer_verdict;

// The enums stay one byte.
static_assert(std::is_same_v<std::underlying_type_t<peer_policy_mode>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<peer_refusal>,
                             std::uint8_t>);
static_assert(peer_refusal::none < peer_refusal::denied);
static_assert(peer_refusal::denied < peer_refusal::not_on_allow_list);
static_assert(peer_policy_mode::accept_all < peer_policy_mode::reject_all);

// The verdict is a plain aggregate with defaulted equality.
static_assert(std::is_aggregate_v<peer_verdict>);
static_assert(std::is_same_v<decltype(peer_verdict{}.accepted), bool>);
static_assert(std::is_same_v<decltype(peer_verdict{}.reason),
                             peer_refusal>);

// The policy class shape: non-copyable, non-movable (references into
// it must stay stable for the listener engines' live window).
static_assert(!std::is_copy_constructible_v<peer_policy>);
static_assert(!std::is_move_constructible_v<peer_policy>);
static_assert(std::is_default_constructible_v<peer_policy>);
static_assert(std::is_same_v<decltype(std::declval<peer_policy&>()
                                          .deny(std::string_view{})),
                             http::outcome>);
static_assert(std::is_same_v<decltype(std::declval<peer_policy&>()
                                          .allow(std::string_view{})),
                             http::outcome>);
static_assert(std::is_same_v<decltype(std::declval<peer_policy&>()
                                          .remove_denied(
                                              std::string_view{})),
                             http::outcome>);
static_assert(std::is_same_v<decltype(std::declval<peer_policy&>()
                                          .remove_allowed(
                                              std::string_view{})),
                             http::outcome>);
static_assert(std::is_same_v<
              decltype(std::declval<peer_policy&>().set_mode(
                  peer_policy_mode{})),
              void>);
static_assert(std::is_same_v<
              decltype(std::declval<const peer_policy&>().mode()),
              peer_policy_mode>);
static_assert(std::is_same_v<
              decltype(std::declval<peer_policy&>().set_enabled(true)),
              void>);
static_assert(std::is_same_v<
              decltype(std::declval<const peer_policy&>().enabled()),
              bool>);
static_assert(std::is_same_v<
              decltype(std::declval<const peer_policy&>().armed()),
              bool>);
static_assert(std::is_same_v<
              decltype(std::declval<const peer_policy&>().classify(
                  std::declval<const net::peer_address&>())),
              peer_verdict>);

}  // namespace

int main() {
    // The compile is the contract; nothing runs.
    return 0;
}
