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

// TASK-102 Step 2: header-time exchange decisions (PRD-V3N-REQ-023/
// 024/025). Pins the one exchange state machine every routed head goes
// through:
//   - the decision matrix: respond (commit or reject), admit_body,
//     suspend, upgrade, each legal only from its documented states;
//   - typed failures that never mutate state: invalid_state on double
//     terminal actions (sink invoked exactly once), not_supported for
//     an upgrade off a non-HTTP/1.1 head;
//   - suspension: a live resume signal with timeout, cross-thread
//     resume, and disconnect resolving the wait as cancelled;
//   - disconnect notification: idempotent, fans out to the stop token
//     and every recorded resume signal, moves non-terminal states to
//     cancelled while terminal states stay terminal, and turns later
//     decisions into connection_closed carrying the stored detail.
//
// The suite runs against detail::recording_sink, a call-counting fake
// of the engine seam, so no transport exists yet.

#include <utility>
#include <chrono>
#include <string>
#include <thread>
#include <type_traits>

#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/server/routes.hpp>

#include "./littletest.hpp"

using httpserver::body_policy;
using httpserver::exchange;
using httpserver::exchange_state;
using httpserver::manual_executor;
using httpserver::resume_outcome;
using httpserver::resume_signal;
using httpserver::spawn;
using httpserver::stop_token;
using httpserver::task;
using httpserver::task_result;
using httpserver::ws_upgrade_options;
namespace http = httpserver::http;
namespace detail = httpserver::detail;
namespace srv = httpserver::server;

static_assert(std::is_move_constructible_v<exchange>,
              "exchange is movable (engines hand exchange&& between hops)");
static_assert(std::is_move_assignable_v<exchange>,
              "exchange is movable (engines hand exchange&& between hops)");
static_assert(!std::is_copy_constructible_v<exchange>,
              "one exchange per request head; copies are a bug");
static_assert(!std::is_copy_assignable_v<exchange>,
              "one exchange per request head; copies are a bug");

namespace {

http::outcome decide_upgrade(exchange& x, ws_upgrade_options options) {
    manual_executor ex; std::optional<http::outcome> result;
    spawn(ex, x.upgrade(std::move(options)), [&](auto r) { result.emplace(r.value().status); });
    ex.run_pending(); return *result;
}

enum class wait_note { none, resumed, timeout, cancelled };

http::request_head make_head(http::protocol version = http::protocol::http_1_1) {
    http::request_head head;
    head.raw_target = "/things";
    head.route_path = "/things";
    head.request_method = http::method::known(http::method_id::get);
    head.request_protocol = version;
    return head;
}

// Drains the executor until `flag` turns non-zero or the spin budget
// (one millisecond per spin) runs out. LT_CHECK macro context stays in
// the test bodies; this helper only reports whether the wait landed.
bool drain_until(manual_executor& ex, int& flag, int spins) {
    for (int i = 0; i < spins && flag == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ex.run_pending();
    }
    return flag != 0;
}

task<void> observe_wait_for(exchange& x, resume_signal* stored,
                            std::chrono::milliseconds budget,
                            wait_note* note) {
    resume_signal sig;
    if (!x.suspend(sig).ok()) co_return;
    *stored = sig;  // test-side handle on the same one-shot event
    const resume_outcome seen = co_await sig.wait_for(budget);
    if (seen == resume_outcome::resumed) {
        *note = wait_note::resumed;
    } else if (seen == resume_outcome::timeout) {
        *note = wait_note::timeout;
    } else {
        *note = wait_note::cancelled;
    }
}

task<void> observe_wait(exchange& x, resume_signal* stored, wait_note* note) {
    resume_signal sig;
    if (!x.suspend(sig).ok()) co_return;
    *stored = sig;  // test-side handle on the same one-shot event
    const resume_outcome seen = co_await sig.wait();
    *note = seen == resume_outcome::resumed ? wait_note::resumed
                                            : wait_note::cancelled;
}

task<void> await_stop_token(exchange& x) {
    co_await x.cancellation().cancelled();
}

}  // namespace

LT_BEGIN_SUITE(exchange_decisions_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(exchange_decisions_suite)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_route_handler_still_fits)
    // The canonical handler shape survives unchanged: a coroutine over
    // the exchange (DR-V3-003). Constructing it is the contract.
    srv::route_handler handler([](exchange&) -> task<void> { co_return; });
    LT_CHECK(static_cast<bool>(handler));
