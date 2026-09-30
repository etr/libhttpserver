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

// TASK-100 step 1: the shared I/O backend contract (architecture §3.4,
// DR-V3-004). The scenario functions below are written exactly once and
// instantiated for every io_backend driver: fake_io_backend (the
// known-good TASK-099 oracle, green from step 1) and the poll/WSAPoll
// driver of this task (poll_contract_suite). "One socket scenario passes
// the same backend contract on POSIX and Windows" is literal: one
// header, one fixture seam, many drivers.
//
// Driver differences are confined to backend_fixture. Shared scenarios
// call only those hooks, so a driver that changes observable behavior
// (exactly-once terminal claims, the cancel/close outcome taxonomy,
// timer ordering) fails here under real littletest CHECK reporting --
// every LT_CHECK* macro inside a scenario expands against the
// __lt_tr__ / __lt_name__ pair the enclosing LT test body forwards.
//
// All waits in this file use deadline-bounded loops that are failure
// bounds only, never pass conditions: a healthy driver settles in a few
// milliseconds, a broken one trips the bound and reports a failure.
// The only timing pass conditions in TASK-100 (monotonic idle /
// iteration bounds) live in the poll-only busy-loop scenarios.
//
// Scenario families (S1-S11 are driver-agnostic; S12-S18, the
// socket-only scenarios, live at the bottom, typed against the concrete
// poll driver):
//   S1  read delivers the stimulated bytes exactly once
//   S2  write completes with transferred == stimulated size
//   S3  timer fires at/after its deadline, never before (S3b: two
//       timers expire earliest-deadline first, sequence breaks ties)
//   S4  wake completes all pending wakes exactly once, never reads
//   S5  cancel op on a pending target: target cancelled, cancel op ok
//   S6  cancel op on a terminal target: cancel op invalid_state
//   S7  close() sweeps every pending op connection_closed exactly once,
//       double close is a no-op
//   S8  submit after close completes immediately connection_closed
//   S9  late request_cancel on a terminal op reports invalid_state
//   S10 N coroutine awaiters resume exactly once
//   S11 cancel-vs-stimulus race: exactly one terminal per iteration,
//       both outcome classes observed across the loop

#ifndef TEST_UNIT_IO_BACKEND_CONTRACT_HPP_
#define TEST_UNIT_IO_BACKEND_CONTRACT_HPP_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "./httpserver/concurrency/task.hpp"
#include "./httpserver/detail/fake_io_backend.hpp"
#include "./httpserver/detail/io_connection_owner.hpp"
#include "./httpserver/detail/io_operation.hpp"
#include "./httpserver/detail/io_poll_backend.hpp"

#include "./io_loopback.hpp"

#include "./littletest.hpp"

using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;

namespace hd = httpserver::detail;
namespace hh = httpserver::http;
namespace pollsys = httpserver::detail::pollsys;

using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""us;
using std::chrono_literals::operator""s;

namespace io_contract {

// How long any single wait may run before the scenario reports a
// failure. Generous on purpose: it bounds a broken driver, it is never
// a timing pass condition.
constexpr std::chrono::milliseconds kWaitBudget{5000};

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

// Per-operation observation: the terminal result the awaiting task saw
// and how many times it was delivered (more than one is a failure).
struct probe {
    hd::io_result observed{};
    std::atomic<int> delivered{0};
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

// Executor + owner + buffers for one deterministic scenario (the same
// shape fake_io_backend_test.cpp uses). Lives INSIDE the fixture as the
// base-class member on purpose: derived fixtures construct rig first
// and their backend last, so the backend is destroyed before the rig --
// the documented teardown order (backend close -> owner destruction).
// A thread-backed driver outliving the executor would complete pending
// ops into a destroyed owner.
struct contract_rig {
    contract_rig() : owner(ex) { }

    manual_executor ex;
    hd::io_connection_owner owner;
    std::byte buffer[16]{};
    const std::byte cbuffer[16]{};
};

// The driver differences, confined to one seam. Scenarios instantiate
// one concrete fixture per driver and never touch the driver directly.
struct backend_fixture {
    virtual ~backend_fixture() = default;

