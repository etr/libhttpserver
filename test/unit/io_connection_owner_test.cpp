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

// TASK-099 step 2: per-connection completion serializer (architecture
// §3.4, DR-V3-004). Pins the io_connection_owner contracts:
//   - FIFO: records apply in arrival order -- the one deterministic
//     linear order a connection's state machine observes;
//   - no reentrancy: while record N applies (even when its resumed task
//     re-enters enqueue), record N+1 waits; draining() is observable;
//   - coalescing: enqueues while a drain is pending post exactly one
//     drain job on the executor;
//   - concurrent stress: 4 threads x 200 records apply exactly once, no
//     duplicates, one linear order;
//   - two owners on one executor stay independent and linear;
//   - resumption goes through the awaited frame's executor (visible
//     only after the executor runs the posted job);
//   - the destructor applies still-queued records exactly once.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "./httpserver/concurrency/task.hpp"
#include "./httpserver/detail/io_connection_owner.hpp"
#include "./httpserver/detail/io_operation.hpp"

#include "./littletest.hpp"

using httpserver::current_executor;
using httpserver::executor;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;

namespace hd = httpserver::detail;
namespace hh = httpserver::http;

namespace {

// Mutex-protected application-order log shared by awaiting tasks.
class order_log {
 public:
    void push(int id) {
        std::lock_guard<std::mutex> lock(mu_);
        ids_.push_back(id);
    }

    std::vector<int> snapshot() const {
        std::lock_guard<std::mutex> lock(mu_);
        return ids_;
    }

 private:
    mutable std::mutex mu_;
    std::vector<int> ids_;
};

// Spawn completion callback counting exactly-once deliveries; a
// non-value result forces a second count so totals catch it.
class delivery_counter {
 public:
    explicit delivery_counter(std::atomic<int>* delivered) noexcept
        : delivered_(delivered) { }

    void operator()(task_result<void> result) const {
        ++*delivered_;
        if (!result.has_value()) ++*delivered_;
    }

 private:
    std::atomic<int>* delivered_;
};

task<void> await_and_log(hd::read_operation op, order_log* log, int id) {
    co_await std::move(op);
    log->push(id);
}

task<void> await_note_executor(hd::read_operation op,
                               const executor** seen_ex) {
    co_await std::move(op);
    *seen_ex = current_executor();
}

// Task body of the no-reentrancy probe: after record 1 applies, note
// that the drain is still running (this body runs inside it), then
// re-enter enqueue with record 2 from inside the resumed task.
task<void> reenter_mid_drain(hd::read_operation op, order_log* log,
                             std::atomic<bool>* saw_draining,
                             hd::io_connection_owner* owner,
                             const std::shared_ptr<hd::op_state>& s2) {
    co_await std::move(op);
    log->push(1);
    saw_draining->store(owner->draining());
    owner->enqueue(s2, hd::io_result{});
}

}  // namespace

