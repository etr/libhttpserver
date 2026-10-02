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

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
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
#include "./response_source_rig.hpp"

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

// Drains the executor until `flag` turns non-zero (one millisecond per
// spin), for the cross-thread disconnect round whose wake-ups post
// from another thread.
bool drain_until(manual_executor& ex, int& flag, int spins) {
    for (int i = 0; i < spins && flag == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ex.run_pending();
    }
    return flag != 0;
}

// (S3 rig) the temp_file scratch asset moved to
// response_source_rig.hpp with TASK-113's file-backed source suites
// (fake::temp_file).

std::string repeating(std::size_t n) {
    std::string out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<char>('a' + (i % 26)));
    }
    return out;
}

// The cursor's per-pull read size (mirrors detail::k_file_chunk_bytes
// for the parking choreography below).
constexpr std::size_t k_file_chunk_capacity = 16u * 1024u;

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
    for (const std::string& value :
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

// (S3.1) A 40 KiB reopen_file body streams whole and in order across
// the cursor's 16 KiB reads, with Content-Length pinned per send.
LT_BEGIN_AUTO_TEST(response_definition_suite, file_send_streams_whole_body)
    const std::string payload = repeating(40 * 1024);
    fake::temp_file asset(payload);

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::reopen_file(
        http::status::from_code(200), http::fields(), asset.path(),
        def).ok());

    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.ok());
    LT_CHECK_EQ(report.body_bytes, payload.size());
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK(sink.responded.first("content-length").value_or("")
             == std::to_string(payload.size()));
    LT_CHECK_EQ(out.drain(1u << 20), payload.size());
    LT_CHECK(of(out.drained_bytes()) == payload);
    LT_CHECK_EQ(out.end_calls(), 1);
LT_END_AUTO_TEST(file_send_streams_whole_body)

// (S3.2) Every send reopens the file: appending to the file between
// sends changes the next send's pinned length and body, and the
// definition's own fields never gain a Content-Length.
LT_BEGIN_AUTO_TEST(response_definition_suite, file_send_reopens_per_send)
    fake::temp_file asset("one");

    response_definition def;
    LT_CHECK(response_definition::reopen_file(
        http::status::from_code(200), http::fields(), asset.path(),
        def).ok());

    capturing_sink sink1;
    fake::scripted_body_sink out1;
    exchange first(make_head(), &sink1, 0, nullptr, &out1);
    manual_executor ex1;
    const send_report one = run_send(first, ex1, def, {});
    LT_CHECK(one.status.ok());
    LT_CHECK(sink1.responded.first("content-length").value_or("") == "3");
    LT_CHECK_EQ(out1.drain(64), static_cast<std::size_t>(3));
    LT_CHECK(of(out1.drained_bytes()) == "one");

    asset.rewrite("one-two");

    capturing_sink sink2;
    fake::scripted_body_sink out2;
    exchange second(make_head(), &sink2, 0, nullptr, &out2);
    manual_executor ex2;
    const send_report two = run_send(second, ex2, def, {});
    LT_CHECK(two.status.ok());
    LT_CHECK_EQ(two.body_bytes, static_cast<std::size_t>(7));
    LT_CHECK(sink2.responded.first("content-length").value_or("") == "7");
    LT_CHECK_EQ(out2.drain(64), static_cast<std::size_t>(7));
    LT_CHECK(of(out2.drained_bytes()) == "one-two");

    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(file_send_reopens_per_send)

// (S3.3) A file that cannot be opened fails the send BEFORE the head
// commits: typed, no engine decision, exchange at head.
LT_BEGIN_AUTO_TEST(response_definition_suite, file_send_missing_file_fails_pre_commit)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::reopen_file(
        http::status::from_code(200), http::fields(),
        "/definitely/not/here/task112.txt", def).ok());

    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!report.status.message().empty());
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK_EQ(out.push_calls(), 0);
    LT_CHECK(x.state() == exchange_state::head);
LT_END_AUTO_TEST(file_send_missing_file_fails_pre_commit)

