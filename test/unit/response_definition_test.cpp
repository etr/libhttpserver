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

// TASK-112: immutable reusable response definitions and overlays
// (PRD-V3N-REQ-026/028/029/030, DR-V3-005). A definition is an
// immutable value built once and sent any number of times; a send
// borrows it plus one per-request overlay and never mutates either.
// The suite pins, in layered steps:
//   S1 - the validating factories (wire-safe base fields, framing
//        self-consistency, auto Content-Length pin, immutability and
//        copy-sharing of the frozen state);
//   S2 - the overlay merge (ordered append after the base fields,
//        pre-commit validation) and the send path for owned bytes;
//   S3 - the reopenable file and source-factory body cursors;
//   S4 - concurrent sends of one definition staying independent.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/response_definition.hpp>

#include "./body_sink_fake.hpp"
#include "./littletest.hpp"

using httpserver::body_chunk;
using httpserver::body_factory;
using httpserver::body_producer;
using httpserver::exchange;
using httpserver::exchange_state;
using httpserver::manual_executor;
using httpserver::response_definition;
using httpserver::response_overlay;
using httpserver::send_definition;
using httpserver::send_report;
using httpserver::spawn;
using httpserver::task_result;
namespace http = httpserver::http;
namespace fake = httpserver_test;

namespace {

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

// A factory source whose producers yield `payload` once and end.
body_factory repeating_producer(const std::string& payload) {
    return [payload]() -> body_producer {
        const std::vector<std::byte> owned = bytes(payload);
        return [owned, sent = false]() mutable -> body_chunk {
            if (sent) {
                return body_chunk{http::outcome::okay(), {}, true};
            }
            sent = true;
            return body_chunk{
                http::outcome::okay(),
                std::span<const std::byte>(owned.data(), owned.size()),
                false};
        };
    };
}

http::fields base_fields() {
    http::fields f;
    f.append("Content-Type", "text/plain");
    return f;
}

http::request_head make_head() {
    http::request_head head;
    head.raw_target = "/things";
    head.route_path = "/things";
    head.request_method = http::method::known(http::method_id::get);
    head.request_protocol = http::protocol::http_1_1;
    return head;
}

// Decision oracle keeping the committed fields whole (the merge-order
// assertions need the entries, not just counts).
class capturing_sink final : public httpserver::detail::exchange_sink {
 public:
    void on_admit(const httpserver::body_policy&) override {
        ++admit_calls;
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
};

// Runs one send to completion on `ex` (no parking: the rig's sink
// capacity always exceeds the payloads) and returns the report.
send_report run_send(exchange& x, manual_executor& ex,
                     const response_definition& def,
                     const response_overlay& overlay) {
    send_report seen;
    spawn(ex, send_definition(x, def, overlay),
          [&](task_result<send_report> r) {
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    return seen;
}

}  // namespace

LT_BEGIN_SUITE(response_definition_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(response_definition_suite)

// (S1.1) owned_bytes pins Content-Length to the body size when the
// fields carry neither framing field, leaving the caller's object
// untouched (the pin lands on the frozen copy only).
LT_BEGIN_AUTO_TEST(response_definition_suite, owned_bytes_pins_length_when_unframed)
    response_definition def;
    http::fields f = base_fields();
    const http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), f, bytes("hi"), def);

