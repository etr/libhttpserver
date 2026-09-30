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
#include "./io_backend_contract.hpp"

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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
