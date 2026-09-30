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

#include <chrono>
#include <cstddef>
#include <string_view>

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
