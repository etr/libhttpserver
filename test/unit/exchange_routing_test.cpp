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

// TASK-102 Step 3: route execution (PRD-V3N-REQ-009, DR-V3-003). Pins
// the two halves of running one complete request head:
//   - route_registry::match: segment-wise lookup with {name} captures,
//     first match in registration order, null handler on a miss;
//   - detail::run_route: the one exchange state machine pass with
//     exception containment — a synchronous handler-creation throw, a
//     task-body throw, and a post-commit throw each end inside the
//     runner (500 synthesis, on_abort after a commit), a handler with
//     no terminal action is answered with exactly one 500, and a
//     disconnect cancels a suspended handler quietly (no 500, no
//     abort).
// The registry itself carries no protocol dimension, so the same route
// executes for heads differing only in request_protocol (REQ-009).

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <type_traits>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/server/routes.hpp>

#include "./littletest.hpp"

using httpserver::body_policy;
using httpserver::exchange;
using httpserver::exchange_state;
using httpserver::manual_executor;
using httpserver::resume_outcome;
using httpserver::resume_signal;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;
using httpserver::ws_upgrade_options;
namespace http = httpserver::http;
namespace detail = httpserver::detail;
namespace srv = httpserver::server;

namespace {

srv::resource_budget budget_with_routes(std::size_t capacity) {
    srv::budget_limits limits;
    limits.set(srv::resource::routes, capacity);
    return srv::resource_budget::root(limits);
}

http::request_head make_head(http::method method, std::string path,
                             http::protocol version = http::protocol::http_1_1) {
    http::request_head head;
    head.raw_target = path + "?query=1";
    head.route_path = std::move(path);
    head.request_method = method;
    head.request_protocol = version;
    return head;
}

// What a route handler saw, for the head-fidelity assertions.
struct observed_head {
    bool invoked = false;
    std::string method_name;
    std::string raw_target;
    std::string route_path;
    std::size_t field_count = 0;
};

void drain(manual_executor& ex) {
    while (ex.run_pending() > 0) {
    }
}

// Drains until `flag` turns non-zero (one millisecond per spin), for
// the cross-thread disconnect rounds whose wake-ups post from another
// thread.
bool drain_until(manual_executor& ex, int& flag, int spins) {
    for (int i = 0; i < spins && flag == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ex.run_pending();
    }
    return flag != 0;
}

}  // namespace

LT_BEGIN_SUITE(exchange_routing_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(exchange_routing_suite)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, registry_matches_literals)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    const http::method get = http::method::known(http::method_id::get);
    LT_CHECK(registry.route(get, "/things",
                            [](exchange&) -> task<void> { co_return; }).ok());

    const srv::route_registry::match_result hit =
        registry.match(get, "/things");
    LT_CHECK(hit.handler != nullptr);
    LT_CHECK(hit.parameters.empty());

    LT_CHECK(registry.match(http::method::known(http::method_id::post),
                            "/things").handler == nullptr);
    LT_CHECK(registry.match(get, "/other").handler == nullptr);
    LT_CHECK(registry.match(get, "/things/extra").handler == nullptr);
LT_END_AUTO_TEST(registry_matches_literals)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, registry_captures_parameters)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    const http::method get = http::method::known(http::method_id::get);
    LT_CHECK(registry.route(get, "/users/{id}",
                            [](exchange&) -> task<void> { co_return; }).ok());
    LT_CHECK(registry.route(get, "/files/{dir}/{name}",
                            [](exchange&) -> task<void> { co_return; }).ok());

    const srv::route_registry::match_result one =
        registry.match(get, "/users/42");
    LT_CHECK(one.handler != nullptr);
    LT_CHECK_EQ(one.parameters.size(), static_cast<std::size_t>(1));
    LT_CHECK(one.parameters[0] == "42");

    const srv::route_registry::match_result two =
        registry.match(get, "/files/a/b");
    LT_CHECK(two.handler != nullptr);
    LT_CHECK_EQ(two.parameters.size(), static_cast<std::size_t>(2));
    LT_CHECK(two.parameters[0] == "a");
    LT_CHECK(two.parameters[1] == "b");

    // An empty segment is never captured; a length mismatch is a miss.
    LT_CHECK(registry.match(get, "/users/").handler == nullptr);
    LT_CHECK(registry.match(get, "/users/42/extra").handler == nullptr);
    LT_CHECK(registry.match(get, "/files/a").handler == nullptr);
