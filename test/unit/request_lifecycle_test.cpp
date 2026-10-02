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

// TASK-118 step 3: the request dispatch pipeline (plan D3/D4/D5) --
// route resolve, hook firing in the documented order, the v2 default
// error pages with Allow, custom pages through the construction-time
// factories, named captures delivered as path args, and the
// after_handler/response_sent firing through the interceptor sink at
// response-head commit. Driven over the real registry + bus + runner
// with a manual executor, exactly like the route_sync rig.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/detail/lifecycle_sink.hpp>
#include <httpserver/detail/request_lifecycle.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/server/hooks.hpp>
#include <httpserver/server/route_sync.hpp>

#include "./body_sink_fake.hpp"
#include "./body_source_fake.hpp"
#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;
namespace srv = httpserver::server;
namespace detail = httpserver::detail;
namespace fake = httpserver_test;

using httpserver::exchange;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;

// --- rig ----------------------------------------------------------------------

srv::resource_budget budget_with_routes(std::size_t capacity) {
    srv::budget_limits limits;
    limits.set(srv::resource::routes, capacity);
    return srv::resource_budget::root(limits);
}

http::request_head make_head(const http::method& m, std::string path) {
    http::request_head head;
    head.route_path = std::move(path);
    head.request_method = m;
    head.request_protocol = http::protocol::http_1_1;
    return head;
}

const http::method kGet = http::method::known(http::method_id::get);
const http::method kHead = http::method::known(http::method_id::head);
const http::method kPost = http::method::known(http::method_id::post);
const http::method kDelete = http::method::known(http::method_id::del);

