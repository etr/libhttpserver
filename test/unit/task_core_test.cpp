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

// TASK-098 Step 2: move-only, single-consumer, lazy-start task<T> with
// an owned coroutine frame (architecture §3.1, DR-V3-003). Pins:
//   - static move-only ABI (no copy construct/assign);
//   - lazy start: the frame does not run at creation, only when
//     spawn()ed or co_awaited;
//   - move transfers frame ownership; the source becomes invalid;
//   - destroying an unconsumed task destroys its frame (un-awaited is a
//     documented cancellation);
//   - completion values propagate through co_await for task<void> and
//     task<int>; exceptions are captured and rethrown at the await;
//   - spawn delivers its callback exactly once, on the spawn executor,
//     including for exceptions and for multi-await chains;
//   - current_executor() inside the coroutine body is the spawn executor;
//   - a moved-from task fails valid() and cannot be consumed twice.

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/concurrency/task.hpp>

#include "./littletest.hpp"

using httpserver::executor;
using httpserver::inline_executor;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;

static_assert(!std::is_copy_constructible_v<task<void>>,
              "task must be move-only (single owner of the frame)");
static_assert(!std::is_copy_assignable_v<task<int>>,
              "task must be move-only (single owner of the frame)");
static_assert(std::is_move_constructible_v<task<int>>,
              "task must be movable");
static_assert(std::is_move_assignable_v<task<std::string>>,
              "task must be movable");

namespace {

int g_body_runs = 0;

task<void> counting_body() {
    ++g_body_runs;
    co_return;
}

task<int> value_body(int v) {
    co_return v;
}

task<void> failing_body() {
    throw std::runtime_error("boom");
    co_return;
}

task<std::string> string_body() {
    co_return std::string("hello");
}

// A driver coroutine used to exercise operator co_await from a task.
task<int> await_and_forward(task<int> inner) {
    co_return co_await std::move(inner);
}

task<void> await_void_chain(task<void> inner, int* runs) {
    co_await std::move(inner);
    ++*runs;
    co_return;
}

}  // namespace

LT_BEGIN_SUITE(task_core_suite)
    void set_up() {
        g_body_runs = 0;
    }

    void tear_down() {
    }
LT_END_SUITE(task_core_suite)

LT_BEGIN_AUTO_TEST(task_core_suite, task_starts_lazily)
    auto t = counting_body();
    LT_ASSERT(t.valid());
    LT_ASSERT_EQ(g_body_runs, 0);

    manual_executor ex;
    bool completed = false;
    spawn(ex, std::move(t), [&](task_result<void> r) {
        completed = true;
        LT_ASSERT(r.has_value());
    });
    LT_ASSERT_EQ(g_body_runs, 0);  // still lazy until the executor drains
    ex.run_pending();
    LT_ASSERT(completed);
    LT_ASSERT_EQ(g_body_runs, 1);
LT_END_AUTO_TEST(task_starts_lazily)

LT_BEGIN_AUTO_TEST(task_core_suite, move_transfers_ownership_and_invalidates_source)
    task<int> source = value_body(1);
    LT_ASSERT(source.valid());
    task<int> target = std::move(source);
    LT_ASSERT(!source.valid());
    LT_ASSERT(target.valid());
    target = std::move(target);  // self-move must not corrupt
    LT_ASSERT(target.valid());

    manual_executor ex;
    bool completed = false;
    spawn(ex, std::move(target), [&](task_result<int> r) {
        completed = true;
        LT_ASSERT(r.has_value());
        LT_ASSERT_EQ(r.value(), 1);
    });
    ex.run_pending();
    LT_ASSERT(completed);
LT_END_AUTO_TEST(move_transfers_ownership_and_invalidates_source)

LT_BEGIN_AUTO_TEST(task_core_suite, move_assignment_destroys_pending_frame)
    int runs = 0;
    auto make_counter = [&]() -> task<void> {
        ++runs;
        co_return;
    };
    task<void> t = make_counter();
    t = make_counter();  // destroys the first pending frame
    LT_ASSERT_EQ(runs, 0);  // neither started yet

    manual_executor ex;
    spawn(ex, std::move(t), [](task_result<void>) {});
    ex.run_pending();
    LT_ASSERT_EQ(runs, 1);  // only the surviving frame ran