// (S3.4) A file short of the length the send declares (explicit
// Content-Length in the base fields) fails that send with the
// engine's short-body diagnosis, after the committed head (the
// engine owns the connection's fate).
LT_BEGIN_AUTO_TEST(response_definition_suite, file_send_short_of_declared_length_fails)
    const std::string payload = repeating(40 * 1024);
    fake::temp_file asset(payload);

    http::fields framed;
    framed.append("Content-Length", std::to_string(payload.size()));
    response_definition def;
    LT_CHECK(response_definition::reopen_file(
        http::status::from_code(200), framed, asset.path(), def).ok());

    asset.rewrite(repeating(100));

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report short_body = run_send(x, ex, def, {});
    LT_CHECK(short_body.status.code() == http::outcome_code::protocol_error);
    LT_CHECK(short_body.status.message().find(
                 "shorter than the declared Content-Length")
             != std::string::npos);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK(sink.responded.first("content-length").value_or("")
             == std::to_string(40 * 1024));
    LT_CHECK_EQ(out.end_calls(), 0);
LT_END_AUTO_TEST(file_send_short_of_declared_length_fails)

// (S3.4b) The same diagnosis covers a file that shrinks MID-send:
// after the send's own prepare, the read bound stays pinned, so the
// next pull meets a clean EOF with bytes still owed.
LT_BEGIN_AUTO_TEST(response_definition_suite, file_send_truncated_mid_send_fails)
    const std::string payload = repeating(40 * 1024);
    fake::temp_file asset(payload);

    response_definition def;
    LT_CHECK(response_definition::reopen_file(
        http::status::from_code(200), http::fields(), asset.path(),
        def).ok());

    capturing_sink sink;
    // Capacity fits the first 16 KiB chunk exactly: the second chunk
    // parks, and the truncation lands between the send's pulls.
    fake::scripted_body_sink out(k_file_chunk_capacity);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    send_report seen;
    int done = 0;
    spawn(ex, send_definition(x, def, {}),
          [&](task_result<send_report> r) {
              ++done;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    LT_CHECK_EQ(out.queued(), k_file_chunk_capacity);
    LT_CHECK(out.parked());

    asset.rewrite(repeating(100));
    LT_CHECK_EQ(out.drain(1u << 20),
                static_cast<std::size_t>(k_file_chunk_capacity));
    ex.run_pending();
    LT_CHECK_EQ(done, 1);
    LT_CHECK(seen.status.code() == http::outcome_code::protocol_error);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(out.end_calls(), 0);
LT_END_AUTO_TEST(file_send_truncated_mid_send_fails)

// (S3.5) A factory source invokes once per send, and each producer
// serves exactly its own send (fresh state, own pull sequence); a
// factory body is never Content-Length pinned by the send.
LT_BEGIN_AUTO_TEST(response_definition_suite, factory_send_serves_each_send_fresh)
    std::vector<std::shared_ptr<int>> pull_counts;
    int invocations = 0;

    response_definition def;
    LT_CHECK(response_definition::factory(
        http::status::from_code(200), http::fields(),
        [&pull_counts, &invocations]() -> body_producer {
            ++invocations;
            auto count = std::make_shared<int>(0);
            pull_counts.push_back(count);
            const auto tag = std::make_shared<std::string>(
                "body-" + std::to_string(invocations));
            return [count, tag, sent = false]() mutable -> body_chunk {
                ++*count;
                if (sent) {
                    return body_chunk{http::outcome::okay(), {}, true};
                }
                sent = true;
                return body_chunk{
                    http::outcome::okay(),
                    std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tag->data()),
                        tag->size()),
                    false};
            };
        }, def).ok());

    for (const std::string& expected : {std::string("body-1"),
                                        std::string("body-2")}) {
        capturing_sink sink;
        fake::scripted_body_sink out;
        exchange x(make_head(), &sink, 0, nullptr, &out);
        manual_executor ex;
        const send_report report = run_send(x, ex, def, {});
        LT_CHECK(report.status.ok());
        LT_CHECK_EQ(out.drain(64), expected.size());
        LT_CHECK(of(out.drained_bytes()) == expected);
        LT_CHECK_EQ(sink.responded.count("content-length"),
                    static_cast<std::size_t>(0));
    }

    LT_CHECK_EQ(invocations, 2);
    LT_CHECK_EQ(pull_counts.size(), static_cast<std::size_t>(2));
    if (pull_counts.size() == 2) {
        LT_CHECK_EQ(*pull_counts[0], 2);   // data pull + end pull
        LT_CHECK_EQ(*pull_counts[1], 2);
    }
