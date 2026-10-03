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

// TASK-119 step 2: the peer policy store (server/peer_policy.hpp):
// the v2 classify_decision truth table verbatim, the allow-overrides-
// deny precedence, the wildcard-weight insert rule in both directions
// (unit twins of the two v2 integ cases), remove idempotence, typed
// pattern failures, armed() transitions, the disabled posture, the
// unspec peer rule, and concurrent deny/classify.

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <httpserver/net/address.hpp>
#include <httpserver/server/peer_policy.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;
namespace net = httpserver::net;
namespace srv = httpserver::server;

using srv::peer_policy;
using srv::peer_policy_mode;
using srv::peer_refusal;
using srv::peer_verdict;

net::peer_address peer(const char* text, std::uint16_t port = 0) {
    net::peer_address out;
    out.address = net::parse_address(text).value_or(net::address{});
    out.port = port;
    return out;
}

peer_verdict refused(peer_refusal reason) {
    return peer_verdict{false, reason};
}

peer_verdict accepted() {
    return peer_verdict{true, peer_refusal::none};
}

}  // namespace

LT_BEGIN_SUITE(peer_policy_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(peer_policy_suite)

LT_BEGIN_AUTO_TEST(peer_policy_suite, truth_table_accept_all)
    peer_policy policy;
    LT_CHECK(policy.classify(peer("10.0.0.1")) == accepted());
    LT_CHECK(policy.deny("10.0.0.1").ok());
    LT_CHECK(policy.classify(peer("10.0.0.1"))
             == refused(peer_refusal::denied));
    LT_CHECK(policy.classify(peer("10.0.0.2")) == accepted());
    // allow overrides deny under accept_all.
    LT_CHECK(policy.allow("10.0.0.1").ok());
    LT_CHECK(policy.classify(peer("10.0.0.1")) == accepted());
LT_END_AUTO_TEST(truth_table_accept_all)

LT_BEGIN_AUTO_TEST(peer_policy_suite, truth_table_reject_all)
    peer_policy policy;
    policy.set_mode(peer_policy_mode::reject_all);
    // Neither denied nor allowed: not on the allow list.
    LT_CHECK(policy.classify(peer("10.0.0.1"))
             == refused(peer_refusal::not_on_allow_list));
    LT_CHECK(policy.allow("10.0.0.1").ok());
    LT_CHECK(policy.classify(peer("10.0.0.1")) == accepted());
    // Denied still refuses under reject_all, allow notwithstanding
    // (the deny arm is checked first; the reason stays "denied").
    LT_CHECK(policy.deny("10.0.0.1").ok());
    LT_CHECK(policy.classify(peer("10.0.0.1"))
             == refused(peer_refusal::denied));
    // Denied but not allowed: reason "denied", not the allow-list one.
    LT_CHECK(policy.remove_allowed("10.0.0.1").ok());
    LT_CHECK(policy.classify(peer("10.0.0.1"))
             == refused(peer_refusal::denied));
LT_END_AUTO_TEST(truth_table_reject_all)

LT_BEGIN_AUTO_TEST(peer_policy_suite, wildcard_weight_both_directions)
    // v2 integ twin 1: inserting the wider pattern after the narrower
    // one keeps only the wider (the narrower is covered).
    peer_policy wider_last;
    LT_CHECK(wider_last.deny("127.0.0.1").ok());
    LT_CHECK(wider_last.deny("127.0.0.*").ok());
    LT_CHECK(wider_last.classify(peer("127.0.0.9"))
             == refused(peer_refusal::denied));
    // v2 integ twin 2: inserting the narrower pattern after the wider
    // one is a no-op (already covered).
    peer_policy wider_first;
    LT_CHECK(wider_first.deny("127.0.0.*").ok());
    LT_CHECK(wider_first.deny("127.0.0.1").ok());
    LT_CHECK(wider_first.classify(peer("127.0.0.9"))
             == refused(peer_refusal::denied));
    // Removing the wide spelling drops the single stored entry; the
    // narrow insert never landed as a second one.
    LT_CHECK(wider_first.remove_denied("127.0.0.*").ok());
    LT_CHECK(wider_first.classify(peer("127.0.0.9")) == accepted());
    LT_CHECK(wider_first.classify(peer("127.0.0.1")) == accepted());
LT_END_AUTO_TEST(wildcard_weight_both_directions)

LT_BEGIN_AUTO_TEST(peer_policy_suite, remove_is_idempotent_and_typed)
    peer_policy policy;
    LT_CHECK(policy.deny("10.0.0.0/8").ok());
    // Removing a present pattern twice: the second is a no-op ok.
    LT_CHECK(policy.remove_denied("10.0.0.0/8").ok());
    LT_CHECK(policy.remove_denied("10.0.0.0/8").ok());
    // Removing a never-inserted pattern: ok.
    LT_CHECK(policy.remove_allowed("192.0.2.1").ok());
    // A non-parsing spelling fails typed either way.
    const http::outcome bad_deny = policy.deny("127.*.0.1");
    LT_CHECK(!bad_deny.ok());
    LT_CHECK_EQ(static_cast<int>(bad_deny.code()),
                static_cast<int>(http::outcome_code::invalid_argument));
    const http::outcome bad_remove = policy.remove_denied("nonsense");
    LT_CHECK(!bad_remove.ok());
    LT_CHECK_EQ(static_cast<int>(bad_remove.code()),
                static_cast<int>(http::outcome_code::invalid_argument));
LT_END_AUTO_TEST(remove_is_idempotent_and_typed)

LT_BEGIN_AUTO_TEST(peer_policy_suite, armed_tracks_the_stance)
    peer_policy policy;
    LT_CHECK(!policy.armed());
    LT_CHECK(policy.deny("10.0.0.1").ok());
    LT_CHECK(policy.armed());
    LT_CHECK(policy.remove_denied("10.0.0.1").ok());
    LT_CHECK(!policy.armed());
    LT_CHECK(policy.allow("10.0.0.1").ok());
    LT_CHECK(policy.armed());
    LT_CHECK(policy.remove_allowed("10.0.0.1").ok());
    LT_CHECK(!policy.armed());
    policy.set_mode(peer_policy_mode::reject_all);
    LT_CHECK(policy.mode() == peer_policy_mode::reject_all);
    LT_CHECK(policy.armed());
    policy.set_mode(peer_policy_mode::accept_all);
    LT_CHECK(policy.mode() == peer_policy_mode::accept_all);
    LT_CHECK(!policy.armed());
    // Enabled default true; disabling disarms without losing state.
    LT_CHECK(policy.enabled());
    LT_CHECK(policy.deny("10.0.0.1").ok());
    policy.set_enabled(false);
    LT_CHECK(!policy.enabled());
    LT_CHECK(!policy.armed());
    policy.set_enabled(true);
    LT_CHECK(policy.armed());
LT_END_AUTO_TEST(armed_tracks_the_stance)

LT_BEGIN_AUTO_TEST(peer_policy_suite, disabled_admits_every_peer)
    peer_policy policy;
    policy.set_mode(peer_policy_mode::reject_all);
    LT_CHECK(policy.deny("0.0.0.0/0").ok());
    policy.set_enabled(false);
    LT_CHECK(policy.classify(peer("10.0.0.1")) == accepted());
    // The lists survive: re-enabling restores the stance.
    policy.set_enabled(true);
    LT_CHECK(policy.classify(peer("10.0.0.1"))
             == refused(peer_refusal::denied));
LT_END_AUTO_TEST(disabled_admits_every_peer)

LT_BEGIN_AUTO_TEST(peer_policy_suite, unspec_peer_rules)
    peer_policy policy;
    // accept_all: an unspec peer matches nothing and is admitted.
    LT_CHECK(policy.classify(net::peer_address{}) == accepted());
    policy.set_mode(peer_policy_mode::reject_all);
    // reject_all: an unspec peer is never on the allow list.
    LT_CHECK(policy.classify(net::peer_address{})
             == refused(peer_refusal::not_on_allow_list));
    // Even a catch-all allow does not match an unspec peer.
    LT_CHECK(policy.allow("::/0").ok());
    LT_CHECK(policy.classify(net::peer_address{})
             == refused(peer_refusal::not_on_allow_list));
LT_END_AUTO_TEST(unspec_peer_rules)

LT_BEGIN_AUTO_TEST(peer_policy_suite, families_and_ports_do_not_bleed)
    peer_policy policy;
    policy.set_mode(peer_policy_mode::reject_all);
    LT_CHECK(policy.allow("127.0.0.1").ok());
    LT_CHECK(policy.classify(peer("127.0.0.1", 80)) == accepted());
    // The port is not part of the match.
    LT_CHECK(policy.classify(peer("127.0.0.1", 9999)) == accepted());
    // The v6 loopback is a different address.
    LT_CHECK(policy.classify(peer("::1"))
             == refused(peer_refusal::not_on_allow_list));
    LT_CHECK(policy.allow("::1").ok());
    LT_CHECK(policy.classify(peer("::1")) == accepted());
LT_END_AUTO_TEST(families_and_ports_do_not_bleed)

LT_BEGIN_AUTO_TEST(peer_policy_suite, concurrent_mutation_and_reads)
    peer_policy policy;
    std::atomic<bool> stop{false};
    std::atomic<int> refusals{0};
    std::vector<std::thread> threads;
    // Two mutators: deny odd / allow even second-octet patterns.
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&policy, t] {
            for (int i = 0; i < 200; ++i) {
                const std::string pattern =
                    "10." + std::to_string(2 * (i % 25) + t) + ".0.0/16";
                if ((i + t) % 2 == 0) {
                    static_cast<void>(policy.deny(pattern));
                } else {
                    static_cast<void>(policy.remove_denied(pattern));
                }
            }
        });
    }
    // Six classifiers over one hot address.
    for (int t = 0; t < 6; ++t) {
        threads.emplace_back([&policy, &stop, &refusals] {
            const net::peer_address probe = peer("10.4.0.1");
            while (!stop.load(std::memory_order_acquire)) {
                if (!policy.classify(probe).accepted) refusals.fetch_add(1);
                // armed implies enabled (the invariant the gate sums up).
                if (policy.armed()) {
                    if (!policy.enabled()) refusals.store(-1);
                }
            }
        });
    }
    for (std::size_t i = 0; i < 2; ++i) {
        threads[i].join();
    }
    stop.store(true, std::memory_order_release);
    for (std::size_t i = 2; i < threads.size(); ++i) {
        threads[i].join();
    }
    LT_CHECK(refusals.load() >= 0);
    // The store stays consistent after the storm.
    const http::outcome settled = policy.deny("203.0.113.1");
    LT_CHECK(settled.ok());
    LT_CHECK(policy.classify(peer("203.0.113.1"))
             == refused(peer_refusal::denied));
LT_END_AUTO_TEST(concurrent_mutation_and_reads)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