LT_END_AUTO_TEST(move_assignment_destroys_pending_frame)

LT_BEGIN_AUTO_TEST(task_core_suite, destroying_unconsumed_task_destroys_frame)
    [&] {  // the task is destroyed without being consumed: its frame is
           // destroyed and the body never runs (documented as a
           // cancellation at this layer)
        auto t = counting_body();
        LT_ASSERT(t.valid());
    }();
    LT_ASSERT_EQ(g_body_runs, 0);
LT_END_AUTO_TEST(destroying_unconsumed_task_destroys_frame)

LT_BEGIN_AUTO_TEST(task_core_suite, completion_value_propagates_through_co_await)
    manual_executor ex;
    bool completed = false;
    spawn(ex, await_and_forward(value_body(42)), [&](task_result<int> r) {
        completed = true;
        LT_ASSERT(r.has_value());
        LT_ASSERT_EQ(r.value(), 42);
    });
    ex.run_pending();
    LT_ASSERT(completed);
LT_END_AUTO_TEST(completion_value_propagates_through_co_await)

LT_BEGIN_AUTO_TEST(task_core_suite, void_task_awaits_and_sequences)
    int runs = 0;
    manual_executor ex;
    bool completed = false;
    spawn(ex, await_void_chain(counting_body(), &runs), [&](task_result<void> r) {
        completed = true;
        LT_ASSERT(r.has_value());
    });
    ex.run_pending();
    LT_ASSERT(completed);
    LT_ASSERT_EQ(runs, 1);
    LT_ASSERT_EQ(g_body_runs, 1);
LT_END_AUTO_TEST(void_task_awaits_and_sequences)

LT_BEGIN_AUTO_TEST(task_core_suite, exceptions_are_captured_and_rethrown_at_await)
    manual_executor ex;

    // Through spawn: the callback observes the exception pointer.
    bool spawn_saw_exception = false;
    spawn(ex, failing_body(), [&](task_result<void> r) {
        spawn_saw_exception = r.is_exception();
        if (r.is_exception()) {
            try {
                std::rethrow_exception(r.exception());
            } catch (const std::runtime_error& e) {
                LT_ASSERT(std::string(e.what()) == "boom");
            }
        }
    });
    ex.run_pending();
    LT_ASSERT(spawn_saw_exception);

    // Through co_await: the awaiting task's frame captures the same
    // exception and delivers it onward (no exception escapes a completion
    // path).
    auto driver = [](task<void> inner) -> task<void> {
        co_await std::move(inner);
        co_return;
    };
    bool await_saw_exception = false;
    spawn(ex, driver(failing_body()), [&](task_result<void> r) {
        await_saw_exception = r.is_exception();
    });
    ex.run_pending();
    LT_ASSERT(await_saw_exception);
LT_END_AUTO_TEST(exceptions_are_captured_and_rethrown_at_await)

LT_BEGIN_AUTO_TEST(task_core_suite, spawn_callback_delivered_exactly_once_on_spawn_executor)
    for (int i = 0; i < 50; ++i) {
        manual_executor ex;
        int deliveries = 0;
        executor* seen = nullptr;
        spawn(ex, value_body(i), [&](task_result<int> r) {
            ++deliveries;
            seen = httpserver::current_executor();
            LT_ASSERT(r.has_value());
        });
        ex.run_pending();
        LT_ASSERT_EQ(deliveries, 1);
        LT_ASSERT(seen == &ex);
        LT_ASSERT_EQ(ex.pending(), static_cast<std::size_t>(0));
    }
LT_END_AUTO_TEST(spawn_callback_delivered_exactly_once_on_spawn_executor)

