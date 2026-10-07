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

// TASK-100 step 1: the shared backend contract run over the known-good
// fake driver. fake_contract_suite being green proves the harness
// itself is sound before any new driver code exists (harness TDD):
// the identical scenario functions instantiate against the poll /
// WSAPoll driver in poll_contract_suite (steps 2+), which is how one
// socket scenario passes "the same backend contract" on POSIX and
// Windows.
//
// deadline_conversion_suite (step 2) pins the monotonic deadline ->
// poll-timeout conversion as a pure unit suite.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "./httpserver/detail/drain_scope.hpp"
#include "./httpserver/detail/fake_io_backend.hpp"
#include "./httpserver/detail/io_poll_backend.hpp"
#include "./io_backend_contract.hpp"
#include "./io_loopback.hpp"

#include "./littletest.hpp"

namespace {

namespace hd = httpserver::detail;
namespace hh = httpserver::http;

// Scripted fixture: every stimulus is a direct fake_io_backend
// completion carrying the scenario's expected shape.
struct fake_fixture final : io_contract::backend_fixture {
    hd::io_backend& backend() override { return instance; }

    void pump() override {
        instance.expire_timers(std::chrono::steady_clock::now());
    }

    void deliver_read(hd::op_state& state,
                      std::string_view bytes) override {
        instance.complete(state, hd::io_result{hh::outcome_code::ok,
                                               bytes.size(), 0});
    }

    void deliver_write(hd::op_state& state,
                       std::size_t transferred) override {
        instance.complete(state, hd::io_result{hh::outcome_code::ok,
                                               transferred, 0});
    }

    void fire_wake() override { instance.fire_wake(); }

    std::size_t close_backend() override { return instance.close(); }

    std::size_t pending_count() override {
        return instance.pending_count();
    }

    hd::fake_io_backend instance;
};

// Real-driver fixture for poll_contract_suite: connection id 1 is a
// live loopback pair adopted by the io_poll_backend, deliver_read
// pushes bytes through the peer socket, and timers/wakes run on the
// driver's own thread (pump is a no-op -- the loop owns time).
struct poll_fixture final : io_contract::backend_fixture {
    poll_fixture() {
        pair_ = io_loopback::pair::make();
        instance.adopt_connection(1, pair_.local());
        (void)pair_.detach_local();  // backend owns the adopted end
    }

    hd::io_backend& backend() override { return instance; }

    void pump() override { }

    void deliver_read(hd::op_state& state,
                      std::string_view bytes) override {
        (void)state;
        io_loopback::write_all(pair_.peer(), bytes.data(), bytes.size());
    }

    // No scripted write stimulus: real writability decides, so the
    // write op completes through the driver alone.
    void deliver_write(hd::op_state& state,
                       std::size_t transferred) override {
        (void)state;
        (void)transferred;
    }

    void fire_wake() override { instance.wake(); }

    std::size_t close_backend() override { return instance.close(); }

    std::size_t pending_count() override {
        return instance.pending_count();
    }

    hd::io_poll_backend instance;
    io_loopback::pair pair_;
};

// Test-only member pointers control the otherwise uninjectable interval
// between a real socket step returning would-block and registry rearm.
// Explicit instantiation grants access without changing headers, class
// definitions, compiler flags or the linked production implementation.
template<typename Tag, typename Tag::type Pointer>
struct poll_member {
    friend typename Tag::type member(Tag) { return Pointer; }
};

template<typename Pointer, int Index = 0>
struct poll_tag {
    using type = Pointer;
    friend type member(poll_tag);
};

using poll_stop = poll_tag<std::atomic_bool hd::io_poll_backend::*>;
using poll_thread = poll_tag<std::thread hd::io_poll_backend::*>;
using poll_wake = poll_tag<hd::pollsys::wake_source hd::io_poll_backend::*>;
using poll_mutex = poll_tag<std::mutex hd::io_poll_backend::*>;
using op_batch = std::vector<std::shared_ptr<hd::op_state>>;
using poll_detach = poll_tag<void (hd::io_poll_backend::*)(
    std::uint64_t, bool, op_batch&)>;
using poll_rearm = poll_tag<void (hd::io_poll_backend::*)(
    const op_batch&, std::size_t)>;
using poll_read = poll_tag<hd::step_outcome (hd::io_poll_backend::*)(
    hd::pollsys::native_socket_t, const std::shared_ptr<hd::op_state>&)>;
using poll_accept = poll_tag<poll_read::type, 1>;
using poll_project = poll_tag<std::optional<std::chrono::steady_clock::time_point> (hd::io_poll_backend::*)(
        std::vector<hd::pollsys::poll_slot>&, std::vector<std::uint64_t>&)>;

template struct poll_member<poll_stop, &hd::io_poll_backend::stop_>;
template struct poll_member<poll_thread, &hd::io_poll_backend::thread_>;
template struct poll_member<poll_wake, &hd::io_poll_backend::wake_>;
template struct poll_member<poll_mutex, &hd::io_poll_backend::mu_>;
template struct poll_member<poll_detach, &hd::io_poll_backend::take_direction_locked>;
template struct poll_member<poll_rearm, &hd::io_poll_backend::rearm_after_would_block>;
template struct poll_member<poll_read, &hd::io_poll_backend::read_step>;
template struct poll_member<poll_accept, &hd::io_poll_backend::accept_step>;
template struct poll_member<poll_project, &hd::io_poll_backend::build_projection>;

struct controlled_poll_fixture {
    controlled_poll_fixture() {
        // Stop scheduling only: leave the real backend and sockets open.
        (instance.*member(poll_stop{})).store(true, std::memory_order_release);
        (instance.*member(poll_wake{})).signal();
        (instance.*member(poll_thread{})).join();
    }