LT_END_AUTO_TEST(factory_send_serves_each_send_fresh)

// (S3.6) A producer failure surfaces typed from the send, after the
// committed head, with no body end pushed.
LT_BEGIN_AUTO_TEST(response_definition_suite, factory_send_producer_failure_surfaces)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::factory(
        http::status::from_code(200), http::fields(),
        []() -> body_producer {
            return []() -> body_chunk {
                return body_chunk{
                    http::outcome(http::outcome_code::protocol_error,
                                  "producer broke"),
                    {}, false};
            };
        }, def).ok());

    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.code() == http::outcome_code::protocol_error);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(out.end_calls(), 0);
LT_END_AUTO_TEST(factory_send_producer_failure_surfaces)

// (S3.7) An empty non-end chunk is a producer contract violation:
// typed invalid_argument, never silently skipped.
LT_BEGIN_AUTO_TEST(response_definition_suite, factory_send_empty_chunk_fails)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::factory(
        http::status::from_code(200), http::fields(),
        []() -> body_producer {
            return []() -> body_chunk {
                return body_chunk{http::outcome::okay(), {}, false};
            };
        }, def).ok());

    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(out.end_calls(), 0);
LT_END_AUTO_TEST(factory_send_empty_chunk_fails)

// (S3.8) A factory returning an empty producer fails the send BEFORE
// the head commits: the exchange stays at head.
LT_BEGIN_AUTO_TEST(response_definition_suite, factory_send_empty_producer_pre_commit)
    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::factory(
        http::status::from_code(200), http::fields(),
        []() -> body_producer { return body_producer{}; }, def).ok());

    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK(x.state() == exchange_state::head);
LT_END_AUTO_TEST(factory_send_empty_producer_pre_commit)

// (S4.1) Four sends of ONE shared owned_bytes definition interleaved
// on one executor (each parked mid-body, all woken together): every
// sink receives the full payload, every head carries only its own
// overlay, and the definition is bit-for-bit untouched.
LT_BEGIN_AUTO_TEST(response_definition_suite, interleaved_sends_one_definition_independent)
    constexpr int k_sends = 4;
    const std::string payload = repeating(32);

    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), base_fields(), bytes(payload),
        def).ok());
    const http::fields fields_before = def.fields();
    const http::fields* const state_before = &def.fields();

    std::vector<std::unique_ptr<capturing_sink>> sinks;
    std::vector<std::unique_ptr<fake::scripted_body_sink>> outs;
    std::vector<std::unique_ptr<exchange>> exchanges;
    std::vector<send_report> reports(k_sends);
    std::vector<int> done(k_sends, 0);
    manual_executor ex;

    for (int k = 0; k < k_sends; ++k) {
        sinks.emplace_back(new capturing_sink);
        outs.emplace_back(new fake::scripted_body_sink(8));
        exchanges.emplace_back(
            new exchange(make_head(), sinks.back().get(), 0, nullptr,
                         outs.back().get()));
        response_overlay overlay;
        overlay.headers.append("X-Conn", std::to_string(k));
        spawn(ex, send_definition(*exchanges[k], def, overlay),
              [&, k](task_result<send_report> r) {
                  ++done[k];
                  if (r.has_value()) reports[k] = r.value();
              });
    }
    // One drain drives all four coroutines: each runs to its mid-body
    // park, so the sends provably interleave on the shared executor.
    ex.run_pending();
    for (int k = 0; k < k_sends; ++k) {
        LT_CHECK(outs[k]->parked());
    }

    // Rounds of engine progress: drain every sink, let every parked
    // send push its next capacity worth. 32 bytes over an 8-byte queue
    // needs a handful of rounds; 16 is a generous failure bound.
    int rounds = 0;
    while (std::find(done.begin(), done.end(), 0) != done.end()
            && rounds < 16) {
        for (int k = 0; k < k_sends; ++k) {
            LT_CHECK(outs[k]->drain(1u << 20) <= 8);
        }
        ex.run_pending();
        ++rounds;
    }
    for (int k = 0; k < k_sends; ++k) {
        LT_CHECK_EQ(done[k], 1);
    }

    for (int k = 0; k < k_sends; ++k) {
        LT_CHECK(reports[k].status.ok());
        LT_CHECK_EQ(reports[k].body_bytes, payload.size());
        LT_CHECK(of(outs[k]->drained_bytes()) == payload);
        LT_CHECK_EQ(sinks[k]->respond_calls, 1);
        LT_CHECK_EQ(sinks[k]->responded.count("x-conn"),
                    static_cast<std::size_t>(1));
        LT_CHECK(sinks[k]->responded.first("x-conn").value_or("")
                 == std::to_string(k));
    }

    LT_CHECK(def.fields() == fields_before);
    LT_CHECK(&def.fields() == state_before);
