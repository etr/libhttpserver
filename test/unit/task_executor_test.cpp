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

// TASK-098 Step 1: executor contract and the two deterministic test
// executors (PRD-V3N-REQ-024 substrate, DR-V3-003). Pins:
//   - httpserver::executor is an abstract, non-copyable scheduling seam;
//   - manual_executor drains posted work in FIFO order, only when
//     run_pending()/run_one() is called (work posted from another thread
//     never runs inline);
//   - inline_executor runs post() immediately on the calling thread;
//   - current_executor() is visible inside posted work and null outside;
//   - unique_function is move-only (move-only callables are accepted).

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>

#include "./littletest.hpp"

static_assert(!std::is_copy_constructible_v<httpserver::executor>,
              "executor must be non-copyable (identity is address-based)");
static_assert(!std::is_copy_assignable_v<httpserver::executor>,
              "executor must be non-copy-assignable");
static_assert(std::is_abstract_v<httpserver::executor>,
              "executor is a pure scheduling interface");
static_assert(!std::is_copy_constructible_v<httpserver::concurrency::unique_function<void()>>,
              "unique_function must be move-only");
static_assert(std::is_move_constructible_v<httpserver::concurrency::unique_function<void()>>,
              "unique_function must be movable");

LT_BEGIN_SUITE(task_executor_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(task_executor_suite)

LT_BEGIN_AUTO_TEST(task_executor_suite, manual_executor_is_fifo)
    httpserver::manual_executor ex;
    std::vector<int> order;
    ex.post([&] { order.push_back(1); });
    ex.post([&] { order.push_back(2); });
    ex.post([&] { order.push_back(3); });
    LT_ASSERT_EQ(ex.pending(), static_cast<std::size_t>(3));
    LT_ASSERT_EQ(order.size(), static_cast<std::size_t>(0));

    const auto drained = ex.run_pending();
    LT_ASSERT_EQ(drained, static_cast<std::size_t>(3));
    LT_ASSERT_EQ(order.size(), static_cast<std::size_t>(3));
    LT_ASSERT(order[0] == 1 && order[1] == 2 && order[2] == 3);
    LT_ASSERT_EQ(ex.pending(), static_cast<std::size_t>(0));
    LT_ASSERT_EQ(ex.run_pending(), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(manual_executor_is_fifo)

LT_BEGIN_AUTO_TEST(task_executor_suite, manual_executor_run_one_is_incremental)
    httpserver::manual_executor ex;
    int count = 0;
    ex.post([&] { ++count; });
    ex.post([&] { ++count; });
    LT_ASSERT(ex.run_one());
    LT_ASSERT_EQ(count, 1);
    LT_ASSERT(ex.run_one());
    LT_ASSERT_EQ(count, 2);
    LT_ASSERT(!ex.run_one());
LT_END_AUTO_TEST(manual_executor_run_one_is_incremental)

LT_BEGIN_AUTO_TEST(task_executor_suite, manual_executor_post_from_other_thread_defers)
    httpserver::manual_executor ex;
    bool ran = false;
    std::thread poster([&] { ex.post([&] { ran = true; }); });
    poster.join();
    // The post happened on another thread; nothing runs until this thread
    // drains the queue (no inline execution, no background worker).
    LT_ASSERT(!ran);
    LT_ASSERT_EQ(ex.pending(), static_cast<std::size_t>(1));
    ex.run_pending();
    LT_ASSERT(ran);
LT_END_AUTO_TEST(manual_executor_post_from_other_thread_defers)

LT_BEGIN_AUTO_TEST(task_executor_suite, inline_executor_runs_immediately)
    httpserver::inline_executor ex;
    bool ran = false;
    ex.post([&] { ran = true; });
    LT_ASSERT(ran);
LT_END_AUTO_TEST(inline_executor_runs_immediately)

LT_BEGIN_AUTO_TEST(task_executor_suite, manual_executor_is_current_only_while_draining)
    httpserver::manual_executor ex;
    LT_ASSERT(!ex.is_current());
    bool saw_current = false;
    ex.post([&] { saw_current = ex.is_current(); });
    LT_ASSERT(!saw_current);
    ex.run_pending();
    LT_ASSERT(saw_current);
LT_END_AUTO_TEST(manual_executor_is_current_only_while_draining)

LT_BEGIN_AUTO_TEST(task_executor_suite, inline_executor_is_current_while_running_post)
    httpserver::inline_executor ex;
    LT_ASSERT(!ex.is_current());
    bool saw_current = false;
    ex.post([&] { saw_current = ex.is_current(); });
    LT_ASSERT(saw_current);
    LT_ASSERT(!ex.is_current());
LT_END_AUTO_TEST(inline_executor_is_current_while_running_post)

LT_BEGIN_AUTO_TEST(task_executor_suite, current_executor_visible_inside_posted_work)
    httpserver::manual_executor ex;
    httpserver::executor* observed = nullptr;
    ex.post([&] { observed = httpserver::current_executor(); });
    LT_ASSERT(observed == nullptr);
    ex.run_pending();
    LT_ASSERT(observed == &ex);

    httpserver::inline_executor ix;
    httpserver::executor* inline_observed = nullptr;
    ix.post([&] { inline_observed = httpserver::current_executor(); });
    LT_ASSERT(inline_observed == &ix);

    LT_ASSERT(httpserver::current_executor() == nullptr);
LT_END_AUTO_TEST(current_executor_visible_inside_posted_work)

LT_BEGIN_AUTO_TEST(task_executor_suite, current_executor_nested_restores_previous)
    httpserver::inline_executor outer;
    httpserver::inline_executor inner;
    httpserver::executor* seen_after_nested = nullptr;
    outer.post([&] {
        inner.post([&] {
            LT_ASSERT(httpserver::current_executor() == &inner);
        });
        seen_after_nested = httpserver::current_executor();
    });
    LT_ASSERT(seen_after_nested == &outer);
LT_END_AUTO_TEST(current_executor_nested_restores_previous)

LT_BEGIN_AUTO_TEST(task_executor_suite, post_accepts_move_only_callables)
    httpserver::manual_executor ex;
    auto token = std::make_unique<int>(7);
    int* const raw = token.get();
    int observed = 0;
    // unique_ptr is not copyable; accepting it proves the task abstraction
    // is a move-only callable wrapper, not std::function.
    ex.post([t = std::move(token), &observed] { observed = *t; });
    LT_ASSERT_EQ(observed, 0);
    ex.run_pending();
    LT_ASSERT_EQ(observed, 7);
    LT_ASSERT(raw != nullptr);
LT_END_AUTO_TEST(post_accepts_move_only_callables)

LT_BEGIN_AUTO_TEST(task_executor_suite, manual_executor_is_thread_safe)
    httpserver::manual_executor ex;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 200;
    std::atomic<int> total{0};
    std::vector<std::thread> posters;
    for (int t = 0; t < kThreads; ++t) {
        posters.emplace_back([&ex, &total] {
            for (int i = 0; i < kPerThread; ++i) {
                ex.post([&total] { total.fetch_add(1, std::memory_order_relaxed); });
            }
        });
    }
    for (auto& p : posters) p.join();
    LT_ASSERT_EQ(ex.pending(), static_cast<std::size_t>(kThreads * kPerThread));
    ex.run_pending();
    LT_ASSERT_EQ(total.load(), kThreads * kPerThread);
LT_END_AUTO_TEST(manual_executor_is_thread_safe)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