    op_batch detach(std::uint64_t id) {
        op_batch batch;
        std::lock_guard<std::mutex> lock(instance.*member(poll_mutex{}));
        (instance.*member(poll_detach{}))(id, true, batch);
        return batch;
    }

    void prune() {
        std::vector<hd::pollsys::poll_slot> fds;
        std::vector<std::uint64_t> ids;
        (instance.*member(poll_project{}))(fds, ids);
    }

    io_contract::contract_rig rig;
    hd::drain_scope scope;
    hd::io_poll_backend instance;
};

template<typename Op>
task<void> counted_probe(Op op, io_contract::probe* observed,
                         hd::drain_scope* scope) {
    hd::drain_scope::unit counted(*scope);
    observed->observed = co_await std::move(op);
    ++observed->delivered;
}

template<typename Op>
void release_detached_would_block(littletest::test_runner* __lt_tr__,
                                  const char* __lt_name__,
                                  controlled_poll_fixture& fx, Op op,
                                  bool prune) {
    io_contract::probe observed;
    op.submit(fx.instance);
    spawn(fx.rig.ex, counted_probe(std::move(op), &observed, &fx.scope),
          [](task_result<void>) { });
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(fx.scope.active(), std::size_t{1});
    auto batch = fx.detach(1);
    LT_CHECK_EQ(batch.size(), std::size_t{1});
    LT_CHECK_EQ(fx.instance.pending_count(), std::size_t{0});
    const auto step = batch.front()->kind() == hd::io_op_kind::accept
        ? member(poll_accept{}) : member(poll_read{});
    LT_CHECK((fx.instance.*step)(fx.instance.native_handle(1), batch.front())
              == hd::step_outcome::pending_again);
    LT_CHECK_EQ(observed.delivered.load(), 0);

    // This is the deterministic gate: the operation has actually reached
    // would-block and is absent from the pending registry during release.
    fx.instance.release_connection(1);
    if (prune) fx.prune();
    (fx.instance.*member(poll_rearm{}))(batch, 0);
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(observed.delivered.load(), 1);
    LT_CHECK(observed.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(fx.instance.pending_count(), std::size_t{0});
    LT_CHECK_EQ(fx.scope.active(), std::size_t{0});

    // Duplicate stale dispatch must not deliver twice. Cleanup happens
    // only after the release-only success conditions have been checked;
    // it also safely unwinds the deliberately failing pre-fix case.
    (fx.instance.*member(poll_rearm{}))(batch, 0);
    fx.instance.close();
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(observed.delivered.load(), 1);
    LT_CHECK_EQ(fx.scope.active(), std::size_t{0});
}

void released_read_case(littletest::test_runner* __lt_tr__,
                        const char* __lt_name__, bool prune) {
    controlled_poll_fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    fx.instance.adopt_connection(1, pair.detach_local());
    hd::read_operation op(fx.rig.owner, 1, fx.rig.buffer);
    release_detached_would_block(__lt_tr__, __lt_name__, fx, std::move(op),
                                 prune);
}

void released_accept_case(littletest::test_runner* __lt_tr__,
                          const char* __lt_name__, bool prune) {
    controlled_poll_fixture fx;
    auto listener = io_loopback::listener::open();
    LT_ASSERT(listener.ok());
    fx.instance.adopt_listener(1, listener.socket());
    listener.detach();
    hd::accept_operation op(fx.rig.owner, 1);
    release_detached_would_block(__lt_tr__, __lt_name__, fx, std::move(op),
                                 prune);
}

}  // namespace

LT_BEGIN_SUITE(fake_contract_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(fake_contract_suite)

LT_BEGIN_AUTO_TEST(fake_contract_suite, read_delivers_bytes_exactly_once)
    fake_fixture fx;
    io_contract::read_delivers_bytes_exactly_once(__lt_tr__, __lt_name__,
                                                  fx);
LT_END_AUTO_TEST(read_delivers_bytes_exactly_once)

LT_BEGIN_AUTO_TEST(fake_contract_suite, write_completes_with_transferred)
    fake_fixture fx;
    io_contract::write_completes_with_transferred(__lt_tr__, __lt_name__,
                                                  fx);
LT_END_AUTO_TEST(write_completes_with_transferred)

LT_BEGIN_AUTO_TEST(fake_contract_suite, timer_fires_at_deadline_not_before)
    fake_fixture fx;
    io_contract::timer_fires_at_deadline_not_before(__lt_tr__, __lt_name__,
                                                    fx);
LT_END_AUTO_TEST(timer_fires_at_deadline_not_before)

LT_BEGIN_AUTO_TEST(fake_contract_suite,
                   two_timers_earliest_first_sequence_tie)
    fake_fixture fx;
    io_contract::two_timers_earliest_first_sequence_tie(__lt_tr__,
                                                        __lt_name__, fx);
LT_END_AUTO_TEST(two_timers_earliest_first_sequence_tie)

LT_BEGIN_AUTO_TEST(fake_contract_suite, wake_completes_all_wakes_once)
    fake_fixture fx;
    io_contract::wake_completes_all_wakes_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(wake_completes_all_wakes_once)

LT_BEGIN_AUTO_TEST(fake_contract_suite, cancel_pending_target)
    fake_fixture fx;
    io_contract::cancel_pending_target(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(cancel_pending_target)

LT_BEGIN_AUTO_TEST(fake_contract_suite,
                   cancel_terminal_target_reports_invalid_state)
    fake_fixture fx;
    io_contract::cancel_terminal_target_reports_invalid_state(
        __lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(cancel_terminal_target_reports_invalid_state)

LT_BEGIN_AUTO_TEST(fake_contract_suite, close_sweeps_every_pending_once)
    fake_fixture fx;
    io_contract::close_sweeps_every_pending_once(__lt_tr__, __lt_name__,
                                                 fx);
LT_END_AUTO_TEST(close_sweeps_every_pending_once)

LT_BEGIN_AUTO_TEST(fake_contract_suite,
                   submit_after_close_connection_closed)
    fake_fixture fx;
    io_contract::submit_after_close_connection_closed(__lt_tr__,
                                                      __lt_name__, fx);
LT_END_AUTO_TEST(submit_after_close_connection_closed)

LT_BEGIN_AUTO_TEST(fake_contract_suite,
                   late_request_cancel_reports_invalid_state)
    fake_fixture fx;
    io_contract::late_request_cancel_reports_invalid_state(
        __lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(late_request_cancel_reports_invalid_state)

LT_BEGIN_AUTO_TEST(fake_contract_suite, n_awaiter_resume_exactly_once)
    fake_fixture fx;
    io_contract::n_awaiter_resume_exactly_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(n_awaiter_resume_exactly_once)

LT_BEGIN_AUTO_TEST(fake_contract_suite, cancel_vs_stimulus_race)
    fake_fixture fx;
    io_contract::cancel_vs_stimulus_race(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(cancel_vs_stimulus_race)

// The same shared scenarios against the poll/WSAPoll driver. These are
// the "one contract, two drivers" instantiations; S12-S18 below pin the
// socket-only behavior the scripted fixture cannot express.
LT_BEGIN_SUITE(poll_contract_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(poll_contract_suite)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_read_delivers_bytes_exactly_once)
    poll_fixture fx;
    io_contract::read_delivers_bytes_exactly_once(__lt_tr__, __lt_name__,
                                                  fx);
LT_END_AUTO_TEST(poll_read_delivers_bytes_exactly_once)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_write_completes_with_transferred)
    poll_fixture fx;
    io_contract::write_completes_with_transferred(__lt_tr__, __lt_name__,
                                                  fx);
LT_END_AUTO_TEST(poll_write_completes_with_transferred)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_timer_fires_at_deadline_not_before)
    poll_fixture fx;
    io_contract::timer_fires_at_deadline_not_before(__lt_tr__, __lt_name__,
                                                    fx);
LT_END_AUTO_TEST(poll_timer_fires_at_deadline_not_before)

LT_BEGIN_AUTO_TEST(poll_contract_suite,
                   poll_two_timers_earliest_first_sequence_tie)
    poll_fixture fx;
    io_contract::two_timers_earliest_first_sequence_tie(__lt_tr__,
                                                        __lt_name__, fx);
LT_END_AUTO_TEST(poll_two_timers_earliest_first_sequence_tie)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_wake_completes_all_wakes_once)
    poll_fixture fx;
    io_contract::wake_completes_all_wakes_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(poll_wake_completes_all_wakes_once)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_cancel_pending_target)
    poll_fixture fx;
    io_contract::cancel_pending_target(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(poll_cancel_pending_target)

LT_BEGIN_AUTO_TEST(poll_contract_suite,
                   poll_cancel_terminal_target_reports_invalid_state)
    poll_fixture fx;
    io_contract::cancel_terminal_target_reports_invalid_state(
        __lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(poll_cancel_terminal_target_reports_invalid_state)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_close_sweeps_every_pending_once)
    poll_fixture fx;
    io_contract::close_sweeps_every_pending_once(__lt_tr__, __lt_name__,
                                                 fx);
LT_END_AUTO_TEST(poll_close_sweeps_every_pending_once)

LT_BEGIN_AUTO_TEST(poll_contract_suite,
                   poll_submit_after_close_connection_closed)
    poll_fixture fx;
    io_contract::submit_after_close_connection_closed(__lt_tr__,
                                                      __lt_name__, fx);
LT_END_AUTO_TEST(poll_submit_after_close_connection_closed)

// A dead record is pruned independently of owner delivery. Later
// registrations must still fail instead of reviving a released id.
LT_BEGIN_AUTO_TEST(poll_contract_suite,
                   poll_released_and_absent_ids_reject_future_submits)
    poll_fixture fx;
    fx.instance.release_connection(1);
    const auto initial = fx.instance.poll_iterations();
    const auto deadline = std::chrono::steady_clock::now()
        + io_contract::kWaitBudget;
    while (fx.instance.poll_iterations() < initial + 2
            && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    LT_CHECK(fx.instance.poll_iterations() >= initial + 2);

    hd::wake_operation released_wake(fx.rig.owner, 1);
    hd::timer_operation released_timer(fx.rig.owner, 1,
                                       deadline + io_contract::kWaitBudget);
    hd::wake_operation absent_wake(fx.rig.owner, 99);
    hd::timer_operation absent_timer(fx.rig.owner, 99,
                                     deadline + io_contract::kWaitBudget);
    released_wake.submit(fx.instance);
    released_timer.submit(fx.instance);
    absent_wake.submit(fx.instance);
    absent_timer.submit(fx.instance);
    io_contract::probe observations[4];
    std::vector<task<void>> tasks;
    io_contract::launch_probe(fx.rig, std::move(released_wake),
                              &observations[0], tasks);
    io_contract::launch_probe(fx.rig, std::move(released_timer),
                              &observations[1], tasks);
    io_contract::launch_probe(fx.rig, std::move(absent_wake),
                              &observations[2], tasks);
    io_contract::launch_probe(fx.rig, std::move(absent_timer),
                              &observations[3], tasks);
    fx.rig.ex.run_pending();
    for (const auto& observed : observations) {
        LT_CHECK_EQ(observed.delivered.load(), 1);
        LT_CHECK(observed.observed.code == hh::outcome_code::connection_closed);
    }
    LT_CHECK_EQ(fx.instance.pending_count(), std::size_t{0});
    // Close also gives a failing implementation a safe, exact-once unwind.
    fx.instance.close();
    fx.rig.ex.run_pending();
    for (const auto& observed : observations) {
        LT_CHECK_EQ(observed.delivered.load(), 1);
        LT_CHECK(observed.observed.code == hh::outcome_code::connection_closed);
    }
LT_END_AUTO_TEST(poll_released_and_absent_ids_reject_future_submits)

LT_BEGIN_AUTO_TEST(poll_contract_suite,
                   poll_release_preserves_global_live_and_readopted_ops)
    poll_fixture fx;
    auto other = io_loopback::pair::make();
    LT_CHECK(other.ok());
    if (!other.ok()) return;
    fx.instance.adopt_connection(2, other.detach_local());
    hd::wake_operation live(fx.rig.owner, 2);
    hd::wake_operation global(fx.rig.owner, 0);
    hd::timer_operation global_timer(fx.rig.owner, 0,
                                     std::chrono::steady_clock::now());
    live.submit(fx.instance);
    global.submit(fx.instance);
    global_timer.submit(fx.instance);
    fx.instance.release_connection(1);

    auto replacement = io_loopback::pair::make();
    LT_CHECK(replacement.ok());
    if (!replacement.ok()) {
        fx.instance.close();
        fx.rig.ex.run_pending();
        return;
    }
    fx.instance.adopt_connection(1, replacement.detach_local());
    hd::wake_operation readopted(fx.rig.owner, 1);
    readopted.submit(fx.instance);
    io_contract::probe observations[4];
    std::vector<task<void>> tasks;
    io_contract::launch_probe(fx.rig, std::move(live), &observations[0], tasks);
    io_contract::launch_probe(fx.rig, std::move(global), &observations[1], tasks);
    io_contract::launch_probe(fx.rig, std::move(global_timer),
                              &observations[2], tasks);
    io_contract::launch_probe(fx.rig, std::move(readopted),
                              &observations[3], tasks);
    fx.instance.wake();
    for (auto& observed : observations) {
        LT_CHECK(io_contract::wait_terminal(fx.rig, observed));
        LT_CHECK_EQ(observed.delivered.load(), 1);
        LT_CHECK(observed.observed.code == hh::outcome_code::ok);
    }
    fx.instance.close();
    fx.rig.ex.run_pending();
    for (const auto& observed : observations) {
        LT_CHECK_EQ(observed.delivered.load(), 1);
    }
LT_END_AUTO_TEST(poll_release_preserves_global_live_and_readopted_ops)

LT_BEGIN_AUTO_TEST(poll_contract_suite,
                   poll_concurrent_release_submit_delivers_closed_once)
    poll_fixture fx;
    for (int iteration = 0; iteration < 16; ++iteration) {
        if (iteration != 0) {
            auto replacement = io_loopback::pair::make();
            LT_CHECK(replacement.ok());
            if (!replacement.ok()) return;
            fx.instance.adopt_connection(1, replacement.detach_local());
        }
        hd::wake_operation op(fx.rig.owner, 1);
        std::atomic<int> arrived{0};
        io_contract::gate barrier(&arrived);
        std::thread submitter([&] {
            barrier.arrive();
            barrier.wait(2);
            op.submit(fx.instance);
        });
        std::thread releaser([&] {
            barrier.arrive();
            barrier.wait(2);
            fx.instance.release_connection(1);
        });
        submitter.join();
        releaser.join();
        io_contract::probe observed;
        std::vector<task<void>> tasks;
        io_contract::launch_probe(fx.rig, std::move(op), &observed, tasks);
        fx.rig.ex.run_pending();
        LT_CHECK_EQ(observed.delivered.load(), 1);
        LT_CHECK(observed.observed.code == hh::outcome_code::connection_closed);
        // Ensure an implementation that missed the release can unwind.
        fx.instance.release_connection(1);
        fx.rig.ex.run_pending();
        LT_CHECK_EQ(observed.delivered.load(), 1);
        LT_CHECK_EQ(fx.instance.pending_count(), std::size_t{0});
    }
LT_END_AUTO_TEST(poll_concurrent_release_submit_delivers_closed_once)

// Native would-block boundary regressions: dead and pruned registrations
// must complete detached work without backend close or a second release.
LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_detached_read_release_before_rearm)
    released_read_case(__lt_tr__, __lt_name__, false);
LT_END_AUTO_TEST(poll_detached_read_release_before_rearm)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_detached_read_prune_before_rearm)
    released_read_case(__lt_tr__, __lt_name__, true);
LT_END_AUTO_TEST(poll_detached_read_prune_before_rearm)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_detached_accept_release_before_rearm)
    released_accept_case(__lt_tr__, __lt_name__, false);
LT_END_AUTO_TEST(poll_detached_accept_release_before_rearm)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_detached_accept_prune_before_rearm)
    released_accept_case(__lt_tr__, __lt_name__, true);
