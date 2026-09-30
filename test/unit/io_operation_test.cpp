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

// TASK-099 step 1: private operation I/O contract (architecture §3.4,
// DR-V3-004). Pins the shape and the exactly-once terminal claim of the
// six owned operation handles:
//   - static ABI: non-copyable/movable handles, uint8-backed io_op_kind
//     with the six enumerators, io_result default {ok, 0, 0};
//   - op_state::claim_terminal is the single enforcement point: the
//     first claimant wins, every later claim is a no-op;
//   - single-consumer discipline: double submit and double await are
//     reported as std::logic_error (same posture as task<T>);
//   - sequence()/connection() metadata round-trips (sequence assigned by
//     the backend at submit, monotonic per backend, submission order as
//     metadata only -- never as delivery order).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "./httpserver/concurrency/task.hpp"
#include "./httpserver/detail/io_connection_owner.hpp"
#include "./httpserver/detail/io_operation.hpp"

#include "./littletest.hpp"

using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;

namespace hd = httpserver::detail;
namespace hh = httpserver::http;

static_assert(!std::is_copy_constructible_v<hd::accept_operation>,
              "operations must be move-only (single owner)");
static_assert(!std::is_copy_constructible_v<hd::read_operation>,
              "operations must be move-only (single owner)");
static_assert(!std::is_copy_assignable_v<hd::write_operation>,
              "operations must be move-only (single owner)");
static_assert(std::is_move_constructible_v<hd::read_operation>,
              "operations must be movable");
static_assert(std::is_move_assignable_v<hd::timer_operation>,
              "operations must be movable");
static_assert(std::is_move_constructible_v<hd::wake_operation>,
              "operations must be movable");
static_assert(std::is_move_assignable_v<hd::cancel_operation>,
              "operations must be movable");

static_assert(std::is_same_v<std::underlying_type_t<hd::io_op_kind>, std::uint8_t>,
              "io_op_kind is uint8-backed");
static_assert(static_cast<std::uint8_t>(hd::io_op_kind::accept) == 0
                  && static_cast<std::uint8_t>(hd::io_op_kind::read) == 1
                  && static_cast<std::uint8_t>(hd::io_op_kind::write) == 2
                  && static_cast<std::uint8_t>(hd::io_op_kind::timer) == 3
                  && static_cast<std::uint8_t>(hd::io_op_kind::wake) == 4
                  && static_cast<std::uint8_t>(hd::io_op_kind::cancel) == 5,
              "io_op_kind has exactly the six enumerators accept..cancel");

constexpr hd::io_result kDefaultResult{};
static_assert(kDefaultResult.code == hh::outcome_code::ok
                  && kDefaultResult.transferred == 0
                  && kDefaultResult.accepted_id == 0,
              "io_result defaults to {ok, 0, 0}");

namespace {

// Minimal stand-in backend for handle-discipline tests: the seam's
// submit() is the backend's chance to bind the sequence, mirroring the
// documented backend duty ("Backend binds sequence").
class sequence_backend final : public hd::io_backend {
 public:
    void submit(hd::op_state& op) override { op.set_sequence(++next_); }

    hh::outcome_code request_cancel(hd::op_state& target) override {
        if (!target.claim_terminal()) return hh::outcome_code::invalid_state;
        target.owner()->enqueue(
            std::move(target.shared_from_this()),
            hd::io_result{hh::outcome_code::cancelled});
        return hh::outcome_code::ok;
    }

 private:
    std::uint64_t next_ = 0;
};

}  // namespace

