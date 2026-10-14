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

// TASK-098 Step 5: completion, timeout, disconnect and stop race suite
// (architecture §3.1 exactly-once invariant, DR-V3-003/DR-V3-008).
// Four stress families, sized for make check -j1 (wall-clock budget a
// few seconds). Every family asserts the exactly-once invariant via a
// per-waiter delivered count (a second delivery is a failure) and, where
// the plan requires it, that both outcome classes actually raced.
//
// Threads are synchronized with atomic spin gates (two bools) rather
// than std::barrier to stay portable across C++20 standard libraries.

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/resume_signal.hpp>

#include "./littletest.hpp"
#include "unit/manual_executor_post_probe.hpp"

using httpserver::executor;
using httpserver::manual_executor;
using httpserver::resume_outcome;
using httpserver::resume_signal;
using httpserver::spawn;
using httpserver::stop_source;
using httpserver::stop_token;
using httpserver::task;
using httpserver::task_result;

using std::chrono_literals::operator""ms;

namespace {

// Spin gate for two threads: both must arrive before either proceeds.
class gate {
 public:
    explicit gate(std::atomic<int>* counter) : counter_(counter) { }
    void arrive() { counter_->fetch_add(1, std::memory_order_acq_rel); }
    void wait(int expected) {
        while (counter_->load(std::memory_order_acquire) < expected) {
            std::this_thread::yield();
        }
    }

 private:
    std::atomic<int>* counter_;
};

task<int> value_body(int v) {
    co_return v;
}

task<int> await_and_forward(task<int> inner) {
    co_return co_await std::move(inner);
}

task<resume_outcome> signal_waiter(resume_signal sig, resume_outcome* out) {
    *out = co_await sig.wait();
    co_return *out;
}

task<resume_outcome> timed_signal_waiter(resume_signal sig,
                                         std::chrono::steady_clock::duration d,
                                         resume_outcome* out) {
    *out = co_await sig.wait_for(d);
    co_return *out;
}

task<void> cancel_waiter(stop_token token, std::atomic<int>* completions) {
    co_await token.cancelled();
    ++*completions;  // unreachable when fired
    co_return;
}

// A waiter task that is created but deliberately never consumed: used to
// prove that abandoning it races a concurrent signal() harmlessly.
task<void> abandoned_waiter(resume_signal sig) {
    co_await sig.wait();
    co_return;
}

}  // namespace