// The engine-seam stand-in: records every committed decision.
class capturing_sink final : public detail::exchange_sink {
 public:
    void on_admit(const httpserver::body_policy& policy) override {
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

// The observed hook trail of one dispatch.
struct trail {
    std::vector<std::string> order;   // phase visit order
    std::string value_of(const char* name) const {
        for (const std::string& visit : order) {
            if (visit.rfind(name, 0) == 0) return visit.substr(std::string(name).size());
        }
        return "";
    }
    bool saw(const char* name) const {
        for (const std::string& visit : order) {
            if (visit.rfind(name, 0) == 0) return true;
        }
        return false;
    }
    std::size_t count(const char* name) const {
        std::size_t n = 0;
        for (const std::string& visit : order) {
            if (visit == name) ++n;
        }
        return n;
    }
};

// Installs one recording hook per phase, each appending its tag (and
// optional payload) to the shared trail.
void record_all_phases(srv::hook_bus& bus, std::shared_ptr<trail> seen) {
    (void)bus.add<srv::hook_phase::request_received>(
        [seen](srv::request_received_ctx& c) -> srv::hook_action {
            seen->order.push_back(std::string("request_received ")
                                       + std::string(c.request.route_path));
            return srv::hook_action::pass();
        }).detach();
    (void)bus.add<srv::hook_phase::route_resolved>(
        [seen](srv::route_resolved_ctx& c) -> srv::hook_action {
            seen->order.push_back(std::string("route_resolved matched=")
                                       + (c.matched ? "1" : "0"));
            return srv::hook_action::pass();
        }).detach();
    (void)bus.add<srv::hook_phase::before_handler>(
        [seen](srv::before_handler_ctx& c) -> srv::hook_action {
            seen->order.push_back("before_handler");
            return srv::hook_action::pass();
        }).detach();
    (void)bus.add<srv::hook_phase::handler_exception>(
        [seen](srv::handler_exception_ctx& c) -> srv::hook_action {
            seen->order.push_back(std::string("handler_exception error=")
                                       + (c.error ? "1" : "0"));
            return srv::hook_action::pass();
        }).detach();
    (void)bus.add<srv::hook_phase::after_handler>(
        [seen](srv::after_handler_ctx& c) -> srv::hook_action {
            seen->order.push_back(std::string("after_handler ")
                                       + std::to_string(c.status.code()));
            return srv::hook_action::pass();
        }).detach();
    (void)bus.add<srv::hook_phase::response_sent>(
        [seen](srv::response_sent_ctx& c) -> srv::hook_action {
            seen->order.push_back(std::string("response_sent ")
                                       + std::to_string(c.status));
            return srv::hook_action::pass();
        }).detach();
    (void)bus.add<srv::hook_phase::request_completed>(
        [seen](srv::request_completed_ctx& c) -> srv::hook_action {
            seen->order.push_back(std::string("request_completed succeeded=")
                                       + (c.succeeded ? "1" : "0") + " end="
                                       + std::to_string(static_cast<int>(c.end.code())));
            return srv::hook_action::pass();
        }).detach();
}

// One dispatch dispatch_round: builds the interceptor over the capturing sink,
// spawns dispatch_request, drains, reports delivery.
struct dispatch_round {
    srv::route_registry registry;
    srv::hook_bus bus;
    detail::error_page_factories pages;
    capturing_sink inner;
    std::shared_ptr<trail> seen = std::make_shared<trail>();
    fake::scripted_body_sink responses;
    int deliveries = 0;

    dispatch_round() {
        (void)srv::route_registry::create(budget_with_routes(16), registry);
    }

    // Runs one head through the pipeline. LT_CHECK expands
    // harness-local identifiers, so the verdicts live in members the
    // test bodies assert on.
    void dispatch(const http::request_head& head) {
        detail::lifecycle_sink sink(inner, bus, head);
        exchange x(head, &sink, 0, nullptr, &responses);
        manual_executor ex;
        spawn(ex, detail::dispatch_request(registry, bus, pages, sink, x),
              [&](task_result<void> r) {
                  if (!r.is_exception()) ++deliveries;
              });
        while (ex.run_pending() > 0) {
        }
    }

    bool delivered_once() const { return deliveries >= 1; }
};

srv::route_handler text_handler(std::string body,
                                std::uint16_t code = 200) {
    return [out = std::move(body), code](
               exchange& x) -> task<void> {
        srv::sync_response value;
        value.status = http::status::from_code(code);
        value.fields.append("Content-Type", "text/plain");
        const std::byte* raw =
            reinterpret_cast<const std::byte*>(out.data());
        value.body.assign(raw, raw + out.size());
        co_await srv::detail::commit_sync_value(x, std::move(value));
    };
}

srv::hook_response hook_page(std::uint16_t code, std::string body) {
    srv::hook_response page;
    page.status = http::status::from_code(code);
    page.fields.append("Content-Type", "text/plain");
    const std::byte* raw = reinterpret_cast<const std::byte*>(body.data());
    page.body.assign(raw, raw + body.size());
    return page;
}

std::string field_of(const http::fields& f, const char* name) {
    const std::optional<std::string_view> value = f.first(name);
    return value == std::nullopt ? std::string() : std::string(*value);
}

}  // namespace

LT_BEGIN_SUITE(request_lifecycle_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(request_lifecycle_suite)

// (1) Exact hit: the full firing order -- route_resolved, before_
// handler, the handler, after_handler at commit, response_sent once,
// request_completed(succeeded) exactly once.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, exact_hit_firing_order)
    dispatch_round r;
    LT_CHECK(r.registry.route(kGet, "/hello", text_handler("OK")).ok());
    record_all_phases(r.bus, r.seen);

    r.dispatch(make_head(kGet, "/hello"));

    LT_CHECK(r.seen->order.size() == std::size_t{6});
    LT_CHECK(r.seen->order[0] == "request_received /hello");
    LT_CHECK(r.seen->order[1] == "route_resolved matched=1");
    LT_CHECK(r.seen->order[2] == "before_handler");
    LT_CHECK(r.seen->order[3] == "after_handler 200");
    LT_CHECK(r.seen->order[4] == "response_sent 200");
    LT_CHECK(r.seen->order[5].rfind("request_completed succeeded=1", 0) == 0);
    LT_CHECK(r.inner.respond_calls == 1);
    LT_CHECK_EQ(r.inner.code, std::uint16_t{200});
    LT_CHECK(field_of(r.inner.responded, "Content-Type") == "text/plain");
    LT_CHECK(field_of(r.inner.responded, "Content-Length") == "2");
LT_END_AUTO_TEST(exact_hit_firing_order)

// (2) Parameterized hit: the captures are stamped as path args the
// handler reads.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, parameterized_path_args)
    dispatch_round r;
    std::string seen_args;
    LT_CHECK(r.registry.route(
        kGet, "/params/{id}/name/{name}",
        [&seen_args](exchange& x) -> task<void> {
            for (const srv::route_captures& arg : x.path_args()) {
                seen_args.append(arg.name).append("=").append(arg.value)
                    .append(";");
            }
            co_return;
        }).ok());

    r.dispatch(make_head(kGet, "/params/42/name/jane"));
    LT_CHECK(seen_args == "id=42;name=jane;");
    // No terminal decision: the synthesized 500 commits.
    LT_CHECK_EQ(r.inner.code, std::uint16_t{500});
LT_END_AUTO_TEST(parameterized_path_args)

// (3) Miss: the v2 default 404 page, after_handler fired on the
// synthesis path.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, default_404_page)
    dispatch_round r;
    LT_CHECK(r.registry.route(kGet, "/hello", text_handler("OK")).ok());
    record_all_phases(r.bus, r.seen);

    r.dispatch(make_head(kGet, "/definitely/not/there"));

    LT_CHECK(r.seen->saw("route_resolved matched=0"));
    LT_CHECK(r.seen->saw("after_handler"));
    LT_CHECK(r.inner.respond_calls == 1);
    LT_CHECK_EQ(r.inner.code, std::uint16_t{404});
    LT_CHECK(field_of(r.inner.responded, "Content-Type") == "text/plain");
    LT_CHECK(field_of(r.inner.responded, "Content-Length") == "9");
    LT_CHECK(r.seen->order.back().rfind("request_completed succeeded=1", 0)
             == 0);
LT_END_AUTO_TEST(default_404_page)

// (4) Custom 404: the construction-time factory supplies the body.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, custom_404_page)
    dispatch_round r;
    r.pages.not_found = [](const http::request_head&) -> srv::hook_response {
        return hook_page(404, "custom-not-found");
    };
    LT_CHECK(r.registry.route(kGet, "/hello", text_handler("OK")).ok());
    record_all_phases(r.bus, r.seen);

    r.dispatch(make_head(kGet, "/nope"));

    LT_CHECK_EQ(r.inner.code, std::uint16_t{404});
    LT_CHECK(field_of(r.inner.responded, "Content-Type") == "text/plain");
    LT_CHECK(field_of(r.inner.responded, "Content-Length") == "16");
    LT_CHECK(r.seen->saw("after_handler"));
    // The factory response carries no X-Hook; after_handler can add one
    // (covered below); here the pinned corpus shape is the custom body.
LT_END_AUTO_TEST(custom_404_page)

// (5) Method miss: 405 with Allow in method order and the v2 default
// body.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, default_405_with_allow)
    dispatch_round r;
    LT_CHECK(r.registry.route(kGet, "/get_only", text_handler("x")).ok());
    record_all_phases(r.bus, r.seen);

    r.dispatch(make_head(kPost, "/get_only"));

    LT_CHECK_EQ(r.inner.code, std::uint16_t{405});
    LT_CHECK(field_of(r.inner.responded, "Allow") == "GET");
    LT_CHECK(field_of(r.inner.responded, "Content-Type") == "text/plain");
    LT_CHECK(field_of(r.inner.responded, "Content-Length") == "18");
    // before_handler is the consultation point; after_handler fires on
    // the synthesized 405.
    LT_CHECK(r.seen->saw("before_handler"));
    LT_CHECK(r.seen->saw("after_handler"));
LT_END_AUTO_TEST(default_405_with_allow)

// (6) Custom 405 body keeps Allow.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, custom_405_keeps_allow)
    dispatch_round r;
    r.pages.method_not_allowed =
        [](const http::request_head&) -> srv::hook_response {
            return hook_page(405, "custom-not-allowed");
        };
    http::method_set both;
    both.set(http::method_id::get);
    both.set(http::method_id::head);
    LT_CHECK(r.registry.route(both, "/both", text_handler("x")).ok());

    r.dispatch(make_head(kPost, "/both"));

    LT_CHECK_EQ(r.inner.code, std::uint16_t{405});
    LT_CHECK(field_of(r.inner.responded, "Allow") == "GET, HEAD");
    LT_CHECK(field_of(r.inner.responded, "Content-Length") == "18");
LT_END_AUTO_TEST(custom_405_keeps_allow)

// (7) HEAD on a GET+HEAD registration runs the handler (headers-only
// framing is the engine framer's job).
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, head_on_get_head_set)
    dispatch_round r;
    http::method_set both;
    both.set(http::method_id::get);
    both.set(http::method_id::head);
    int handler_runs = 0;
    LT_CHECK(r.registry.route(
        both, "/both", [&handler_runs](exchange& x) -> task<void> {
            ++handler_runs;
            co_await srv::detail::commit_sync_value(
                x, srv::sync_response{http::status::from_code(200),
                                      http::fields(), {}});
        }).ok());

