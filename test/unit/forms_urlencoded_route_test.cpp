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

// TASK-116 step 3: the bounded urlencoded route adapter and the
// streaming read (PRD-V3N-REQ-021/022, DR-V3-003 shape). Driven
// through detail::run_route with the scripted fakes (the route_sync
// convention); the members live in the library, so the suite links
// libhttpserver.la (default LDADD). The suite pins:
//   - decoded fields reach the handler (arrival order, repeats), and
//     the returned value commits with auto Content-Length;
//   - a body past the byte cap answers 413 and the handler never
//     runs; exactly-at-cap succeeds;
//   - a malformed escape answers 400 with the length-framed empty
//     body and the handler never runs;
//   - the v2 content-type gate: a non-matching Content-Type (value
//     end, ';'/whitespace boundary, case-insensitive) still drains
//     under the cap and reaches the handler with EMPTY fields -- even
//     a body that would not decode;
//   - a bodyless POST reaches the handler with empty fields;
//   - a segmented body source decodes identically to a one-shot feed;
//   - a disconnect mid-collect ends quietly (no commit);
//   - the factory refuses a zero cap or an empty handler at
//     registration time;
//   - read_urlencoded: admits under the byte cap, streams segments
//     into identical fields, and surfaces the typed verdicts.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/server/routes.hpp>

#include "./body_sink_fake.hpp"
#include "./body_source_fake.hpp"
#include "./littletest.hpp"

using httpserver::exchange;
using httpserver::exchange_state;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task_result;
namespace http = httpserver::http;
namespace forms = httpserver::forms;
namespace srv = httpserver::server;
namespace detail = httpserver::detail;
namespace fake = httpserver_test;

namespace {

constexpr const char* k_type = "application/x-www-form-urlencoded";

srv::resource_budget budget_with_routes(std::size_t capacity) {
    srv::budget_limits limits;
    limits.set(srv::resource::routes, capacity);
    return srv::resource_budget::root(limits);
}

std::vector<std::byte> bytes(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

http::request_head make_head(const char* content_type) {
    http::request_head head;
    head.raw_target = "/echo?query=1";
    head.route_path = "/echo";
    head.request_method = http::method::known(http::method_id::post);
    head.request_protocol = http::protocol::http_1_1;
    if (content_type != nullptr) {
        head.head_fields.append("Content-Type", content_type);
    }
    return head;
}

// Decision oracle keeping the committed fields whole (the framing
// assertions need the entries).
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

// What one form handler saw.
struct form_observed {
    int invoked = 0;
    std::string rendered;  // entries as name=value joined by ';'
};

std::string render_fields(const forms::form_fields& fields) {
    std::string out;
    bool first = true;
    for (const std::pair<std::string, std::string>& e : fields.entries()) {
        if (!first) out += ";";
        first = false;
        out += e.first + "=" + e.second;
    }
    return out;
}

// Registers /echo as a form route echoing the decoded entries.
http::outcome install_echo_route(srv::route_registry& registry,
                                 form_observed& seen,
                                 const forms::urlencoded_limits& limits) {
    return registry.route(
        http::method::known(http::method_id::post), "/echo",
        forms::make_urlencoded_route(
            limits,
            [&seen](const http::request_head&,
                    const forms::form_fields& fields) -> srv::sync_response {
                ++seen.invoked;
                seen.rendered = render_fields(fields);
                srv::sync_response out;
                out.status = http::status::from_code(200);
                out.fields.append("Content-Type", "text/plain");
                out.body = bytes(seen.rendered);
                return out;
            }));
}

void drain(manual_executor& ex) {
    while (ex.run_pending() > 0) {
    }
}

bool drain_until(manual_executor& ex, int& flag, int spins) {
    for (int i = 0; i < spins && flag == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ex.run_pending();
    }
    return flag != 0;
}

// Runs read_urlencoded over one staged body; ""-returning verdict
// probes come from @p probe.
std::string stream_read_diff(const std::vector<std::string>& segments,
                             const forms::urlencoded_limits& limits) {
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    for (const std::string& segment : segments) {
        source.stage(bytes(segment));
    }
    source.stage_end();
    exchange x(make_head(k_type), &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    forms::form_read read;
    spawn(ex, forms::read_urlencoded(x, limits),
          [&](task_result<forms::form_read> r) {
              ++deliveries;
              if (r.has_value()) read = r.value();
          });
    drain(ex);
    if (deliveries != 1) return "deliveries " + std::to_string(deliveries);
    if (sink.admit_calls != 1) return "admit calls missing";
    if (sink.admitted_bytes != limits.max_total_bytes) {
        return "admission did not carry the byte cap";
    }
    if (sink.respond_calls != 0) return "the read committed a response";
    return read.ok() ? "ok:" + render_fields(read.fields)
                     : "code:" + std::to_string(
                           static_cast<int>(read.status.code()));
}

// Segmented happy path: identical fields to a one-shot feed.
std::string stream_segments_diff() {
    const std::string verdict = stream_read_diff(
        {"a=", "1&b=tw", "%6F"}, forms::urlencoded_limits{});
    if (verdict != "ok:a=1;b=two") return "verdict was " + verdict;
    return "";
}

// A malformed escape surfaces the typed 400 verdict with no fields.
std::string stream_malformed_diff() {
    const std::string verdict = stream_read_diff(
        {"a=1&b=%G1"}, forms::urlencoded_limits{});
    if (verdict != "code:1") {
        return "verdict was " + verdict + " (1 = invalid_argument)";
    }
    return "";
}

// The decoder's own byte counter trips one past the cap.
std::string stream_overcap_diff() {
    const std::string verdict = stream_read_diff(
        {"a=1&b="}, forms::urlencoded_limits{5, 64});
    if (verdict != "code:3") {
        return "verdict was " + verdict + " (3 = limit_exceeded)";
    }
    return "";
}

}  // namespace

LT_BEGIN_SUITE(forms_urlencoded_route_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(forms_urlencoded_route_suite)

// (1) Decoded fields reach the handler in arrival order and the
// returned value commits with auto Content-Length.
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_serves_decoded_fields)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_echo_route(registry, seen,
                                forms::urlencoded_limits{}).ok());