LT_END_AUTO_TEST(poll_detached_accept_prune_before_rearm)

LT_BEGIN_AUTO_TEST(poll_contract_suite,
                   poll_late_request_cancel_reports_invalid_state)
    poll_fixture fx;
    io_contract::late_request_cancel_reports_invalid_state(
        __lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(poll_late_request_cancel_reports_invalid_state)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_n_awaiter_resume_exactly_once)
    poll_fixture fx;
    io_contract::n_awaiter_resume_exactly_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(poll_n_awaiter_resume_exactly_once)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_cancel_vs_stimulus_race)
    poll_fixture fx;
    io_contract::cancel_vs_stimulus_race(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(poll_cancel_vs_stimulus_race)

// Socket-only scenarios (S12-S15): the poll rig is scenario-local so
// every test gets a fresh backend, pair and listener.
LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_accept_round_trip)
    io_contract::poll_rig rig;
    io_contract::accept_round_trip(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(poll_accept_round_trip)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_interleaved_native_listeners_keep_distinct_ids)
    io_contract::poll_rig rig;
    io_contract::interleaved_native_listeners_keep_distinct_ids(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(poll_interleaved_native_listeners_keep_distinct_ids)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_http1_round_trip)
    io_contract::poll_rig rig;
    io_contract::http1_round_trip(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(poll_http1_round_trip)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_partial_read)
    io_contract::poll_rig rig;
    io_contract::partial_read(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(poll_partial_read)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_read_hangup)
    io_contract::poll_rig rig;
    io_contract::read_hangup(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(poll_read_hangup)

// Backpressure and hangup bounds (S16-S18).
LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_write_hangup)
    io_contract::poll_rig rig;
    io_contract::write_hangup(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(poll_write_hangup)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_idle_iterations_bounded)
    io_contract::poll_rig rig;
    io_contract::idle_iterations_stay_bounded(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(poll_idle_iterations_bounded)

LT_BEGIN_AUTO_TEST(poll_contract_suite, poll_slow_reader_no_busy_loop)
    io_contract::poll_rig rig;
    io_contract::slow_reader_no_busy_loop(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(poll_slow_reader_no_busy_loop)

// Pure unit suite for the monotonic deadline -> poll-timeout
// conversion (plan deliverable 2b). Fully deterministic: the clock
// reading is a parameter, so nothing here depends on wall-clock speed.
LT_BEGIN_SUITE(deadline_conversion_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(deadline_conversion_suite)

LT_BEGIN_AUTO_TEST(deadline_conversion_suite, idle_cap_without_deadline)
    namespace sc = std::chrono;
    const auto now = sc::steady_clock::now();
    LT_CHECK_EQ(hd::poll_timeout_ms(now, std::nullopt), 1000);
LT_END_AUTO_TEST(idle_cap_without_deadline)

LT_BEGIN_AUTO_TEST(deadline_conversion_suite, past_deadline_is_zero)
    namespace sc = std::chrono;
    const auto now = sc::steady_clock::now();
    LT_CHECK_EQ(hd::poll_timeout_ms(now, now), 0);
    LT_CHECK_EQ(hd::poll_timeout_ms(now, now - 1ms), 0);
LT_END_AUTO_TEST(past_deadline_is_zero)

LT_BEGIN_AUTO_TEST(deadline_conversion_suite, ceils_up_and_clamps)
    namespace sc = std::chrono;
    const auto now = sc::steady_clock::now();
    LT_CHECK_EQ(hd::poll_timeout_ms(now, now + 1ms), 1);
    LT_CHECK_EQ(hd::poll_timeout_ms(now, now + 999us), 1);
    LT_CHECK_EQ(hd::poll_timeout_ms(now, now + 1001us), 2);
    LT_CHECK_EQ(hd::poll_timeout_ms(now, now + 1500us), 2);
    LT_CHECK_EQ(hd::poll_timeout_ms(now, now + 1s), 1000);
    LT_CHECK_EQ(hd::poll_timeout_ms(now, now + 5s), 1000);
LT_END_AUTO_TEST(ceils_up_and_clamps)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
