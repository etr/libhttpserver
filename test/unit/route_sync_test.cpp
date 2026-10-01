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

// TASK-111: the bounded synchronous value-returning route adapter
// (PRD-V3N-REQ-021/022, DR-V3-003). make_sync_route wraps a plain
// value-returning handler into the canonical route_handler. Per
// request the adapter: admits the body carrying the declared cap,
// buffers it via collect(cap) -- a body past the cap answers 413 and
// the handler never runs -- invokes the handler on the worker thread,
// and commits the returned value, pinning Content-Length when the
// handler framed nothing. The suite pins:
//   - the happy path (head + exact body bytes in, one committed
//     response out, auto Content-Length);
//   - the collect boundary (exactly-at-cap succeeds, cap+1 refuses);
//   - the one-shot empty-body commit with no writer traffic;
//   - handler-pinned framing passing through verbatim;
//   - throw containment and the invalid-status synthesized 500 (both
//     inherited from run_route);
//   - a quiet end when the exchange disconnects mid-collect;
//   - REQ-009: one registration serves both HTTP/1.0 and HTTP/1.1.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/server/route_sync.hpp>
#include <httpserver/server/routes.hpp>

#include "./body_sink_fake.hpp"
#include "./body_source_fake.hpp"
#include "./littletest.hpp"

using httpserver::body_collect;
using httpserver::body_policy;
using httpserver::exchange;
using httpserver::exchange_state;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;
namespace http = httpserver::http;
namespace detail = httpserver::detail;
namespace srv = httpserver::server;
namespace fake = httpserver_test;

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

std::vector<std::byte> bytes(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string of(std::span<const std::byte> data) {
    std::string out;
    out.reserve(data.size());
    for (const std::byte b : data) {
        out.push_back(static_cast<char>(b));
    }
    return out;
}

// Decision oracle with the committed fields kept whole (recording_sink
// counts them; the framing assertions need the entries).
class capturing_sink final : public detail::exchange_sink {
 public:
    void on_admit(const body_policy& policy) override {
        ++admit_calls;
        admitted_bytes = policy.max_buffer_bytes;
    }

    void on_respond(const http::status& s, const http::fields& f) override {
        ++respond_calls;
        code = s.code();
        responded = f;
    }

    void on_upgrade(const httpserver::ws_upgrade_options&) override {
        ++upgrade_calls;
    }

    void on_abort() override {
        ++abort_calls;
    }

    int admit_calls = 0;
    int respond_calls = 0;
    int upgrade_calls = 0;
    int abort_calls = 0;
    std::uint16_t code = 0;
    http::fields responded;
    std::uint64_t admitted_bytes = 0;
};

// What one sync handler saw.
struct observed {
    int invoked = 0;
    std::string method_name;
    std::string raw_target;
    std::string body;
};

void drain(manual_executor& ex) {
    while (ex.run_pending() > 0) {
    }
}

// Drains until `flag` turns non-zero (one millisecond per spin), for
// the cross-thread disconnect round whose wake-ups post from another
// thread.
bool drain_until(manual_executor& ex, int& flag, int spins) {
    for (int i = 0; i < spins && flag == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ex.run_pending();
    }
    return flag != 0;
}

}  // namespace

LT_BEGIN_SUITE(route_sync_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(route_sync_suite)

// (1) Happy path: a POST within the cap invokes the handler exactly
// once with the head and the exact body bytes, and commits one
// response whose framing the adapter completed (auto Content-Length).
LT_BEGIN_AUTO_TEST(route_sync_suite, sync_route_serves_post_within_cap)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    observed seen;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/echo",
        srv::make_sync_route(
            [&seen](const http::request_head& h,
                    std::span<const std::byte> body) -> srv::sync_response {
                ++seen.invoked;
                seen.method_name = std::string(h.request_method.name());
                seen.raw_target = h.raw_target;
                seen.body = of(body);
                srv::sync_response out;
                out.status = http::status::from_code(200);
                out.fields.append("Content-Type", "text/plain");
                out.body.assign(body.begin(), body.end());
                return out;
            },
            4096)).ok());

    http::request_head head = make_head(
        http::method::known(http::method_id::post), "/echo");
    head.head_fields.append("Content-Type", "text/plain");
    source.stage(bytes("hello"));
    source.stage_end();

    exchange x(head, &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void> r) {
              ++deliveries;
              LT_CHECK(!r.is_exception());
          });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(seen.invoked, 1);
    LT_CHECK(seen.method_name == "POST");
    LT_CHECK(seen.raw_target == "/echo?query=1");
    LT_CHECK(seen.body == "hello");
    // The admission carries the declared cap to the engine.
    LT_CHECK_EQ(sink.admit_calls, 1);
    LT_CHECK_EQ(sink.admitted_bytes, static_cast<std::uint64_t>(4096));
    // One committed response, framing completed by the adapter.
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
    LT_CHECK(sink.responded.first("content-length").value_or("") == "5");
    LT_CHECK(sink.responded.first("content-type").value_or("")
             == "text/plain");
    // The body streamed through the writer and ended.
    LT_CHECK_EQ(responses.produced(), static_cast<std::size_t>(5));
    LT_CHECK_EQ(responses.push_calls(), 1);
    LT_CHECK_EQ(responses.end_calls(), 1);
    LT_CHECK(responses.ended());
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(sync_route_serves_post_within_cap)

