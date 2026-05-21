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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// TASK-027 Cycle I: concurrent registration + lookup stress test for the
// v2 3-tier route table. Spawns N writer threads doing register / unregister
// and M reader threads doing lookup_v2 against disjoint and overlapping
// paths. Without correct lock discipline (route_table_mutex_ as a writer
// lock during register/unregister, shared during lookup; cache mutex
// always taken AFTER table mutex when both are held), this test will
// either deadlock or race.
//
// **Manual TSan gate (documented but not on every CI run):**
// Rebuild with `CXXFLAGS="-fsanitize=thread -g -O1" LDFLAGS="-fsanitize=thread"`
// and re-run this test. Adding TSan as a CI matrix variant is a follow-up
// outside TASK-027 scope.

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "./httpserver.hpp"
#include "./httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;

class noop_resource : public ht::http_resource {
 public:
    ht::http_response render_get(const ht::http_request&) override {
        return ht::http_response::string("ok");
    }
};

LT_BEGIN_SUITE(route_table_concurrency_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(route_table_concurrency_suite)

LT_BEGIN_AUTO_TEST(route_table_concurrency_suite,
                   concurrent_register_and_lookup_no_data_race)
    ht::webserver ws{ht::create_webserver(8080)
                          .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    auto& impl = *ht::webserver_test_access::impl(ws);

    // Pre-register a stable set of 32 paths so readers always have
    // something to find.
    for (int i = 0; i < 32; ++i) {
        ws.register_path("/stable/" + std::to_string(i),
                         std::make_shared<noop_resource>());
    }

    std::atomic<bool> stop{false};
    std::atomic<int> writer_ops{0};
    std::atomic<int> reader_ops{0};

    constexpr int kWriters = 4;
    constexpr int kReaders = 16;

    std::vector<std::thread> threads;
    threads.reserve(kWriters + kReaders);

    // Writers: register / unregister disjoint paths in their own
    // numeric range to avoid duplicate-registration throws.
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&, w] {
            int counter = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                std::string p = "/dyn/" + std::to_string(w) + "/"
                                + std::to_string(counter % 8);
                try {
                    ws.register_path(p, std::make_shared<noop_resource>());
                    ws.unregister_path(p);
                } catch (...) {
                    // Race-on-duplicate is permitted under stress; we are
                    // exercising the lock discipline, not the user-facing
                    // duplicate-throw contract.
                }
                ++counter;
                writer_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Readers: hammer lookup_v2 against the stable + dynamic paths.
    for (int r = 0; r < kReaders; ++r) {
        threads.emplace_back([&, r] {
            int counter = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                std::string p = "/stable/" + std::to_string(counter % 32);
                (void)impl.lookup_v2(ht::http_method::get, p);
                std::string p2 = "/dyn/" + std::to_string(r % 4) + "/"
                                 + std::to_string(counter % 8);
                (void)impl.lookup_v2(ht::http_method::get, p2);
                ++counter;
                reader_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Run for ~500ms baseline (long enough to cover several context-
    // switch windows; short enough that `make check` stays under a
    // second on a normal box). Then keep going in 100ms chunks until
    // both counters are non-zero, up to a 30s safety ceiling — this
    // keeps the test reliable on heavily-instrumented lanes such as
    // valgrind/memcheck where a single register_path+regex-compile
    // can take longer than the entire baseline window.
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    while ((writer_ops.load() == 0 || reader_ops.load() == 0)
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : threads) t.join();

    // We don't assert specific counts — the gate is "completed without
    // deadlock or crash". TSan-detected races would break the build
    // when this same TU is rebuilt under the manual TSan gate.
    LT_CHECK(writer_ops.load() > 0);
    LT_CHECK(reader_ops.load() > 0);
LT_END_AUTO_TEST(concurrent_register_and_lookup_no_data_race)

// Cycle J: concurrent register/unregister of parameterised paths alongside
// readers. Exercises the radix tree's wildcard_child_ node allocation and
// deallocation paths under contention (DR-007 / DR-008). Writers use paths
// like /dyn/{id}/wN so the radix tree must allocate and free wildcard nodes
// concurrently with readers calling lookup_v2 on those same paths.
LT_BEGIN_AUTO_TEST(route_table_concurrency_suite,
                   concurrent_parameterized_register_and_lookup_no_data_race)
    ht::webserver ws{ht::create_webserver(8080)
                          .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    auto& impl = *ht::webserver_test_access::impl(ws);

    // Pre-register a stable set of parameterised paths so readers always
    // have something to find regardless of writer timing.
    for (int i = 0; i < 16; ++i) {
        ws.register_path("/stable/{id}/item/" + std::to_string(i),
                         std::make_shared<noop_resource>());
    }

    std::atomic<bool> stop{false};
    std::atomic<int> writer_ops{0};
    std::atomic<int> reader_ops{0};

    constexpr int kWriters = 4;
    constexpr int kReaders = 8;

    std::vector<std::thread> threads;
    threads.reserve(kWriters + kReaders);

    // Writers: concurrently register / unregister parameterised paths with
    // different writer indices so each writer owns a disjoint numeric prefix
    // for the wildcard segment, exercising the radix tree's wildcard_child_
    // allocation and deallocation path concurrently.
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&, w] {
            int counter = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                std::string p = "/dyn/{id}/w" + std::to_string(w)
                                + "/" + std::to_string(counter % 8);
                try {
                    ws.register_path(p, std::make_shared<noop_resource>());
                    ws.unregister_path(p);
                } catch (...) {
                    // Duplicate-registration races are tolerated; the
                    // lock-discipline gate is "no crash or deadlock".
                }
                ++counter;
                writer_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Readers: lookup both the stable parameterised paths and the paths
    // under concurrent mutation to exercise the shared_lock path on the
    // radix tree's wildcard_child_ node.
    for (int r = 0; r < kReaders; ++r) {
        threads.emplace_back([&, r] {
            int counter = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                // Stable parameterised path — always found.
                std::string p = "/stable/42/item/" + std::to_string(counter % 16);
                (void)impl.lookup_v2(ht::http_method::get, p);
                // Dynamic path — may or may not be registered at this instant.
                std::string p2 = "/dyn/99/w" + std::to_string(r % kWriters)
                                 + "/" + std::to_string(counter % 8);
                (void)impl.lookup_v2(ht::http_method::get, p2);
                ++counter;
                reader_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Same baseline + valgrind-tolerant extension as Cycle I above.
    auto deadline2 =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    while ((writer_ops.load() == 0 || reader_ops.load() == 0)
           && std::chrono::steady_clock::now() < deadline2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : threads) t.join();

    LT_CHECK(writer_ops.load() > 0);
    LT_CHECK(reader_ops.load() > 0);
LT_END_AUTO_TEST(concurrent_parameterized_register_and_lookup_no_data_race)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
