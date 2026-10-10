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
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stop_token>
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
    // Long unmatched scans establish overlapping reader ownership before writers.
    for (int i = 0; i < 1024; ++i) {
        LT_CHECK(policy.deny("192.0." + std::to_string(i / 256) + "."
                             + std::to_string(i % 256)).ok());
    }
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> readers_ready{0};
    std::atomic<int> writers_done{0};
    std::atomic<int> mutations{0};
    std::atomic<int> reads{0};
    std::atomic<bool> valid{true};
    std::vector<std::jthread> threads;
    for (int t = 0; t < 6; ++t) {
        threads.emplace_back([&](std::stop_token token) {
            const auto probe = peer("10.4.0.1");
            readers_ready.fetch_add(1);
            while (!start.load() && !token.stop_requested()) std::this_thread::yield();
            while (!stop.load() && !token.stop_requested()) {
                const auto verdict = policy.classify(probe);
                if (verdict != accepted()
                        && verdict != refused(peer_refusal::denied)) valid = false;
                static_cast<void>(policy.enabled());
                static_cast<void>(policy.mode());
                reads.fetch_add(1);
            }
        });
    }
    while (readers_ready.load() != 6) std::this_thread::yield();
    start = true;
    while (reads.load() < 600) std::this_thread::yield();
    const auto began = std::chrono::steady_clock::now();
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&, t](std::stop_token token) {
            try {
                for (int i = 0; i < 200 && !stop.load() && !token.stop_requested(); ++i) {
                    const std::string pattern = "10."
                        + std::to_string(2 * (i % 25) + t) + ".0.0/16";
                    const auto outcome = (i + t) % 2 == 0
                        ? policy.deny(pattern) : policy.remove_denied(pattern);
                    if (!outcome.ok()) valid = false;
                    mutations.fetch_add(1);
                }
            } catch (...) { valid = false; }
            writers_done.fetch_add(1);
        });
    }
    const auto deadline = began + std::chrono::seconds(15);
    while (writers_done.load() != 2 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool completed = writers_done.load() == 2;
    const int before_cleanup = mutations.load();
    const int reader_progress = reads.load();
    // Record failure before releasing reader pressure; assert only after joins.
    stop = true;
    for (auto& thread : threads) thread.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();
    std::printf("peer progress: completed=%d mutations=%d/400 cleanup=%d "
                "reads=%d elapsed_ms=%jd bound_ms=15000\n", completed,
                before_cleanup, mutations.load(), reader_progress,
                static_cast<std::intmax_t>(elapsed));
    LT_CHECK(completed);
    LT_CHECK_EQ(before_cleanup, 400);
    LT_CHECK(reader_progress >= 600);
    LT_CHECK(valid.load());
    LT_CHECK(policy.deny("203.0.113.1").ok());
    LT_CHECK(policy.classify(peer("203.0.113.1")) == refused(peer_refusal::denied));
LT_END_AUTO_TEST(concurrent_mutation_and_reads)

LT_BEGIN_AUTO_TEST(peer_policy_suite, independent_writers_retain_additions_and_removals)
    peer_policy policy;
    std::atomic<bool> start{false};
    std::atomic<bool> valid{true};
    auto mutate = [&](std::stop_token token, int t, bool remove) {
        while (!start.load() && !token.stop_requested()) std::this_thread::yield();
        if (token.stop_requested()) return;
        try {
            for (int i = 0; i < 200; ++i) {
                const auto pattern = "10." + std::to_string(t) + ".0." + std::to_string(i);
                if (!(remove ? policy.remove_denied(pattern) : policy.deny(pattern)).ok()
                    || !(remove ? policy.remove_allowed(pattern) : policy.allow(pattern)).ok())
                    valid = false;
            }
        } catch (...) { valid = false; }
    };
    for (bool remove : {false, true}) {
        start = false;
        std::jthread first(mutate, 1, remove);
        std::jthread second(mutate, 2, remove);
        std::jthread stance([&](std::stop_token token) {
            while (!start.load() && !token.stop_requested()) std::this_thread::yield();
            for (int i = 0; i < 400 && !token.stop_requested(); ++i) {
                policy.set_enabled(i % 2 == 0);
                policy.set_mode(i % 2 ? peer_policy_mode::accept_all : peer_policy_mode::reject_all);
            }
        });
        start = true;
        first.join();
        second.join();
        stance.join();
        policy.set_enabled(true);
        for (int t : {1, 2}) {
            for (int i = 0; i < 200; ++i) {
                const auto address = "10." + std::to_string(t) + ".0." + std::to_string(i);
                policy.set_mode(peer_policy_mode::accept_all);
                LT_CHECK(policy.classify(peer(address.c_str())) == accepted());
                policy.set_mode(peer_policy_mode::reject_all);
                LT_CHECK(policy.classify(peer(address.c_str())) == refused(remove
                    ? peer_refusal::not_on_allow_list : peer_refusal::denied));
            }
        }
    }
    LT_CHECK(valid.load());
LT_END_AUTO_TEST(independent_writers_retain_additions_and_removals)

LT_BEGIN_AUTO_TEST(peer_policy_suite, enabled_mode_generations_remain_complete)
    peer_policy policy;
    LT_CHECK(policy.deny("10.0.0.1").ok());
    LT_CHECK(policy.allow("10.0.0.1").ok());
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::atomic<bool> valid{true};
    std::vector<std::jthread> readers;
    for (int i = 0; i < 6; ++i) readers.emplace_back([&](std::stop_token token) {
        while (!stop.load() && !token.stop_requested()) {
            const auto both = policy.classify(peer("10.0.0.1"));
            const auto unspec = policy.classify(net::peer_address{});
            const auto other = policy.classify(peer("::1"));
            if ((both != accepted() && both != refused(peer_refusal::denied))
                || (unspec != accepted() && unspec != refused(peer_refusal::not_on_allow_list))
                || (other != accepted() && other != refused(peer_refusal::not_on_allow_list)))
                valid = false;
            reads.fetch_add(1);
        }
    });
    while (reads.load() < 600) std::this_thread::yield();
    for (int i = 0; i < 400; ++i) {
        policy.set_enabled(false);
        policy.set_mode(i % 2 ? peer_policy_mode::accept_all : peer_policy_mode::reject_all);
        policy.set_enabled(true);
    }
    stop = true;
    for (auto& thread : readers) thread.join();
    LT_CHECK(valid.load());
    policy.set_enabled(false);
    LT_CHECK(!policy.armed());
    LT_CHECK(policy.classify(peer("10.0.0.1")) == accepted());
    policy.set_mode(peer_policy_mode::reject_all);
    policy.set_enabled(true);
    LT_CHECK(policy.armed());
    LT_CHECK(policy.classify(peer("10.0.0.1")) == refused(peer_refusal::denied));
LT_END_AUTO_TEST(enabled_mode_generations_remain_complete)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