LT_BEGIN_AUTO_TEST(task_core_suite, current_executor_inside_body_is_spawn_executor)
    manual_executor ex;
    executor* inside = nullptr;
    auto body = [&]() -> task<void> {
        inside = httpserver::current_executor();
        co_return;
    };
    spawn(ex, body(), [](task_result<void>) {});
    LT_ASSERT(inside == nullptr);  // not yet started
    ex.run_pending();
    LT_ASSERT(inside == &ex);
LT_END_AUTO_TEST(current_executor_inside_body_is_spawn_executor)

LT_BEGIN_AUTO_TEST(task_core_suite, awaited_task_runs_on_consumer_executor)
    manual_executor ex;
    executor* inner_saw = nullptr;
    auto inner = [&]() -> task<void> {
        inner_saw = httpserver::current_executor();
        co_return;
    };
    auto outer = [](task<void> t) -> task<void> {
        co_await std::move(t);
        co_return;
    };
    spawn(ex, outer(inner()), [](task_result<void>) {});
    ex.run_pending();
    LT_ASSERT(inner_saw == &ex);
LT_END_AUTO_TEST(awaited_task_runs_on_consumer_executor)

LT_BEGIN_AUTO_TEST(task_core_suite, moved_from_task_fails_valid)
    task<int> t = value_body(7);
    task<int> consumed = std::move(t);
    LT_ASSERT(!t.valid());
    LT_ASSERT(consumed.valid());

    manual_executor ex;
    bool completed = false;
    spawn(ex, std::move(consumed), [&](task_result<int> r) {
        completed = true;
        LT_ASSERT(r.has_value());
        LT_ASSERT_EQ(r.value(), 7);
    });
    ex.run_pending();
    LT_ASSERT(completed);
LT_END_AUTO_TEST(moved_from_task_fails_valid)

LT_BEGIN_AUTO_TEST(task_core_suite, default_task_is_invalid_and_destroyable)
    task<void> t;
    LT_ASSERT(!t.valid());
    task<void> u = counting_body();
    u = task<void>();  // move-assign from invalid task
    LT_ASSERT(!u.valid());
    LT_ASSERT_EQ(g_body_runs, 0);
LT_END_AUTO_TEST(default_task_is_invalid_and_destroyable)

LT_BEGIN_AUTO_TEST(task_core_suite, string_value_moves_through)
    manual_executor ex;
    bool completed = false;
    spawn(ex, string_body(), [&](task_result<std::string> r) {
        completed = true;
        LT_ASSERT(r.has_value());
        LT_ASSERT(r.value() == "hello");
    });
    ex.run_pending();
    LT_ASSERT(completed);
LT_END_AUTO_TEST(string_value_moves_through)

LT_BEGIN_AUTO_TEST(task_core_suite, deep_await_chain_delivers_once)
    manual_executor ex;
    int deliveries = 0;
    auto level3 = []() -> task<int> { co_return 3; };
    auto level2 = [](task<int> t) -> task<int> { co_return co_await std::move(t); };
    auto level1 = [](task<int> t) -> task<int> { co_return co_await std::move(t); };
    spawn(ex, level1(level2(level3())), [&](task_result<int> r) {
        ++deliveries;
        LT_ASSERT(r.has_value());
        LT_ASSERT_EQ(r.value(), 3);
    });
    ex.run_pending();
    LT_ASSERT_EQ(deliveries, 1);
LT_END_AUTO_TEST(deep_await_chain_delivers_once)

LT_BEGIN_AUTO_TEST(task_core_suite, awaiting_inline_executor_task_completes_inline)
    inline_executor ex;
    bool completed = false;
    executor* seen = nullptr;
    spawn(ex, value_body(5), [&](task_result<int> r) {
        completed = true;
        seen = httpserver::current_executor();
        LT_ASSERT(r.has_value());
        LT_ASSERT_EQ(r.value(), 5);
    });
    LT_ASSERT(completed);  // inline executor runs everything synchronously
    LT_ASSERT(seen == &ex);
LT_END_AUTO_TEST(awaiting_inline_executor_task_completes_inline)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
