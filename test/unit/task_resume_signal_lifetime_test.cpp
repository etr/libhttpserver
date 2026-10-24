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

#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>
#include <utility>
#include <httpserver/concurrency/resume_signal.hpp>
#include "./littletest.hpp"
#include "../../src/detail/darwin_focused_diagnostics.hpp"
namespace h = httpserver;
// TSan owns the allocator; only the allocation-hook case needs an override.
#if defined(LHS_SANITIZER_OWNS_OPERATOR_NEW) || defined(__SANITIZE_THREAD__)
#define LHS_SKIP_NEW_OVERRIDE 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer) || __has_feature(leak_sanitizer)
#define LHS_SKIP_NEW_OVERRIDE 1
#endif
#endif
#ifndef LHS_SKIP_NEW_OVERRIDE
namespace {
thread_local void (*allocation_hook)() = nullptr;
std::shared_ptr<h::detail::resume_state> state;
std::unique_ptr<h::resume_waiter> waiter;
int deliveries = 0;
int hook_calls = 0;
h::resume_outcome observed = h::resume_outcome::timeout;
class reclaiming_executor : public h::executor {
 public:
    void post(handler work) override {
        ++deliveries;
        observed = waiter->await_resume();
        waiter.reset();  // reclaim the published awaiter before await_suspend returns
        work();
    }
    bool is_current() const noexcept override { return true; }
};
int trigger = 1;
void reclaim_on_timer_allocation() {
    if (!state->head) return;
    allocation_hook = nullptr;
    ++hook_calls;
    h::detail::fire_trigger(state, trigger,
        trigger == 1 ? h::resume_outcome::resumed : h::resume_outcome::cancelled);
}
}  // namespace
void* operator new(std::size_t size) {
    if (allocation_hook) allocation_hook();
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
#endif

namespace {
h::task<h::resume_outcome> signal_waiter(h::resume_signal signal) {
    co_return co_await signal.wait();
}

h::task<void> retiring_waiter(std::shared_ptr<h::detail::resume_state> shared,
                            std::atomic<int>* resumptions) {
    co_await h::resume_waiter(std::move(shared));
    ++*resumptions;
}

void await_start(std::atomic<int>* arrivals, std::atomic<bool>* go) {
    ++*arrivals;
    while (!go->load()) std::this_thread::yield();
}
}  // namespace
LT_BEGIN_SUITE(resume_lifetime_suite)
    void set_up() { }
    void tear_down() {
#ifndef LHS_SKIP_NEW_OVERRIDE
        allocation_hook = nullptr;
        waiter.reset();
        state.reset();
#endif
    }
LT_END_SUITE(resume_lifetime_suite)
LT_BEGIN_AUTO_TEST(resume_lifetime_suite, published_waiter_can_die_during_timeout_registration)
#ifdef LHS_SKIP_NEW_OVERRIDE
    LT_SKIP("sanitizer owns operator new; timer-allocation reclaim hook runs in normal and ASan/UBSan lanes");
#else
    // Timer construction is outside the forced interleaving. The hook
    // fires only when a published waiter starts allocating its timeout.
    static_cast<void>(h::detail::default_timer_queue());
    for (int winner : {1, 2}) {
        trigger = winner; deliveries = 0; hook_calls = 0;
        observed = h::resume_outcome::timeout;
        state = std::make_shared<h::detail::resume_state>();
        waiter = std::make_unique<h::resume_waiter>(state,
            std::chrono::steady_clock::now() + std::chrono::seconds(60));
        reclaiming_executor target;
        auto* previous = h::detail::current_executor_slot();
        h::detail::current_executor_slot() = &target;
        allocation_hook = reclaim_on_timer_allocation;
        auto continuation = waiter->await_suspend(std::noop_coroutine());
        h::detail::current_executor_slot() = previous;
        allocation_hook = nullptr;
        LHS_DARWIN_DIAG("resume winner=%d returned=%p local_noop=%p hooks=%d deliveries=%d observed=%d waiter_alive=%d head_alive=%d\n",
            winner, continuation.address(), std::noop_coroutine().address(), hook_calls,
            deliveries, static_cast<int>(observed), static_cast<bool>(waiter), static_cast<bool>(state->head));
        LT_CHECK(continuation == std::noop_coroutine());
        LT_CHECK_EQ(hook_calls, 1);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK(observed == (winner == 1 ? h::resume_outcome::resumed : h::resume_outcome::cancelled));
        LT_CHECK(!waiter);
        LT_CHECK(!state->head);
    }
#endif
LT_END_AUTO_TEST(published_waiter_can_die_during_timeout_registration)

LT_BEGIN_AUTO_TEST(resume_lifetime_suite, competing_signal_and_cancel_deliver_once)
    for (int iteration = 0; iteration < 100; ++iteration) {
        h::manual_executor target;
        h::resume_signal signal;
        int completions = 0;
        bool valid_result = false;
        auto outcome = h::resume_outcome::timeout;
        h::spawn(target, signal_waiter(signal), [&](auto result) {
            ++completions;
            valid_result = result.has_value();
            if (valid_result) outcome = result.value();
        });
        target.run_pending();
        LT_CHECK_EQ(completions, 0);
        std::atomic<int> arrivals{0};
        std::atomic<bool> go{false};
        std::thread signaller([&] { await_start(&arrivals, &go); signal.signal(); });
        std::thread canceller([&] { await_start(&arrivals, &go); signal.cancel(); });
        while (arrivals.load() != 2) std::this_thread::yield();
        go = true;
        signaller.join();
        canceller.join();
        target.run_pending();
        signal.cancel();
        signal.signal();
        target.run_pending();
        LT_CHECK_EQ(completions, 1);
        LT_CHECK(valid_result);
        LT_CHECK(outcome == h::resume_outcome::resumed || outcome == h::resume_outcome::cancelled);
        LT_CHECK_EQ(target.pending(), static_cast<std::size_t>(0));
    }
LT_END_AUTO_TEST(competing_signal_and_cancel_deliver_once)

LT_BEGIN_AUTO_TEST(resume_lifetime_suite, frame_retirement_disarms_racing_and_queued_resumption)
    for (int iteration = 0; iteration < 100; ++iteration) {
        for (int winner : {1, 2}) {
            h::manual_executor target;
            auto shared = std::make_shared<h::detail::resume_state>();
            std::atomic<int> resumptions{0};
            auto pending = retiring_waiter(shared, &resumptions);
            auto frame = pending.release();
            frame.promise().set_affinity(&target);
            auto witness = frame.promise().frame_witness_ptr();
            h::detail::guarded_resume(witness, frame);
            LT_ASSERT(shared->head != nullptr);
            std::weak_ptr<h::detail::resume_node> node = shared->head;
            auto fire = [&] {
                h::detail::fire_trigger(shared, winner,
                    winner == 1 ? h::resume_outcome::resumed : h::resume_outcome::cancelled);
            };
            // Even iterations prove that a job already queued before retirement
            // becomes a no-op. Odd iterations race publication with retirement.
            if (iteration % 2 == 0) {
                fire();
                LT_CHECK_EQ(target.pending(), static_cast<std::size_t>(1));
            }
            std::atomic<int> arrivals{0};
            std::atomic<bool> go{false};
            std::thread igniter([&] { await_start(&arrivals, &go); fire(); });
            std::thread retirer([&] {
                await_start(&arrivals, &go);
                frame.promise().invalidate();
                frame.destroy();
            });
            while (arrivals.load() != 2) std::this_thread::yield();
            go = true;
            igniter.join();
            retirer.join();
            target.run_pending();
            LT_CHECK_EQ(resumptions.load(), 0);
            LT_CHECK(!shared->head);
            LT_CHECK(node.expired());
            LT_CHECK(!witness->valid);
            LT_CHECK_EQ(target.pending(), static_cast<std::size_t>(0));
        }
    }
LT_END_AUTO_TEST(frame_retirement_disarms_racing_and_queued_resumption)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