    contract_rig rig;

    virtual hd::io_backend& backend() = 0;

    // Advances driver-owned time. fake: expires due timers against the
    // wall clock; poll: no-op (the driver thread owns time).
    virtual void pump() = 0;

    // Stimulates a pending read op. fake: scripted completion carrying
    // the byte count; poll: writes the bytes to the connection's peer
    // socket and lets the driver read them.
    virtual void deliver_read(hd::op_state& state,
                              std::string_view bytes) = 0;

    // Stimulates a pending write op. fake: scripted completion with the
    // given transferred count; poll: no-op, real writability decides.
    virtual void deliver_write(hd::op_state& state,
                               std::size_t transferred) = 0;

    virtual void fire_wake() = 0;
    virtual std::size_t close_backend() = 0;
    virtual std::size_t pending_count() = 0;
};

// Deadline-bounded wait for one delivery. Returns true as soon as the
// probe has been delivered; false is a scenario failure.
inline bool wait_terminal(contract_rig& r, backend_fixture& fx, probe& p,
                          std::chrono::steady_clock::time_point give_up) {
    while (std::chrono::steady_clock::now() < give_up) {
        fx.pump();
        r.ex.run_pending();
        if (p.delivered.load(std::memory_order_acquire) != 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

inline bool wait_terminal(contract_rig& r, backend_fixture& fx, probe& p) {
    return wait_terminal(r, fx, p, std::chrono::steady_clock::now()
                                       + kWaitBudget);
}

// Fixture-less wait for the poll-only scenarios: the driver thread owns
// time, so there is nothing to pump.
inline bool wait_terminal(contract_rig& r, probe& p,
                          std::chrono::steady_clock::time_point give_up) {
    while (std::chrono::steady_clock::now() < give_up) {
        r.ex.run_pending();
        if (p.delivered.load(std::memory_order_acquire) != 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

inline bool wait_terminal(contract_rig& r, probe& p) {
    return wait_terminal(r, p, std::chrono::steady_clock::now()
                                   + kWaitBudget);
}

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
void launch_probe(contract_rig& r, Op op, probe* p,
                  std::vector<task<void>>& sink) {
    sink.push_back(await_into(std::move(op), p));
    spawn(r.ex, std::move(sink.back()), [](task_result<void>) { });
}

template<typename Op>
void launch_logged(contract_rig& r, Op op, order_log* log, int id,
                   std::vector<task<void>>& sink) {
    sink.push_back(await_and_log(std::move(op), log, id));
    spawn(r.ex, std::move(sink.back()), [](task_result<void>) { });
}

// Shared ownership note: a spawned task self-destroys its frame on
// completion, and the frame held the op's only op_state reference.
// Scenarios must therefore snapshot op.state() as a shared_ptr (never a
// bare reference) whenever the state is touched after a wait.

// S1: a pending read op delivers the stimulated bytes exactly once.
template <typename FX>
void read_delivers_bytes_exactly_once(littletest::test_runner* __lt_tr__,
                                      const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    hd::read_operation op(r.owner, 1, std::span<std::byte>(r.buffer));
    op.submit(fx.backend());
    std::shared_ptr<hd::op_state> state = op.state();
    probe p;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(op), &p, tasks);
    r.ex.run_pending();

    fx.deliver_read(*state, "hello");
    LT_CHECK(wait_terminal(r, fx, p));

    LT_CHECK_EQ(p.delivered.load(), 1);
    LT_CHECK(p.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(p.observed.transferred, std::size_t{5});
    LT_CHECK_EQ(fx.pending_count(), std::size_t{0});
}

// S2: a pending write op completes with transferred == stimulated size.
template <typename FX>
void write_completes_with_transferred(littletest::test_runner* __lt_tr__,
                                      const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    hd::write_operation op(r.owner, 1,
                           std::span<const std::byte>(r.cbuffer));
    op.submit(fx.backend());
    std::shared_ptr<hd::op_state> state = op.state();
    probe p;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(op), &p, tasks);
    r.ex.run_pending();

    fx.deliver_write(*state, sizeof(r.cbuffer));
    LT_CHECK(wait_terminal(r, fx, p));

    LT_CHECK_EQ(p.delivered.load(), 1);
    LT_CHECK(p.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(p.observed.transferred, std::size_t{16});
    LT_CHECK_EQ(fx.pending_count(), std::size_t{0});
}

// S3: a timer fires at/after its deadline and never before.
template <typename FX>
void timer_fires_at_deadline_not_before(littletest::test_runner* __lt_tr__,
                                        const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    const auto start = std::chrono::steady_clock::now();
    hd::timer_operation op(r.owner, 1, start + 120ms);
    op.submit(fx.backend());
    probe p;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(op), &p, tasks);
    r.ex.run_pending();

    fx.pump();
    r.ex.run_pending();
    LT_CHECK_EQ(p.delivered.load(), 0);  // not before the deadline
    LT_CHECK(wait_terminal(r, fx, p, start + kWaitBudget));

    LT_CHECK_EQ(p.delivered.load(), 1);
    LT_CHECK(p.observed.code == hh::outcome_code::ok);
    LT_CHECK(std::chrono::steady_clock::now() - start >= 100ms);
}

// S3b: two timers expire earliest-deadline first; sequence breaks ties.
template <typename FX>
void two_timers_earliest_first_sequence_tie(
    littletest::test_runner* __lt_tr__, const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    order_log log;
    const auto t0 = std::chrono::steady_clock::now();
    hd::timer_operation t1(r.owner, 1, t0 + 150ms);  // seq 1
    hd::timer_operation t2(r.owner, 1, t0 + 80ms);   // seq 2
    hd::timer_operation t3(r.owner, 1, t0 + 80ms);   // seq 3
    t1.submit(fx.backend());
    t2.submit(fx.backend());
    t3.submit(fx.backend());
    std::vector<task<void>> tasks;
    launch_logged(r, std::move(t1), &log, 1, tasks);
    launch_logged(r, std::move(t2), &log, 2, tasks);
    launch_logged(r, std::move(t3), &log, 3, tasks);
    r.ex.run_pending();

    const auto give_up = t0 + kWaitBudget;
    while (log.snapshot().size() < 3
           && std::chrono::steady_clock::now() < give_up) {
        fx.pump();
        r.ex.run_pending();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const std::vector<int> got = log.snapshot();
    const std::vector<int> want{2, 3, 1};
    LT_CHECK_EQ(got.size(), std::size_t{3});  // each fired exactly once
    LT_CHECK_COLLECTIONS_EQ(got.begin(), got.end(), want.begin());
}

// S4: wake completes every pending wake exactly once, never reads.
template <typename FX>
void wake_completes_all_wakes_once(littletest::test_runner* __lt_tr__,
                                   const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    hd::wake_operation w1(r.owner, 1);
    hd::wake_operation w2(r.owner, 1);
    hd::read_operation read_op(r.owner, 1, std::span<std::byte>(r.buffer));
    w1.submit(fx.backend());
    w2.submit(fx.backend());
    read_op.submit(fx.backend());
    probe p1;
    probe p2;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(w1), &p1, tasks);
    launch_probe(r, std::move(w2), &p2, tasks);
    r.ex.run_pending();

    fx.fire_wake();
    LT_CHECK(wait_terminal(r, fx, p1));
    LT_CHECK(wait_terminal(r, fx, p2));

    LT_CHECK_EQ(p1.delivered.load(), 1);
    LT_CHECK_EQ(p2.delivered.load(), 1);
    LT_CHECK(p1.observed.code == hh::outcome_code::ok);
    LT_CHECK(p2.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(fx.pending_count(), std::size_t{1});  // the read survives
}

// S5: a cancel op whose target is pending delivers the target
// cancelled once and reports ok once.
template <typename FX>
void cancel_pending_target(littletest::test_runner* __lt_tr__,
                           const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    hd::read_operation target(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::cancel_operation cancel_op(r.owner, 1, target);
    target.submit(fx.backend());
    cancel_op.submit(fx.backend());
    probe p_target;
    probe p_cancel;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(target), &p_target, tasks);
    launch_probe(r, std::move(cancel_op), &p_cancel, tasks);
    r.ex.run_pending();

    LT_CHECK(wait_terminal(r, fx, p_target));
    LT_CHECK(wait_terminal(r, fx, p_cancel));

    LT_CHECK_EQ(p_target.delivered.load(), 1);
    LT_CHECK(p_target.observed.code == hh::outcome_code::cancelled);
    LT_CHECK_EQ(p_cancel.delivered.load(), 1);
    LT_CHECK(p_cancel.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(fx.pending_count(), std::size_t{0});
}

// S6: a cancel op whose target already reached a terminal result
// reports invalid_state; the target keeps its original result.
template <typename FX>
void cancel_terminal_target_reports_invalid_state(
    littletest::test_runner* __lt_tr__, const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    hd::read_operation target(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::cancel_operation cancel_op(r.owner, 1, target);
    target.submit(fx.backend());
    std::shared_ptr<hd::op_state> target_state = target.state();
    probe p_target;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(target), &p_target, tasks);
    r.ex.run_pending();

    fx.deliver_read(*target_state, "abc");
    LT_CHECK(wait_terminal(r, fx, p_target));
    LT_CHECK_EQ(p_target.observed.transferred, std::size_t{3});

    cancel_op.submit(fx.backend());  // resolves at submit time
    probe p_cancel;
    launch_probe(r, std::move(cancel_op), &p_cancel, tasks);
    r.ex.run_pending();

    LT_CHECK_EQ(p_cancel.delivered.load(), 1);
    LT_CHECK(p_cancel.observed.code == hh::outcome_code::invalid_state);
    LT_CHECK_EQ(p_target.delivered.load(), 1);
    LT_CHECK_EQ(p_target.observed.transferred, std::size_t{3});
}

// S7: close() sweeps every pending op connection_closed exactly once;
// a double close is a no-op. The write op is completed through the
// harness first: a thread-backed driver completes a write on a fresh
// socket immediately, so the sweep also proves a terminal history is
// not re-completed.
template <typename FX>
void close_sweeps_every_pending_once(littletest::test_runner* __lt_tr__,
                                     const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    const auto far_deadline = std::chrono::steady_clock::now()
                              + std::chrono::hours(1);
    hd::accept_operation accept_op(r.owner, 1);
    hd::read_operation read_op(r.owner, 1, std::span<std::byte>(r.buffer));
    hd::write_operation write_op(r.owner, 1,
                                 std::span<const std::byte>(r.cbuffer));
    hd::timer_operation timer_op(r.owner, 1, far_deadline);
    hd::wake_operation wake_op(r.owner, 1);
    accept_op.submit(fx.backend());
    read_op.submit(fx.backend());
    write_op.submit(fx.backend());
    timer_op.submit(fx.backend());
    wake_op.submit(fx.backend());
    std::shared_ptr<hd::op_state> write_state = write_op.state();
    probe p_accept;
    probe p_read;
    probe p_write;
    probe p_timer;
    probe p_wake;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(accept_op), &p_accept, tasks);
    launch_probe(r, std::move(read_op), &p_read, tasks);
    launch_probe(r, std::move(write_op), &p_write, tasks);
    launch_probe(r, std::move(timer_op), &p_timer, tasks);
    launch_probe(r, std::move(wake_op), &p_wake, tasks);
    r.ex.run_pending();

    fx.deliver_write(*write_state, sizeof(r.cbuffer));
    LT_CHECK(wait_terminal(r, fx, p_write));
    LT_CHECK(p_write.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(p_write.observed.transferred, std::size_t{16});

    LT_CHECK_EQ(fx.pending_count(), std::size_t{4});
    LT_CHECK_EQ(fx.close_backend(), std::size_t{4});
    LT_CHECK(wait_terminal(r, fx, p_accept));
    LT_CHECK(wait_terminal(r, fx, p_read));
    LT_CHECK(wait_terminal(r, fx, p_timer));
    LT_CHECK(wait_terminal(r, fx, p_wake));

    LT_CHECK_EQ(p_accept.delivered.load(), 1);
    LT_CHECK(p_accept.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(p_read.delivered.load(), 1);
    LT_CHECK(p_read.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(p_timer.delivered.load(), 1);
    LT_CHECK(p_timer.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(p_wake.delivered.load(), 1);
    LT_CHECK(p_wake.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(p_write.delivered.load(), 1);  // still exactly once

    LT_CHECK_EQ(fx.close_backend(), std::size_t{0});  // double close
    LT_CHECK_EQ(fx.pending_count(), std::size_t{0});
    r.ex.run_pending();
    LT_CHECK_EQ(p_write.delivered.load(), 1);
}

// S8: a submit after close completes immediately with
// connection_closed -- no silent drops.
template <typename FX>
void submit_after_close_connection_closed(
    littletest::test_runner* __lt_tr__, const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    LT_CHECK_EQ(fx.close_backend(), std::size_t{0});

    hd::read_operation op(r.owner, 1, std::span<std::byte>(r.buffer));
    op.submit(fx.backend());  // completes inline with connection_closed
    probe p;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(op), &p, tasks);
    r.ex.run_pending();

    LT_CHECK_EQ(p.delivered.load(), 1);
    LT_CHECK(p.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(fx.pending_count(), std::size_t{0});
}

// S9: request_cancel on an already-terminal op reports invalid_state
// and leaves the original result in place.
template <typename FX>
void late_request_cancel_reports_invalid_state(
    littletest::test_runner* __lt_tr__, const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    contract_rig& r = fx.rig;
    hd::read_operation op(r.owner, 1, std::span<std::byte>(r.buffer));
    op.submit(fx.backend());
    std::shared_ptr<hd::op_state> state = op.state();
    probe p;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(op), &p, tasks);
    r.ex.run_pending();

    fx.deliver_read(*state, "ok!");
    LT_CHECK(wait_terminal(r, fx, p));
    LT_CHECK_EQ(p.observed.transferred, std::size_t{3});

    LT_CHECK(fx.backend().request_cancel(*state)
             == hh::outcome_code::invalid_state);
    LT_CHECK_EQ(p.delivered.load(), 1);
    LT_CHECK_EQ(p.observed.transferred, std::size_t{3});
}

// S10: N coroutine awaiters resume exactly once.
template <typename FX>
void n_awaiter_resume_exactly_once(littletest::test_runner* __lt_tr__,
                                   const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    constexpr int kTasks = 20;
    contract_rig& r = fx.rig;
    std::vector<hd::read_operation> ops;
    // Shared ownership: completed spawned tasks self-destroy their
    // frames, so bare op_state pointers alone would dangle here.
    std::vector<std::shared_ptr<hd::op_state>> states;
    std::vector<probe> probes(static_cast<std::size_t>(kTasks));
    std::vector<task<void>> tasks;
    ops.reserve(static_cast<std::size_t>(kTasks));
    for (int i = 0; i < kTasks; ++i) {
        ops.emplace_back(r.owner, 1, std::span<std::byte>(r.buffer));
        ops.back().submit(fx.backend());
        states.push_back(ops.back().state());
    }
    for (int i = 0; i < kTasks; ++i) {
        launch_probe(r, std::move(ops[static_cast<std::size_t>(i)]),
                     &probes[static_cast<std::size_t>(i)], tasks);
    }
    r.ex.run_pending();

    // One stimulated delivery per op, in submission order; the driver
    // must complete exactly the addressed op each time.
    for (int i = 0; i < kTasks; ++i) {
        fx.deliver_read(*states[static_cast<std::size_t>(i)], "x");
        LT_CHECK(wait_terminal(
            r, fx, probes[static_cast<std::size_t>(i)]));
    }
    for (int i = 0; i < kTasks; ++i) {
        const probe& p = probes[static_cast<std::size_t>(i)];
        LT_CHECK_EQ(p.delivered.load(), 1);
        LT_CHECK(p.observed.code == hh::outcome_code::ok);
        LT_CHECK_EQ(p.observed.transferred, std::size_t{1});
    }
    LT_CHECK_EQ(fx.pending_count(), std::size_t{0});
}

// S11: cancel-vs-stimulus race. Both contenders start on a spin gate;
// per iteration the target is terminal exactly once with outcome ok or
// cancelled; both classes must occur across the loop. The alternating
// bias keeps both classes alive on any scheduler.
template <typename FX>
void cancel_vs_stimulus_race(littletest::test_runner* __lt_tr__,
                             const char* __lt_name__, FX& fx) {
    (void)__lt_name__;
    constexpr int kIterations = 100;
    int ok_count = 0;
    int cancelled_count = 0;

    contract_rig& r = fx.rig;
    for (int i = 0; i < kIterations; ++i) {
        hd::read_operation op(r.owner, 1, std::span<std::byte>(r.buffer));
        op.submit(fx.backend());
        // The stimulus/cancel threads may complete the op (destroying
        // its frame) while the other thread still addresses it: hold
        // shared ownership for the lifetime of both threads.
        std::shared_ptr<hd::op_state> state = op.state();
        probe p;
        std::vector<task<void>> tasks;
        launch_probe(r, std::move(op), &p, tasks);
        r.ex.run_pending();  // suspended

        std::atomic<int> arrivals{0};
        gate g(&arrivals);
        const auto loser_delay = std::chrono::microseconds(50);
        std::thread stimulator([&state, &fx, i, &g, loser_delay] {
            g.arrive();
            g.wait(2);
            if (i % 2 == 1) std::this_thread::sleep_for(loser_delay);
            fx.deliver_read(*state, "x");
        });
        std::thread canceler([&state, &fx, i, &g, loser_delay] {
            g.arrive();
            g.wait(2);
            if (i % 2 == 0) std::this_thread::sleep_for(loser_delay);
            fx.backend().request_cancel(*state);
        });
        stimulator.join();
        canceler.join();
        LT_CHECK(wait_terminal(r, fx, p));

        if (p.delivered.load() != 1) {
            LT_FAIL("cancel/stimulus race: not terminal exactly once");
        }
        if (p.observed.code == hh::outcome_code::ok) {
            ++ok_count;
        } else if (p.observed.code == hh::outcome_code::cancelled) {
            ++cancelled_count;
        } else {
            LT_FAIL("cancel/stimulus race: unexpected outcome");
        }
    }
    LT_CHECK(ok_count > 0);
    LT_CHECK(cancelled_count > 0);
}

// ---- poll-only scenarios (S12-S18) ----------------------------------------
//
// These pin the socket behavior the scripted fixture cannot express:
// accept, byte-exact HTTP/1-shaped round trips, partial reads, hangups,
// and the no-busy-loop bounds. They type against the concrete driver,
// not the seam. Member order keeps the documented teardown: the backend
// is destroyed before the executor/owner rig it enqueues into.

struct poll_rig {
    contract_rig rig;
    hd::io_poll_backend backend;

    // Adopts a fresh loopback pair under @p id; the peer end stays with
    // the returned pair, the adopted end's handle moves to the backend
    // (detach: exactly one close, via release_connection / dtor).
    io_loopback::pair adopt_pair(std::uint64_t id) {
        io_loopback::pair conn = io_loopback::pair::make();
        backend.adopt_connection(id, conn.local());
        (void)conn.detach_local();
        return conn;
    }

    // Adopts a fresh loopback listener under @p id; the handle moves to
    // the backend, the port stays on the returned listener.
    io_loopback::listener adopt_listener(std::uint64_t id) {
        io_loopback::listener l = io_loopback::listener::open();
        backend.adopt_listener(id, l.socket());
        l.detach();
        return l;
    }
};

// Byte-span helper for wire-shaped literals.
inline std::span<const std::byte> as_bytes(const char* text) noexcept {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text),
        std::char_traits<char>::length(text));
}

inline bool bytes_equal(const std::byte* lhs, const std::byte* rhs,
                        std::size_t n) {
    return std::memcmp(lhs, rhs, n) == 0;
}

// S12: accept round trip. A listener adopted under id 2 accepts one
// connection; the fresh fabricated id carries a valid handle; read and
// write ops work over it in both directions.
inline void accept_round_trip(littletest::test_runner* __lt_tr__,
                              const char* __lt_name__, poll_rig& rig) {
    (void)__lt_name__;
    contract_rig& r = rig.rig;
    hd::io_poll_backend& backend = rig.backend;
    const io_loopback::listener listener = rig.adopt_listener(2);
    LT_CHECK(listener.port() != 0);

    hd::accept_operation accept_op(r.owner, 2);
    accept_op.submit(backend);
    probe p_accept;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(accept_op), &p_accept, tasks);
    r.ex.run_pending();

    pollsys::native_socket_t client = io_loopback::connect_to(
        listener.port());
    LT_CHECK(client != pollsys::k_invalid_socket);
    LT_CHECK(wait_terminal(r, p_accept));
    LT_CHECK_EQ(p_accept.delivered.load(), 1);
    LT_CHECK(p_accept.observed.code == hh::outcome_code::ok);
    const std::uint64_t accepted_id = p_accept.observed.accepted_id;
    LT_CHECK(accepted_id != 0);
    LT_CHECK(accepted_id != 2);
    LT_CHECK(backend.native_handle(accepted_id)
             != pollsys::k_invalid_socket);

    // Write op over the accepted connection; the client reads it.
    hd::write_operation resp_op(r.owner, accepted_id,
                                std::span<const std::byte>(r.cbuffer));
    resp_op.submit(backend);
    probe p_write;
    launch_probe(r, std::move(resp_op), &p_write, tasks);
    r.ex.run_pending();
    LT_CHECK(wait_terminal(r, p_write));
    LT_CHECK_EQ(p_write.observed.transferred, std::size_t{16});
    std::byte seen[16]{};
    LT_CHECK(io_loopback::read_exact(client, seen, sizeof(seen)));

    // Read op over the accepted connection; the client writes.
    hd::read_operation req_op(r.owner, accepted_id,
                              std::span<std::byte>(r.buffer));
    req_op.submit(backend);
    probe p_read;
    launch_probe(r, std::move(req_op), &p_read, tasks);
    r.ex.run_pending();
    io_loopback::write_all(client, "abcd", 4);
    LT_CHECK(wait_terminal(r, p_read));
    LT_CHECK_EQ(p_read.observed.transferred, std::size_t{4});

    pollsys::close_socket(client);
    backend.release_connection(accepted_id);
    backend.release_connection(2);
    LT_CHECK_EQ(backend.pending_count(), std::size_t{0});
}

// S13: the acceptance-criteria scenario. One HTTP/1-shaped round trip:
// a write op pushes a response head while a read op receives a GET
// request -- byte-exact in both directions over one loopback pair.
inline void http1_round_trip(littletest::test_runner* __lt_tr__,
                             const char* __lt_name__, poll_rig& rig) {
    (void)__lt_name__;
    contract_rig& r = rig.rig;
    const io_loopback::pair conn = rig.adopt_pair(1);

    static constexpr char kRequest[] =
        "GET /index.html HTTP/1.1\r\nHost: example\r\n\r\n";
    static constexpr char kResponse[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
    std::byte request_in[256]{};

    hd::write_operation resp_op(r.owner, 1, as_bytes(kResponse));
    hd::read_operation req_op(r.owner, 1,
                              std::span<std::byte>(request_in));
    resp_op.submit(rig.backend);
    req_op.submit(rig.backend);
    probe p_write;
    probe p_read;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(resp_op), &p_write, tasks);
    launch_probe(r, std::move(req_op), &p_read, tasks);
    r.ex.run_pending();

    io_loopback::write_all(conn.peer(), kRequest, sizeof(kRequest) - 1);
    LT_CHECK(wait_terminal(r, p_read));
    LT_CHECK(wait_terminal(r, p_write));

    LT_CHECK_EQ(p_read.delivered.load(), 1);
    LT_CHECK(p_read.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(p_read.observed.transferred, sizeof(kRequest) - 1);
    LT_CHECK(bytes_equal(request_in, as_bytes(kRequest).data(),
                         sizeof(kRequest) - 1));

    LT_CHECK_EQ(p_write.delivered.load(), 1);
    LT_CHECK(p_write.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(p_write.observed.transferred, sizeof(kResponse) - 1);
    std::byte response_in[64]{};
    LT_CHECK(io_loopback::read_exact(conn.peer(), response_in,
                                     sizeof(kResponse) - 1));
    LT_CHECK(bytes_equal(response_in, as_bytes(kResponse).data(),
                         sizeof(kResponse) - 1));

    rig.backend.release_connection(1);
}

// S14: partial read. A peer sending more than the op buffer completes
// the op with transferred == buffer size; the next op receives the
// remainder, byte-exact.
inline void partial_read(littletest::test_runner* __lt_tr__,
                         const char* __lt_name__, poll_rig& rig) {
    (void)__lt_name__;
    contract_rig& r = rig.rig;
    const io_loopback::pair conn = rig.adopt_pair(1);
    static constexpr char kPayload[] = "0123456789ABCDEFGHIJ";  // 20 bytes

    std::byte first[16]{};
    std::byte second[16]{};
    hd::read_operation first_op(r.owner, 1, std::span<std::byte>(first));
    hd::read_operation second_op(r.owner, 1, std::span<std::byte>(second));
    first_op.submit(rig.backend);
    second_op.submit(rig.backend);
    probe p_first;
    probe p_second;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(first_op), &p_first, tasks);
    launch_probe(r, std::move(second_op), &p_second, tasks);
    r.ex.run_pending();

    io_loopback::write_all(conn.peer(), kPayload, sizeof(kPayload) - 1);
    LT_CHECK(wait_terminal(r, p_first));
    LT_CHECK(wait_terminal(r, p_second));

    LT_CHECK_EQ(p_first.delivered.load(), 1);
    LT_CHECK_EQ(p_first.observed.transferred, std::size_t{16});
    LT_CHECK(bytes_equal(first, as_bytes(kPayload).data(), 16));
    LT_CHECK_EQ(p_second.delivered.load(), 1);
    LT_CHECK_EQ(p_second.observed.transferred, std::size_t{4});
    LT_CHECK(bytes_equal(second, as_bytes(kPayload).data() + 16, 4));

    rig.backend.release_connection(1);
}

// S15: read hangup. The peer closes while a read is pending: the read
// completes connection_closed exactly once and nothing stays pending.
inline void read_hangup(littletest::test_runner* __lt_tr__,
                        const char* __lt_name__, poll_rig& rig) {
    (void)__lt_name__;
    contract_rig& r = rig.rig;
    io_loopback::pair conn = rig.adopt_pair(1);

    hd::read_operation read_op(r.owner, 1, std::span<std::byte>(r.buffer));
    read_op.submit(rig.backend);
    probe p_read;
    std::vector<task<void>> tasks;
    launch_probe(r, std::move(read_op), &p_read, tasks);
    r.ex.run_pending();

    conn.close_peer();  // hangup while the read is pending
    LT_CHECK(wait_terminal(r, p_read));

    LT_CHECK_EQ(p_read.delivered.load(), 1);
    LT_CHECK(p_read.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(rig.backend.pending_count(), std::size_t{0});

    rig.backend.release_connection(1);
}

}  // namespace io_contract

#endif  // TEST_UNIT_IO_BACKEND_CONTRACT_HPP_