LT_END_AUTO_TEST(registry_captures_parameters)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, registry_first_registration_wins)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    const http::method get = http::method::known(http::method_id::get);
    LT_CHECK(registry.route(get, "/a/{x}",
                            [](exchange&) -> task<void> { co_return; }).ok());
    LT_CHECK(registry.route(get, "/a/literal",
                            [](exchange&) -> task<void> { co_return; }).ok());

    // First match in registration order: the parameter route shadows
    // the later literal for the same path.
    const srv::route_registry::match_result found =
        registry.match(get, "/a/literal");
    LT_CHECK(found.handler != nullptr);
    LT_CHECK_EQ(found.parameters.size(), static_cast<std::size_t>(1));
    LT_CHECK(found.parameters[0] == "literal");
LT_END_AUTO_TEST(registry_first_registration_wins)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, registry_root_pattern)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    const http::method get = http::method::known(http::method_id::get);
    LT_CHECK(registry.route(get, "/",
                            [](exchange&) -> task<void> { co_return; }).ok());

    LT_CHECK(registry.match(get, "/").handler != nullptr);
    LT_CHECK(registry.match(get, "").handler != nullptr);
LT_END_AUTO_TEST(registry_root_pattern)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_passes_head_to_handler)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    detail::recording_sink sink;
    observed_head seen;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::get), "/things",
        [&seen](exchange& x) -> task<void> {
            seen.invoked = true;
            seen.method_name = std::string(x.head().request_method.name());
            seen.raw_target = x.head().raw_target;
            seen.route_path = x.head().route_path;
            seen.field_count = x.head().head_fields.size();
            x.respond(http::status::from_code(200), http::fields());
            co_return;
        }).ok());

    http::request_head head = make_head(
        http::method::known(http::method_id::get), "/things");
    head.head_fields.append("Accept", "text/plain");
    head.head_fields.append("X-Trace", "t1");

    exchange x(head, &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.invoked);
    LT_CHECK(seen.method_name == "GET");
    // raw_target keeps the received bytes; route_path is the matching
    // input (architecture §2: the two are distinct on purpose).
    LT_CHECK(seen.raw_target == "/things?query=1");
    LT_CHECK(seen.route_path == "/things");
    LT_CHECK_EQ(seen.field_count, static_cast<std::size_t>(2));
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(200));
    LT_CHECK(x.state() == exchange_state::responded);
    LT_CHECK_EQ(sink.abort_calls, 0);
LT_END_AUTO_TEST(run_route_passes_head_to_handler)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_synthesizes_404_on_miss)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    const http::method get = http::method::known(http::method_id::get);
    LT_CHECK(registry.route(get, "/known",
                            [](exchange&) -> task<void> { co_return; }).ok());

    detail::recording_sink sink;
    exchange x(make_head(get, "/unknown"), &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(404));
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(run_route_synthesizes_404_on_miss)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_synthesizes_501_on_bad_method)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());

    detail::recording_sink sink;
    http::request_head head;
    head.raw_target = "/things";
    head.route_path = "/things";
    // request_method left default-constructed: invalid.
    exchange x(head, &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(501));
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(run_route_synthesizes_501_on_bad_method)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_is_protocol_invariant)
    // REQ-009: one registration executes for every enabled version; the
    // matcher and the runner carry no protocol dimension.
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    detail::recording_sink sink;
    int invocations = 0;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::get), "/v",
        [&invocations](exchange& x) -> task<void> {
            ++invocations;
            x.respond(http::status::from_code(200), http::fields());
            co_return;
        }).ok());

    manual_executor ex;
    for (const http::protocol version :
         {http::protocol::http_1_0, http::protocol::http_1_1}) {
        const int before = invocations;
        detail::recording_sink per_version_sink;
        exchange x(make_head(http::method::known(http::method_id::get),
                             "/v", version),
                   &per_version_sink);
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(invocations, before + 1);
        LT_CHECK_EQ(per_version_sink.respond_code,
                    static_cast<std::uint16_t>(200));
    }
LT_END_AUTO_TEST(run_route_is_protocol_invariant)

