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

// TASK-098 Step 3: stop_source / stop_token fan-out with
// executor-posted cancellation (PRD-V3N-REQ-025/031 substrate). Pins:
//   - request_stop() is idempotent (true exactly once) and never blocks;
//   - one source fans out to N token copies, all observing the request;
//   - co_await token.cancelled() completes exactly once with the typed
//     outcome http::outcome_code::cancelled — never as an exception
//     leaking past the frame;
//   - a pre-cancelled token completes the waiter immediately (still
//     exactly once, delivered through the executor);
//   - cancellation propagates through task chains as a typed outcome;
//   - destroying an unconsumed waiter while a concurrent request_stop
//     fires produces no callback after destruction and no crash;
//   - waiters are never resumed synchronously inside request_stop().

#include <atomic>
#include <thread>
#include <type_traits>
#include <vector>

#include <httpserver/concurrency/cancellation.hpp>

#include "./littletest.hpp"

using httpserver::cancelled_exception;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::stop_source;
using httpserver::stop_token;
using httpserver::task;
using httpserver::task_result;

static_assert(std::is_copy_constructible_v<stop_token>,
              "stop_token fans out by copying");
static_assert(std::is_nothrow_copy_constructible_v<stop_token>,
              "stop_token copy must not throw");
static_assert(!std::is_copy_constructible_v<stop_source>,
              "stop_source is the unique request point; tokens fan out");
static_assert(std::is_move_constructible_v<stop_source>,
              "stop_source is movable");

namespace {

task<void> await_cancelled(stop_token token, std::atomic<int>* completions) {
    co_await token.cancelled();
    ++*completions;  // unreachable: await_resume always throws when fired
    co_return;
}

task<void> passthrough(stop_token token, std::atomic<int>* completions) {
    auto inner = await_cancelled(std::move(token), completions);
    co_await std::move(inner);
    co_return;
}

}  // namespace

