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

// TASK-108 step 2: the native server's thread-pool executor
// (detail::worker_pool). Pins: the workers == 0 auto-resolution
// (hardware_concurrency clamped to [1, 16]), explicit thread counts,
// FIFO execution order on the shared queue, the executor seam
// (current_executor visible inside posted work), the drain-and-join
// contract (every queued item runs to completion before drain returns,
// on the workers or, once they are joined, on the calling thread), and
// the destructor's implicit drain. Waiting is deadline-bound: the pass
// conditions are the work items' completion, never a sleep.

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/detail/worker_pool.hpp>

#include "./littletest.hpp"

namespace {

namespace pool_ns = httpserver::detail;

// Deadline-bound condition wait: false only when @p pred stayed false
// through the whole budget (the failure path), never a pass condition.
template<typename Pred>
bool wait_until(Pred pred) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(10000);
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

}  // namespace

LT_BEGIN_SUITE(worker_pool_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(worker_pool_suite)

// workers == 0 resolves to the detected parallelism, clamped to
// [1, 16].
LT_BEGIN_AUTO_TEST(worker_pool_suite, zero_workers_auto_resolves)
    pool_ns::worker_pool pool(0);
    LT_CHECK(pool.thread_count() >= 1);
    LT_CHECK(pool.thread_count() <= 16);
LT_END_AUTO_TEST(zero_workers_auto_resolves)

// An explicit count is honored verbatim.
LT_BEGIN_AUTO_TEST(worker_pool_suite, explicit_thread_count)
    pool_ns::worker_pool pool(3);
    LT_CHECK_EQ(pool.thread_count(), std::size_t{3});
LT_END_AUTO_TEST(explicit_thread_count)

// Posted work runs to completion.
LT_BEGIN_AUTO_TEST(worker_pool_suite, posted_work_executes)
    pool_ns::worker_pool pool(2);
    std::atomic<int> done{0};
    for (int i = 0; i < 10; ++i) {
        pool.post([&done] { ++done; });
    }
    LT_CHECK(wait_until([&done] { return done.load() == 10; }));
LT_END_AUTO_TEST(posted_work_executes)

// One worker executes the shared FIFO in post order.
LT_BEGIN_AUTO_TEST(worker_pool_suite, fifo_execution_order)
    pool_ns::worker_pool pool(1);
    std::mutex guard;
    std::vector<int> seen;
    for (int i = 0; i < 64; ++i) {
        pool.post([&guard, &seen, i] {
            std::lock_guard<std::mutex> lock(guard);
            seen.push_back(i);
        });
    }
    LT_CHECK(wait_until([&guard, &seen] {
        std::lock_guard<std::mutex> lock(guard);
        return seen.size() == 64;
    }));
    for (int i = 0; i < 64; ++i) {
        std::lock_guard<std::mutex> lock(guard);
        LT_CHECK_EQ(seen[i], i);
    }
LT_END_AUTO_TEST(fifo_execution_order)

// Inside posted work the executor seam reports this pool.
LT_BEGIN_AUTO_TEST(worker_pool_suite, is_current_inside_work)
    pool_ns::worker_pool pool(1);
    std::atomic<bool> inside{false};
    std::atomic<bool> finished{false};
    pool.post([&pool, &inside, &finished] {
        inside.store(httpserver::current_executor()
                     == static_cast<httpserver::executor*>(&pool));
        finished.store(true);
    });
    LT_CHECK(wait_until([&finished] { return finished.load(); }));
    LT_CHECK(inside.load());
    LT_CHECK(!pool.is_current());
LT_END_AUTO_TEST(is_current_inside_work)

// drain_and_join runs every queued item before returning.
LT_BEGIN_AUTO_TEST(worker_pool_suite, drain_runs_pending)
    pool_ns::worker_pool pool(2);
    std::atomic<int> done{0};
    for (int i = 0; i < 50; ++i) {
        pool.post([&done] { ++done; });
    }
    pool.drain_and_join();
    LT_CHECK_EQ(done.load(), 50);
LT_END_AUTO_TEST(drain_runs_pending)

// The destructor drains: nothing queued is lost.
LT_BEGIN_AUTO_TEST(worker_pool_suite, destructor_drains)
    std::atomic<int> done{0};
    {
        pool_ns::worker_pool pool(2);
        for (int i = 0; i < 20; ++i) {
            pool.post([&done] { ++done; });
        }
    }
    LT_CHECK_EQ(done.load(), 20);
LT_END_AUTO_TEST(destructor_drains)

// Work items queued before drain may post more work; the inline drain
// tail still runs them (the engine's stop path leans on this).
LT_BEGIN_AUTO_TEST(worker_pool_suite, drain_runs_chained_posts)
    pool_ns::worker_pool pool(1);
    std::atomic<int> done{0};
    pool.post([&pool, &done] {
        pool.post([&done] { ++done; });
    });
    pool.drain_and_join();
    LT_CHECK_EQ(done.load(), 1);
LT_END_AUTO_TEST(drain_runs_chained_posts)

// Posting after a completed drain is a safe no-op (the stop contract
// posts nothing afterwards; this pins the non-throwing posture).
LT_BEGIN_AUTO_TEST(worker_pool_suite, post_after_drain_is_safe)
    pool_ns::worker_pool pool(1);
    pool.drain_and_join();
    pool.post([] { });
    LT_CHECK(true);
LT_END_AUTO_TEST(post_after_drain_is_safe)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