namespace {

// A route handler whose *invocation* throws, before any task exists —
// the synchronous-containment case of the route boundary.
struct throwing_invocation {
    task<void> operator()(exchange&) const {
        throw std::runtime_error("handler creation failed");
    }
};

}  // namespace

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_contains_synchronous_throw)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    LT_CHECK(registry.route(http::method::known(http::method_id::get), "/x",
                            throwing_invocation{}).ok());

    detail::recording_sink sink;
    exchange x(make_head(http::method::known(http::method_id::get), "/x"),
               &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void> r) {
              ++deliveries;
              // The containment swallows the failure inside the runner:
              // the route completes normally.
              LT_CHECK(!r.is_exception());
          });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(500));
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(run_route_contains_synchronous_throw)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_contains_task_body_throw)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    LT_CHECK(registry.route(
        http::method::known(http::method_id::get), "/x",
        [](exchange&) -> task<void> {
            throw std::runtime_error("handler body failed");
        }).ok());

    detail::recording_sink sink;
    exchange x(make_head(http::method::known(http::method_id::get), "/x"),
               &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void> r) {
              ++deliveries;
              LT_CHECK(!r.is_exception());
          });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(500));
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(run_route_contains_task_body_throw)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_aborts_after_commit_throw)
    // A handler that throws after its terminal response gets no second
    // commit: exactly one on_respond, exactly one on_abort, nothing
    // escapes the route boundary.
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    LT_CHECK(registry.route(
        http::method::known(http::method_id::get), "/x",
        [](exchange& x) -> task<void> {
            const http::outcome committed =
                x.respond(http::status::from_code(200), http::fields());
            if (!committed.ok()) co_return;
            throw std::runtime_error("failed after responding");
        }).ok());

    detail::recording_sink sink;
    exchange x(make_head(http::method::known(http::method_id::get), "/x"),
               &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void> r) {
              ++deliveries;
              LT_CHECK(!r.is_exception());
          });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(200));
    LT_CHECK_EQ(sink.abort_calls, 1);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(run_route_aborts_after_commit_throw)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_answers_missing_terminal)
    // A handler that ends without a terminal decision is answered with
    // exactly one synthesized 500 (DR-V3-003).
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    LT_CHECK(registry.route(
        http::method::known(http::method_id::get), "/x",
        [](exchange&) -> task<void> { co_return; }).ok());

    detail::recording_sink sink;
    exchange x(make_head(http::method::known(http::method_id::get), "/x"),
               &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(500));
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(run_route_answers_missing_terminal)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_quiet_when_disconnect_cancels)
    // The engine disconnects while the handler waits on its suspension:
    // the handler observes cancelled, and the runner ends the route
    // quietly — no 500, no abort (the connection is already gone).
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    bool cancelled_observed = false;
    LT_CHECK(registry.route(
        http::method::known(http::method_id::get), "/x",
        [&cancelled_observed](exchange& x) -> task<void> {
            resume_signal sig;
            if (!x.suspend(sig).ok()) co_return;
            if (co_await sig.wait() == resume_outcome::cancelled) {
                cancelled_observed = true;
                co_return;
            }
            x.respond(http::status::from_code(200), http::fields());
            co_return;
        }).ok());

    detail::recording_sink sink;
    exchange x(make_head(http::method::known(http::method_id::get), "/x"),
               &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void> r) {
              ++deliveries;
              LT_CHECK(!r.is_exception());
          });
    ex.run_pending();  // lookup hit; the handler suspends on the signal
    LT_CHECK(x.suspended());

    std::thread engine([&x] {
        x.disconnect(http::outcome_code::connection_closed, "peer left");
    });
    engine.join();
    LT_CHECK(drain_until(ex, deliveries, 400));

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(cancelled_observed);
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK_EQ(sink.abort_calls, 0);
LT_END_AUTO_TEST(run_route_quiet_when_disconnect_cancels)

LT_BEGIN_AUTO_TEST(exchange_routing_suite,
                   run_route_quiet_when_stop_token_cancels)
    // Same containment for a handler parked on the stop token: the
    // typed cancellation marker never escapes the runner.
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    LT_CHECK(registry.route(
        http::method::known(http::method_id::get), "/x",
        [](exchange& x) -> task<void> {
            co_await x.cancellation().cancelled();
            x.respond(http::status::from_code(200), http::fields());
            co_return;
        }).ok());

    detail::recording_sink sink;
    exchange x(make_head(http::method::known(http::method_id::get), "/x"),
               &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void> r) {
              ++deliveries;
              LT_CHECK(!r.is_exception());
          });
    ex.run_pending();  // the handler parks on the stop token

    std::thread engine([&x] {
        x.disconnect(http::outcome_code::connection_closed, "peer left");
    });
    engine.join();
    LT_CHECK(drain_until(ex, deliveries, 400));

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK_EQ(sink.abort_calls, 0);
LT_END_AUTO_TEST(run_route_quiet_when_stop_token_cancels)

LT_BEGIN_AUTO_TEST(exchange_routing_suite, run_route_full_happy_path)
    // The POST shape: admit the body, then commit the response.
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/uploads",
        [](exchange& x) -> task<void> {
            body_policy policy;
            policy.max_buffer_bytes = 4096;
            const http::outcome admitted = x.admit_body(policy);
            if (!admitted.ok()) {
                x.respond(http::status::from_code(500), http::fields());
                co_return;
            }
            x.respond(http::status::from_code(201), http::fields());
            co_return;
        }).ok());

    detail::recording_sink sink;
    exchange x(make_head(http::method::known(http::method_id::post),
                         "/uploads"),
               &sink);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.admit_calls, 1);
    LT_CHECK_EQ(sink.admitted_bytes, static_cast<std::uint64_t>(4096));
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.respond_code, static_cast<std::uint16_t>(201));
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(run_route_full_happy_path)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