LT_BEGIN_SUITE(io_connection_owner_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(io_connection_owner_suite)

// Test 1: FIFO linearization. A, B, C enqueue in arrival order; the
// owner applies them in exactly that order.
LT_BEGIN_AUTO_TEST(io_connection_owner_suite, applies_in_arrival_order)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::byte buffer[8];
    order_log log;
    std::atomic<int> delivered_a{0};
    std::atomic<int> delivered_b{0};
    std::atomic<int> delivered_c{0};

    hd::read_operation a(owner, 1, std::span<std::byte>(buffer));
    hd::read_operation b(owner, 1, std::span<std::byte>(buffer));
    hd::read_operation c(owner, 1, std::span<std::byte>(buffer));
    const auto sa = a.state();
    const auto sb = b.state();
    const auto sc = c.state();

    task<void> ta = await_and_log(std::move(a), &log, 1);
    task<void> tb = await_and_log(std::move(b), &log, 2);
    task<void> tc = await_and_log(std::move(c), &log, 3);
    spawn(ex, std::move(ta), delivery_counter{&delivered_a});
    spawn(ex, std::move(tb), delivery_counter{&delivered_b});
    spawn(ex, std::move(tc), delivery_counter{&delivered_c});
    ex.run_pending();  // tasks suspend at co_await

    owner.enqueue(sa, hd::io_result{});
    owner.enqueue(sb, hd::io_result{});
    owner.enqueue(sc, hd::io_result{});
    ex.run_pending();  // one drain applies all three in order

    const std::vector<int> want{1, 2, 3};
    const std::vector<int> got = log.snapshot();
    LT_CHECK_COLLECTIONS_EQ(got.begin(), got.end(), want.begin());
    LT_CHECK_EQ(delivered_a.load(), 1);
    LT_CHECK_EQ(delivered_b.load(), 1);
    LT_CHECK_EQ(delivered_c.load(), 1);
LT_END_AUTO_TEST(applies_in_arrival_order)

// Test 2: no reentrancy. While record 1 applies (its resumed task
// observes draining() == true and re-enters enqueue with record 2),
// record 2 does not start; it applies in the continued drain, exactly
// once, and no second drain job is posted.
LT_BEGIN_AUTO_TEST(io_connection_owner_suite, no_reentrancy_during_apply)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::byte buffer[8];
    order_log log;
    std::atomic<int> delivered_1{0};
    std::atomic<int> delivered_2{0};
    std::atomic<bool> saw_draining{false};

    hd::read_operation first(owner, 1, std::span<std::byte>(buffer));
    hd::read_operation second(owner, 1, std::span<std::byte>(buffer));
    const auto s1 = first.state();
    const auto s2 = second.state();

    task<void> t1 = reenter_mid_drain(std::move(first), &log, &saw_draining,
                                      &owner, s2);
    task<void> t2 = await_and_log(std::move(second), &log, 2);
    spawn(ex, std::move(t1), delivery_counter{&delivered_1});
    spawn(ex, std::move(t2), delivery_counter{&delivered_2});
    ex.run_pending();  // both tasks suspended

    owner.enqueue(s1, hd::io_result{});
    ex.run_pending();  // drain applies 1; task re-enqueues 2; drain continues

    const std::vector<int> want{1, 2};
    const std::vector<int> got = log.snapshot();
    LT_CHECK_COLLECTIONS_EQ(got.begin(), got.end(), want.begin());
    LT_CHECK(saw_draining.load());
    LT_CHECK_EQ(delivered_1.load(), 1);
    LT_CHECK_EQ(delivered_2.load(), 1);
    LT_CHECK_EQ(ex.pending(), std::size_t{0});  // coalescing held: no 2nd job
LT_END_AUTO_TEST(no_reentrancy_during_apply)

// Test 3: coalesced posting. N enqueues with no drain running produce
// exactly one drain job; the next enqueue posts a fresh one.
LT_BEGIN_AUTO_TEST(io_connection_owner_suite, drain_jobs_coalesce)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::byte buffer[8];
    order_log log;
    constexpr int kRecords = 5;

    std::vector<hd::read_operation> ops;
    ops.reserve(kRecords);
    std::vector<std::shared_ptr<hd::op_state>> states;
    std::vector<std::atomic<int>> counters(kRecords);
    for (int i = 0; i < kRecords; ++i) {
        ops.emplace_back(owner, 1, std::span<std::byte>(buffer));
        states.push_back(ops.back().state());
    }
    for (int i = 0; i < kRecords; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        task<void> body = await_and_log(std::move(ops[idx]), &log, i + 1);
        spawn(ex, std::move(body), delivery_counter{&counters[idx]});
    }
    ex.run_pending();  // all suspended

    for (const auto& state : states) owner.enqueue(state, hd::io_result{});
    LT_CHECK_EQ(ex.pending(), std::size_t{1});   // one drain job, not five
    LT_CHECK_EQ(owner.pending(), std::size_t{kRecords});
    ex.run_pending();

    LT_CHECK_EQ(log.snapshot().size(), std::size_t{kRecords});
    for (int i = 0; i < kRecords; ++i) {
        LT_CHECK_EQ(counters[static_cast<std::size_t>(i)].load(), 1);
    }

    // After the drain the coalescing flag resets: a new record posts a
    // new job.
    owner.enqueue(states.front(), hd::io_result{});
    LT_CHECK_EQ(ex.pending(), std::size_t{1});
    ex.run_pending();
    LT_CHECK_EQ(log.snapshot().size(), std::size_t{kRecords});
LT_END_AUTO_TEST(drain_jobs_coalesce)

// Test 4: concurrent stress. 4 worker threads enqueue 200 records each
// while the main thread drains concurrently; every record applies
// exactly once, with no duplicates, in one linear order.
LT_BEGIN_AUTO_TEST(io_connection_owner_suite, concurrent_stress_single_order)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    order_log log;
    std::mutex delivered_mu;
    std::unordered_map<int, int> delivered;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 200;
    constexpr int kTotal = kThreads * kPerThread;

    auto worker = [&](int t) {
        for (int i = 0; i < kPerThread; ++i) {
            const int id = t * kPerThread + i;
            hd::read_operation op(owner, 1, std::span<std::byte>{});
            auto state = op.state();
            task<void> body = await_and_log(std::move(op), &log, id);
            spawn(ex, std::move(body), [&, id](task_result<void> result) {
                std::lock_guard<std::mutex> lock(delivered_mu);
                int& count = delivered[id];
                ++count;
                if (!result.has_value()) ++count;  // force a second count
            });
            owner.enqueue(std::move(state), hd::io_result{});
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) threads.emplace_back(worker, t);
    // Drain concurrently with the workers (the realistic race); the
    // spin cap keeps a stall a failure instead of a hang.
    for (int spin = 0; spin < 200000 && log.snapshot().size() < kTotal;
         ++spin) {
        if (!ex.run_one()) std::this_thread::yield();
    }
    for (auto& thread : threads) thread.join();
    ex.run_pending();

    const std::vector<int> applied = log.snapshot();
    LT_CHECK_EQ(applied.size(), std::size_t{kTotal});
    std::vector<int> sorted(applied);
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (sorted[i] != static_cast<int>(i)) {
            LT_FAIL("stress applied-order is not a permutation of 0..N-1");
        }
    }
    std::lock_guard<std::mutex> lock(delivered_mu);
    LT_CHECK_EQ(delivered.size(), static_cast<std::size_t>(kTotal));
    for (const auto& entry : delivered) {
        if (entry.second != 1) {
            LT_FAIL("stress record delivered more than once");
        }
    }
LT_END_AUTO_TEST(concurrent_stress_single_order)

// Test 5: two owners on one executor. Interleaved enqueues keep each
// owner's application order independent and linear.
LT_BEGIN_AUTO_TEST(io_connection_owner_suite, two_owners_independent_orders)
    manual_executor ex;
    hd::io_connection_owner owner1(ex);
    hd::io_connection_owner owner2(ex);
    std::byte buffer[8];
    order_log log1;
    order_log log2;
    std::atomic<int> delivered[4]{};

    hd::read_operation a1(owner1, 1, std::span<std::byte>(buffer));
    hd::read_operation a2(owner1, 1, std::span<std::byte>(buffer));
    hd::read_operation b1(owner2, 2, std::span<std::byte>(buffer));
    hd::read_operation b2(owner2, 2, std::span<std::byte>(buffer));
    const auto sa1 = a1.state();
    const auto sa2 = a2.state();
    const auto sb1 = b1.state();
    const auto sb2 = b2.state();

    task<void> ta1 = await_and_log(std::move(a1), &log1, 1);
    task<void> ta2 = await_and_log(std::move(a2), &log1, 2);
    task<void> tb1 = await_and_log(std::move(b1), &log2, 1);
    task<void> tb2 = await_and_log(std::move(b2), &log2, 2);
    spawn(ex, std::move(ta1), delivery_counter{&delivered[0]});
    spawn(ex, std::move(ta2), delivery_counter{&delivered[1]});
    spawn(ex, std::move(tb1), delivery_counter{&delivered[2]});
    spawn(ex, std::move(tb2), delivery_counter{&delivered[3]});
    ex.run_pending();

    // Interleave the two owners deliberately.
    owner2.enqueue(sb1, hd::io_result{});
    owner1.enqueue(sa1, hd::io_result{});
    owner2.enqueue(sb2, hd::io_result{});
    owner1.enqueue(sa2, hd::io_result{});
    ex.run_pending();

    const std::vector<int> want{1, 2};
    const std::vector<int> got1 = log1.snapshot();
    const std::vector<int> got2 = log2.snapshot();
    LT_CHECK_COLLECTIONS_EQ(got1.begin(), got1.end(), want.begin());
    LT_CHECK_COLLECTIONS_EQ(got2.begin(), got2.end(), want.begin());
    for (int i = 0; i < 4; ++i) LT_CHECK_EQ(delivered[i].load(), 1);
LT_END_AUTO_TEST(two_owners_independent_orders)

// Test 6: resumption goes through the awaited frame's executor. The
// completed-but-not-drained record leaves the task suspended; only
// running the posted drain job resumes it, on the executor.
LT_BEGIN_AUTO_TEST(io_connection_owner_suite, resume_runs_on_frame_executor)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::byte buffer[8];
    std::atomic<int> delivered{0};
    const executor* seen = nullptr;

    hd::read_operation op(owner, 1, std::span<std::byte>(buffer));
    const auto state = op.state();
    task<void> body = await_note_executor(std::move(op), &seen);
    spawn(ex, std::move(body), delivery_counter{&delivered});
    ex.run_pending();  // task suspended at co_await
    LT_CHECK(seen == nullptr);

    owner.enqueue(state, hd::io_result{});
    LT_CHECK_EQ(delivered.load(), 0);       // queued: no inline resumption
    LT_CHECK_EQ(ex.pending(), std::size_t{1});

    ex.run_pending();
    LT_CHECK_EQ(delivered.load(), 1);
    LT_CHECK(seen == &ex);
LT_END_AUTO_TEST(resume_runs_on_frame_executor)

// Test 7: the destructor applies still-queued records with their
// already-decided results -- a claimed completion is never dropped.
// Observation is by direct op_state inspection: the drain job posted
// before destruction belongs to the destroyed owner, so (per the
// documented teardown contract) the executor is not drained afterwards.
LT_BEGIN_AUTO_TEST(io_connection_owner_suite, destructor_applies_queued)
    std::byte buffer[8];
    std::shared_ptr<hd::op_state> sa;
    std::shared_ptr<hd::op_state> sb;
    std::shared_ptr<hd::op_state> sc;
    {
        manual_executor ex;
        hd::io_connection_owner owner(ex);
        hd::read_operation a(owner, 1, std::span<std::byte>(buffer));
        hd::read_operation b(owner, 1, std::span<std::byte>(buffer));
        hd::read_operation c(owner, 1, std::span<std::byte>(buffer));
        sa = a.state();
        sb = b.state();
        sc = c.state();

        owner.enqueue(sa, hd::io_result{hh::outcome_code::ok, 1, 0});
        owner.enqueue(sb, hd::io_result{hh::outcome_code::ok, 2, 0});
        owner.enqueue(sc, hd::io_result{hh::outcome_code::ok, 3, 0});
        LT_CHECK_EQ(owner.pending(), std::size_t{3});
        LT_CHECK(!sa->applied());  // queued, not yet applied
    }

    LT_CHECK(sa->applied());
    LT_CHECK_EQ(sa->stored_result().transferred, std::size_t{1});
    LT_CHECK(sb->applied());
    LT_CHECK_EQ(sb->stored_result().transferred, std::size_t{2});
    LT_CHECK(sc->applied());
    LT_CHECK_EQ(sc->stored_result().transferred, std::size_t{3});
LT_END_AUTO_TEST(destructor_applies_queued)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