LT_END_AUTO_TEST(exchange_route_handler_still_fits)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_respond_from_head_commits)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    http::fields body_type;
    body_type.append("Content-Type", "text/plain");
    const http::outcome committed = x.respond(http::status::from_code(200),
                                              body_type);
    LT_CHECK(committed.ok());
    LT_CHECK(x.state() == exchange_state::responded);
    LT_CHECK(x.terminal());
    LT_CHECK(!x.suspended());
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(200));
    LT_CHECK_EQ(sink.respond_fields_size, static_cast<std::size_t>(1));
    LT_CHECK_EQ(sink.admit_calls, 0);
LT_END_AUTO_TEST(exchange_respond_from_head_commits)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_admit_then_respond)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    body_policy policy;
    policy.max_buffer_bytes = 4096;
    const http::outcome admitted = x.admit_body(policy);
    LT_CHECK(admitted.ok());
    LT_CHECK(x.state() == exchange_state::admitted);
    LT_CHECK(!x.terminal());
    LT_CHECK_EQ(sink.admit_calls, 1);
    LT_CHECK_EQ(sink.admitted_bytes, static_cast<std::uint64_t>(4096));

    const http::outcome committed = x.respond(http::status::from_code(201),
                                              http::fields());
    LT_CHECK(committed.ok());
    LT_CHECK(x.state() == exchange_state::responded);
    LT_CHECK_EQ(sink.respond_calls, 1);
LT_END_AUTO_TEST(exchange_admit_then_respond)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_reject_is_error_respond)
    // The reject decision is an error-status respond straight from the
    // head, with no body admission (413-style).
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    const http::outcome rejected = x.respond(http::status::from_code(413),
                                             http::fields());
    LT_CHECK(rejected.ok());
    LT_CHECK(x.state() == exchange_state::responded);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(413));
    LT_CHECK_EQ(sink.admit_calls, 0);
LT_END_AUTO_TEST(exchange_reject_is_error_respond)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_suspend_from_head_is_live)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);
    manual_executor ex;

    resume_signal stored;
    wait_note note = wait_note::none;
    int deliveries = 0;
    spawn(ex,
          observe_wait_for(x, &stored, std::chrono::milliseconds(5000),
                           &note),
          [&](task_result<void>) { ++deliveries; });
    ex.run_pending();  // start: the handler suspends on the signal

    LT_CHECK(x.suspended());
    LT_CHECK(x.state() == exchange_state::head);
    LT_CHECK_EQ(deliveries, 0);

    // The signal is live: resuming it completes the wait.
    stored.signal();
    ex.run_pending();

    LT_CHECK(note == wait_note::resumed);
    LT_CHECK_EQ(deliveries, 1);

    const http::outcome committed = x.respond(http::status::from_code(200),
                                              http::fields());
    LT_CHECK(committed.ok());
    LT_CHECK(!x.suspended());
LT_END_AUTO_TEST(exchange_suspend_from_head_is_live)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_suspend_after_admit_ok)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    resume_signal sig;
    LT_CHECK(x.admit_body(body_policy()).ok());
    const http::outcome suspended = x.suspend(sig);
    LT_CHECK(suspended.ok());
    LT_CHECK(x.suspended());
    LT_CHECK(x.state() == exchange_state::admitted);
LT_END_AUTO_TEST(exchange_suspend_after_admit_ok)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_upgrade_from_1_1_head)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    ws_upgrade_options options;
    options.subprotocols.push_back("chat.example");
    const http::outcome upgraded = decide_upgrade(x, options);
    LT_CHECK(upgraded.ok());
    LT_CHECK(x.state() == exchange_state::upgraded);
    LT_CHECK(x.terminal());
    LT_CHECK_EQ(sink.upgrade_calls, 1);
    LT_CHECK_EQ(sink.upgrade_subprotocols, static_cast<std::size_t>(1));
LT_END_AUTO_TEST(exchange_upgrade_from_1_1_head)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_upgrade_on_1_0_refused)
    detail::recording_sink sink;
    exchange x(make_head(http::protocol::http_1_0), &sink);

    const http::outcome refused = decide_upgrade(x, ws_upgrade_options());
    LT_CHECK(refused.code() == http::outcome_code::not_supported);
    LT_CHECK(!refused.message().empty());
    LT_CHECK(x.state() == exchange_state::head);
    LT_CHECK_EQ(sink.upgrade_calls, 0);