    LT_CHECK(made.ok());
    LT_CHECK(def.valid());
    LT_CHECK(def.kind() == response_definition::source_kind::owned_bytes);
    LT_CHECK_EQ(def.status().code(), 200);
    LT_CHECK_EQ(def.fields().size(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(def.fields().first("content-length").value_or("") == "2");
    LT_CHECK(def.fields().first("content-type").value_or("")
             == "text/plain");
    LT_CHECK_EQ(f.count("content-length"), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(owned_bytes_pins_length_when_unframed)

// (S1.2) an empty owned body pins "0", keeping the framing honest.
LT_BEGIN_AUTO_TEST(response_definition_suite, owned_bytes_empty_body_pins_zero)
    response_definition def;
    const http::outcome made = response_definition::owned_bytes(
        http::status::from_code(204), http::fields(), {}, def);

    LT_CHECK(made.ok());
    LT_CHECK(def.valid());
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(def.fields().first("content-length").value_or("") == "0");
LT_END_AUTO_TEST(owned_bytes_empty_body_pins_zero)

// (S1.3) an explicit Content-Length passes through verbatim (no
// re-pinning, no second occurrence).
LT_BEGIN_AUTO_TEST(response_definition_suite, owned_bytes_explicit_length_kept)
    response_definition def;
    http::fields f;
    f.append("Content-Length", "42");
    const http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), f, bytes("hi"), def);

    LT_CHECK(made.ok());
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(def.fields().first("content-length").value_or("") == "42");
LT_END_AUTO_TEST(owned_bytes_explicit_length_kept)

// (S1.4) a Transfer-Encoding in the base fields is respected: no
// Content-Length is pinned beside it.
LT_BEGIN_AUTO_TEST(response_definition_suite, owned_bytes_te_respected)
    response_definition def;
    http::fields f;
    f.append("Transfer-Encoding", "chunked");
    const http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), f, bytes("hi"), def);

    LT_CHECK(made.ok());
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(0));
    LT_CHECK_EQ(def.fields().count("transfer-encoding"),
                static_cast<std::size_t>(1));
LT_END_AUTO_TEST(owned_bytes_te_respected)

// (S1.5) every factory rejects an invalid status and leaves `out`
// untouched (the pre-seeded definition survives the failed call).
LT_BEGIN_AUTO_TEST(response_definition_suite, factories_reject_invalid_status)
    const http::status bad = http::status::from_code(0);

    response_definition seeded;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("x"),
        seeded).ok());

    response_definition def = seeded;
    http::outcome made = response_definition::owned_bytes(
        bad, http::fields(), bytes("x"), def);
    LT_CHECK(!made.ok());
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!made.message().empty());
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");

    def = seeded;
    made = response_definition::reopen_file(
        bad, http::fields(), "/tmp/somewhere", def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");

    def = seeded;
    made = response_definition::factory(
        bad, http::fields(), repeating_producer("x"), def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");
LT_END_AUTO_TEST(factories_reject_invalid_status)

// (S1.6) a base field name outside the RFC 9110 token set is rejected
// before the definition exists.
LT_BEGIN_AUTO_TEST(response_definition_suite, factories_reject_non_token_name)
    http::fields f;
    f.append("Bad Name", "v");

    response_definition seeded;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("x"),
        seeded).ok());

    response_definition def = seeded;
    http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), f, bytes("x"), def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!made.message().empty());

    def = seeded;
    made = response_definition::reopen_file(
        http::status::from_code(200), f, "/tmp/somewhere", def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);

    def = seeded;
    made = response_definition::factory(
        http::status::from_code(200), f, repeating_producer("x"), def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(def.fields().count("bad name"), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(factories_reject_non_token_name)

// (S1.7) control bytes in base field values are rejected; HTAB, legal
// interior OWS, passes.
LT_BEGIN_AUTO_TEST(response_definition_suite, factories_reject_ctl_values)
    for (const std::string value :
         {std::string("a\rb"), std::string("a\nb"),
          std::string("a\x01b"), std::string("a\x7f" "b")}) {
        http::fields f;
        f.append("X-Note", value);
        response_definition def;
        const http::outcome made = response_definition::owned_bytes(
            http::status::from_code(200), f, bytes("x"), def);
        LT_CHECK(made.code() == http::outcome_code::invalid_argument);
        LT_CHECK(!def.valid());
    }

    http::fields htab;
    htab.append("X-Note", "a\tb");
    response_definition ok_def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), htab, bytes("x"), ok_def).ok());
    LT_CHECK(ok_def.fields().first("x-note").value_or("") == "a\tb");
