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

// TASK-119 step 6: parity replay of the ip_controls.tseq corpus (the
// pinned allow_listed_loopback case: REJECT-everything with loopback
// allow-listed answers the v2 wire byte-for-byte) through the REAL
// registry + dispatcher + response framer with the peer policy in the
// loop -- plus the refusal twin the transcript deliberately does NOT
// pin (v2's deny wire shape was timing-dependent): the v3 pinned shape
// is zero application bytes with exactly one request_completed
// (succeeded=false, end=peer_refused).

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/net/address.hpp>
#include <httpserver/server/hooks.hpp>
#include <httpserver/server/peer_policy.hpp>
#include <httpserver/server/routes.hpp>
#include <parity/case_wire.hpp>
#include <parity/normalize.hpp>
#include <parity/response_frame.hpp>
#include <parity/transcript.hpp>
#include <parity/v3_dispatch_replay.hpp>

#include "./littletest.hpp"

namespace {

namespace replay = parity::v3_replay;
namespace srv = httpserver::server;
namespace net = httpserver::net;
namespace http = httpserver::http;
using parity::observed_response;
using parity::response_frame_parser;
using parity::tcase;

const tcase* find_case(const parity::transcript& t, const char* name) {
    for (const tcase& c : t.cases) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

// The transcript's loopback peer.
net::peer_address loopback_peer() {
    net::peer_address peer;
    peer.address =
        net::parse_address("127.0.0.1").value_or(net::address{});
    peer.port = 40000;
    return peer;
}

}  // namespace

LT_BEGIN_SUITE(ip_controls_corpus_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(ip_controls_corpus_suite)

// The pinned case: profile ip_controls (default-REJECT, loopback
// allow-listed) answers GET /hello exactly as v2 did -- status 200,
// text/plain, Content-Length: 2, body "OK", content-length framing,
// keep-alive.
LT_BEGIN_AUTO_TEST(ip_controls_corpus_suite, allow_listed_loopback_replay)
    const parity::transcript t = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR "/ip_controls.tseq"));
    const tcase* found = find_case(t, "allow_listed_loopback");
    LT_CHECK(found != nullptr);
    if (found == nullptr) return;

    srv::budget_limits limits;
    limits.set(srv::resource::routes, 8);
    const srv::resource_budget budget = srv::resource_budget::root(limits);
    const srv::route_registry registry = replay::build_routing_basic(budget);
    srv::hook_bus bus;
    const httpserver::detail::error_page_factories pages;

    srv::peer_policy policy;
    policy.set_mode(srv::peer_policy_mode::reject_all);
    LT_CHECK(policy.allow("127.0.0.1").ok());

    std::string wire;
    for (const replay::replay_request& request :
         replay::requests_of(*found)) {
        bool ok = false;
        const replay::replay_result one = replay::dispatch_once(
            registry, bus, pages, request, &ok, loopback_peer(), &policy);
        LT_CHECK(ok);
        if (!one.keep_alive) {
            std::cerr << "[replay allow_listed_loopback] closed\n";
            LT_CHECK(one.keep_alive);
        }
        wire.append(one.wire);
    }
    LT_CHECK(!wire.empty());

    response_frame_parser parser;
    std::vector<observed_response> responses = parser.feed(wire);
    for (observed_response& r : parser.finish()) {
        responses.push_back(std::move(r));
    }
    LT_CHECK(!parser.failed());
    LT_CHECK_EQ(responses.size(), std::size_t{1});
    if (responses.size() != 1) return;
    const parity::normalized_exchange exchange =
        parity::normalize(responses[0]);
    for (const parity::expectation& e : found->expects) {
        if (e.kind == parity::expect_kind::closer) continue;
        const parity::match_result m = parity::check_expectation(
            e, exchange, PARITY_TRANSCRIPT_DIR);
        if (!m.ok) {
            std::cerr << "[replay allow_listed_loopback] " << m.diff
                      << "\n";
        }
        LT_CHECK(m.ok);
    }
LT_END_AUTO_TEST(allow_listed_loopback_replay)

// The refusal twin (NOT pinned by the v2 transcript -- its deny wire
// was timing-dependent): accept_all with the loopback denied settles
// the exchange with ZERO response bytes and exactly one
// request_completed observation: succeeded=false, end=peer_refused.
LT_BEGIN_AUTO_TEST(ip_controls_corpus_suite, denied_peer_refusal_twin)
    const parity::transcript t = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR "/ip_controls.tseq"));
    const tcase* found = find_case(t, "allow_listed_loopback");
    LT_CHECK(found != nullptr);
    if (found == nullptr) return;

    srv::budget_limits limits;
    limits.set(srv::resource::routes, 8);
    const srv::resource_budget budget = srv::resource_budget::root(limits);
    const srv::route_registry registry = replay::build_routing_basic(budget);
    srv::hook_bus bus;
    const httpserver::detail::error_page_factories pages;

    srv::peer_policy policy;
    LT_CHECK(policy.deny("127.0.0.1").ok());

    int completed = 0;
    int succeeded_failures = 0;
    int peer_refused_ends = 0;
    (void)bus.add<srv::hook_phase::request_completed>(
        [&completed, &succeeded_failures, &peer_refused_ends](
            srv::request_completed_ctx& c) -> srv::hook_action {
            ++completed;
            if (!c.succeeded) ++succeeded_failures;
            if (c.end.code() == http::outcome_code::peer_refused) {
                ++peer_refused_ends;
            }
            return srv::hook_action::pass();
        }).detach();

    for (const replay::replay_request& request :
         replay::requests_of(*found)) {
        bool ok = false;
        const replay::replay_result one = replay::dispatch_once(
            registry, bus, pages, request, &ok, loopback_peer(), &policy);
        // The dispatcher itself completes normally; the refusal is
        // the exchange's settle, not a task failure.
        LT_CHECK(ok);
        LT_CHECK(one.wire.empty());
    }
    LT_CHECK_EQ(completed, 1);
    LT_CHECK_EQ(succeeded_failures, 1);
    LT_CHECK_EQ(peer_refused_ends, 1);
LT_END_AUTO_TEST(denied_peer_refusal_twin)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
