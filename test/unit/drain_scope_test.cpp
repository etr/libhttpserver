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

// TASK-110 step 1: the counted drain scope (PRD-V3N-REQ-032,
// DR-V3-008) in isolation -- no engines, no transport. Units count,
// the deadline-bound wait reports completed or deadline_expired, the
// cancel hook fires at most once across concurrent waiters, and a
// wait entered from work the drain itself counts is rejected with
// would_deadlock unless the count already reached zero. Every wait is
// deadline-bound; a pass condition is an observed result, never a
// sleep.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <utility>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/detail/drain_scope.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/server.hpp>

#include "./littletest.hpp"

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;

using httpserver::detail::drain_scope;

using clock = std::chrono::steady_clock;

constexpr std::chrono::milliseconds kBudget{2000};

template<typename Pred>
bool wait_until(Pred pred) {
    const auto deadline = clock::now() + kBudget;
    while (!pred()) {
        if (clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

}  // namespace

LT_BEGIN_SUITE(drain_scope_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(drain_scope_suite)

// Units count up and down, the zero transition is idempotent, and the
// RAII bracket holds exactly one unit.
LT_BEGIN_AUTO_TEST(drain_scope_suite, units_count_and_notify_zero)
    drain_scope scope;
    LT_CHECK_EQ(scope.active(), std::size_t{0});
    scope.enter();
    scope.enter();
    LT_CHECK_EQ(scope.active(), std::size_t{2});
    scope.leave();
    LT_CHECK_EQ(scope.active(), std::size_t{1});
    scope.leave();
    LT_CHECK_EQ(scope.active(), std::size_t{0});
    // A stray leave cannot drive the count negative.
    scope.leave();
    LT_CHECK_EQ(scope.active(), std::size_t{0});
    {
        drain_scope::unit held{scope};
        LT_CHECK_EQ(scope.active(), std::size_t{1});
    }
    LT_CHECK_EQ(scope.active(), std::size_t{0});
LT_END_AUTO_TEST(units_count_and_notify_zero)

// An armed scope with no live units completes immediately.
LT_BEGIN_AUTO_TEST(drain_scope_suite, wait_completes_when_idle)
    drain_scope scope;
    scope.arm(clock::now() + kBudget, {}, {});
    LT_CHECK(scope.armed());
    srv::drain_result out;
    const http::outcome done = scope.wait(out);
    LT_CHECK(done.ok());
    LT_CHECK(out.status == srv::drain_status::completed);
    LT_CHECK_EQ(out.remaining, std::size_t{0});
LT_END_AUTO_TEST(wait_completes_when_idle)

// An unarmed scope has no drain to wait on.
LT_BEGIN_AUTO_TEST(drain_scope_suite, wait_unarmed_is_invalid_state)
    drain_scope scope;
    LT_CHECK(!scope.armed());
    srv::drain_result out;
    const http::outcome refused = scope.wait(out);
    LT_CHECK(!refused.ok());
    LT_CHECK(refused.code() == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(wait_unarmed_is_invalid_state)

// A hanging unit past a tight deadline expires the wait with the
// pre-cancel remaining count, and the cancel hook fires exactly once
// no matter how many waiters report the same expiry.
LT_BEGIN_AUTO_TEST(drain_scope_suite, wait_deadline_expires_cancels_once)
    drain_scope scope;
    std::atomic<int> cancels{0};
    scope.enter();   // the hanging unit
    scope.arm(clock::now() + std::chrono::milliseconds(150), {},
              [&cancels] { cancels.fetch_add(1); });
    srv::drain_result first_out;
    srv::drain_result second_out;
    std::thread second(
        [&] { static_cast<void>(scope.wait(second_out)); });
    const http::outcome first = scope.wait(first_out);
    second.join();
    LT_CHECK(first.ok());
    LT_CHECK(first_out.status == srv::drain_status::deadline_expired);
    LT_CHECK_EQ(first_out.remaining, std::size_t{1});
    LT_CHECK(second_out.status == srv::drain_status::deadline_expired);
    LT_CHECK_EQ(second_out.remaining, std::size_t{1});
    LT_CHECK_EQ(cancels.load(), 1);
    scope.leave();   // unwind the hanging unit before teardown
LT_END_AUTO_TEST(wait_deadline_expires_cancels_once)

// A unit released while the wait is parked wakes it to completion
// (observed inside the wait's own deadline -- never a timed pass).
LT_BEGIN_AUTO_TEST(drain_scope_suite, wait_completes_after_leave_during_wait)
    drain_scope scope;
    scope.enter();
    scope.arm(clock::now() + kBudget, {}, {});
    srv::drain_result out;
    std::atomic<bool> started{false};
    std::atomic<bool> ok{false};
    std::thread waiter([&] {
        started.store(true);
        ok.store(scope.wait(out).ok());
    });
    LT_CHECK(wait_until([&] { return started.load(); }));
    // Arrangement only: let the waiter park. The pass condition is the
    // observed completion inside the 2 s deadline, so a lost wake fails
    // this as a deadline expiry, not a hang.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    scope.leave();
    waiter.join();
    LT_CHECK(ok.load());
    LT_CHECK(out.status == srv::drain_status::completed);
    LT_CHECK_EQ(out.remaining, std::size_t{0});
LT_END_AUTO_TEST(wait_completes_after_leave_during_wait)

// A wait from work the drain itself counts is rejected with
// would_deadlock; the scope stays usable afterwards.
LT_BEGIN_AUTO_TEST(drain_scope_suite, wait_from_counted_thread_rejects)
    drain_scope scope;
    scope.enter();
    scope.arm(clock::now() + kBudget, [] { return true; }, {});
    srv::drain_result out;
    const http::outcome rejected = scope.wait(out);
    LT_CHECK(!rejected.ok());
    LT_CHECK(rejected.code() == http::outcome_code::would_deadlock);
    // The same caller completes once the counted work ends.
    scope.leave();
    srv::drain_result done;
    const http::outcome complete = scope.wait(done);
    LT_CHECK(complete.ok());
    LT_CHECK(done.status == srv::drain_status::completed);
    LT_CHECK_EQ(done.remaining, std::size_t{0});
LT_END_AUTO_TEST(wait_from_counted_thread_rejects)

// The counted-thread rejection never fires once the count reached
// zero: a pool thread between counted items completes instead.
LT_BEGIN_AUTO_TEST(drain_scope_suite,
                   late_wait_after_units_gone_from_counted_thread_completes)
    drain_scope scope;
    scope.enter();
    scope.leave();
    scope.arm(clock::now() + kBudget, [] { return true; }, {});
    srv::drain_result out;
    const http::outcome done = scope.wait(out);
    LT_CHECK(done.ok());
    LT_CHECK(out.status == srv::drain_status::completed);
    LT_CHECK_EQ(out.remaining, std::size_t{0});
LT_END_AUTO_TEST(late_wait_after_units_gone_from_counted_thread_completes)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