    r.dispatch(make_head(kHead, "/both"));
    LT_CHECK_EQ(handler_runs, 1);
    LT_CHECK_EQ(r.inner.code, std::uint16_t{200});
LT_END_AUTO_TEST(head_on_get_head_set)

// (8) request_received short-circuit: the response is committed, the
// handler never runs, the body is never admitted, after_handler does
// NOT fire, and request_completed still reports success.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, request_received_short_circuit)
    dispatch_round r;
    int handler_runs = 0;
    LT_CHECK(r.registry.route(
        kGet, "/big", [&handler_runs](exchange&) -> task<void> {
            ++handler_runs;
            co_return;
        }).ok());
    (void)r.bus.add<srv::hook_phase::request_received>(
        [](srv::request_received_ctx&) -> srv::hook_action {
            return srv::hook_action::respond_with(hook_page(413, "too big"));
        }).detach();
    record_all_phases(r.bus, r.seen);

    r.dispatch(make_head(kGet, "/big"));

    LT_CHECK_EQ(handler_runs, 0);
    LT_CHECK_EQ(r.inner.admit_calls, 0);
    LT_CHECK_EQ(r.inner.code, std::uint16_t{413});
    LT_CHECK(!r.seen->saw("after_handler"));
    LT_CHECK(r.seen->saw("response_sent"));
    LT_CHECK(r.seen->order.back().rfind("request_completed succeeded=1", 0)
             == 0);
    // The lookup is skipped on this path.
    LT_CHECK(!r.seen->saw("route_resolved"));
LT_END_AUTO_TEST(request_received_short_circuit)

// (9) before_handler short-circuit: the handler never runs and
// after_handler is suppressed (the pinned ~X-Hook shape).
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, before_handler_short_circuit)
    dispatch_round r;
    int handler_runs = 0;
    LT_CHECK(r.registry.route(
        kDelete, "/admin", [&handler_runs](exchange&) -> task<void> {
            ++handler_runs;
            co_return;
        }).ok());
    (void)r.bus.add<srv::hook_phase::before_handler>(
        [](srv::before_handler_ctx&) -> srv::hook_action {
            return srv::hook_action::respond_with(hook_page(403, "hooked403"));
        }).detach();
    record_all_phases(r.bus, r.seen);

    r.dispatch(make_head(kDelete, "/admin"));

    LT_CHECK_EQ(handler_runs, 0);
    LT_CHECK_EQ(r.inner.code, std::uint16_t{403});
    LT_CHECK(!r.seen->saw("after_handler"));
    LT_CHECK(r.seen->saw("response_sent"));