LT_BEGIN_SUITE(io_operation_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(io_operation_suite)

// Test 1/2: the terminal claim CAS is the single enforcement point. The
// first claimant wins; every later claim loses and the stored result is
// written exactly once.
LT_BEGIN_AUTO_TEST(io_operation_suite, terminal_claim_exactly_once)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::byte buffer[16];

    hd::read_operation op(owner, 42, std::span<std::byte>(buffer));
    LT_CHECK(!op.state()->is_terminal());
    LT_CHECK(!op.state()->applied());

    LT_CHECK(op.state()->claim_terminal());
    LT_CHECK(op.state()->is_terminal());
    LT_CHECK(!op.state()->claim_terminal());
    LT_CHECK(!op.state()->claim_terminal());

    op.state()->apply(hd::io_result{hh::outcome_code::ok, 5, 0});
    LT_CHECK(op.state()->applied());
    LT_CHECK_EQ(op.state()->stored_result().transferred, std::size_t{5});
    LT_CHECK(op.state()->stored_result().code == hh::outcome_code::ok);

    // Losing claimants after application are still no-ops.
    LT_CHECK(!op.state()->claim_terminal());
LT_END_AUTO_TEST(terminal_claim_exactly_once)

// Test 3: single-consumer discipline. Double submit and double await are
// std::logic_error, exactly like consuming a task twice.
namespace {

task<void> double_await(hd::read_operation op) {
    co_await std::move(op);
    co_await std::move(op);
}

}  // namespace

LT_BEGIN_AUTO_TEST(io_operation_suite, double_submit_and_double_await_rejected)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::byte buffer[8];
    sequence_backend backend;

    hd::read_operation op(owner, 1, std::span<std::byte>(buffer));
    op.submit(backend);
    bool threw = false;
    try {
        op.submit(backend);
    } catch (const std::logic_error&) {
        threw = true;
    }
    LT_CHECK(threw);

    // Pre-apply so the first await completes inline (await_ready path);
    // the second await must then reject the double consumption.
    op.state()->claim_terminal();
    owner.enqueue(op.state(), hd::io_result{});
    ex.run_pending();

    std::atomic<int> delivered{0};
    std::atomic<bool> was_logic_error{false};
    spawn(ex, double_await(std::move(op)), [&](task_result<void> r) {
        ++delivered;
        if (r.is_exception()) {
            try {
                std::rethrow_exception(r.exception());
            } catch (const std::logic_error&) {
                was_logic_error = true;
            } catch (...) {
            }
        }
    });
    ex.run_pending();

    LT_CHECK_EQ(delivered.load(), 1);
    LT_CHECK(was_logic_error.load());
LT_END_AUTO_TEST(double_submit_and_double_await_rejected)

// Test 7: metadata. sequence() is assigned by the backend at submit,
// monotonic per backend, and connection() round-trips the owner key.
LT_BEGIN_AUTO_TEST(io_operation_suite, sequence_and_connection_metadata)
    manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::byte buffer[8];
    const std::byte cbuffer[8]{};
    sequence_backend backend;

    hd::read_operation read_op(owner, 7, std::span<std::byte>(buffer));
    hd::write_operation write_op(owner, 9, std::span<const std::byte>(cbuffer));
    hd::accept_operation accept_op(owner, 11);

    LT_CHECK_EQ(read_op.sequence(), std::uint64_t{0});  // unsubmitted: no sequence yet

    read_op.submit(backend);
    write_op.submit(backend);
    accept_op.submit(backend);

    LT_CHECK_EQ(read_op.sequence(), std::uint64_t{1});
    LT_CHECK_EQ(write_op.sequence(), std::uint64_t{2});
    LT_CHECK_EQ(accept_op.sequence(), std::uint64_t{3});
    LT_CHECK_EQ(read_op.state()->sequence(), std::uint64_t{1});

    LT_CHECK_EQ(read_op.connection(), std::uint64_t{7});
    LT_CHECK_EQ(write_op.connection(), std::uint64_t{9});
    LT_CHECK_EQ(accept_op.connection(), std::uint64_t{11});
    LT_CHECK(read_op.kind() == hd::io_op_kind::read);
    LT_CHECK(write_op.kind() == hd::io_op_kind::write);
    LT_CHECK(accept_op.kind() == hd::io_op_kind::accept);
LT_END_AUTO_TEST(sequence_and_connection_metadata)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