LT_END_AUTO_TEST(factories_reject_ctl_values)

// (S1.8) framing self-consistency of the base fields: a duplicate
// Content-Length and a Transfer-Encoding beside a Content-Length are
// both rejected (mirroring the engine's response-mode gate).
LT_BEGIN_AUTO_TEST(response_definition_suite, factories_reject_framing_conflicts)
    http::fields dup;
    dup.append("Content-Length", "1");
    dup.append("Content-Length", "2");
    response_definition def;
    http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), dup, bytes("x"), def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!def.valid());

    http::fields both;
    both.append("Transfer-Encoding", "chunked");
    both.append("Content-Length", "1");
    made = response_definition::reopen_file(
        http::status::from_code(200), both, "/tmp/somewhere", def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!def.valid());
LT_END_AUTO_TEST(factories_reject_framing_conflicts)

// (S1.9) reopen_file stores the path without opening anything: a path
// that does not resolve still constructs (each send observes the file
// system at its own prepare).
LT_BEGIN_AUTO_TEST(response_definition_suite, reopen_file_requires_path_and_defers_open)
    response_definition def;
    const http::outcome made = response_definition::reopen_file(
        http::status::from_code(200), http::fields(),
        "/definitely/not/here/task112.txt", def);
    LT_CHECK(made.ok());
    LT_CHECK(def.valid());
    LT_CHECK(def.kind() == response_definition::source_kind::reopen_file);
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(0));

    response_definition rejected;
    const http::outcome empty_path = response_definition::reopen_file(
        http::status::from_code(200), http::fields(), "", rejected);
    LT_CHECK(empty_path.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!rejected.valid());
LT_END_AUTO_TEST(reopen_file_requires_path_and_defers_open)

// (S1.10) a factory source requires a non-empty callable.
LT_BEGIN_AUTO_TEST(response_definition_suite, factory_requires_callable)
    response_definition def;
    const http::outcome made = response_definition::factory(
        http::status::from_code(200), http::fields(), body_factory{},
        def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!def.valid());

    response_definition ok_def;
    LT_CHECK(response_definition::factory(
        http::status::from_code(200), http::fields(),
        repeating_producer("x"), ok_def).ok());
    LT_CHECK(ok_def.valid());
    LT_CHECK(ok_def.kind() == response_definition::source_kind::factory);
LT_END_AUTO_TEST(factory_requires_callable)

// (S1.11) copies share the one frozen state (the fields and status
// alias the same immutable block); a move hands the state over and
// leaves the source empty.
LT_BEGIN_AUTO_TEST(response_definition_suite, copies_share_frozen_state)
    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), base_fields(), bytes("abcd"),
        def).ok());

    const response_definition copy = def;
    LT_CHECK(copy.valid());
    LT_CHECK(&copy.fields() == &def.fields());
    LT_CHECK(&copy.status() == &def.status());
    LT_CHECK(copy.kind() == def.kind());

    const response_definition moved = std::move(def);
    LT_CHECK(moved.valid());
    LT_CHECK(moved.fields().first("content-length").value_or("") == "4");
    LT_CHECK(!def.valid());
LT_END_AUTO_TEST(copies_share_frozen_state)

// (S2.1) The overlay's headers append AFTER the definition's base
// fields, in entries() order, repeated names included: the committed
// wire order is base-then-overlay (REQ-030).
LT_BEGIN_AUTO_TEST(response_definition_suite, send_appends_overlay_headers_after_base)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    http::fields base;
    base.append("A", "1");
    base.append("B", "2");
    base.append("Content-Length", "7");
    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), base, bytes("payload"), def).ok());

    response_overlay overlay;
    overlay.headers.append("C", "3");
    overlay.headers.append("A", "4");

    const send_report report = run_send(x, ex, def, overlay);
    LT_CHECK(report.status.ok());
    LT_CHECK_EQ(sink.respond_calls, 1);

    http::fields expected;
    expected.append("A", "1");
    expected.append("B", "2");
    expected.append("Content-Length", "7");
    expected.append("C", "3");
    expected.append("A", "4");
    LT_CHECK(sink.responded == expected);