LT_BEGIN_SUITE(task_race_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(task_race_suite)

// Family 1: completion race. spawn() races a concurrent drain of the
// same manual_executor; the chain completes exactly once and the spawn
// callback runs on the spawn executor.
LT_BEGIN_AUTO_TEST(task_race_suite, completion_race_spawn_vs_drain)
    constexpr int kIterations = 300;
    for (int i = 0; i < kIterations; ++i) {
        manual_executor ex;
        std::atomic<int> arrivals{0};
        std::atomic<int> deliveries{0};
        std::atomic<bool> on_spawn_executor{false};
        gate g(&arrivals);

        std::thread drainer([&] {
            g.arrive();
            g.wait(2);
            while (deliveries.load() == 0) {
                if (!ex.run_one()) std::this_thread::yield();
            }
        });
        std::thread starter([&] {
            g.arrive();
            g.wait(2);
            spawn(ex, await_and_forward(value_body(42)),
                  [&](task_result<int> r) {
                      ++deliveries;
                      on_spawn_executor =
                          httpserver::current_executor() == &ex;
                      if (!r.has_value() || r.value() != 42) {
                          ++deliveries;  // force a second count: bad result
                      }
                  });
        });
        drainer.join();
        starter.join();
        while (ex.run_one()) {
        }

        if (deliveries != 1) {
            std::cout << "[completion-race] iteration " << i
                      << " deliveries=" << deliveries.load() << std::endl;
        }
        LT_CHECK_EQ(deliveries.load(), 1);
        LT_CHECK(on_spawn_executor.load());
    }
LT_END_AUTO_TEST(completion_race_spawn_vs_drain)

// Family 2: timeout race. A short deadline expires while a
// concurrent thread fires signal(): each waiter delivered exactly once
// with resumed or timeout. The first two cases guarantee both outcomes;
// the remaining cases still race real deadlines.
LT_BEGIN_AUTO_TEST(task_race_suite, timeout_race_signal_vs_deadline)
    constexpr int kIterations = 200;
    int resumed_count = 0;
    int timeout_count = 0;
    for (int i = 0; i < kIterations; ++i) {
        manual_executor ex;
        resume_signal sig;
        resume_outcome observed = resume_outcome::cancelled;
        std::atomic<int> deliveries{0};
        const auto deadline = std::chrono::milliseconds(1 + (i % 5));

        if (i == 0) sig.signal();
        spawn(ex, timed_signal_waiter(sig, deadline, &observed),
              [&](task_result<resume_outcome>) { ++deliveries; });
        ex.run_pending();  // suspend + arm

        std::thread firing([&] {
            if (i == 1) return;  // the real deadline wins this case
            std::this_thread::sleep_for(deadline);  // race the deadline
            sig.signal();
        });
        firing.join();

        for (int spin = 0; spin < 2000 && deliveries.load() == 0; ++spin) {
            std::this_thread::sleep_for(1ms);
            ex.run_pending();
        }
        // No guessed teardown grace period: the losing node cannot post,
        // and the winning post has no executor access after publication.
        ex.run_pending();

        LT_CHECK_EQ(deliveries.load(), 1);
        if (observed == resume_outcome::resumed) ++resumed_count;
        else if (observed == resume_outcome::timeout) ++timeout_count;
        else LT_CHECK(false);
    }
    LT_CHECK(resumed_count > 0);
    LT_CHECK(timeout_count > 0);
LT_END_AUTO_TEST(timeout_race_signal_vs_deadline)

// Family 3: disconnect race (PRD-V3N-REQ-025). Waiters suspended in
// wait() racing a concurrent signal()/cancel(); alternating iterations
// destroy an unconsumed waiter task while signal() fires. Exactly one
// outcome per consumed waiter; no crashes.
LT_BEGIN_AUTO_TEST(task_race_suite, disconnect_race)
    constexpr int kIterations = 300;
    for (int i = 0; i < kIterations; ++i) {
        stop_source source;  // per-iteration "connection" stop state
        if (i % 3 == 0) {
            // Abandoned-waiter variant: destroy the (never consumed)
            // waiter task while a concurrent signal() fires.
            resume_signal sig;
            std::atomic<int> arrivals{0};
            gate g(&arrivals);
            task<void> pending = abandoned_waiter(sig);
            std::thread igniter([&] {
                g.arrive();
                g.wait(2);
                sig.signal();
            });
            g.arrive();
            g.wait(2);
            pending = task<void>();  // destroy the unconsumed frame
            igniter.join();
        } else {
            // Consumed-waiter variant: waiter suspended in wait();
            // a concurrent thread disconnects by signal() or cancel().
            manual_executor ex;
            resume_signal sig;
            resume_outcome observed = resume_outcome::timeout;
            std::atomic<int> deliveries{0};
            std::atomic<int> arrivals{0};
            gate g(&arrivals);
            const bool cancel_disconnect = (i % 3 == 2);

            spawn(ex, signal_waiter(sig, &observed),
                  [&](task_result<resume_outcome>) { ++deliveries; });
            ex.run_pending();  // suspended

            std::thread disconnect([&] {
                g.arrive();
                g.wait(2);
                if (cancel_disconnect) sig.cancel();
                else sig.signal();
                if (source.request_stop()) {
                    // exactly one stop request wins; no waiter may be
                    // resumed here (asserted by the drain ordering below)
                }
            });
            g.arrive();
            g.wait(2);
            disconnect.join();

            for (int spin = 0; spin < 2000 && deliveries.load() == 0; ++spin) {
                std::this_thread::sleep_for(1ms);
                ex.run_pending();
            }
            std::this_thread::sleep_for(2ms);
            ex.run_pending();

            LT_CHECK_EQ(deliveries.load(), 1);
            LT_CHECK(observed == resume_outcome::resumed
                     || observed == resume_outcome::cancelled);
        }
    }
LT_END_AUTO_TEST(disconnect_race)

// Family 4: stop race. Eight threads concurrently mix request_stop(),
// signal() and cancel() on one source + one signal; the cancelled()
// waiter is delivered exactly once, request_stop() wins for exactly one
// thread, and no waiter is resumed inline on a requesting thread.
LT_BEGIN_AUTO_TEST(task_race_suite, stop_race_eight_threads)
    constexpr int kIterations = 200;
    constexpr int kThreads = 8;
    for (int i = 0; i < kIterations; ++i) {
        manual_executor ex;
        stop_source source;
        resume_signal sig;
        std::atomic<int> completions{0};
        std::atomic<int> stop_winners{0};
        std::atomic<int> deliveries{0};
        const std::thread::id main_id = std::this_thread::get_id();
        std::atomic<bool> resumed_on_requester{false};

        spawn(ex, cancel_waiter(source.get_token(), &completions),
              [&](task_result<void> r) {
                  ++deliveries;
                  if (httpserver::current_executor() != &ex
                      || std::this_thread::get_id() != main_id) {
                      resumed_on_requester = true;
                  }
                  if (!r.has_outcome()
                      || r.outcome()
                             != httpserver::http::outcome_code::cancelled) {
                      ++deliveries;  // force second count: bad result
                  }
              });
        ex.run_pending();  // waiter suspended in cancelled()

        std::vector<std::thread> requesters;
        for (int t = 0; t < kThreads; ++t) {
            requesters.emplace_back([&, t] {
                if (source.request_stop()) ++stop_winners;
                if (t % 2 == 0) sig.signal();
                else sig.cancel();
            });
        }
        for (auto& thread : requesters) thread.join();

        for (int spin = 0; spin < 2000 && deliveries.load() == 0; ++spin) {
            std::this_thread::sleep_for(1ms);
            ex.run_pending();
        }
        std::this_thread::sleep_for(2ms);
        ex.run_pending();

        LT_CHECK_EQ(stop_winners.load(), 1);
        LT_CHECK_EQ(deliveries.load(), 1);
        LT_CHECK(completions.load() == 0);  // body never runs past co_await
        LT_CHECK(!resumed_on_requester.load());
        (void)sig;
    }
LT_END_AUTO_TEST(stop_race_eight_threads)

// The real timer publishes its resumption, then pauses at the queue unlock.
// Frame completion is observable while post() is still on the producer stack.
LT_BEGIN_AUTO_TEST(task_race_suite, timer_publication_has_no_executor_tail)
#if defined(LHS_TEST_POST_TAIL_WRAP)
    manual_executor ex;
    resume_signal sig;
    auto probe = std::make_shared<post_probe::publication>();
    post_probe::capture = probe.get();
    ex.pending();
    post_probe::capture = nullptr;
    resume_outcome observed = resume_outcome::resumed;
    int deliveries = 0;
    bool valid_result = false;
    bool affinity = false;
    spawn(ex, timed_signal_waiter(sig, 0ms, &observed),
          [&](task_result<resume_outcome> result) {
              ++deliveries;
              valid_result = result.has_value()
                  && result.value() == resume_outcome::timeout;
              affinity = ex.is_current()
                  && httpserver::current_executor() == &ex;
          });
    post_probe::active.store(probe, std::memory_order_release);
    ex.run_pending();
    const bool entered = post_probe::await(probe->entered);
    ex.run_pending();
    probe->retired.store(true, std::memory_order_release);
    sig.signal();  // loses to timeout; frame already retired
    sig.cancel();
    const auto remaining = ex.pending();
    probe->release.store(true, std::memory_order_release);
    // A callback on the same serial timer thread acknowledges post exit.
    auto exit = std::make_shared<std::promise<void>>();
    auto exited = exit->get_future();
    auto fence = httpserver::detail::default_timer_queue().schedule(
        std::chrono::steady_clock::now(), [exit] {
            post_probe::producer = nullptr;
            exit->set_value();
        });
    const bool timely_exit = exited.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    exited.wait();  // keep probe/executor alive even if diagnostics failed
    post_probe::active.store(nullptr, std::memory_order_release);
    ex.run_pending();
    LT_CHECK(entered);
    LT_CHECK(timely_exit);
    LT_CHECK(!probe->expired.load());
    LT_CHECK_EQ(probe->publications.load(), 1);
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(valid_result);
    LT_CHECK(affinity);
    LT_CHECK(observed == resume_outcome::timeout);
    LT_CHECK_EQ(remaining, 0u);
    LT_CHECK_EQ(probe->late_notifications.load(), 0);
    LT_CHECK_EQ(ex.pending(), 0u);
#else
    LT_SKIP("Queue-unlock interposition is Linux-only; nonlocal proof is CI-owned");
#endif
LT_END_AUTO_TEST(timer_publication_has_no_executor_tail)

// Hold the real timer service before arming zero deadlines. Signal/cancel
// must complete first; releasing the service exercises the cancelled entry.
LT_BEGIN_AUTO_TEST(task_race_suite, timed_wait_trigger_winners_retire_before_timer)
#if defined(LHS_TEST_POST_TAIL_WRAP)
    for (const bool cancel : {false, true}) {
        manual_executor ex;
        resume_signal sig;
        auto probe = std::make_shared<post_probe::publication>();
        std::atomic<bool> timer_entered{false};
        std::atomic<bool> timer_release{false};
        std::atomic<bool> timer_expired{false};
        auto blocked = httpserver::detail::default_timer_queue().schedule(
            std::chrono::steady_clock::now(), [&] {
                timer_entered.store(true, std::memory_order_release);
                if (!post_probe::await(timer_release)) timer_expired = true;
            });
        const bool entered = post_probe::await(timer_entered);
        post_probe::capture = probe.get();
        ex.pending();
        post_probe::capture = nullptr;
        resume_outcome observed = resume_outcome::timeout;
        const auto expected = cancel ? resume_outcome::cancelled
                                     : resume_outcome::resumed;
        int deliveries = 0;
        bool valid_result = false;
        bool affinity = false;
        spawn(ex, timed_signal_waiter(sig, 0ms, &observed),
              [&](task_result<resume_outcome> result) {
                  ++deliveries;
                  valid_result = result.has_value() && result.value() == expected;
                  affinity = ex.is_current()
                      && httpserver::current_executor() == &ex;
              });
        ex.run_pending();
        post_probe::active.store(probe, std::memory_order_release);
        std::thread firing([&] {
            if (cancel) sig.cancel();
            else sig.signal();
            post_probe::producer = nullptr;
        });
        const bool published = post_probe::await(probe->entered);
        ex.run_pending();
        probe->retired.store(true, std::memory_order_release);
        sig.signal();
        sig.cancel();
        probe->release.store(true, std::memory_order_release);
        firing.join();
        timer_release.store(true, std::memory_order_release);
        auto exit = std::make_shared<std::promise<void>>();
        auto exited = exit->get_future();
        auto fence = httpserver::detail::default_timer_queue().schedule(
            std::chrono::steady_clock::now(), [exit] { exit->set_value(); });
        const bool timely_exit = exited.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        exited.wait();
        post_probe::active.store(nullptr, std::memory_order_release);
        const auto late_work = ex.run_pending();
        LT_CHECK(entered);
        LT_CHECK(published);
        LT_CHECK(timely_exit);
        LT_CHECK(!timer_expired.load());
        LT_CHECK(!probe->expired.load());
        LT_CHECK_EQ(probe->publications.load(), 1);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK(valid_result);
        LT_CHECK(affinity);
        LT_CHECK(observed == expected);
        LT_CHECK_EQ(probe->late_notifications.load(), 0);
        LT_CHECK_EQ(late_work, 0u);
    }
#else
    LT_SKIP("Queue-unlock interposition is Linux-only; nonlocal proof is CI-owned");
#endif
LT_END_AUTO_TEST(timed_wait_trigger_winners_retire_before_timer)

// An unrelated unlock can hold a lookup across owner retirement. Keep that
// reader paused until the owner has dropped every reference to the probe.
LT_BEGIN_AUTO_TEST(task_race_suite, probe_lookup_retains_retired_publication)
#if defined(LHS_TEST_POST_TAIL_WRAP)
    manual_executor ex;
    std::mutex unrelated;
    post_probe::lookup_gate gate;
    auto probe = std::make_shared<post_probe::publication>();
    std::weak_ptr<post_probe::publication> lifetime = probe;
    post_probe::capture = probe.get();
    ex.pending();
    post_probe::capture = nullptr;
    post_probe::active.store(probe, std::memory_order_release);
    std::thread reader([&] {
        post_probe::lookup_pause = &gate;
        unrelated.lock();
        unrelated.unlock();
        post_probe::lookup_pause = nullptr;
    });
    const bool entered = post_probe::await(gate.entered);
    post_probe::active.store(nullptr, std::memory_order_release);
    probe.reset();
    const bool retained = !lifetime.expired();
    gate.release.store(true, std::memory_order_release);
    reader.join();
    LT_CHECK(entered);
    LT_CHECK(!gate.expired.load());
    LT_CHECK(retained);
    LT_CHECK(lifetime.expired());
#else
    LT_SKIP("Queue-unlock interposition is Linux-only; nonlocal proof is CI-owned");
#endif
LT_END_AUTO_TEST(probe_lookup_retains_retired_publication)

LT_BEGIN_AUTO_TEST_ENV()
#if defined(LHS_TEST_POST_TAIL_ONLY)
    littletest::auto_test_runner(&timer_publication_has_no_executor_tail);
    littletest::auto_test_runner(&timed_wait_trigger_winners_retire_before_timer);
    littletest::auto_test_runner(&probe_lookup_retains_retired_publication);
    int __lt_result__ = littletest::auto_test_runner();
#else
    AUTORUN_TESTS()
#endif
LT_END_AUTO_TEST_ENV()
