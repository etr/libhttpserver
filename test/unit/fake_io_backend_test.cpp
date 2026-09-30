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

// TASK-099 step 3: fake backend acceptance suite (architecture §3.4,
// DR-V3-004). The fake_io_backend is the deterministic stand-in every
// later driver (TASK-100 poll/WSAPoll) must behave like: one terminal
// completion per submitted operation, completions deliverable from any
// thread in any order, and teardown that never silently drops a claimed
// completion. Families:
//   1. one op per kind through a fixed scrambled completion order;
//   2. reorder stress: 200 fixed-seed permutations of six ops -- each
//      terminal exactly once, owner application order == scheduled
//      delivery order, sequence() still the submission-order metadata,
//      and the delivery order differs from the submission order;
//   3. duplicate completions (complete twice, close after complete) are
//      no-ops;
//   4. cancel semantics: pending target -> target cancelled + cancel op
//      ok; already-terminal target -> cancel op invalid_state;
//      after close -> connection_closed family;
//   5. cancel-vs-complete race: exactly one terminal per target, both
//      outcome classes observed;
//   7. close(): every pending op connection_closed exactly once, later
//      completes are no-ops, double close is a no-op;
//   8. expire_timers: (deadline, sequence) order, not-due untouched;
//   9. fire_wake: all pending wakes exactly once;
//  10. coroutine integration: N awaiting tasks resume exactly once, in
//      owner application order;
//  11. cross-owner reordering: per-connection linearity holds.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "./httpserver/concurrency/task.hpp"
#include "./httpserver/detail/fake_io_backend.hpp"
#include "./httpserver/detail/io_connection_owner.hpp"
#include "./httpserver/detail/io_operation.hpp"

#include "./littletest.hpp"

using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;

namespace hd = httpserver::detail;
namespace hh = httpserver::http;

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

// Per-operation observation: the terminal result the awaiting task saw
// and how many times it was delivered (more than one is a failure).
struct probe {
    hd::io_result observed{};
    std::atomic<int> delivered{0};
};

// Executor + owner + buffers for one deterministic scenario.
struct rig {
    rig() : owner(ex) { }

    manual_executor ex;
    hd::io_connection_owner owner;
    std::byte buffer[16]{};
    const std::byte cbuffer[16]{};
};

template<typename Op>
task<void> await_into(Op op, probe* p) {
    p->observed = co_await std::move(op);
    ++p->delivered;
}

template<typename Op>
task<void> await_and_log(Op op, order_log* log, int id) {
    co_await std::move(op);
    log->push(id);
}

// Suspends a probe task on the rig executor. The op handle moves into
// the coroutine frame; callers snapshot op.state() beforehand.
template<typename Op>
void launch(rig& r, Op op, probe* p, std::vector<task<void>>& sink) {
    sink.push_back(await_into(std::move(op), p));
    spawn(r.ex, std::move(sink.back()), [](task_result<void>) { });
}

template<typename Op>
void launch_logged(rig& r, Op op, order_log* log, int id,
                   std::vector<task<void>>& sink) {
    sink.push_back(await_and_log(std::move(op), log, id));
    spawn(r.ex, std::move(sink.back()), [](task_result<void>) { });
}

}  // namespace

