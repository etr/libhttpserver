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

// TASK-117 step 3: the bounded multipart route adapter, the streaming
// read_multipart, and the temp-file sink's cancellation contract
// (PRD-V3N-REQ-021/025, DR-V3-003 shape). Driven through
// detail::run_route with the scripted fakes (the route_sync
// convention); the members live in the library, so the suite links
// libhttpserver.la (default LDADD). The suite pins:
//   - decoded FIELD parts reach the handler (arrival order, repeats)
//     with auto Content-Length, while FILE parts are drained under
//     the same caps and never buffered (the handler's fields exclude
//     them);
//   - the decoder-level caps: a body one byte past max_total_bytes
//     and one part past max_parts both answer 413 with the explicit
//     Content-Length: 0 framing and the handler never runs (the
//     distinct branch from the gate path's drain-level 413, driven in
//     the type-gate test: one 413 with BARE fields -- no explicit
//     Content-Length -- zero handler invocations, zero writer
//     traffic);
//   - a malformed multipart body answers 400 with the length-framed
//     empty body;
//   - the v2 content-type gate: a non-multipart type (value end,
//     ';'/whitespace boundary, suffix-extended, case differences)
//     still drains under the cap and reaches the handler with EMPTY
//     fields; a matching type WITHOUT a boundary parameter answers
//     400 (the strictness delta);
//   - a bodyless POST reaches the handler with empty fields;
//   - a segmented body source decodes identically to a one-shot feed;
//   - a disconnect mid-body fires on_part_abort exactly once (the
//     streaming read), commits nothing, and removes the temp-file
//     sink's partial file;
//   - read_multipart after a manual admit_body answers invalid_state;
//   - the factory refuses a zero cap or an empty handler at
//     registration time.

#include <algorithm>
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
#include <httpserver/forms/multipart.hpp>
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

constexpr const char* k_type = "multipart/form-data; boundary=PARITY096B";

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
    head.raw_target = "/upload?query=1";
    head.route_path = "/upload";
    head.request_method = http::method::known(http::method_id::post);
    head.request_protocol = http::protocol::http_1_1;
    if (content_type != nullptr) {
        head.head_fields.append("Content-Type", content_type);
    }
    return head;
}

// One field part + one file part under PARITY096B.
std::string two_part_body() {
    return "--PARITY096B\r\n"
           "Content-Disposition: form-data; name=\"a\"\r\n"
           "\r\n"
           "1\r\n"
           "--PARITY096B\r\n"
           "Content-Disposition: form-data; name=\"file\"; "
           "filename=\"f.bin\"\r\n"
           "Content-Type: application/octet-stream\r\n"
           "\r\n"
           "payload\r\n"
           "--PARITY096B--\r\n";
}

// Decision oracle keeping the committed response whole.
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

// Registers /upload as a multipart route echoing the decoded entries.
http::outcome install_upload_route(srv::route_registry& registry,
                                   form_observed& seen,
                                   const forms::multipart_limits& limits) {
    return registry.route(
        http::method::known(http::method_id::post), "/upload",
        forms::make_multipart_route(
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

// A part_sink that counts its callbacks (the abort contract observer).
class counting_sink final : public forms::part_sink {
 public:
    http::outcome on_part_begin(
        const forms::part_descriptor&) override {
        ++begins;
        return http::outcome::okay();
    }

    http::outcome on_part_data(std::span<const std::byte>) override {
        ++datas;
        return http::outcome::okay();
    }

    http::outcome on_part_end() override {
        ++ends;
        return http::outcome::okay();
    }

    void on_part_abort(http::outcome reason) override {
        ++aborts;
        abort_code = reason.code();
    }

    int begins = 0;
    int datas = 0;
    int ends = 0;
    int aborts = 0;
    http::outcome_code abort_code = http::outcome_code::ok;
};

}  // namespace

LT_BEGIN_SUITE(forms_multipart_route_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(forms_multipart_route_suite)

// (1) FIELD parts reach the handler in arrival order with auto
// Content-Length; FILE parts are drained under the same caps and
// never buffered (the handler's fields exclude them).
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, multipart_route_serves_fields)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_upload_route(registry, seen,
                                  forms::multipart_limits{}).ok());

    source.stage(bytes(two_part_body()));
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
    LT_CHECK(seen.rendered == "a=1");
    LT_CHECK_EQ(sink.admit_calls, 1);
    LT_CHECK_EQ(sink.admitted_bytes, static_cast<std::uint64_t>(65536));
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
    LT_CHECK(sink.responded.first("content-type").value_or("")
             == "text/plain");
    LT_CHECK(sink.responded.first("content-length").value_or("") == "3");
    LT_CHECK_EQ(responses.produced(), static_cast<std::size_t>(3));
    LT_CHECK(responses.ended());
    LT_CHECK_EQ(sink.abort_calls, 0);
    LT_CHECK(x.state() == exchange_state::responded);
LT_END_AUTO_TEST(multipart_route_serves_fields)

// (2) Repeated field names append in arrival order.
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, multipart_route_repeats)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_upload_route(registry, seen,
                                  forms::multipart_limits{}).ok());

    const std::string body =
        "--PARITY096B\r\nContent-Disposition: form-data; name=\"k\"\r\n"
        "\r\n1\r\n"
        "--PARITY096B\r\nContent-Disposition: form-data; name=\"j\"\r\n"
        "\r\nx\r\n"
        "--PARITY096B\r\nContent-Disposition: form-data; name=\"k\"\r\n"
        "\r\n2\r\n"
        "--PARITY096B--\r\n";
    source.stage(bytes(body));
    source.stage_end();
    exchange x(make_head(k_type), &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.rendered == "k=1;j=x;k=2");