LT_END_AUTO_TEST(exchange_upgrade_on_1_0_refused)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_upgrade_after_admit_fails)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(x.admit_body(body_policy()).ok());
    const http::outcome refused = decide_upgrade(x, ws_upgrade_options());
    LT_CHECK(refused.code() == http::outcome_code::invalid_state);
    LT_CHECK(x.state() == exchange_state::admitted);
    LT_CHECK_EQ(sink.upgrade_calls, 0);
LT_END_AUTO_TEST(exchange_upgrade_after_admit_fails)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_upgrade_after_respond_fails)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(x.respond(http::status::from_code(200), http::fields()).ok());
    const http::outcome refused = decide_upgrade(x, ws_upgrade_options());
    LT_CHECK(refused.code() == http::outcome_code::invalid_state);
    LT_CHECK(x.state() == exchange_state::responded);
    LT_CHECK_EQ(sink.upgrade_calls, 0);
LT_END_AUTO_TEST(exchange_upgrade_after_respond_fails)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_double_respond_fails)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(x.respond(http::status::from_code(200), http::fields()).ok());
    const http::outcome again = x.respond(http::status::from_code(404),
                                          http::fields());
    LT_CHECK(again.code() == http::outcome_code::invalid_state);
    LT_CHECK(!again.message().empty());
    LT_CHECK(x.state() == exchange_state::responded);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(200));
LT_END_AUTO_TEST(exchange_double_respond_fails)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_double_admit_fails)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(x.admit_body(body_policy()).ok());
    const http::outcome again = x.admit_body(body_policy());
    LT_CHECK(again.code() == http::outcome_code::invalid_state);
    LT_CHECK(!again.message().empty());
    LT_CHECK(x.state() == exchange_state::admitted);
    LT_CHECK_EQ(sink.admit_calls, 1);
LT_END_AUTO_TEST(exchange_double_admit_fails)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_admit_after_terminal_fails)
    detail::recording_sink sink;
    exchange responded(make_head(), &sink);
    LT_CHECK(responded.respond(http::status::from_code(204),
                               http::fields()).ok());
    LT_CHECK(responded.admit_body(body_policy()).code()
             == http::outcome_code::invalid_state);

    exchange upgraded(make_head(), &sink);
    LT_CHECK(decide_upgrade(upgraded, ws_upgrade_options()).ok());
    LT_CHECK(upgraded.admit_body(body_policy()).code()
             == http::outcome_code::invalid_state);
    LT_CHECK_EQ(sink.admit_calls, 0);
LT_END_AUTO_TEST(exchange_admit_after_terminal_fails)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_respond_after_upgrade_fails)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(decide_upgrade(x, ws_upgrade_options()).ok());
    const http::outcome refused = x.respond(http::status::from_code(200),
                                            http::fields());
    LT_CHECK(refused.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK_EQ(sink.upgrade_calls, 1);
LT_END_AUTO_TEST(exchange_respond_after_upgrade_fails)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_double_upgrade_fails)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(decide_upgrade(x, ws_upgrade_options()).ok());
    const http::outcome again = decide_upgrade(x, ws_upgrade_options());
    LT_CHECK(again.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(sink.upgrade_calls, 1);
LT_END_AUTO_TEST(exchange_double_upgrade_fails)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_suspend_times_out)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);
    manual_executor ex;

    wait_note note = wait_note::none;
    int deliveries = 0;
    resume_signal unused;
    spawn(ex,
          observe_wait_for(x, &unused, std::chrono::milliseconds(50), &note),
          [&](task_result<void>) { ++deliveries; });
    ex.run_pending();  // start: the handler suspends with the deadline
    LT_CHECK_EQ(deliveries, 0);

    LT_CHECK(drain_until(ex, deliveries, 400));
    LT_CHECK(note == wait_note::timeout);
    LT_CHECK_EQ(deliveries, 1);

    // A timed-out suspension is not terminal: the handler still owns
    // the decision.
    const http::outcome committed = x.respond(http::status::from_code(200),
                                              http::fields());
    LT_CHECK(committed.ok());
LT_END_AUTO_TEST(exchange_suspend_times_out)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_resume_from_other_thread)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);
    manual_executor ex;

    resume_signal stored;
    wait_note note = wait_note::none;
    int deliveries = 0;
    spawn(ex, observe_wait(x, &stored, &note),
          [&](task_result<void>) { ++deliveries; });
    ex.run_pending();  // the handler suspends
    LT_CHECK(x.suspended());

    std::thread waker([&stored] { stored.signal(); });
    waker.join();
    LT_CHECK(drain_until(ex, deliveries, 400));
    LT_CHECK(note == wait_note::resumed);
