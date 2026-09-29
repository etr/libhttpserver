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

// TASK-098 Step 4: idempotent one-shot application resume signal with
// timeout (architecture §3.1, PRD-V3N-REQ-024/025). Pins:
//   - the first of {signal, cancel} wins; later calls are no-ops;
//   - a waiter completes exactly once with the winning outcome;
//   - wait() after an already-fired signal completes immediately;
//   - wait_for() delivers resumed / timeout / cancelled by which trigger
//     precedes the deadline;
//   - multiple waiters fan out, each delivered exactly once;
//   - the signal is copyable and shared by copies (exchange hands copies
//     to application code while retaining its own).

#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>

#include <httpserver/concurrency/resume_signal.hpp>

#include "./littletest.hpp"

using httpserver::manual_executor;
using httpserver::resume_outcome;
using httpserver::resume_signal;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;

using namespace std::chrono_literals;

static_assert(std::is_copy_constructible_v<resume_signal>,
              "resume_signal hands copies to application code");
static_assert(std::is_nothrow_move_constructible_v<resume_signal>,
              "resume_signal moves its shared state handle");
static_assert(std::is_enum_v<resume_outcome>,
              "resume_outcome is a typed result enum");

namespace {

// Drives a coroutine that suspends on the signal and records the result.
task<void> waiter(resume_signal sig, resume_outcome* observed) {
    *observed = co_await sig.wait();
    co_return;
}

task<void> timed_waiter(resume_signal sig,
                        std::chrono::steady_clock::duration d,
                        resume_outcome* observed) {
    *observed = co_await sig.wait_for(d);
    co_return;
}

}  // namespace

LT_BEGIN_SUITE(task_resume_signal_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(task_resume_signal_suite)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, first_trigger_wins_and_rest_are_noops)
    resume_signal sig;
    sig.signal();
    sig.signal();
    sig.cancel();  // lost the race: no-op
    sig.signal();  // no-op

    manual_executor ex;
    resume_outcome observed = resume_outcome::cancelled;
    spawn(ex, waiter(sig, &observed), [](task_result<void>) {});
    ex.run_pending();
    LT_ASSERT(observed == resume_outcome::resumed);
LT_END_AUTO_TEST(first_trigger_wins_and_rest_are_noops)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, cancel_wins_over_signal)
    resume_signal sig;
    sig.cancel();
    sig.signal();  // no-op: cancel already fired

    manual_executor ex;
    resume_outcome observed = resume_outcome::resumed;
    spawn(ex, waiter(sig, &observed), [](task_result<void>) {});
    ex.run_pending();
    LT_ASSERT(observed == resume_outcome::cancelled);
LT_END_AUTO_TEST(cancel_wins_over_signal)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, waiter_completes_once_on_signal)
    manual_executor ex;
    resume_signal sig;
    resume_outcome observed = resume_outcome::cancelled;
    int deliveries = 0;
    spawn(ex, waiter(sig, &observed), [&](task_result<void>) { ++deliveries; });
    ex.run_pending();  // waiter suspended
    LT_ASSERT(observed == resume_outcome::cancelled
              || observed == resume_outcome::resumed);
    observed = resume_outcome::timeout;  // sentinel: untouched by wait()

    sig.signal();
    sig.signal();
    ex.run_pending();
    LT_ASSERT(observed == resume_outcome::resumed);
    LT_ASSERT_EQ(deliveries, 1);
    LT_ASSERT_EQ(ex.pending(), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(waiter_completes_once_on_signal)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, waiter_completes_once_on_cancel)
    manual_executor ex;
    resume_signal sig;
    resume_outcome observed = resume_outcome::resumed;
    int deliveries = 0;
    spawn(ex, waiter(sig, &observed), [&](task_result<void>) { ++deliveries; });
    ex.run_pending();

    sig.cancel();
    ex.run_pending();
    LT_ASSERT(observed == resume_outcome::cancelled);
    LT_ASSERT_EQ(deliveries, 1);
LT_END_AUTO_TEST(waiter_completes_once_on_cancel)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, wait_after_fired_signal_completes_immediately)
    resume_signal sig;
    sig.signal();
    manual_executor ex;
    resume_outcome observed = resume_outcome::cancelled;
    spawn(ex, waiter(sig, &observed), [](task_result<void>) {});
    ex.run_pending();
    LT_ASSERT(observed == resume_outcome::resumed);