LT_END_AUTO_TEST(before_handler_short_circuit)

// (10) A before_handler-supplied 405 keeps Allow.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, before_handler_supplied_405)
    dispatch_round r;
    LT_CHECK(r.registry.route(kGet, "/get_only", text_handler("x")).ok());
    (void)r.bus.add<srv::hook_phase::before_handler>(
        [](srv::before_handler_ctx& c) -> srv::hook_action {
            if (c.request.request_method
                    == http::method::known(http::method_id::post)) {
                return srv::hook_action::respond_with(hook_page(405, "no"));
            }
            return srv::hook_action::pass();
        }).detach();

    r.dispatch(make_head(kPost, "/get_only"));
    LT_CHECK_EQ(r.inner.code, std::uint16_t{405});
    LT_CHECK(field_of(r.inner.responded, "Allow") == "GET");
    LT_CHECK(field_of(r.inner.responded, "Content-Length") == "2");
LT_END_AUTO_TEST(before_handler_supplied_405)

// (11) A throwing handler: the handler_exception chain is consulted,
// then the default 500; after_handler fires on the exception path.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, handler_throw_default_500)
    dispatch_round r;
    LT_CHECK(r.registry.route(
        kGet, "/boom", [](exchange&) -> task<void> {
            throw std::runtime_error("boom");
        }).ok());
    record_all_phases(r.bus, r.seen);

    r.dispatch(make_head(kGet, "/boom"));

    LT_CHECK(r.seen->saw("handler_exception error=1"));
    LT_CHECK_EQ(r.inner.respond_calls, 1);
    LT_CHECK_EQ(r.inner.code, std::uint16_t{500});
    LT_CHECK(r.seen->saw("after_handler"));
    LT_CHECK(r.seen->saw("response_sent"));
    LT_CHECK(r.seen->order.back().rfind("request_completed succeeded=1", 0)
             == 0);