LT_END_AUTO_TEST(interleaved_sends_one_definition_independent)

// (S4.2) True concurrency: four threads, each with its own executor,
// exchange, and sink, all sending ONE shared definition; every body
// arrives whole and every head carries its own connection tag only.
LT_BEGIN_AUTO_TEST(response_definition_suite, threaded_sends_of_one_definition_stay_independent)
    constexpr int k_threads = 4;
    const std::string payload = repeating(2048);

    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), base_fields(), bytes(payload),
        def).ok());
    const http::fields fields_before = def.fields();

    std::vector<int> done(k_threads, 0);
    std::vector<std::string> bodies(k_threads);
    std::vector<std::string> tags(k_threads);
    std::vector<std::thread> workers;
    for (int k = 0; k < k_threads; ++k) {
        workers.emplace_back([&, k] {
            capturing_sink sink;
            fake::scripted_body_sink out;
            exchange x(make_head(), &sink, 0, nullptr, &out);
            manual_executor ex;
            response_overlay overlay;
            overlay.headers.append("X-Conn", std::to_string(k));
            spawn(ex, send_definition(x, def, overlay),
                  [&](task_result<send_report> r) {
                      if (r.has_value() && r.value().status.ok()) {
                          done[k] = 1;
                      }
                  });
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(5);
            while (done[k] == 0
                    && std::chrono::steady_clock::now() < deadline) {
                ex.run_pending();
                out.drain(1u << 20);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
            }
            out.drain(1u << 20);
            bodies[k] = of(out.drained_bytes());
            tags[k] = std::string(
                sink.responded.first("x-conn").value_or(""));
        });
    }
    for (std::thread& t : workers) t.join();

    for (int k = 0; k < k_threads; ++k) {
        LT_CHECK_EQ(done[k], 1);
        LT_CHECK(bodies[k] == payload);
        LT_CHECK(tags[k] == std::to_string(k));
    }
    LT_CHECK(def.fields() == fields_before);
LT_END_AUTO_TEST(threaded_sends_of_one_definition_stay_independent)