// (2) REQ-022 boundary: a body of exactly cap bytes succeeds.
LT_BEGIN_AUTO_TEST(route_sync_suite, sync_route_body_exactly_at_cap)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    observed seen;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/exact",
        srv::make_sync_route(
            [&seen](const http::request_head&,
                    std::span<const std::byte> body) -> srv::sync_response {
                ++seen.invoked;
                seen.body = of(body);
                srv::sync_response out;
                out.status = http::status::from_code(200);
                out.body.assign(body.begin(), body.end());
                return out;
            },
            8)).ok());

    source.stage(bytes("12345678"));  // exactly the cap
    source.stage_end();
    exchange x(make_head(http::method::known(http::method_id::post), "/exact"),
               &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(seen.invoked, 1);
    LT_CHECK(seen.body == "12345678");
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
    LT_CHECK(sink.responded.first("content-length").value_or("") == "8");
    LT_CHECK_EQ(responses.produced(), static_cast<std::size_t>(8));
    LT_CHECK(responses.ended());
LT_END_AUTO_TEST(sync_route_body_exactly_at_cap)

// (3) A body one byte past the cap: the handler never runs; exactly
// one 413 commits; no response body is produced.
LT_BEGIN_AUTO_TEST(route_sync_suite, sync_route_over_cap_answers_413)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    observed seen;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/capped",
        srv::make_sync_route(
            [&seen](const http::request_head&,
                    std::span<const std::byte>) -> srv::sync_response {
                ++seen.invoked;
                return srv::sync_response{};
            },
            8)).ok());

    source.stage(bytes("123456789"));  // cap + 1
    source.stage_end();
    exchange x(make_head(http::method::known(http::method_id::post), "/capped"),
               &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    // The handler is provably never invoked.
    LT_CHECK_EQ(seen.invoked, 0);
    LT_CHECK_EQ(sink.admit_calls, 1);
    LT_CHECK_EQ(sink.admitted_bytes, static_cast<std::uint64_t>(8));
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(413));
    LT_CHECK_EQ(sink.responded.size(), static_cast<std::size_t>(0));
    // The writer stays untouched: the 413 is a head-only commit.
    LT_CHECK_EQ(responses.push_calls(), 0);
    LT_CHECK_EQ(responses.end_calls(), 0);
    LT_CHECK_EQ(responses.produced(), static_cast<std::size_t>(0));
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(sync_route_over_cap_answers_413)

