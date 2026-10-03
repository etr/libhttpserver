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

#include <cstdlib>
#include <memory>
#include <new>
#include <httpserver/concurrency/resume_signal.hpp>
#include "./littletest.hpp"
namespace h = httpserver;
namespace {
thread_local void (*allocation_hook)() = nullptr;
std::shared_ptr<h::detail::resume_state> state;
std::unique_ptr<h::resume_waiter> waiter;
int deliveries = 0;
class reclaiming_executor : public h::executor {
 public:
    void post(handler work) override {
        ++deliveries;
        waiter.reset();  // reclaim the published awaiter before await_suspend returns
        work();
    }
    bool is_current() const noexcept override { return true; }
};
int trigger = 1;
void reclaim_on_timer_allocation() {
    if (!state->head) return;
    allocation_hook = nullptr;
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
LT_BEGIN_SUITE(resume_lifetime_suite)
    void set_up() { }
    void tear_down() { allocation_hook = nullptr; waiter.reset(); state.reset(); }
LT_END_SUITE(resume_lifetime_suite)
LT_BEGIN_AUTO_TEST(resume_lifetime_suite, published_waiter_can_die_during_timeout_registration)
    // Timer construction is outside the forced interleaving. The hook
    // fires only when a published waiter starts allocating its timeout.
    static_cast<void>(h::detail::default_timer_queue());
    for (int winner : {1, 2}) {
        trigger = winner; deliveries = 0;
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
        LT_CHECK(continuation == std::noop_coroutine());
        LT_CHECK_EQ(deliveries, 1); LT_CHECK(!waiter); LT_CHECK(!state->head);
    }
LT_END_AUTO_TEST(published_waiter_can_die_during_timeout_registration)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