// (S4.3) The factory variant under the same four-thread concurrency:
// four producers in flight at once (each first pull waits for all
// four), one invocation per send, and each send's body matches its
// own invocation tag only.
LT_BEGIN_AUTO_TEST(response_definition_suite, threaded_factory_sends_run_concurrent_producers)
    constexpr int k_threads = 4;
    std::atomic<int> invocations{0};
    std::atomic<int> arrived{0};
    std::atomic<bool> saw_all_four{true};

    response_definition def;
    LT_CHECK(response_definition::factory(
        http::status::from_code(200), http::fields(),
        [&]() -> body_producer {
            const int index = invocations.fetch_add(1) + 1;
            const auto tag = std::make_shared<std::string>(
                "thread-" + std::to_string(index));
            return [&, tag, sent = false]() mutable -> body_chunk {
                if (sent) {
                    return body_chunk{http::outcome::okay(), {}, true};
                }
                sent = true;
                arrived.fetch_add(1);
                const auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds(5);
                while (arrived.load() < k_threads) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        saw_all_four.store(false);
                        break;
                    }
                    std::this_thread::yield();
                }
                return body_chunk{
                    http::outcome::okay(),
                    std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tag->data()),
                        tag->size()),
                    false};
            };
        }, def).ok());
    const http::fields fields_before = def.fields();

    std::vector<int> done(k_threads, 0);
    std::vector<std::string> bodies(k_threads);
    std::vector<std::thread> workers;
    for (int k = 0; k < k_threads; ++k) {
        workers.emplace_back([&, k] {
            capturing_sink sink;
            fake::scripted_body_sink out;
            exchange x(make_head(), &sink, 0, nullptr, &out);
            manual_executor ex;
            spawn(ex, send_definition(x, def, {}),
                  [&](task_result<send_report> r) {
                      if (r.has_value() && r.value().status.ok()) {
                          done[k] = 1;
                      }
                  });
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(8);
            while (done[k] == 0
                    && std::chrono::steady_clock::now() < deadline) {
                ex.run_pending();
                out.drain(1u << 20);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
            }
            out.drain(1u << 20);
            bodies[k] = of(out.drained_bytes());
        });
    }
    for (std::thread& t : workers) t.join();

    LT_CHECK(saw_all_four.load());
    LT_CHECK_EQ(invocations.load(), k_threads);
    std::vector<std::string> expected;
    for (int k = 1; k <= k_threads; ++k) {
        expected.push_back("thread-" + std::to_string(k));
    }
    for (int k = 0; k < k_threads; ++k) {
        LT_CHECK_EQ(done[k], 1);
        LT_CHECK(std::find(expected.begin(), expected.end(),
                           bodies[k]) != expected.end());
    }
    std::sort(bodies.begin(), bodies.end());
    LT_CHECK(bodies == expected);
    LT_CHECK(def.fields() == fields_before);
LT_END_AUTO_TEST(threaded_factory_sends_run_concurrent_producers)

// (S4.4) Cancellation independence: send A parked on a full sink,
// send B flowing; disconnecting A cancels only A — B has already
// completed — and the shared definition is untouched.
LT_BEGIN_AUTO_TEST(response_definition_suite, disconnect_cancels_only_its_own_send)
    const std::string payload = "park-me-please";

    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes(payload),
        def).ok());
    const http::fields fields_before = def.fields();

    capturing_sink sink_a;
    fake::scripted_body_sink out_a(4);   // parks mid-body
    exchange a(make_head(), &sink_a, 0, nullptr, &out_a);
    manual_executor ex_a;
    spawn(ex_a, fake::watch_stop_and_resume(a, out_a),
          [&](task_result<void>) { });
    send_report seen_a;
    int done_a = 0;
    spawn(ex_a, send_definition(a, def, {}),
          [&](task_result<send_report> r) {
              ++done_a;
              if (r.has_value()) seen_a = r.value();
          });
    ex_a.run_pending();
    LT_CHECK(out_a.parked());

    capturing_sink sink_b;
    fake::scripted_body_sink out_b;
    exchange b(make_head(), &sink_b, 0, nullptr, &out_b);
    manual_executor ex_b;
    const send_report seen_b = run_send(b, ex_b, def, {});
    LT_CHECK(seen_b.status.ok());
    LT_CHECK_EQ(out_b.drain(64), payload.size());
    LT_CHECK(of(out_b.drained_bytes()) == payload);
    LT_CHECK_EQ(out_b.end_calls(), 1);

    LT_CHECK(a.disconnect(http::outcome_code::connection_closed,
                          "peer went away").ok());
    LT_CHECK(drain_until(ex_a, done_a, 400));
    LT_CHECK_EQ(done_a, 1);
    LT_CHECK(seen_a.status.code() == http::outcome_code::cancelled);
    LT_CHECK_EQ(out_a.end_calls(), 0);

    LT_CHECK(def.fields() == fields_before);
LT_END_AUTO_TEST(disconnect_cancels_only_its_own_send)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