LT_END_AUTO_TEST(exchange_resume_from_other_thread)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_disconnect_cancels_waiter)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);
    manual_executor ex;

    resume_signal stored;
    wait_note note = wait_note::none;
    int deliveries = 0;
    spawn(ex, observe_wait(x, &stored, &note),
          [&](task_result<void>) { ++deliveries; });
    ex.run_pending();  // the handler suspends

    const http::outcome gone = x.disconnect(http::outcome_code::connection_closed,
                                            "peer went away");
    LT_CHECK(gone.ok());
    LT_CHECK(x.disconnected());
    LT_CHECK(x.state() == exchange_state::cancelled);
    LT_CHECK(drain_until(ex, deliveries, 400));
    LT_CHECK(note == wait_note::cancelled);
    LT_CHECK_EQ(deliveries, 1);
LT_END_AUTO_TEST(exchange_disconnect_cancels_waiter)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_disconnect_fans_out_stop)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);
    manual_executor ex;

    int deliveries = 0;
    bool typed_cancelled = false;
    spawn(ex, await_stop_token(x),
          [&](task_result<void> r) {
              ++deliveries;
              typed_cancelled = r.has_outcome()
                  && r.outcome() == http::outcome_code::cancelled;
          });
    ex.run_pending();  // the handler suspends on the stop token

    LT_CHECK(x.disconnect(http::outcome_code::connection_closed, "gone").ok());
    LT_CHECK(drain_until(ex, deliveries, 400));
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(typed_cancelled);
    LT_CHECK(x.cancellation().stop_requested());
LT_END_AUTO_TEST(exchange_disconnect_fans_out_stop)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_disconnect_is_idempotent)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "first").ok());
    LT_CHECK(x.disconnect(http::outcome_code::protocol_error,
                          "second").ok());
    LT_CHECK(x.disconnect_reason().code()
             == http::outcome_code::connection_closed);
    LT_CHECK(x.disconnect_reason().message() == "first");
    LT_CHECK(x.state() == exchange_state::cancelled);
LT_END_AUTO_TEST(exchange_disconnect_is_idempotent)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_decisions_after_disconnect)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(x.disconnect(http::outcome_code::connection_closed, "gone").ok());

    const http::outcome responded = x.respond(http::status::from_code(200),
                                              http::fields());
    LT_CHECK(responded.code() == http::outcome_code::connection_closed);
    LT_CHECK(responded.message().find("gone") != std::string::npos);
    LT_CHECK(x.admit_body(body_policy()).code()
             == http::outcome_code::connection_closed);
    resume_signal sig;
    LT_CHECK(x.suspend(sig).code()
             == http::outcome_code::connection_closed);
    LT_CHECK(decide_upgrade(x, ws_upgrade_options()).code()
             == http::outcome_code::connection_closed);

    // Typed failures never mutate state and never reach the engine.
    LT_CHECK(x.state() == exchange_state::cancelled);
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK_EQ(sink.admit_calls, 0);
    LT_CHECK_EQ(sink.upgrade_calls, 0);
LT_END_AUTO_TEST(exchange_decisions_after_disconnect)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_disconnect_after_terminal)
    // A terminal state stays terminal across a disconnect (an upgraded
    // connection carries on), but the cancellation fan-out still fires.
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(decide_upgrade(x, ws_upgrade_options()).ok());
    LT_CHECK(x.disconnect(http::outcome_code::connection_closed, "gone").ok());
    LT_CHECK(x.state() == exchange_state::upgraded);
    LT_CHECK(x.terminal());
    LT_CHECK(x.disconnected());
    LT_CHECK(x.cancellation().stop_requested());
LT_END_AUTO_TEST(exchange_disconnect_after_terminal)

LT_BEGIN_AUTO_TEST(exchange_decisions_suite, exchange_disconnect_needs_reason)
    detail::recording_sink sink;
    exchange x(make_head(), &sink);

    LT_CHECK(x.disconnect(http::outcome_code::ok, "not a reason").code()
             == http::outcome_code::invalid_argument);
    LT_CHECK(!x.disconnected());
    LT_CHECK(x.respond(http::status::from_code(200), http::fields()).ok());
LT_END_AUTO_TEST(exchange_disconnect_needs_reason)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