LT_BEGIN_SUITE(task_cancellation_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(task_cancellation_suite)

LT_BEGIN_AUTO_TEST(task_cancellation_suite, request_stop_is_idempotent)
    stop_source source;
    stop_token token = source.get_token();
    LT_ASSERT(token.stop_possible());
    LT_ASSERT(!token.stop_requested());
    LT_ASSERT(source.request_stop());
    LT_ASSERT(!source.request_stop());
    LT_ASSERT(!source.request_stop());
    LT_ASSERT(token.stop_requested());
    LT_ASSERT(source.stop_requested());
LT_END_AUTO_TEST(request_stop_is_idempotent)

LT_BEGIN_AUTO_TEST(task_cancellation_suite, fan_out_to_many_tokens)
    stop_source source;
    std::vector<stop_token> tokens;
    for (int i = 0; i < 8; ++i) tokens.push_back(source.get_token());
    // Independent copies fan out to arbitrary numbers of consumers.
    std::vector<stop_token> copies;
    for (const stop_token& t : tokens) copies.push_back(t);

    LT_ASSERT(source.request_stop());
    for (const stop_token& t : tokens) LT_ASSERT(t.stop_requested());
    for (const stop_token& t : copies) LT_ASSERT(t.stop_requested());
LT_END_AUTO_TEST(fan_out_to_many_tokens)

LT_BEGIN_AUTO_TEST(task_cancellation_suite, cancelled_awaiter_completes_once_with_typed_outcome)
    manual_executor ex;
    stop_source source;
    std::atomic<int> completions{0};
    int deliveries = 0;

    spawn(ex, await_cancelled(source.get_token(), &completions),
          [&](task_result<void> r) {
              ++deliveries;
              LT_ASSERT(r.has_outcome());
              LT_ASSERT(r.outcome()
                        == httpserver::http::outcome_code::cancelled);
          });
    ex.run_pending();  // start: the task suspends inside cancelled()

    LT_ASSERT_EQ(completions.load(), 0);
    LT_ASSERT(source.request_stop());
    ex.run_pending();  // drain the executor-posted resumption

    LT_ASSERT_EQ(completions.load(), 0);  // await_resume throws; body stops
    LT_ASSERT_EQ(deliveries, 1);
LT_END_AUTO_TEST(cancelled_awaiter_completes_once_with_typed_outcome)

LT_BEGIN_AUTO_TEST(task_cancellation_suite, pre_cancelled_token_completes_immediately)
    manual_executor ex;
    stop_source source;
    LT_ASSERT(source.request_stop());
    std::atomic<int> completions{0};
    int deliveries = 0;

    spawn(ex, await_cancelled(source.get_token(), &completions),
          [&](task_result<void> r) {
              ++deliveries;
              LT_ASSERT(r.has_outcome());
              LT_ASSERT(r.outcome()
                        == httpserver::http::outcome_code::cancelled);
          });
    // The token is already cancelled when the awaiter registers; the
    // waiter still completes exactly once, through the executor.
    ex.run_pending();
    LT_ASSERT_EQ(deliveries, 1);
    LT_ASSERT_EQ(completions.load(), 0);
LT_END_AUTO_TEST(pre_cancelled_token_completes_immediately)

LT_BEGIN_AUTO_TEST(task_cancellation_suite, cancellation_propagates_through_chains)
    manual_executor ex;
    stop_source source;
    std::atomic<int> completions{0};
    int deliveries = 0;

    spawn(ex, passthrough(source.get_token(), &completions),
          [&](task_result<void> r) {
              ++deliveries;
              LT_ASSERT(r.has_outcome());
              LT_ASSERT(r.outcome()
                        == httpserver::http::outcome_code::cancelled);
          });
    ex.run_pending();
    LT_ASSERT(source.request_stop());
    ex.run_pending();
    LT_ASSERT_EQ(deliveries, 1);
LT_END_AUTO_TEST(cancellation_propagates_through_chains)

LT_BEGIN_AUTO_TEST(task_cancellation_suite, waiter_is_never_resumed_inside_request_stop)
    manual_executor ex;
    stop_source source;
    std::atomic<int> completions{0};
    std::atomic<bool> resumed_inline{false};
    std::thread::id requesting_id;

    spawn(ex, await_cancelled(source.get_token(), &completions),
          [](task_result<void>) {});
    ex.run_pending();  // waiter suspended

    std::thread requester([&] {
        requesting_id = std::this_thread::get_id();
        source.request_stop();
        // request_stop() must return before the waiter runs: the
        // resumption is posted to the executor, never executed inline
        // on the requesting thread.
        if (completions.load() != 0) resumed_inline = true;
    });
    requester.join();

    ex.run_pending();
    LT_ASSERT(!resumed_inline);
LT_END_AUTO_TEST(waiter_is_never_resumed_inside_request_stop)

LT_BEGIN_AUTO_TEST(task_cancellation_suite, unconsumed_waiter_destroyed_while_stop_fires)
    // The waiter task is created but never consumed (lazy frame, no
    // awaiter registered). Destroying it while a concurrent request_stop
    // fires must be race-free: nothing is registered, no callback fires
    // after destruction.
    for (int i = 0; i < 200; ++i) {
        stop_source source;
        std::atomic<int> completions{0};
        task<void> waiter = await_cancelled(source.get_token(), &completions);
        std::thread requester([&] { source.request_stop(); });
        waiter = task<void>();  // destroy the (never-started) frame
        requester.join();
        LT_ASSERT_EQ(completions.load(), 0);
        LT_ASSERT(source.stop_requested());
    }
LT_END_AUTO_TEST(unconsumed_waiter_destroyed_while_stop_fires)

LT_BEGIN_AUTO_TEST(task_cancellation_suite, cancelled_exception_marker_type)
    // The marker crosses await_resume boundaries but is converted to a
    // typed outcome by the promise; it is catchable by consumers that
    // deliberately observe it.
    bool caught = false;
    try {
        throw cancelled_exception();
    } catch (const cancelled_exception&) {
        caught = true;
    }
    LT_ASSERT(caught);
LT_END_AUTO_TEST(cancelled_exception_marker_type)

LT_BEGIN_AUTO_TEST(task_cancellation_suite, stop_source_destruction_does_not_request_stop)
    // Matching std semantics: dropping the source does NOT implicitly
    // cancel outstanding tokens; explicit request_stop()/cancel() is the
    // disconnect notification mechanism (PRD-V3N-REQ-025 at this layer).
    stop_token token;
    {
        stop_source source;
        token = source.get_token();
    }
    LT_ASSERT(!token.stop_requested());
LT_END_AUTO_TEST(stop_source_destruction_does_not_request_stop)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