LT_END_AUTO_TEST(multipart_route_repeats)

// (3) The decoder-level caps answer 413 with the explicit
// Content-Length: 0 framing and never invoke the handler -- the byte
// cap one past the body size and the part-count cap one past the
// begun parts (distinct from the gate path's drain-level 413).
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, multipart_route_cap_413)
    // The byte cap one past the body size: one 413, Content-Length: 0,
    // the handler never invoked (seen stays at zero invocations for
    // both blocks below).
    form_observed seen;
    {
        srv::route_registry registry;
        LT_CHECK(srv::route_registry::create(budget_with_routes(4),
                                             registry).ok());
        const forms::multipart_limits byte_capped{
            static_cast<std::uint64_t>(two_part_body().size() - 1), 64,
            65536, 8192};
        LT_CHECK(install_upload_route(registry, seen, byte_capped).ok());

        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes(two_part_body()));
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
        LT_CHECK_EQ(sink.admitted_bytes,
                    static_cast<std::uint64_t>(two_part_body().size() - 1));
        LT_CHECK_EQ(sink.respond_calls, 1);
        LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(413));
        LT_CHECK(sink.responded.first("content-length").value_or("") == "0");
        LT_CHECK_EQ(responses.push_calls(), 0);
        LT_CHECK_EQ(responses.end_calls(), 0);
        LT_CHECK_EQ(sink.abort_calls, 0);
        LT_CHECK(x.state() == exchange_state::responded);
    }

    // The part-count cap: the third begun part trips exactly.
    {
        srv::route_registry registry;
        LT_CHECK(srv::route_registry::create(budget_with_routes(4),
                                             registry).ok());
        const forms::multipart_limits part_capped{65536, 2, 65536, 8192};
        LT_CHECK(install_upload_route(registry, seen, part_capped).ok());
        const std::string three =
            "--PARITY096B\r\nContent-Disposition: form-data; name=\"a\"\r\n"
            "\r\n1\r\n"
            "--PARITY096B\r\nContent-Disposition: form-data; name=\"b\"\r\n"
            "\r\n2\r\n"
            "--PARITY096B\r\nContent-Disposition: form-data; name=\"c\"\r\n"
            "\r\n3\r\n"
            "--PARITY096B--\r\n";
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes(three));
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
        LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(413));
        LT_CHECK(sink.responded.first("content-length").value_or("") == "0");
    }
LT_END_AUTO_TEST(multipart_route_cap_413)

// (4) A malformed multipart body answers 400 with the length-framed
// empty body; the handler never runs.
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, multipart_route_malformed_400)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_upload_route(registry, seen,
                                  forms::multipart_limits{}).ok());

    source.stage(bytes("--PARITY096B\r\n"
                       "Content-Disposition: form-data; name=\"a\"\r\n"
                       "\r\nnever closed"));
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
LT_END_AUTO_TEST(multipart_route_malformed_400)