LT_END_AUTO_TEST(send_appends_overlay_headers_after_base)

// (S2.2) A malformed or framing-conflicting overlay fails typed
// BEFORE the head commits: no engine decision, no writer traffic, the
// exchange stays at head (DR-V3-005).
LT_BEGIN_AUTO_TEST(response_definition_suite, send_rejects_bad_overlay_before_commit)
    struct case_spec {
        const char* name;
        response_overlay overlay;
    };
    std::vector<case_spec> cases;

    response_overlay bad_name;
    bad_name.headers.append("Bad Name", "v");
    cases.push_back({"non-token header name", bad_name});

    response_overlay ctl_value;
    ctl_value.headers.append("X-Note", "a\rb");
    cases.push_back({"ctl in header value", ctl_value});

    response_overlay second_cl;
    second_cl.headers.append("Content-Length", "9");
    cases.push_back({"second content-length", second_cl});

    response_overlay te_over_cl;
    te_over_cl.headers.append("Transfer-Encoding", "chunked");
    cases.push_back({"transfer-encoding over content-length", te_over_cl});

    response_overlay trailer_te;
    trailer_te.trailers.append("Transfer-Encoding", "chunked");
    cases.push_back({"transfer-encoding trailer", trailer_te});

    response_overlay trailer_cl;
    trailer_cl.trailers.append("Content-Length", "9");
    cases.push_back({"content-length trailer", trailer_cl});

    response_overlay trailer_host;
    trailer_host.trailers.append("Host", "h");
    cases.push_back({"host trailer", trailer_host});

    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("payload"),
        def).ok());

    for (const case_spec& c : cases) {
        capturing_sink sink;
        fake::scripted_body_sink out;
        exchange x(make_head(), &sink, 0, nullptr, &out);
        manual_executor ex;
        const send_report report = run_send(x, ex, def, c.overlay);
        LT_CHECK(report.status.code() == http::outcome_code::invalid_argument);
        LT_CHECK(!report.status.message().empty());
        LT_CHECK_EQ(sink.respond_calls, 0);
        LT_CHECK_EQ(out.push_calls(), 0);
        LT_CHECK_EQ(out.end_calls(), 0);
        LT_CHECK(x.state() == exchange_state::head);
    }
LT_END_AUTO_TEST(send_rejects_bad_overlay_before_commit)

// (S2.3) The happy path: head committed once with the merged fields,
// the whole body streamed, one body end, and the report counting the
// bytes.
LT_BEGIN_AUTO_TEST(response_definition_suite, send_streams_body_and_finishes)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), base_fields(), bytes("payload"),
        def).ok());

    response_overlay overlay;
    overlay.headers.append("X-Request", "17");

    const send_report report = run_send(x, ex, def, overlay);
    LT_CHECK(report.status.ok());
    LT_CHECK_EQ(report.body_bytes, static_cast<std::size_t>(7));
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.code, 200);
    LT_CHECK_EQ(sink.responded.count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(sink.responded.first("content-length").value_or("") == "7");
    LT_CHECK(sink.responded.first("x-request").value_or("") == "17");
    LT_CHECK_EQ(out.produced(), static_cast<std::size_t>(7));
    LT_CHECK_EQ(out.drain(64), static_cast<std::size_t>(7));
    LT_CHECK(of(out.drained_bytes()) == "payload");
    LT_CHECK_EQ(out.end_calls(), 1);
    LT_CHECK(out.ended());
LT_END_AUTO_TEST(send_streams_body_and_finishes)

// (S2.4) An empty owned body sends no body chunks at all: the cursor
// ends at once, the framed "0" rides the head, exactly one body end.
LT_BEGIN_AUTO_TEST(response_definition_suite, send_empty_body_ends_immediately)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), {}, def).ok());

    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.ok());
    LT_CHECK_EQ(report.body_bytes, static_cast<std::size_t>(0));
    LT_CHECK_EQ(out.push_calls(), 0);
    LT_CHECK_EQ(out.end_calls(), 1);
    LT_CHECK(sink.responded.first("content-length").value_or("") == "0");