LT_END_AUTO_TEST(wait_after_fired_signal_completes_immediately)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, wait_for_times_out_when_nothing_fires)
    manual_executor ex;
    resume_signal sig;
    resume_outcome observed = resume_outcome::resumed;
    int deliveries = 0;
    spawn(ex, timed_waiter(sig, 1ms, &observed),
          [&](task_result<void>) { ++deliveries; });
    ex.run_pending();  // suspend + arm the deadline
    // The timeout fires on the timer thread; wait out the deadline, then
    // drain the posted resumption.
    std::this_thread::sleep_for(50ms);
    ex.run_pending();
    LT_ASSERT_EQ(deliveries, 1);
    LT_ASSERT(observed == resume_outcome::timeout);
LT_END_AUTO_TEST(wait_for_times_out_when_nothing_fires)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, wait_for_returns_resumed_before_deadline)
    manual_executor ex;
    resume_signal sig;
    resume_outcome observed = resume_outcome::timeout;
    spawn(ex, timed_waiter(sig, 5s, &observed), [](task_result<void>) {});
    ex.run_pending();  // suspended with a pending deadline

    sig.signal();
    ex.run_pending();
    LT_ASSERT(observed == resume_outcome::resumed);
LT_END_AUTO_TEST(wait_for_returns_resumed_before_deadline)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, wait_for_returns_cancelled_before_deadline)
    manual_executor ex;
    resume_signal sig;
    resume_outcome observed = resume_outcome::timeout;
    spawn(ex, timed_waiter(sig, 5s, &observed), [](task_result<void>) {});
    ex.run_pending();

    sig.cancel();
    ex.run_pending();
    LT_ASSERT(observed == resume_outcome::cancelled);
LT_END_AUTO_TEST(wait_for_returns_cancelled_before_deadline)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, fan_out_delivers_each_waiter_once)
    manual_executor ex;
    resume_signal sig;
    constexpr int kWaiters = 8;
    int deliveries[kWaiters] = {};
    resume_outcome observed[kWaiters] = {};
    for (int i = 0; i < kWaiters; ++i) {
        spawn(ex, waiter(sig, &observed[i]),
              [&deliveries, i](task_result<void>) { ++deliveries[i]; });
    }
    ex.run_pending();

    sig.signal();
    ex.run_pending();
    for (int i = 0; i < kWaiters; ++i) {
        LT_ASSERT_EQ(deliveries[i], 1);
        LT_ASSERT(observed[i] == resume_outcome::resumed);
    }
LT_END_AUTO_TEST(fan_out_delivers_each_waiter_once)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, copies_share_state)
    manual_executor ex;
    resume_signal owned;
    resume_signal copy = owned;
    resume_outcome observed = resume_outcome::timeout;
    spawn(ex, waiter(copy, &observed), [](task_result<void>) {});
    ex.run_pending();

    owned.signal();
    ex.run_pending();
    LT_ASSERT(observed == resume_outcome::resumed);

    // The copy also observes subsequent state: already fired.
    resume_outcome second = resume_outcome::timeout;
    spawn(ex, waiter(owned, &second), [](task_result<void>) {});
    ex.run_pending();
    LT_ASSERT(second == resume_outcome::resumed);
LT_END_AUTO_TEST(copies_share_state)

LT_BEGIN_AUTO_TEST(task_resume_signal_suite, timeout_path_never_fires_after_signal_delivery)
    // Sub-millisecond deadlines raced against signal(): each waiter is
    // delivered exactly once with either outcome; over the run both
    // outcome classes must occur.
    int resumed_count = 0;
    int timeout_count = 0;
    for (int i = 0; i < 100; ++i) {
        const auto deadline = std::chrono::milliseconds(2 + (i % 5));
        manual_executor ex;
        resume_signal sig;
        resume_outcome observed = resume_outcome::cancelled;
        int deliveries = 0;
        spawn(ex, timed_waiter(sig, deadline, &observed),
              [&](task_result<void>) { ++deliveries; });
        ex.run_pending();  // suspend + arm the deadline
        // Signal AT the deadline so signal and timeout genuinely race;
        // the delivered-CAS must yield exactly one outcome per waiter.
        std::this_thread::sleep_for(deadline);
        sig.signal();
        for (int spin = 0; spin < 1000 && deliveries == 0; ++spin) {
            std::this_thread::sleep_for(1ms);
            ex.run_pending();
        }
        LT_CHECK_EQ(deliveries, 1);
        if (observed == resume_outcome::resumed) ++resumed_count;
        else if (observed == resume_outcome::timeout) ++timeout_count;
        else LT_CHECK(false);
    }
    LT_CHECK(resumed_count > 0);
    LT_CHECK(timeout_count > 0);
    LT_CHECK_EQ(resumed_count + timeout_count, 100);
LT_END_AUTO_TEST(timeout_path_never_fires_after_signal_delivery)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