    source.stage(bytes("a=1&b=two"));
    source.stage_end();
    exchange x(make_head(k_type), &sink, 0, &source, &responses);
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
    LT_CHECK(seen.rendered == "a=1;b=two");
    LT_CHECK_EQ(sink.admit_calls, 1);
    LT_CHECK_EQ(sink.admitted_bytes, static_cast<std::uint64_t>(65536));
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
    LT_CHECK(sink.responded.first("content-type").value_or("")
             == "text/plain");
    LT_CHECK(sink.responded.first("content-length").value_or("") == "9");
    LT_CHECK_EQ(responses.produced(), static_cast<std::size_t>(9));
    LT_CHECK(responses.ended());
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(form_route_serves_decoded_fields)

// (2) Repeated names append in arrival order (the first is what a
// flat lookup sees).
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_repeats_in_order)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_echo_route(registry, seen,
                                forms::urlencoded_limits{}).ok());

    source.stage(bytes("k=1&j=x&k=2"));
    source.stage_end();
    exchange x(make_head(k_type), &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.rendered == "k=1;j=x;k=2");
LT_END_AUTO_TEST(form_route_repeats_in_order)

// (3) A body past the byte cap: the handler never runs and exactly
// one 413 commits with no writer traffic; exactly-at-cap succeeds.
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_over_cap_answers_413)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    form_observed seen;
    const forms::urlencoded_limits capped{5, 64};
    LT_CHECK(install_echo_route(registry, seen, capped).ok());

    // Exactly at the cap: served.
    {
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes("a=1&b"));
        source.stage_end();
        exchange x(make_head(k_type), &sink, 0, &source, &responses);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(seen.invoked, 1);
        LT_CHECK(seen.rendered == "a=1;b=");
        LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
    }

    // One byte past the cap: refused before the handler.
    seen.invoked = 0;
    {
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes("a=1&b="));
        source.stage_end();
        exchange x(make_head(k_type), &sink, 0, &source, &responses);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(seen.invoked, 0);
        LT_CHECK_EQ(sink.admit_calls, 1);
        LT_CHECK_EQ(sink.admitted_bytes, static_cast<std::uint64_t>(5));
        LT_CHECK_EQ(sink.respond_calls, 1);
        LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(413));
        LT_CHECK_EQ(sink.responded.size(), static_cast<std::size_t>(0));
        LT_CHECK_EQ(responses.push_calls(), 0);
        LT_CHECK_EQ(responses.end_calls(), 0);
        LT_CHECK_EQ(sink.abort_calls, 0);
        LT_CHECK(x.state() == exchange_state::responded);
    }
LT_END_AUTO_TEST(form_route_over_cap_answers_413)

// (4) A malformed escape: the handler never runs and the 400 carries
// the length-framed empty body.
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_malformed_answers_400)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_echo_route(registry, seen,
                                forms::urlencoded_limits{}).ok());

    source.stage(bytes("a=1&b=%G1"));
    source.stage_end();
    exchange x(make_head(k_type), &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(seen.invoked, 0);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(400));
    LT_CHECK(sink.responded.first("content-length").value_or("") == "0");
    LT_CHECK_EQ(responses.push_calls(), 0);
    LT_CHECK_EQ(responses.end_calls(), 0);
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(form_route_malformed_answers_400)