LT_END_AUTO_TEST(send_empty_body_ends_immediately)

// (S2.5) The overlay's trailers ride the send's final framing.
LT_BEGIN_AUTO_TEST(response_definition_suite, send_delivers_overlay_trailers)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("ab"), def).ok());

    response_overlay overlay;
    overlay.trailers.append("X-Checksum", "abc");

    const send_report report = run_send(x, ex, def, overlay);
    LT_CHECK(report.status.ok());
    http::fields expected_trailers;
    expected_trailers.append("X-Checksum", "abc");
    LT_CHECK(out.trailers() == expected_trailers);
LT_END_AUTO_TEST(send_delivers_overlay_trailers)

// (S2.6) A mid-body engine failure propagates typed; the head stays
// committed (the engine owns the connection's fate) and no body end is
// pushed.
LT_BEGIN_AUTO_TEST(response_definition_suite, send_failure_mid_body_propagates)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    out.stage_failure(http::outcome_code::protocol_error, "transport gone");

    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("payload"),
        def).ok());

    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.code() == http::outcome_code::protocol_error);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(out.end_calls(), 0);
LT_END_AUTO_TEST(send_failure_mid_body_propagates)

// (S2.7) Neither the definition nor the overlay is mutated by a send:
// deep-equal snapshots before and after, and the frozen state stays
// the same shared block.
LT_BEGIN_AUTO_TEST(response_definition_suite, send_mutates_neither_side)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), base_fields(), bytes("payload"),
        def).ok());
    const http::fields fields_before = def.fields();
    const http::status* const state_before = &def.status();

    response_overlay overlay;
    overlay.headers.append("X-Request", "17");
    overlay.trailers.append("X-Checksum", "abc");
    const http::fields overlay_headers_before = overlay.headers;
    const http::fields overlay_trailers_before = overlay.trailers;

    const send_report report = run_send(x, ex, def, overlay);
    LT_CHECK(report.status.ok());

    LT_CHECK(def.fields() == fields_before);
    LT_CHECK(&def.status() == state_before);
    LT_CHECK(overlay.headers == overlay_headers_before);
    LT_CHECK(overlay.trailers == overlay_trailers_before);
    // The committed head carries the merged copy, not the definition.
    LT_CHECK(sink.responded != fields_before);
LT_END_AUTO_TEST(send_mutates_neither_side)

// (S2.8) Gate failures: an empty definition and a disconnected
// exchange each fail typed with the exchange untouched.
LT_BEGIN_AUTO_TEST(response_definition_suite, send_gates_fail_typed)
    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("x"), def).ok());

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    const send_report empty = run_send(x, ex, response_definition{}, {});
    LT_CHECK(empty.status.code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK(x.state() == exchange_state::head);

    const send_report ok_once = run_send(x, ex, def, {});
    LT_CHECK(ok_once.status.ok());
    LT_CHECK_EQ(sink.respond_calls, 1);

    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "peer closed").ok());
    capturing_sink sink2;
    fake::scripted_body_sink out2;
    exchange y(make_head(), &sink2, 0, nullptr, &out2);
    LT_CHECK(y.disconnect(http::outcome_code::connection_closed,
                          "peer closed").ok());
    const send_report closed = run_send(y, ex, def, {});
    LT_CHECK(closed.status.code() == http::outcome_code::connection_closed);
    LT_CHECK_EQ(sink2.respond_calls, 0);
LT_END_AUTO_TEST(send_gates_fail_typed)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