LT_END_AUTO_TEST(handler_throw_default_500)

// (11b) A handler_exception hook supplies the response.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, handler_exception_supplies)
    dispatch_round r;
    LT_CHECK(r.registry.route(
        kGet, "/boom", [](exchange&) -> task<void> {
            throw std::runtime_error("boom");
        }).ok());
    (void)r.bus.add<srv::hook_phase::handler_exception>(
        [](srv::handler_exception_ctx&) -> srv::hook_action {
            return srv::hook_action::respond_with(hook_page(503, "caught"));
        }).detach();

    r.dispatch(make_head(kGet, "/boom"));
    LT_CHECK_EQ(r.inner.code, std::uint16_t{503});
    LT_CHECK(field_of(r.inner.responded, "Content-Length") == "6");
LT_END_AUTO_TEST(handler_exception_supplies)

// (11c) A throwing handler_exception hook is contained (treated as
// pass): the default 500 lands.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, throwing_exception_hook_contained)
    dispatch_round r;
    LT_CHECK(r.registry.route(
        kGet, "/boom", [](exchange&) -> task<void> {
            throw std::runtime_error("boom");
        }).ok());
    (void)r.bus.add<srv::hook_phase::handler_exception>(
        [](srv::handler_exception_ctx&) -> srv::hook_action {
            throw std::runtime_error("hook blew up");
        }).detach();

    r.dispatch(make_head(kGet, "/boom"));
    LT_CHECK_EQ(r.inner.code, std::uint16_t{500});
    LT_CHECK(r.inner.respond_calls == 1);
LT_END_AUTO_TEST(throwing_exception_hook_contained)

// (11d) A throwing before_handler hook is contained (treated as pass):
// the handler still runs.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, throwing_before_hook_contained)
    dispatch_round r;
    int handler_runs = 0;
    LT_CHECK(r.registry.route(
        kGet, "/ok", [&handler_runs](exchange& x) -> task<void> {
            ++handler_runs;
            co_await srv::detail::commit_sync_value(
                x, srv::sync_response{http::status::from_code(200),
                                      http::fields(), {}});
        }).ok());
    (void)r.bus.add<srv::hook_phase::before_handler>(
        [](srv::before_handler_ctx&) -> srv::hook_action {
            throw std::runtime_error("hook blew up");
        }).detach();

    r.dispatch(make_head(kGet, "/ok"));
    LT_CHECK_EQ(handler_runs, 1);
    LT_CHECK_EQ(r.inner.code, std::uint16_t{200});
