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

// Concurrent registration + lookup stress test for detail::ws_registry.
//
// ws_registry's class comment documents "its mutex is independent of
// every other cluster's; no call site holds two of them at once" --
// but unlike route_table (route_table_concurrency.cpp) and hook_bus
// (folded into threadsafety_stress.cpp), ws_registry had zero
// concurrency verification anywhere in the suite. This binary mirrors
// route_table_concurrency.cpp's pattern: N writer threads doing
// try_register/unregister and M reader threads doing find/empty
// against disjoint and overlapping keys. Without correct lock
// discipline in ws_registry's shared_mutex (unique_lock for
// try_register/unregister, shared_lock for find/empty), this test
// would deadlock, crash, or (under TSan) report a data race.
//
// Unlike route_table_concurrency.cpp, this test needs no webserver /
// MHD daemon at all -- ws_registry is a self-contained collaborator
// (it only stores/erases/copies shared_ptr<websocket_handler>, never
// dereferencing the pointee), so the stress driver talks to a bare
// instance directly.
//
// **TSan gate:** rebuild with `CXXFLAGS="-fsanitize=thread -g -O1"
// LDFLAGS="-fsanitize=thread"` and re-run this binary; the tsan CI lane
// (`build-type: tsan` in .github/workflows/verify-build.yml) picks it
// up automatically via `make check` (no separate wiring needed --
// route_table_concurrency required the RTC_ITERATIONS repeat gate
// because it is TASK-092's headline stress target; this binary rides
// the ordinary single-pass `make check` under that same tsan lane).

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "./httpserver.hpp"
#include "./httpserver/detail/ws_registry.hpp"
#include "./littletest.hpp"
#include "./test_utils.hpp"

namespace ht = httpserver;
namespace htd = httpserver::detail;

namespace {
class noop_ws_handler : public ht::websocket_handler {
 public:
    void on_message(ht::websocket_session&, std::string_view) override {}
};
}  // namespace

LT_BEGIN_SUITE(ws_registry_concurrency_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(ws_registry_concurrency_suite)

LT_BEGIN_AUTO_TEST(ws_registry_concurrency_suite,
                   concurrent_register_and_find_no_data_race)
    htd::ws_registry reg;

    // Pre-register a stable set of keys so readers always have
    // something to find regardless of writer timing.
    for (int i = 0; i < 32; ++i) {
        reg.try_register("/stable/" + std::to_string(i),
                         std::make_shared<noop_ws_handler>());
    }

    std::atomic<bool> stop{false};
    std::atomic<int> writer_ops{0};
    std::atomic<int> reader_ops{0};

    const int kWriters = stress_threads(4);
    const int kReaders = stress_threads(16);

    std::vector<std::thread> threads;
    threads.reserve(kWriters + kReaders);

    // Writers: register / unregister disjoint keys in their own numeric
    // range so try_register's duplicate-rejection path (false return)
    // is exercised only by the intentional re-register-after-unregister
    // sequence below, not by cross-writer collisions.
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&, w] {
            int counter = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                std::string key = "/dyn/" + std::to_string(w) + "/"
                                  + std::to_string(counter % 8);
                (void)reg.try_register(key, std::make_shared<noop_ws_handler>());
                reg.unregister(key);
                ++counter;
                writer_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Readers: hammer find()/empty() against the stable + dynamic keys.
    for (int r = 0; r < kReaders; ++r) {
        threads.emplace_back([&, r] {
            int counter = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                std::string key = "/stable/" + std::to_string(counter % 32);
                (void)reg.find(key);
                std::string key2 = "/dyn/" + std::to_string(r % 4) + "/"
                                   + std::to_string(counter % 8);
                (void)reg.find(key2);
                (void)reg.empty();
                ++counter;
                reader_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Run until enough operations have been observed by both roles, or
    // until stress_window() elapses (30s native/TSan; 3s under valgrind so
    // the helgrind/drd happens-before engine fits the CI budget). Mirrors
    // route_table_concurrency.cpp's threshold-based stop condition (avoids a
    // fixed wall-clock sleep).
    constexpr int kOpThreshold = 10000;
    auto deadline = std::chrono::steady_clock::now() + stress_window();
    while ((writer_ops.load() < kOpThreshold || reader_ops.load() < kOpThreshold)
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : threads) t.join();

    // We don't assert specific counts -- the gate is "completed without
    // deadlock or crash". TSan-detected races would break the build
    // when this TU is rebuilt under the manual TSan gate. Liveness (both
    // roles made progress) can be defeated by valgrind's single-core
    // scheduler, so only assert it off-valgrind (same posture as
    // route_table_concurrency.cpp).
    if (!under_valgrind()) {
        LT_CHECK(writer_ops.load() > 0);
        LT_CHECK(reader_ops.load() > 0);
    }
LT_END_AUTO_TEST(concurrent_register_and_find_no_data_race)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