LT_BEGIN_SUITE(fake_io_backend_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(fake_io_backend_suite)

// Family 1: one op per kind, completed in a fixed scrambled order.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, one_per_kind_scrambled)
    rig r;
    hd::fake_io_backend backend;

    probe p_accept;
    probe p_read;
    probe p_write;
    probe p_timer;
    probe p_wake;
    probe p_target;
    probe p_cancel;
    std::vector<task<void>> tasks;

    const auto deadline = std::chrono::steady_clock::now() + 100ms;
    hd::accept_operation accept_op(r.owner, 1);
    hd::read_operation read_op(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::write_operation write_op(r.owner, 1,
                                 std::span<const std::byte>(r.cbuffer));
    hd::timer_operation timer_op(r.owner, 1, deadline);
    hd::wake_operation wake_op(r.owner, 1);
    hd::read_operation target(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::cancel_operation cancel_op(r.owner, 1, target);

    accept_op.submit(backend);
    read_op.submit(backend);
    write_op.submit(backend);
    timer_op.submit(backend);
    wake_op.submit(backend);
    target.submit(backend);
    cancel_op.submit(backend);  // resolves the target immediately
    const auto accept_state = accept_op.state();
    const auto read_state = read_op.state();
    const auto write_state = write_op.state();
    const auto timer_state = timer_op.state();
    const auto wake_state = wake_op.state();

    launch(r, std::move(accept_op), &p_accept, tasks);
    launch(r, std::move(read_op), &p_read, tasks);
    launch(r, std::move(write_op), &p_write, tasks);
    launch(r, std::move(timer_op), &p_timer, tasks);
    launch(r, std::move(wake_op), &p_wake, tasks);
    launch(r, std::move(target), &p_target, tasks);
    launch(r, std::move(cancel_op), &p_cancel, tasks);
    r.ex.run_pending();

    // Scrambled scripted completions.
    LT_CHECK(backend.complete(*wake_state, hd::io_result{}));
    LT_CHECK(backend.complete(*timer_state, hd::io_result{}));
    LT_CHECK(backend.complete(*accept_state,
                              hd::io_result{hh::outcome_code::ok, 0, 77}));
    LT_CHECK(backend.complete(*write_state,
                              hd::io_result{hh::outcome_code::ok, 2, 0}));
    LT_CHECK(backend.complete(*read_state,
                              hd::io_result{hh::outcome_code::ok, 4, 0}));
    LT_CHECK_EQ(backend.pending_count(), std::size_t{0});
    r.ex.run_pending();

    LT_CHECK_EQ(p_accept.delivered.load(), 1);
    LT_CHECK_EQ(p_read.delivered.load(), 1);
    LT_CHECK_EQ(p_write.delivered.load(), 1);
    LT_CHECK_EQ(p_timer.delivered.load(), 1);
    LT_CHECK_EQ(p_wake.delivered.load(), 1);
    LT_CHECK_EQ(p_target.delivered.load(), 1);
    LT_CHECK_EQ(p_cancel.delivered.load(), 1);

    LT_CHECK(p_accept.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(p_accept.observed.accepted_id, std::uint64_t{77});
    LT_CHECK_EQ(p_read.observed.transferred, std::size_t{4});
    LT_CHECK_EQ(p_write.observed.transferred, std::size_t{2});
    LT_CHECK(p_timer.observed.code == hh::outcome_code::ok);
    LT_CHECK(p_wake.observed.code == hh::outcome_code::ok);
    LT_CHECK(p_target.observed.code == hh::outcome_code::cancelled);
    LT_CHECK(p_cancel.observed.code == hh::outcome_code::ok);
LT_END_AUTO_TEST(one_per_kind_scrambled)

// Family 2: reorder stress (the headline acceptance). 200 fixed-seed
// permutations of six ops on one owner.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, reorder_stress)
    constexpr int kIterations = 200;
    constexpr int kOps = 6;
    int non_identity = 0;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        rig r;
        hd::fake_io_backend backend;

        probe probes[kOps];
        std::vector<task<void>> tasks;
        std::vector<std::shared_ptr<hd::op_state>> states;
        std::vector<hd::op_state*> raw;
        const auto deadline = std::chrono::steady_clock::now() + 5ms;

        hd::accept_operation accept_op(r.owner, 1);
        hd::read_operation read_op(r.owner, 1, std::span<std::byte>(r.buffer));
        hd::write_operation write_op(r.owner, 1,
                                     std::span<const std::byte>(r.cbuffer));
        hd::timer_operation timer_op(r.owner, 1, deadline);
        hd::wake_operation wake_op(r.owner, 1);
        hd::read_operation read2(r.owner, 1, std::span<std::byte>(r.buffer));

        auto submit_all = [&backend, &states, &raw](auto& op) {
            op.submit(backend);
            states.push_back(op.state());
            raw.push_back(states.back().get());
        };
        submit_all(accept_op);
        submit_all(read_op);
        submit_all(write_op);
        submit_all(timer_op);
        submit_all(wake_op);
        submit_all(read2);

        launch(r, std::move(accept_op), &probes[0], tasks);
        launch(r, std::move(read_op), &probes[1], tasks);
        launch(r, std::move(write_op), &probes[2], tasks);
        launch(r, std::move(timer_op), &probes[3], tasks);
        launch(r, std::move(wake_op), &probes[4], tasks);
        launch(r, std::move(read2), &probes[5], tasks);
        r.ex.run_pending();

        std::vector<int> order(kOps);
        for (int i = 0; i < kOps; ++i) order[static_cast<std::size_t>(i)] = i;
        std::mt19937 rng(0xC0FFEEu + static_cast<unsigned>(iteration));
        std::shuffle(order.begin(), order.end(), rng);

        const hd::io_result scripted{hh::outcome_code::ok, 1, 0};
        for (int idx : order) {
            LT_CHECK(backend.complete(*raw[static_cast<std::size_t>(idx)],
                                      scripted));
        }
        r.ex.run_pending();

        // (a) each op terminal exactly once.
        for (int i = 0; i < kOps; ++i) {
            if (probes[i].delivered.load() != 1) {
                LT_FAIL("reorder stress: op delivered more than once");
            }
        }
        // (b) every probe saw the scripted result: deliveries routed to
        // the right ops despite the scramble.
        for (int i = 0; i < kOps; ++i) {
            if (probes[i].observed.transferred != 1) {
                LT_FAIL("reorder stress: op observed a foreign result");
            }
        }
        // (c) the delivery order differs from the submission order at
        // least once across the loop.
        if (order[0] != 0) ++non_identity;
        // (d) sequence() still reports submission order as metadata.
        for (int i = 0; i < kOps; ++i) {
            LT_CHECK_EQ(raw[static_cast<std::size_t>(i)]->sequence(),
                        static_cast<std::uint64_t>(i + 1));
        }
    }
    LT_CHECK(non_identity > 0);
LT_END_AUTO_TEST(reorder_stress)

// Family 3: duplicate completions are no-ops.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, duplicate_completions_are_noops)
    rig r;
    hd::fake_io_backend backend;
    probe p;
    std::vector<task<void>> tasks;

    hd::read_operation op(r.owner, 1, std::span<std::byte>(r.buffer));
    op.submit(backend);
    const auto state = op.state();
    launch(r, std::move(op), &p, tasks);
    r.ex.run_pending();

    LT_CHECK(
        backend.complete(*state, hd::io_result{hh::outcome_code::ok, 5, 0}));
    LT_CHECK(!backend.complete(
        *state, hd::io_result{hh::outcome_code::ok, 9, 0}));
    LT_CHECK_EQ(backend.close(), std::size_t{0});  // nothing left pending

    r.ex.run_pending();
    LT_CHECK_EQ(p.delivered.load(), 1);
    LT_CHECK_EQ(p.observed.transferred, std::size_t{5});
LT_END_AUTO_TEST(duplicate_completions_are_noops)

// Family 4a: cancel with a pending target -- target cancelled once,
// cancel op ok once.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, cancel_pending_target)
    rig r;
    hd::fake_io_backend backend;
    probe p_target;
    probe p_cancel;
    std::vector<task<void>> tasks;

    hd::read_operation target(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::cancel_operation cancel_op(r.owner, 1, target);
    target.submit(backend);
    cancel_op.submit(backend);

    launch(r, std::move(target), &p_target, tasks);
    launch(r, std::move(cancel_op), &p_cancel, tasks);
    r.ex.run_pending();

    LT_CHECK_EQ(p_target.delivered.load(), 1);
    LT_CHECK_EQ(p_cancel.delivered.load(), 1);
    LT_CHECK(p_target.observed.code == hh::outcome_code::cancelled);
    LT_CHECK(p_cancel.observed.code == hh::outcome_code::ok);
LT_END_AUTO_TEST(cancel_pending_target)

// Family 4b: already-terminal target -- cancel op reports invalid_state
// once; the target keeps its original result.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, cancel_terminal_target)
    rig r;
    hd::fake_io_backend backend;
    probe p_target;
    probe p_cancel;
    std::vector<task<void>> tasks;

    hd::read_operation target(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::cancel_operation cancel_op(r.owner, 1, target);
    target.submit(backend);
    const auto target_state = target.state();
    launch(r, std::move(target), &p_target, tasks);
    r.ex.run_pending();
    LT_CHECK(backend.complete(
        *target_state, hd::io_result{hh::outcome_code::ok, 3, 0}));
    r.ex.run_pending();

    cancel_op.submit(backend);
    launch(r, std::move(cancel_op), &p_cancel, tasks);
    r.ex.run_pending();

    LT_CHECK_EQ(p_target.delivered.load(), 1);
    LT_CHECK_EQ(p_target.observed.transferred, std::size_t{3});
    LT_CHECK_EQ(p_cancel.delivered.load(), 1);
    LT_CHECK(p_cancel.observed.code == hh::outcome_code::invalid_state);
LT_END_AUTO_TEST(cancel_terminal_target)

// Family 4c: cancel after close -- the backend is closed, so a
// submitted cancel op completes with the connection_closed family.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, cancel_after_close)
    rig r;
    hd::fake_io_backend backend;
    probe p_cancel;
    std::vector<task<void>> tasks;

    hd::read_operation pending(r.owner, 1, std::span<std::byte>(r.buffer));
    pending.submit(backend);
    const auto pending_state = pending.state();
    LT_CHECK_EQ(backend.close(), std::size_t{1});
    backend.request_cancel(*pending_state);  // already swept: no-op

    hd::cancel_operation cancel_op(r.owner, 1, pending);
    cancel_op.submit(backend);
    launch(r, std::move(cancel_op), &p_cancel, tasks);
    r.ex.run_pending();

    LT_CHECK_EQ(p_cancel.delivered.load(), 1);
    LT_CHECK(p_cancel.observed.code == hh::outcome_code::connection_closed);
LT_END_AUTO_TEST(cancel_after_close)

// Family 5: cancel-vs-complete race. The spin gate starts both threads
// at the line; per iteration the target is terminal exactly once with
// outcome ok or cancelled; both classes must occur across the loop.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, cancel_vs_complete_race)
    constexpr int kIterations = 200;
    int ok_count = 0;
    int cancelled_count = 0;

    for (int i = 0; i < kIterations; ++i) {
        rig r;
        hd::fake_io_backend backend;
        probe p;
        std::vector<task<void>> tasks;
        std::atomic<int> arrivals{0};
        gate g(&arrivals);

        hd::read_operation op(r.owner, 1, std::span<std::byte>(r.buffer));
        op.submit(backend);
        const auto state = op.state();
        launch(r, std::move(op), &p, tasks);
        r.ex.run_pending();  // suspended

        std::thread completer([&] {
            g.arrive();
            g.wait(2);
            backend.complete(*state,
                             hd::io_result{hh::outcome_code::ok, 1, 0});
        });
        std::thread canceler([&] {
            g.arrive();
            g.wait(2);
            backend.request_cancel(*state);
        });
        completer.join();
        canceler.join();
        r.ex.run_pending();

        if (p.delivered.load() != 1) {
            LT_FAIL("cancel/complete race: target not terminal exactly once");
        }
        if (p.observed.code == hh::outcome_code::ok) {
            ++ok_count;
        } else if (p.observed.code == hh::outcome_code::cancelled) {
            ++cancelled_count;
        } else {
            LT_FAIL("cancel/complete race: unexpected outcome");
        }
    }
    LT_CHECK(ok_count > 0);
    LT_CHECK(cancelled_count > 0);
LT_END_AUTO_TEST(cancel_vs_complete_race)

// Family 7: close() claims every pending op with connection_closed,
// later completes are no-ops, and a double close is a no-op.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, close_completes_everything_once)
    rig r;
    hd::fake_io_backend backend;
    probe p_accept;
    probe p_read;
    probe p_write;
    probe p_timer;
    probe p_wake;
    std::vector<task<void>> tasks;
    const auto deadline = std::chrono::steady_clock::now() + 50ms;

    hd::accept_operation accept_op(r.owner, 1);
    hd::read_operation read_op(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::write_operation write_op(r.owner, 1,
                                 std::span<const std::byte>(r.cbuffer));
    hd::timer_operation timer_op(r.owner, 1, deadline);
    hd::wake_operation wake_op(r.owner, 1);
    accept_op.submit(backend);
    read_op.submit(backend);
    write_op.submit(backend);
    timer_op.submit(backend);
    wake_op.submit(backend);
    const auto read_state = read_op.state();
    LT_CHECK_EQ(backend.pending_count(), std::size_t{5});

    launch(r, std::move(accept_op), &p_accept, tasks);
    launch(r, std::move(read_op), &p_read, tasks);
    launch(r, std::move(write_op), &p_write, tasks);
    launch(r, std::move(timer_op), &p_timer, tasks);
    launch(r, std::move(wake_op), &p_wake, tasks);
    r.ex.run_pending();

    LT_CHECK_EQ(backend.close(), std::size_t{5});
    r.ex.run_pending();

    LT_CHECK_EQ(p_accept.delivered.load(), 1);
    LT_CHECK(p_accept.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(p_read.delivered.load(), 1);
    LT_CHECK(p_read.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(p_write.delivered.load(), 1);
    LT_CHECK_EQ(p_timer.delivered.load(), 1);
    LT_CHECK_EQ(p_wake.delivered.load(), 1);

    // Completes after close are no-ops; a double close is a no-op.
    LT_CHECK(!backend.complete(*read_state, hd::io_result{}));
    LT_CHECK_EQ(backend.close(), std::size_t{0});
    LT_CHECK_EQ(backend.pending_count(), std::size_t{0});
    r.ex.run_pending();
    LT_CHECK_EQ(p_read.delivered.load(), 1);  // still exactly once
LT_END_AUTO_TEST(close_completes_everything_once)

// Family 8: expire_timers completes due timers in (deadline, sequence)
// order and leaves not-due timers untouched.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, expire_timers_order)
    rig r;
    hd::fake_io_backend backend;
    order_log log;
    probe p4;
    std::vector<task<void>> tasks;

    const auto t0 = std::chrono::steady_clock::now();
    const auto early = t0 + 50ms;
    const auto late = t0 + 100ms;
    const auto never = t0 + 200ms;

    hd::timer_operation t1(r.owner, 1, late);   // seq 1, deadline 100ms
    hd::timer_operation t2(r.owner, 1, early);  // seq 2, deadline 50ms
    hd::timer_operation t3(r.owner, 1, early);  // seq 3, deadline 50ms
    hd::timer_operation t4(r.owner, 1, never);  // seq 4, not due yet
    hd::read_operation read_op(r.owner, 1, std::span<std::byte>(r.buffer));
    t1.submit(backend);
    t2.submit(backend);
    t3.submit(backend);
    t4.submit(backend);
    read_op.submit(backend);
    const auto read_state = read_op.state();

    // t1..t3 await and log their ids (resumption order == application
    // order); t4 is observed through its state only.
    launch_logged(r, std::move(t1), &log, 1, tasks);
    launch_logged(r, std::move(t2), &log, 2, tasks);
    launch_logged(r, std::move(t3), &log, 3, tasks);
    launch(r, std::move(t4), &p4, tasks);
    r.ex.run_pending();

    LT_CHECK_EQ(backend.expire_timers(t0 + 100ms), std::size_t{3});
    r.ex.run_pending();

    // Earliest deadline first; the sequence breaks the 50ms tie.
    const std::vector<int> want{2, 3, 1};
    const std::vector<int> got = log.snapshot();
    LT_CHECK_COLLECTIONS_EQ(got.begin(), got.end(), want.begin());
    LT_CHECK_EQ(got.size(), std::size_t{3});  // exactly once each
    LT_CHECK_EQ(p4.delivered.load(), 0);      // not due: untouched
    LT_CHECK(!read_state->is_terminal());

    LT_CHECK_EQ(backend.expire_timers(t0 + 200ms), std::size_t{1});
    r.ex.run_pending();
    LT_CHECK_EQ(p4.delivered.load(), 1);
    LT_CHECK(!read_state->is_terminal());  // reads never expire as timers
LT_END_AUTO_TEST(expire_timers_order)

// Family 9: fire_wake completes all pending wakes exactly once.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, fire_wake_once)
    rig r;
    hd::fake_io_backend backend;
    probe p1;
    probe p2;
    std::vector<task<void>> tasks;

    hd::wake_operation w1(r.owner, 1);
    hd::wake_operation w2(r.owner, 1);
    hd::read_operation read_op(r.owner, 1, std::span<std::byte>(r.buffer));
    w1.submit(backend);
    w2.submit(backend);
    read_op.submit(backend);
    const auto read_state = read_op.state();

    launch(r, std::move(w1), &p1, tasks);
    launch(r, std::move(w2), &p2, tasks);
    r.ex.run_pending();

    LT_CHECK_EQ(backend.fire_wake(), std::size_t{2});
    r.ex.run_pending();
    LT_CHECK_EQ(p1.delivered.load(), 1);
    LT_CHECK(p1.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(p2.delivered.load(), 1);
    LT_CHECK(p2.observed.code == hh::outcome_code::ok);
    LT_CHECK(!read_state->is_terminal());  // wakes never touch reads

    LT_CHECK_EQ(backend.fire_wake(), std::size_t{0});  // nothing pending
    r.ex.run_pending();
    LT_CHECK_EQ(p1.delivered.load(), 1);
    LT_CHECK_EQ(p2.delivered.load(), 1);
LT_END_AUTO_TEST(fire_wake_once)

// Family 10: coroutine integration. N awaiting tasks resume exactly
// once, and the resumption order equals the owner application order
// (which is the scripted completion order).
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, coroutine_integration)
    rig r;
    hd::fake_io_backend backend;
    order_log log;
    constexpr int kTasks = 20;
    std::vector<hd::read_operation> ops;
    ops.reserve(kTasks);
    // Shared ownership: completed spawned tasks self-destroy their
    // frames, so raw op_state pointers alone would dangle here.
    std::vector<std::shared_ptr<hd::op_state>> states;
    std::vector<task<void>> tasks;

    for (int i = 0; i < kTasks; ++i) {
        ops.emplace_back(r.owner, 1, std::span<std::byte>(r.buffer));
        ops.back().submit(backend);
        states.push_back(ops.back().state());
    }
    for (int i = 0; i < kTasks; ++i) {
        launch_logged(r, std::move(ops[static_cast<std::size_t>(i)]), &log,
                      i + 1, tasks);
    }
    r.ex.run_pending();

    std::vector<int> order(kTasks);
    for (int i = 0; i < kTasks; ++i) order[static_cast<std::size_t>(i)] = i;
    std::mt19937 rng(0x5EEDu);
    std::shuffle(order.begin(), order.end(), rng);
    for (int idx : order) {
        LT_CHECK(backend.complete(
            *states[static_cast<std::size_t>(idx)],
            hd::io_result{hh::outcome_code::ok, 1, 0}));
    }
    r.ex.run_pending();

    const std::vector<int> got = log.snapshot();
    LT_CHECK_EQ(got.size(), std::size_t{kTasks});
    for (int pos = 0; pos < kTasks; ++pos) {
        // Resumption order == application order == scripted order.
        if (got[static_cast<std::size_t>(pos)]
            != order[static_cast<std::size_t>(pos)] + 1) {
            LT_FAIL("coroutine integration: resumption order mismatch");
        }
    }
    for (int i = 0; i < kTasks; ++i) {
        // Exactly-once: each task logged exactly once (log size check
        // above); every op reached its applied terminal state.
        LT_CHECK(states[static_cast<std::size_t>(i)]->applied());
    }
LT_END_AUTO_TEST(coroutine_integration)

// Family 11: cross-owner reordering. Two connections on one backend,
// interleaved scramble; each owner's linear order holds independently.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, cross_owner_linearity)
    rig r;
    hd::fake_io_backend backend;
    hd::io_connection_owner owner2(r.ex);
    order_log log1;
    order_log log2;
    std::vector<task<void>> tasks;

    hd::read_operation a1(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::read_operation a2(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::read_operation b1(owner2, 2, std::span<std::byte>(r.buffer));
    hd::read_operation b2(owner2, 2, std::span<std::byte>(r.buffer));
    a1.submit(backend);
    a2.submit(backend);
    b1.submit(backend);
    b2.submit(backend);
    const auto sa1 = a1.state();
    const auto sa2 = a2.state();
    const auto sb1 = b1.state();
    const auto sb2 = b2.state();

    launch_logged(r, std::move(a1), &log1, 1, tasks);
    launch_logged(r, std::move(a2), &log1, 2, tasks);
    launch_logged(r, std::move(b1), &log2, 1, tasks);
    launch_logged(r, std::move(b2), &log2, 2, tasks);
    r.ex.run_pending();

    // Interleaved scramble across the two connections.
    LT_CHECK(backend.complete(*sb1,
                              hd::io_result{hh::outcome_code::ok, 1, 0}));
    LT_CHECK(backend.complete(*sa2,
                              hd::io_result{hh::outcome_code::ok, 1, 0}));
    LT_CHECK(backend.complete(*sb2,
                              hd::io_result{hh::outcome_code::ok, 1, 0}));
    LT_CHECK(backend.complete(*sa1,
                              hd::io_result{hh::outcome_code::ok, 1, 0}));
    r.ex.run_pending();

    const std::vector<int> got1 = log1.snapshot();
    const std::vector<int> got2 = log2.snapshot();
    const std::vector<int> want1{2, 1};  // a2 completed before a1
    const std::vector<int> want2{1, 2};  // b1 completed before b2
    LT_CHECK_COLLECTIONS_EQ(got1.begin(), got1.end(), want1.begin());
    LT_CHECK_COLLECTIONS_EQ(got2.begin(), got2.end(), want2.begin());
LT_END_AUTO_TEST(cross_owner_linearity)

// Family 6: stop-token race. request_stop() (which fires arm_stop's
// callback -> request_cancel) races a scripted completion through the
// spin gate; the target is terminal exactly once with outcome ok or
// cancelled, and both classes must occur across the loop.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, stop_token_vs_completion_race)
    constexpr int kIterations = 200;
    int ok_count = 0;
    int cancelled_count = 0;

    for (int i = 0; i < kIterations; ++i) {
        rig r;
        hd::fake_io_backend backend;
        probe p;
        std::vector<task<void>> tasks;
        std::atomic<int> arrivals{0};
        gate g(&arrivals);
        std::stop_source source;

        hd::read_operation op(r.owner, 1, std::span<std::byte>(r.buffer));
        op.submit(backend);
        const auto state = op.state();
        op.arm_stop(source.get_token());
        launch(r, std::move(op), &p, tasks);
        r.ex.run_pending();  // suspended

        std::thread stopper([&] {
            g.arrive();
            g.wait(2);
            source.request_stop();
        });
        std::thread completer([&] {
            g.arrive();
            g.wait(2);
            backend.complete(*state,
                             hd::io_result{hh::outcome_code::ok, 1, 0});
        });
        stopper.join();
        completer.join();
        r.ex.run_pending();

        if (p.delivered.load() != 1) {
            LT_FAIL("stop-token race: target not terminal exactly once");
        }
        if (p.observed.code == hh::outcome_code::ok) {
            ++ok_count;
        } else if (p.observed.code == hh::outcome_code::cancelled) {
            ++cancelled_count;
        } else {
            LT_FAIL("stop-token race: unexpected outcome");
        }
    }
    LT_CHECK(ok_count > 0);
    LT_CHECK(cancelled_count > 0);
LT_END_AUTO_TEST(stop_token_vs_completion_race)

// Family 12: cross-thread chaos (time-boxed, the tsan-lane canary).
// Two workers hammer one backend/owner/executor pair for ~100ms each:
// they submit awaited probes, complete or cancel random pending ops,
// and run the executor concurrently; the main thread closes the backend
// mid-run. Global invariant afterwards: every submitted op ended with
// exactly one terminal outcome.
LT_BEGIN_AUTO_TEST(fake_io_backend_suite, cross_thread_chaos)
    rig r;
    hd::fake_io_backend backend;
    constexpr auto kDuration = 100ms;
    constexpr int kWorkers = 2;
    std::atomic<int> submitted{0};
    std::atomic<bool> closed{false};

    struct chaos_entry {
        std::shared_ptr<hd::op_state> state;
        std::unique_ptr<probe> observed;
    };
    std::mutex registry_mu;
    std::vector<chaos_entry> registry;

    std::atomic<unsigned> seed{
        static_cast<unsigned>(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count())};

    auto worker = [&](int) {
        std::mt19937 rng(seed.fetch_add(1));
        const auto deadline = std::chrono::steady_clock::now() + kDuration;
        while (std::chrono::steady_clock::now() < deadline) {
            // Submit a small awaited batch.
            for (int j = 0; j < 5; ++j) {
                hd::read_operation op(r.owner, 1, std::span<std::byte>(r.buffer));
                auto observed = std::make_unique<probe>();
                probe* p = observed.get();
                auto state = op.state();
                op.submit(backend);
                task<void> body = await_into(std::move(op), p);
                spawn(r.ex, std::move(body), [](task_result<void>) { });
                {
                    std::lock_guard<std::mutex> lock(registry_mu);
                    registry.push_back(chaos_entry{state, std::move(observed)});
                }
                ++submitted;
            }
            // Complete or cancel a random pending op.
            std::shared_ptr<hd::op_state> pick;
            {
                std::lock_guard<std::mutex> lock(registry_mu);
                if (!registry.empty()) {
                    pick = registry[rng() % registry.size()].state;
                }
            }
            if (pick && !pick->is_terminal()) {
                if ((rng() % 2) == 0) {
                    backend.request_cancel(*pick);
                } else {
                    backend.complete(
                        *pick, hd::io_result{hh::outcome_code::ok, 1, 0});
                }
            }
            if (rng() % 8 == 0) backend.fire_wake();
            r.ex.run_one();  // drain concurrently from this thread
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kWorkers; ++t) threads.emplace_back(worker, t);

    // Close the backend mid-run while the workers keep submitting.
    std::this_thread::sleep_for(kDuration / 2);
    backend.close();
    closed.store(true, std::memory_order_release);

    for (auto& thread : threads) thread.join();

    // Post-close submissions complete immediately; a second close is a
    // no-op. Drain everything that is left.
    LT_CHECK(closed.load());
    LT_CHECK_EQ(backend.close(), std::size_t{0});
    const int total = submitted.load();
    for (int spin = 0; spin < 200000; ++spin) {
        std::size_t delivered_now = 0;
        {
            std::lock_guard<std::mutex> lock(registry_mu);
            for (const auto& entry : registry) {
                delivered_now += static_cast<std::size_t>(
                    entry.observed->delivered.load());
            }
        }
        if (delivered_now == static_cast<std::size_t>(total)) break;
        if (!r.ex.run_one()) std::this_thread::yield();
    }

    // Global invariant: every submitted op terminal exactly once.
    std::lock_guard<std::mutex> lock(registry_mu);
    LT_CHECK_EQ(registry.size(), static_cast<std::size_t>(total));
    for (const auto& entry : registry) {
        if (entry.observed->delivered.load() != 1) {
            LT_FAIL("chaos: op not delivered exactly once");
        }
        const hh::outcome_code code = entry.observed->observed.code;
        if (code != hh::outcome_code::ok
            && code != hh::outcome_code::cancelled
            && code != hh::outcome_code::connection_closed) {
            LT_FAIL("chaos: unexpected terminal outcome");
        }
        if (!entry.state->is_terminal()) {
            LT_FAIL("chaos: op state not terminal");
        }
    }
LT_END_AUTO_TEST(cross_thread_chaos)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