// (5) The v2 content-type gate: a non-matching type drains under the
// cap and reaches the handler with EMPTY fields -- even a body that
// would not decode (v2 ran no form processing there either).
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_wrong_type_empty_fields)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    form_observed seen;
    LT_CHECK(install_echo_route(registry, seen,
                                forms::urlencoded_limits{}).ok());

    // A would-be-malformed body under a text/plain type: served, not
    // 400'd.
    {
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes("a=1&b=%G1"));
        source.stage_end();
        exchange x(make_head("text/plain"), &sink, 0, &source, &responses);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(seen.invoked, 1);
        LT_CHECK(seen.rendered.empty());
        LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
        LT_CHECK(sink.responded.first("content-length").value_or("") == "0");
    }

    // A media type that only LOOKS like the urlencoded one.
    seen.invoked = 0;
    {
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes("a=1"));
        source.stage_end();
        exchange x(make_head("application/x-www-form-urlencoded2"),
                   &sink, 0, &source, &responses);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(seen.invoked, 1);
        LT_CHECK(seen.rendered.empty());
    }
LT_END_AUTO_TEST(form_route_wrong_type_empty_fields)

// (6) Matching content types: parameters after the media type are
// allowed and the match is case-insensitive.
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_type_boundaries)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    form_observed seen;
    LT_CHECK(install_echo_route(registry, seen,
                                forms::urlencoded_limits{}).ok());

    for (const char* type : {k_type,
                             "application/x-www-form-urlencoded; charset=utf-8",
                             "Application/X-WWW-Form-URLEncoded"}) {
        seen.invoked = 0;
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes("a=1&b=two"));
        source.stage_end();
        exchange x(make_head(type), &sink, 0, &source, &responses);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(seen.invoked, 1);
        LT_CHECK(seen.rendered == "a=1;b=two");
    }
LT_END_AUTO_TEST(form_route_type_boundaries)

// (7) A bodyless POST: admission accepts, the (empty) body drains,
// and the handler sees empty fields.
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_serves_bodyless_post)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_echo_route(registry, seen,
                                forms::urlencoded_limits{}).ok());

    fake::scripted_body_source* source = nullptr;  // the engine's bodyless shape
    exchange x(make_head(k_type), &sink, 0, source, &responses);
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
    LT_CHECK(seen.rendered.empty());
    LT_CHECK_EQ(sink.admit_calls, 1);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
    LT_CHECK(sink.responded.first("content-length").value_or("") == "0");
    LT_CHECK_EQ(responses.push_calls(), 0);
    LT_CHECK_EQ(sink.abort_calls, 0);
LT_END_AUTO_TEST(form_route_serves_bodyless_post)

// (8) A segmented body source decodes identically to a one-shot feed
// (the escape accumulator survives segment boundaries end to end).
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_segmented_source)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_echo_route(registry, seen,
                                forms::urlencoded_limits{}).ok());

    source.stage(bytes("a="));
    source.stage(bytes("1&b=tw"));
    source.stage(bytes("%6F"));
    source.stage_end();
    exchange x(make_head(k_type), &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(seen.invoked, 1);
    LT_CHECK(seen.rendered == "a=1;b=two");
LT_END_AUTO_TEST(form_route_segmented_source)

// (9) A disconnect mid-collect ends the route quietly: no response
// commit, no abort (the connection is already gone).
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_disconnect_quiet)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_echo_route(registry, seen,
                                forms::urlencoded_limits{}).ok());

    exchange x(make_head(k_type), &sink, 0, &source, &responses);
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
    LT_CHECK_EQ(seen.invoked, 0);
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK_EQ(responses.push_calls(), 0);
LT_END_AUTO_TEST(form_route_disconnect_quiet)

// (10) The factory refuses a zero cap or an empty handler at
// registration time (the same rule urlencoded_limits::create and
// native_server::route_sync encode for the sync adapter).
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, form_route_factory_validates)
    const auto quiet = [](const http::request_head&,
                          const forms::form_fields&) -> srv::sync_response {
        return srv::sync_response{};
    };
    bool threw = false;
    try {
        srv::route_handler h = forms::make_urlencoded_route(
            forms::urlencoded_limits{0, 64}, quiet);
        static_cast<void>(h);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);

    threw = false;
    try {
        srv::route_handler h = forms::make_urlencoded_route(
            forms::urlencoded_limits{64, 0}, quiet);
        static_cast<void>(h);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);

    threw = false;
    try {
        srv::route_handler h = forms::make_urlencoded_route(
            forms::urlencoded_limits{}, forms::form_route_handler{});
        static_cast<void>(h);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);

    threw = false;
    try {
        srv::route_handler h = forms::make_urlencoded_route(
            forms::urlencoded_limits{}, quiet);
        static_cast<void>(h);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(!threw);
LT_END_AUTO_TEST(form_route_factory_validates)

// (11) The streaming read: admits the body under the byte cap, feeds
// segments into identical fields, and surfaces the typed verdicts.
LT_BEGIN_AUTO_TEST(forms_urlencoded_route_suite, read_urlencoded_streams_and_rejects)
    LT_CHECK(stream_segments_diff().empty());
    LT_CHECK(stream_malformed_diff().empty());
    LT_CHECK(stream_overcap_diff().empty());
LT_END_AUTO_TEST(read_urlencoded_streams_and_rejects)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