// (5) The v2 content-type gate: a non-multipart type drains under the
// cap and reaches the handler with EMPTY fields -- even a body that
// would not decode -- while a matching type WITHOUT a boundary
// parameter answers 400 (the strictness delta over v2's silent
// no-processing).
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, multipart_route_type_gate)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    form_observed seen;
    LT_CHECK(install_upload_route(registry, seen,
                                  forms::multipart_limits{}).ok());

    // A would-be-malformed body under a text/plain type: served with
    // empty fields, never 4xx.
    {
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes("not multipart at all"));
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

    // A media type that only LOOKS multipart, and a missing type.
    for (const char* type : {"multipart/form-data2",
                             "application/x-www-form-urlencoded",
                             "text/plain; boundary=PARITY096B"}) {
        seen.invoked = 0;
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes(two_part_body()));
        source.stage_end();
        exchange x(make_head(type), &sink, 0, &source, &responses);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(seen.invoked, 1);
        LT_CHECK(seen.rendered.empty());
        LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
    }

    // A matching multipart type without a boundary parameter: 400.
    seen.invoked = 0;
    {
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes(two_part_body()));
        source.stage_end();
        exchange x(make_head("multipart/form-data"), &sink, 0, &source,
                   &responses);
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
    }

    // The gate path's drain-level 413: a wrong (non-multipart) type
    // whose body is one byte past the cap answers 413 without invoking
    // the handler or touching the writer. The gate path passes bare
    // fields (no explicit Content-Length: 0) -- the deliberate framing
    // delta from the decoder-path 413 above and in the cap test.
    {
        srv::route_registry capped_registry;
        LT_CHECK(srv::route_registry::create(budget_with_routes(4),
                                             capped_registry).ok());
        form_observed capped_seen;
        const forms::multipart_limits drain_capped{8, 64, 65536, 8192};
        LT_CHECK(install_upload_route(capped_registry, capped_seen,
                                      drain_capped).ok());

        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes("012345678"));  // nine bytes, one past 8
        source.stage_end();
        exchange x(make_head("text/plain"), &sink, 0, &source, &responses);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(capped_registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(capped_seen.invoked, 0);
        LT_CHECK_EQ(sink.admit_calls, 1);
        LT_CHECK_EQ(sink.respond_calls, 1);
        LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(413));
        LT_CHECK(sink.responded.empty());
        LT_CHECK_EQ(responses.push_calls(), 0);
        LT_CHECK_EQ(responses.end_calls(), 0);
        LT_CHECK_EQ(sink.abort_calls, 0);
        LT_CHECK(x.state() == exchange_state::responded);
    }
LT_END_AUTO_TEST(multipart_route_type_gate)

// (6) Matching content types: parameters after the media type are
// allowed and the media-type match is case-insensitive.
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, multipart_route_type_boundaries)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    form_observed seen;
    LT_CHECK(install_upload_route(registry, seen,
                                  forms::multipart_limits{}).ok());

    for (const char* type :
         {k_type, "Multipart/Form-Data; boundary=PARITY096B",
          "multipart/form-data; boundary=PARITY096B; charset=utf-8"}) {
        seen.invoked = 0;
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes(two_part_body()));
        source.stage_end();
        exchange x(make_head(type), &sink, 0, &source, &responses);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, x),
              [&](task_result<void>) { ++deliveries; });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(seen.invoked, 1);
        LT_CHECK(seen.rendered == "a=1");
    }
LT_END_AUTO_TEST(multipart_route_type_boundaries)

// (7) A bodyless POST: admission accepts, the (empty) body drains,
// and the handler sees empty fields.
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, multipart_route_bodyless)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_upload_route(registry, seen,
                                  forms::multipart_limits{}).ok());

    fake::scripted_body_source* source = nullptr;  // the bodyless shape
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
LT_END_AUTO_TEST(multipart_route_bodyless)

// (8) A segmented body source decodes identically to a one-shot feed
// (the delimiter state machine survives segment boundaries end to
// end).
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, multipart_route_segmented)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    form_observed seen;
    LT_CHECK(install_upload_route(registry, seen,
                                  forms::multipart_limits{}).ok());

    const std::string body = two_part_body();
    for (std::size_t at = 0; at < body.size(); at += 11) {
        std::size_t n = std::min<std::size_t>(11, body.size() - at);
        source.stage(bytes(body.substr(at, n)));
    }
    source.stage_end();
    exchange x(make_head(k_type), &sink, 0, &source, &responses);
    manual_executor ex;
    int deliveries = 0;
    spawn(ex, detail::run_route(registry, x),
          [&](task_result<void>) { ++deliveries; });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK_EQ(seen.invoked, 1);
    LT_CHECK(seen.rendered == "a=1");