LT_END_AUTO_TEST(throwing_before_hook_contained)

// (13) after_handler mutates status and fields on every provenance
// that fires it: handler commit, 404, 405.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, after_handler_mutation)
    dispatch_round r;
    (void)r.bus.add<srv::hook_phase::after_handler>(
        [](srv::after_handler_ctx& c) -> srv::hook_action {
            c.fields.append("X-Hook", "after");
            c.status = http::status::from_code(
                static_cast<std::uint16_t>(c.status.code() == 200 ? 201
                                                                  : c.status.code()));
            return srv::hook_action::pass();
        }).detach();
    LT_CHECK(r.registry.route(kGet, "/hello", text_handler("OK")).ok());

    r.dispatch(make_head(kGet, "/hello"));
    LT_CHECK_EQ(r.inner.code, std::uint16_t{201});
    LT_CHECK(field_of(r.inner.responded, "X-Hook") == "after");

    r.dispatch(make_head(kGet, "/missing"));
    LT_CHECK_EQ(r.inner.code, std::uint16_t{404});
    LT_CHECK(field_of(r.inner.responded, "X-Hook") == "after");

    r.dispatch(make_head(kPost, "/hello"));
    LT_CHECK_EQ(r.inner.code, std::uint16_t{405});
    LT_CHECK(field_of(r.inner.responded, "X-Hook") == "after");
    LT_CHECK(field_of(r.inner.responded, "Allow") == "GET");
LT_END_AUTO_TEST(after_handler_mutation)

// (13b) Zero hooks: the pipeline's commits are byte-identical to the
// pre-hook run_route runner.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, zero_hook_path_identical)
    srv::route_registry registry;
    (void)srv::route_registry::create(budget_with_routes(8), registry);
    LT_CHECK(registry.route(kGet, "/hello", text_handler("OK")).ok());

    const http::request_head head = make_head(kGet, "/hello");
    capturing_sink bare;
    fake::scripted_body_sink bare_responses;
    exchange bare_x(head, &bare, 0, nullptr, &bare_responses);
    manual_executor ex;
    spawn(ex, detail::run_route(registry, bare_x),
          [](task_result<void>) { });
    while (ex.run_pending() > 0) {
    }

    dispatch_round r;
    LT_CHECK(r.registry.route(kGet, "/hello", text_handler("OK")).ok());
    r.dispatch(head);

    LT_CHECK(r.inner.respond_calls == bare.respond_calls);
    LT_CHECK_EQ(r.inner.code, bare.code);
    LT_CHECK(r.inner.responded.entries().size()
             == bare.responded.entries().size());
LT_END_AUTO_TEST(zero_hook_path_identical)

// (12-adjacent) A pre-disconnected exchange: only request_completed
// fires, with succeeded=false and the typed end reason.
LT_BEGIN_AUTO_TEST(request_lifecycle_suite, disconnected_exchange_tail)
    dispatch_round r;
    record_all_phases(r.bus, r.seen);
    const http::request_head head = make_head(kGet, "/hello");
    detail::lifecycle_sink sink(r.inner, r.bus, head);
    exchange x(head, &sink, 0, nullptr, &r.responses);
    (void)x.disconnect(http::outcome_code::connection_closed, "peer gone");
    manual_executor ex;
    bool delivered = false;
    spawn(ex, detail::dispatch_request(r.registry, r.bus, r.pages, sink, x),
          [&delivered](task_result<void> result) {
              delivered = !result.is_exception();
          });
    while (ex.run_pending() > 0) {
    }
    LT_CHECK(delivered);

    LT_CHECK_EQ(r.seen->order.size(), std::size_t{1});
    LT_CHECK(r.seen->order[0].rfind("request_completed succeeded=0", 0) == 0);
    LT_CHECK(r.inner.respond_calls == 0);
LT_END_AUTO_TEST(disconnected_exchange_tail)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
