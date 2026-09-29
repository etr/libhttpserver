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

// TASK-098 Step 5: handler-safety acceptance (PRD-V3N-REQ-031,
// DR-V3-008). request_stop() must return while the caller is inside a
// handler that itself holds pending cancellation and resume-signal
// waiters; the handler body runs to completion, and only afterwards —
// when the executor drains — do the waiters observe cancelled, always
// executor-posted, never inline inside request_stop(). Callbacks record
// flags instead of using littletest macros: an exception thrown inside a
// spawned callback is contained by the completion path by design.

#include <atomic>
#include <thread>

#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/resume_signal.hpp>

#include "./littletest.hpp"

using httpserver::manual_executor;
using httpserver::resume_outcome;
using httpserver::resume_signal;
using httpserver::spawn;
using httpserver::stop_source;
using httpserver::stop_token;
using httpserver::task;
using httpserver::task_result;

namespace {

task<void> cancel_waiter(stop_token token, std::atomic<int>* completions) {
    co_await token.cancelled();
    ++*completions;  // unreachable when the stop fires
    co_return;
}

task<void> signal_waiter(resume_signal sig, resume_outcome* observed) {
    *observed = co_await sig.wait();
    co_return;
}

// The "handler": while its frame holds two pending child waiters (a
// cancelled() waiter and a resume-signal waiter), it calls request_stop()
// mid-body — the statement must return — then finishes normally. Records
// flags instead of using littletest macros: this is a coroutine, not a
// test body.
task<void> handler(stop_source& source, resume_signal& sig,
                   std::atomic<bool>* handler_done,
                   std::atomic<bool>* stop_won_exactly_once) {
    const bool first = source.request_stop();  // returns inside the handler
    const bool second = source.request_stop();
    *stop_won_exactly_once = first && !second;
    sig.cancel();  // wins the race against signal()
    *handler_done = true;
    co_return;
}

}  // namespace

LT_BEGIN_SUITE(task_stop_inside_handler_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(task_stop_inside_handler_suite)

LT_BEGIN_AUTO_TEST(task_stop_inside_handler_suite, stop_returns_inside_handler_deterministic)
    manual_executor ex;
    stop_source source;
    resume_signal sig;
    std::atomic<int> completions{0};
    std::atomic<bool> handler_done{false};
    std::atomic<int> cancel_deliveries{0};
    std::atomic<bool> cancel_result_cancelled{false};
    std::atomic<int> signal_deliveries{0};
    std::atomic<bool> stop_won_exactly_once{false};
    resume_outcome observed = resume_outcome::resumed;

    // The handler "holds" two pending waiters, spawned as children it
    // owns. Both suspend before the handler body runs.
    spawn(ex, cancel_waiter(source.get_token(), &completions),
          [&](task_result<void> r) {
              ++cancel_deliveries;
              cancel_result_cancelled =
                  r.has_outcome()
                  && r.outcome() == httpserver::http::outcome_code::cancelled;
          });
    spawn(ex, signal_waiter(sig, &observed),
          [&](task_result<void>) { ++signal_deliveries; });
    ex.run_pending();  // both waiters suspended; handler not started

    spawn(ex, handler(source, sig, &handler_done, &stop_won_exactly_once),
          [](task_result<void>) { });
    ex.run_one();  // runs only the handler body (waiters still pending)

    LT_CHECK(handler_done.load());
    LT_CHECK(stop_won_exactly_once.load());
    LT_CHECK_EQ(completions.load(), 0);
    LT_CHECK_EQ(cancel_deliveries.load(), 0);
    LT_CHECK_EQ(signal_deliveries.load(), 0);

    // Only now, when the executor drains, do the waiters observe
    // cancelled — delivered on the executor, not inside request_stop().
    ex.run_pending();

    LT_CHECK(handler_done.load());
    LT_CHECK_EQ(cancel_deliveries.load(), 1);
    LT_CHECK(cancel_result_cancelled.load());
    LT_CHECK_EQ(signal_deliveries.load(), 1);
    LT_CHECK_EQ(completions.load(), 0);  // cancel body never runs
    LT_CHECK(observed == resume_outcome::cancelled);
LT_END_AUTO_TEST(stop_returns_inside_handler_deterministic)

LT_BEGIN_AUTO_TEST(task_stop_inside_handler_suite, stop_concurrent_with_handler_on_two_threads)
    constexpr int kIterations = 100;
    for (int i = 0; i < kIterations; ++i) {
        manual_executor ex;
        stop_source source;
        resume_signal sig;
        std::atomic<bool> handler_done{false};
        std::atomic<int> deliveries{0};
        std::atomic<int> arrivals{0};

        auto body = [&]() -> task<void> {
            source.request_stop();  // returns inside the handler
            sig.cancel();
            handler_done = true;
            co_return;
        };
        spawn(ex, body(), [&](task_result<void>) { ++deliveries; });

        // Two real threads: one drains (runs the handler), one requests
        // stop concurrently. No deadlock; exactly one delivery.
        std::thread stopper([&] {
            while (arrivals.load() < 1) std::this_thread::yield();
            source.request_stop();
            sig.cancel();
        });
        arrivals.fetch_add(1);
        ex.run_pending();
        stopper.join();
        ex.run_pending();

        LT_CHECK_EQ(deliveries.load(), 1);
        LT_CHECK(handler_done.load());
    }
LT_END_AUTO_TEST(stop_concurrent_with_handler_on_two_threads)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