// (4) An empty body: the handler sees an empty span and the adapter
// commits the value as one head-only respond (no writer traffic), with
// Content-Length: 0 pinned (keeps 1.0 keep-alive honest).
LT_BEGIN_AUTO_TEST(route_sync_suite, sync_route_empty_body_one_shot)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    observed seen;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/empty",
        srv::make_sync_route(
            [&seen](const http::request_head&,
                    std::span<const std::byte> body) -> srv::sync_response {
                ++seen.invoked;
                seen.body = of(body);
                srv::sync_response out;
                out.status = http::status::from_code(204);
                return out;
            },
            64)).ok());

    source.stage_end();  // a bodyless request: end with nothing staged
    exchange x(make_head(http::method::known(http::method_id::post), "/empty"),
               &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(seen.invoked, 1);
    LT_CHECK(seen.body.empty());
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(204));
    LT_CHECK(sink.responded.first("content-length").value_or("") == "0");
    LT_CHECK_EQ(responses.push_calls(), 0);
    LT_CHECK_EQ(responses.end_calls(), 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(sync_route_empty_body_one_shot)

// (5) Handler-pinned framing passes through verbatim: an explicit
// Content-Length is not duplicated.
LT_BEGIN_AUTO_TEST(route_sync_suite, sync_route_pinned_framing_passes_through)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/pinned",
        srv::make_sync_route(
            [](const http::request_head&,
               std::span<const std::byte> body) -> srv::sync_response {
                srv::sync_response out;
                out.status = http::status::from_code(200);
                out.fields.append("Content-Length", "6");
                out.body.assign(body.begin(), body.end());
                return out;
            },
            64)).ok());

    source.stage(bytes("pinned"));
    source.stage_end();
    exchange x(make_head(http::method::known(http::method_id::post), "/pinned"),
               &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
    LT_CHECK_EQ(sink.responded.count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(sink.responded.first("content-length").value_or("") == "6");
    LT_CHECK_EQ(responses.produced(), static_cast<std::size_t>(6));
    LT_CHECK(responses.ended());
LT_END_AUTO_TEST(sync_route_pinned_framing_passes_through)

// (6) A throwing handler is contained by the route boundary: exactly
// one synthesized 500, no abort (the throw preceded any commit).
LT_BEGIN_AUTO_TEST(route_sync_suite, sync_route_contains_handler_throw)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/boom",
        srv::make_sync_route(
            [](const http::request_head&,
               std::span<const std::byte>) -> srv::sync_response {
                throw std::runtime_error("sync handler failed");
            },
            64)).ok());

    source.stage(bytes("fine"));
    source.stage_end();
    exchange x(make_head(http::method::known(http::method_id::post), "/boom"),
               &sink, 0, &source, &responses);
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
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(500));
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK_EQ(responses.push_calls(), 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(sync_route_contains_handler_throw)

// (7) An invalid returned status commits nothing itself: the adapter
// ends without a terminal decision and the runner synthesizes the 500.
LT_BEGIN_AUTO_TEST(route_sync_suite, sync_route_invalid_status_answers_500)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/unstated",
        srv::make_sync_route(
            [](const http::request_head&,
               std::span<const std::byte>) -> srv::sync_response {
                srv::sync_response unstated;
                unstated.body = std::vector<std::byte>(8, std::byte{0});
                return unstated;
            },
            64)).ok());

    source.stage_end();
    exchange x(make_head(http::method::known(http::method_id::post),
                         "/unstated"),
               &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(500));
    LT_CHECK_EQ(responses.push_calls(), 0);
    LT_CHECK_EQ(sink.abort_calls, 0);
LT_END_AUTO_TEST(sync_route_invalid_status_answers_500)

// (8) A disconnect mid-collect ends the route quietly: no response
// commit, no abort (the connection is already gone).
LT_BEGIN_AUTO_TEST(route_sync_suite, sync_route_disconnect_mid_collect_quiet)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    observed seen;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/quiet",
        srv::make_sync_route(
            [&seen](const http::request_head&,
                    std::span<const std::byte>) -> srv::sync_response {
                ++seen.invoked;
                return srv::sync_response{};
            },
            64)).ok());

    exchange x(make_head(http::method::known(http::method_id::post), "/quiet"),
               &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, fake::watch_stop_and_cancel(x, source),
          [](task_result<void>) { });
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void> r) {
              ++deliveries;
              LT_CHECK(!r.is_exception());
          });
    ex.run_pending();  // the watcher parks; the adapter parks in collect
    LT_CHECK(source.parked());
    LT_CHECK_EQ(deliveries, 0);

    std::thread engine([&x] {
        x.disconnect(http::outcome_code::connection_closed, "peer left");
    });
    engine.join();
    LT_CHECK(drain_until(ex, deliveries, 400));

    LT_CHECK_EQ(deliveries, 1);
    // The handler never ran and nothing was committed.
    LT_CHECK_EQ(seen.invoked, 0);
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK_EQ(responses.push_calls(), 0);
LT_END_AUTO_TEST(sync_route_disconnect_mid_collect_quiet)

// (9) REQ-009: one sync registration serves both HTTP/1.0 and HTTP/1.1
// request heads; the adapter carries no protocol dimension.
LT_BEGIN_AUTO_TEST(route_sync_suite, sync_route_serves_http_1_0_and_1_1)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    int invocations = 0;

    LT_CHECK(registry.route(
        http::method::known(http::method_id::post), "/v",
        srv::make_sync_route(
            [&invocations](const http::request_head&,
                           std::span<const std::byte> body)
                -> srv::sync_response {
                ++invocations;
                srv::sync_response out;
                out.status = http::status::from_code(200);
                out.body.assign(body.begin(), body.end());
                return out;
            },
            32)).ok());

    manual_executor ex;
    for (const http::protocol version :
         {http::protocol::http_1_0, http::protocol::http_1_1}) {
        capturing_sink per_version_sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes("v"));
        source.stage_end();
        exchange x(make_head(http::method::known(http::method_id::post),
                             "/v", version),
                   &per_version_sink, 0, &source, &responses);
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(per_version_sink.respond_calls, 1);
        LT_CHECK_EQ(per_version_sink.code, static_cast<std::uint16_t>(200));
        LT_CHECK_EQ(responses.produced(), static_cast<std::size_t>(1));
    }
    LT_CHECK_EQ(invocations, 2);
LT_END_AUTO_TEST(sync_route_serves_http_1_0_and_1_1)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