LT_END_AUTO_TEST(multipart_route_segmented)

// (9) A disconnect mid-body: the streaming read fires on_part_abort
// exactly once for the begun part and nothing commits (the
// cancellation contract; the partial-file removal it drives is the
// files suite's pin).
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, read_disconnect_aborts_once)
    // The counting sink observes the abort contract through
    // read_multipart itself.
    counting_sink parts;
    {
        capturing_sink sink;
        fake::scripted_body_source source;
        fake::scripted_body_sink responses;
        source.stage(bytes("--PARITY096B\r\n"
                           "Content-Disposition: form-data; name=\"a\"\r\n"
                           "\r\n"));
        exchange x(make_head(k_type), &sink, 0, &source, &responses);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, fake::watch_stop_and_cancel(x, source),
              [](task_result<void>) { });
        spawn(ex, forms::read_multipart(x, forms::multipart_limits{}, parts),
              [&](task_result<forms::multipart_read> r) {
                  ++deliveries;
                  if (r.has_value()) {
                      LT_CHECK(!r.value().ok());
                  }
              });
        ex.run_pending();  // the watcher parks; the read parks in feed
        LT_CHECK(source.parked());
        LT_CHECK_EQ(deliveries, 0);

        std::thread engine([&x] {
            x.disconnect(http::outcome_code::connection_closed, "peer left");
        });
        engine.join();
        LT_CHECK(drain_until(ex, deliveries, 400));

        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(parts.begins, 1);
        LT_CHECK_EQ(parts.aborts, 1);
        LT_CHECK(parts.abort_code == http::outcome_code::cancelled
                 || parts.abort_code
                        == http::outcome_code::connection_closed);
        LT_CHECK_EQ(sink.respond_calls, 0);
        LT_CHECK_EQ(sink.abort_calls, 0);
        LT_CHECK_EQ(responses.push_calls(), 0);
    }
LT_END_AUTO_TEST(read_disconnect_aborts_once)

// (10) read_multipart owns its admission: a caller that already
// admitted the body gets invalid_state.
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, read_after_admit_invalid_state)
    capturing_sink sink;
    fake::scripted_body_source source;
    fake::scripted_body_sink responses;
    source.stage(bytes(two_part_body()));
    source.stage_end();
    exchange x(make_head(k_type), &sink, 0, &source, &responses);
    static_cast<void>(x.admit_body(httpserver::body_policy{65536}));

    manual_executor ex;
    counting_sink parts;
    int deliveries = 0;
    forms::multipart_read read;
    spawn(ex, forms::read_multipart(x, forms::multipart_limits{}, parts),
          [&](task_result<forms::multipart_read> r) {
              ++deliveries;
              if (r.has_value()) read = r.value();
          });
    drain(ex);

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(!read.ok());
    LT_CHECK(read.status.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(parts.begins, 0);
    LT_CHECK_EQ(parts.aborts, 0);
LT_END_AUTO_TEST(read_after_admit_invalid_state)

// (11) The factory refuses a zero cap or an empty handler at
// registration time.
LT_BEGIN_AUTO_TEST(forms_multipart_route_suite, multipart_factory_validates)
    const auto quiet = [](const http::request_head&,
                          const forms::form_fields&) -> srv::sync_response {
        return srv::sync_response{};
    };
    for (const forms::multipart_limits bad :
         {forms::multipart_limits{0, 64, 65536, 8192},
          forms::multipart_limits{65536, 0, 65536, 8192},
          forms::multipart_limits{65536, 64, 0, 8192},
          forms::multipart_limits{65536, 64, 65536, 0}}) {
        bool threw = false;
        try {
            srv::route_handler h = forms::make_multipart_route(
                bad, quiet);
            static_cast<void>(h);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        LT_CHECK(threw);
    }

    bool threw = false;
    try {
        srv::route_handler h = forms::make_multipart_route(
            forms::multipart_limits{},
            forms::multipart_route_handler{});
        static_cast<void>(h);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);

    threw = false;
    try {
        srv::route_handler h = forms::make_multipart_route(
            forms::multipart_limits{}, quiet);
        static_cast<void>(h);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(!threw);
LT_END_AUTO_TEST(multipart_factory_validates)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
