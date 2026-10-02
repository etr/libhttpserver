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

// TASK-113: file, pipe and borrowed-buffer response ownership
// (PRD-V3N-REQ-028/029, DR-V3-005, architecture §3.2). Layered steps:
//   B - borrowed memory under an explicit lifetime lease: a valid
//       lease required at the factory, Content-Length pinned to the
//       span size, whole-body streaming, the keeper outliving the
//       application's own reference, replay across sends, concurrent
//       sends sharing one span, exactly-one keeper release;
//   F - owned_file handle transfer (one-shot, exactly-once close);
//   P - owned_pipe one-shot streaming;
//   C - cancellation and concurrent-send cleanup.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
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
using httpserver::body_lease;
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

http::request_head make_head() {
    http::request_head head;
    head.raw_target = "/things";
    head.route_path = "/things";
    head.request_method = http::method::known(http::method_id::get);
    head.request_protocol = http::protocol::http_1_1;
    return head;
}

// Decision oracle keeping the committed fields whole (the framing
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

// Runs one send to completion on `x` (the default sink capacity
// always exceeds the payloads) and returns the report.
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

std::string repeating(std::size_t n) {
    std::string out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<char>('a' + (i % 26)));
    }
    return out;
}

}  // namespace

LT_BEGIN_SUITE(response_sources_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(response_sources_suite)

// (B1) A borrowed body without a valid lease is rejected at the
// factory, typed, before a definition exists: the pre-seeded `out`
// survives the failed call untouched (REQ-028).
LT_BEGIN_AUTO_TEST(response_sources_suite, borrowed_without_lease_rejected)
    response_definition seeded;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("x"),
        seeded).ok());

    const std::string payload = "leased";
    response_definition def = seeded;
    const http::outcome made = response_definition::borrowed(
        http::status::from_code(200), http::fields(),
        fake::byte_span(payload), body_lease{}, def);
    LT_CHECK(!made.ok());
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!made.message().empty());
    LT_CHECK(def.valid());
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");
    LT_CHECK(def.kind() == response_definition::source_kind::owned_bytes);
LT_END_AUTO_TEST(borrowed_without_lease_rejected)

// (B2) The factory pins Content-Length to the span size when the
// fields carry neither framing field; the caller's field set is
// untouched.
LT_BEGIN_AUTO_TEST(response_sources_suite, borrowed_pins_length_when_unframed)
    const std::string payload = "hello";
    auto keeper = std::make_shared<std::string>(payload);

    http::fields f;
    f.append("Content-Type", "text/plain");
    response_definition def;
    const http::outcome made = response_definition::borrowed(
        http::status::from_code(200), f,
        fake::byte_span(*keeper), body_lease(keeper), def);
    LT_CHECK(made.ok());
    LT_CHECK(def.valid());
    LT_CHECK(def.kind() == response_definition::source_kind::borrowed);
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(def.fields().first("content-length").value_or("") == "5");
    LT_CHECK_EQ(f.count("content-length"), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(borrowed_pins_length_when_unframed)

// (B3) An explicit Content-Length passes through verbatim; an empty
// borrowed body pins "0" like any other empty source.
LT_BEGIN_AUTO_TEST(response_sources_suite, borrowed_explicit_length_passthrough)
    const std::string payload = "hello";
    auto keeper = std::make_shared<std::string>(payload);

    http::fields framed;
    framed.append("Content-Length", "42");
    response_definition def;
    LT_CHECK(response_definition::borrowed(
        http::status::from_code(200), framed,
        fake::byte_span(*keeper), body_lease(keeper), def).ok());
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(def.fields().first("content-length").value_or("") == "42");

    auto empty = std::make_shared<std::string>();
    response_definition none;
    LT_CHECK(response_definition::borrowed(
        http::status::from_code(204), http::fields(),
        fake::byte_span(*empty), body_lease(empty), none).ok());
    LT_CHECK(none.fields().first("content-length").value_or("") == "0");
LT_END_AUTO_TEST(borrowed_explicit_length_passthrough)

// (B4) The send streams the whole borrowed body and finishes: one
// committed head with the pinned length, every byte in order, one
// body end.
LT_BEGIN_AUTO_TEST(response_sources_suite, borrowed_streams_body_and_finishes)
    const std::string payload = repeating(2048);
    auto keeper = std::make_shared<std::string>(payload);

    capturing_sink sink;
    fake::scripted_body_sink out(16);
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;

    response_definition def;
    LT_CHECK(response_definition::borrowed(
        http::status::from_code(200), http::fields(),
        fake::byte_span(*keeper), body_lease(keeper), def).ok());

    int done = 0;
    send_report seen;
    spawn(ex, send_definition(x, def, {}),
          [&](task_result<send_report> r) {
              ++done;
              if (r.has_value()) seen = r.value();
          });
    int rounds = 0;
    while (done == 0 && rounds < 512) {
        ex.run_pending();
        out.drain(1u << 20);
        ++rounds;
    }
    LT_CHECK_EQ(done, 1);
    LT_CHECK(seen.status.ok());
    LT_CHECK_EQ(seen.body_bytes, payload.size());
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK(sink.responded.first("content-length").value_or("")
             == std::to_string(payload.size()));
    LT_CHECK_EQ(out.drained(), payload.size());
    LT_CHECK(of(out.drained_bytes()) == payload);
    LT_CHECK_EQ(out.end_calls(), 1);
LT_END_AUTO_TEST(borrowed_streams_body_and_finishes)

// (B5) The lease outlives the application's own reference: after the
// app drops its keeper handle, the definition still holds the buffer
// alive and a send streams it whole.
LT_BEGIN_AUTO_TEST(response_sources_suite, borrowed_lease_outlives_app_reference)
    response_definition def;
    {
        auto keeper = std::make_shared<std::string>(repeating(1024));
        LT_CHECK(response_definition::borrowed(
            http::status::from_code(200), http::fields(),
            fake::byte_span(*keeper), body_lease(keeper), def).ok());
        keeper.reset();
    }

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.ok());
    LT_CHECK_EQ(report.body_bytes, static_cast<std::size_t>(1024));
    LT_CHECK_EQ(out.drain(1u << 20), static_cast<std::size_t>(1024));
    LT_CHECK(of(out.drained_bytes()) == repeating(1024));
LT_END_AUTO_TEST(borrowed_lease_outlives_app_reference)

// (B6) A leased borrowed body is replayable: two sequential sends of
// one definition each stream the whole body (reading a span is
// non-destructive; the lease is what makes sharing safe).
LT_BEGIN_AUTO_TEST(response_sources_suite, borrowed_replays_across_sends)
    const std::string payload = "again and again";
    auto keeper = std::make_shared<std::string>(payload);
    response_definition def;
    LT_CHECK(response_definition::borrowed(
        http::status::from_code(200), http::fields(),
        fake::byte_span(*keeper), body_lease(keeper), def).ok());

    for (int send = 0; send < 2; ++send) {
        capturing_sink sink;
        fake::scripted_body_sink out;
        exchange x(make_head(), &sink, 0, nullptr, &out);
        manual_executor ex;
        const send_report report = run_send(x, ex, def, {});
        LT_CHECK(report.status.ok());
        LT_CHECK_EQ(report.body_bytes, payload.size());
        LT_CHECK_EQ(out.drain(64), payload.size());
        LT_CHECK(of(out.drained_bytes()) == payload);
        LT_CHECK_EQ(out.end_calls(), 1);
    }
LT_END_AUTO_TEST(borrowed_replays_across_sends)

// (B7) True concurrency: four threads, each with its own exchange,
// sending ONE shared leased definition — every body arrives whole
// from the one span (REQ-029 narrowed to leased memory).
LT_BEGIN_AUTO_TEST(response_sources_suite, threaded_borrowed_sends_share_span)
    constexpr int k_threads = 4;
    const std::string payload = repeating(4096);
    auto keeper = std::make_shared<std::string>(payload);
    response_definition def;
    LT_CHECK(response_definition::borrowed(
        http::status::from_code(200), http::fields(),
        fake::byte_span(*keeper), body_lease(keeper), def).ok());

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
        });
    }
    for (std::thread& t : workers) t.join();

    for (int k = 0; k < k_threads; ++k) {
        LT_CHECK_EQ(done[k], 1);
        LT_CHECK(bodies[k] == payload);
    }
LT_END_AUTO_TEST(threaded_borrowed_sends_share_span)

// (B8) Destroying the last definition releases the keeper exactly
// once: copies share the one frozen body block and its one lease.
LT_BEGIN_AUTO_TEST(response_sources_suite, borrowed_destruction_releases_keeper_once)
    auto count = std::make_shared<std::atomic<int>>(0);
    response_definition def;
    {
        auto keeper = std::make_shared<fake::tracked_keeper>(count);
        http::fields f;
        LT_CHECK(response_definition::borrowed(
            http::status::from_code(200), f, fake::byte_span("kept"),
            body_lease(keeper), def).ok());

        capturing_sink sink;
        fake::scripted_body_sink out;
        exchange x(make_head(), &sink, 0, nullptr, &out);
        manual_executor ex;
        const send_report report = run_send(x, ex, def, {});
        LT_CHECK(report.status.ok());
        keeper.reset();
        // The definition (and its copy) still hold the lease.
        LT_CHECK_EQ(count->load(), 0);
    }
    // def and its frozen block are gone here only at scope end.
    {
        const response_definition copy = def;
        LT_CHECK(copy.valid());
    }
    LT_CHECK_EQ(count->load(), 0);
    def = response_definition{};
    LT_CHECK(!def.valid());
    LT_CHECK_EQ(count->load(), 1);
LT_END_AUTO_TEST(borrowed_destruction_releases_keeper_once)

// -----------------------------------------------------------------------
// F - owned_file: a transferred std::FILE* is one-shot and closed
// exactly once on every path.
// -----------------------------------------------------------------------

// (F1) owned_file requires a non-null handle and a non-empty close
// operation; each failed call leaves `out` untouched.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_file_requires_handle)
    response_definition seeded;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("x"),
        seeded).ok());

    fake::temp_file asset("payload");
    response_definition def = seeded;
    http::outcome made = response_definition::owned_file(
        http::status::from_code(200), http::fields(), nullptr, def);
    LT_CHECK(!made.ok());
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!made.message().empty());
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");

    std::FILE* handle = fake::open_for_read(asset);
    made = response_definition::owned_file(
        http::status::from_code(200), http::fields(), handle,
        httpserver::owned_close_fn{}, def);
    LT_CHECK(!made.ok());
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");
    std::fclose(handle);
LT_END_AUTO_TEST(owned_file_requires_handle)

// (F2) The whole file body streams in order; Content-Length is pinned
// per send to the handle's remaining size at transfer (the frozen
// fields carry none), and the definition is NOT reusable-by-factory:
// kind() reports the transfer.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_file_streams_whole_body)
    const std::string payload = repeating(40 * 1024);
    fake::temp_file asset(payload);
    auto closes = std::make_shared<std::atomic<int>>(0);

    response_definition def;
    LT_CHECK(response_definition::owned_file(
        http::status::from_code(200), http::fields(),
        fake::open_for_read(asset), fake::counting_close(closes),
        def).ok());
    LT_CHECK(def.valid());
    LT_CHECK(def.kind() == response_definition::source_kind::owned_file);
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(0));

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.ok());
    LT_CHECK_EQ(report.body_bytes, payload.size());
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK(sink.responded.first("content-length").value_or("")
             == std::to_string(payload.size()));
    LT_CHECK_EQ(out.drain(1u << 20), payload.size());
    LT_CHECK(of(out.drained_bytes()) == payload);
    LT_CHECK_EQ(out.end_calls(), 1);
LT_END_AUTO_TEST(owned_file_streams_whole_body)

// (F3) The transfer honors the handle's position: the body is
// position→EOF of whatever the application handed over.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_file_honors_transfer_position)
    const std::string payload = "HEADER:body-bytes-here";
    fake::temp_file asset(payload);
    auto closes = std::make_shared<std::atomic<int>>(0);
    std::FILE* handle = fake::open_for_read(asset);
    LT_CHECK_EQ(std::fseek(handle, 7, SEEK_SET), 0);

    response_definition def;
    LT_CHECK(response_definition::owned_file(
        http::status::from_code(200), http::fields(), handle,
        fake::counting_close(closes), def).ok());

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.ok());
    const std::string expected = payload.substr(7);
    LT_CHECK_EQ(report.body_bytes, expected.size());
    LT_CHECK(sink.responded.first("content-length").value_or("")
             == std::to_string(expected.size()));
    LT_CHECK_EQ(out.drain(64), expected.size());
    LT_CHECK(of(out.drained_bytes()) == expected);
LT_END_AUTO_TEST(owned_file_honors_transfer_position)

// (F4) The transferred handle closes exactly once after a completed
// send — and stays closed exactly once after the definition itself is
// destroyed (the backstop must not double-close).
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_file_closes_once_after_send)
    fake::temp_file asset(repeating(64));
    auto closes = std::make_shared<std::atomic<int>>(0);

    response_definition def;
    LT_CHECK(response_definition::owned_file(
        http::status::from_code(200), http::fields(),
        fake::open_for_read(asset), fake::counting_close(closes),
        def).ok());

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.ok());
    LT_CHECK_EQ(closes->load(), 1);

    def = response_definition{};
    LT_CHECK(!def.valid());
    LT_CHECK_EQ(closes->load(), 1);
LT_END_AUTO_TEST(owned_file_closes_once_after_send)

// (F5) One handle, one seek position, one send: a second send fails
// invalid_state BEFORE anything is written — no head, no body traffic
// — and closes nothing.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_file_second_send_rejected_before_writing)
    fake::temp_file asset(repeating(128));
    auto closes = std::make_shared<std::atomic<int>>(0);
    response_definition def;
    LT_CHECK(response_definition::owned_file(
        http::status::from_code(200), http::fields(),
        fake::open_for_read(asset), fake::counting_close(closes),
        def).ok());

    capturing_sink sink1;
    fake::scripted_body_sink out1;
    exchange first(make_head(), &sink1, 0, nullptr, &out1);
    manual_executor ex1;
    const send_report one = run_send(first, ex1, def, {});
    LT_CHECK(one.status.ok());
    LT_CHECK_EQ(closes->load(), 1);

    capturing_sink sink2;
    fake::scripted_body_sink out2;
    exchange second(make_head(), &sink2, 0, nullptr, &out2);
    manual_executor ex2;
    const send_report two = run_send(second, ex2, def, {});
    LT_CHECK(two.status.code() == http::outcome_code::invalid_state);
    LT_CHECK(!two.status.message().empty());
    LT_CHECK_EQ(sink2.respond_calls, 0);
    LT_CHECK_EQ(out2.push_calls(), 0);
    LT_CHECK_EQ(out2.end_calls(), 0);
    LT_CHECK(second.state() == exchange_state::head);
    LT_CHECK_EQ(closes->load(), 1);
LT_END_AUTO_TEST(owned_file_second_send_rejected_before_writing)

// (F6) A definition destroyed unsent still closes its handle exactly
// once (the holder is the backstop closer).
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_file_unsent_destruction_closes_once)
    fake::temp_file asset(repeating(32));
    auto closes = std::make_shared<std::atomic<int>>(0);
    {
        response_definition def;
        LT_CHECK(response_definition::owned_file(
            http::status::from_code(200), http::fields(),
            fake::open_for_read(asset), fake::counting_close(closes),
            def).ok());
        LT_CHECK_EQ(closes->load(), 0);
    }
    LT_CHECK_EQ(closes->load(), 1);
LT_END_AUTO_TEST(owned_file_unsent_destruction_closes_once)

// (F7) Copies of the definition share the ONE frozen handle block:
// whichever copy sends consumes it, and one close serves all copies.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_file_copies_share_one_close)
    fake::temp_file asset(repeating(32));
    auto closes = std::make_shared<std::atomic<int>>(0);
    response_definition def;
    LT_CHECK(response_definition::owned_file(
        http::status::from_code(200), http::fields(),
        fake::open_for_read(asset), fake::counting_close(closes),
        def).ok());

    const response_definition copy = def;
    LT_CHECK(copy.valid());
    LT_CHECK(copy.kind() == response_definition::source_kind::owned_file);

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report report = run_send(x, ex, copy, {});
    LT_CHECK(report.status.ok());
    LT_CHECK_EQ(closes->load(), 1);

    def = response_definition{};
    LT_CHECK_EQ(closes->load(), 1);
LT_END_AUTO_TEST(owned_file_copies_share_one_close)

// (F8) A declared Content-Length bounds the read in BOTH directions:
// short of it is the engine's short-body diagnosis (after the
// committed head); over it truncates cleanly to the declared size.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_file_declared_length_bounds_read)
    const std::string payload = repeating(32);
    auto closes = std::make_shared<std::atomic<int>>(0);

    http::fields over;
    over.append("Content-Length", std::to_string(payload.size() + 10));
    response_definition long_def;
    LT_CHECK(response_definition::owned_file(
        http::status::from_code(200), over,
        fake::open_for_read(fake::temp_file(payload)),
        fake::counting_close(closes), long_def).ok());

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report short_body = run_send(x, ex, long_def, {});
    LT_CHECK(short_body.status.code() == http::outcome_code::protocol_error);
    LT_CHECK(short_body.status.message().find(
                 "shorter than the declared Content-Length")
             != std::string::npos);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(out.end_calls(), 0);
    LT_CHECK_EQ(closes->load(), 1);

    http::fields under;
    under.append("Content-Length", "3");
    response_definition trim_def;
    LT_CHECK(response_definition::owned_file(
        http::status::from_code(200), under,
        fake::open_for_read(fake::temp_file(payload)),
        fake::counting_close(closes), trim_def).ok());

    capturing_sink sink2;
    fake::scripted_body_sink out2;
    exchange y(make_head(), &sink2, 0, nullptr, &out2);
    manual_executor ex2;
    const send_report trimmed = run_send(y, ex2, trim_def, {});
    LT_CHECK(trimmed.status.ok());
    LT_CHECK_EQ(trimmed.body_bytes, static_cast<std::size_t>(3));
    LT_CHECK_EQ(out2.drain(64), static_cast<std::size_t>(3));
    LT_CHECK(of(out2.drained_bytes()) == payload.substr(0, 3));
    LT_CHECK_EQ(out2.end_calls(), 1);
    LT_CHECK_EQ(closes->load(), 2);
LT_END_AUTO_TEST(owned_file_declared_length_bounds_read)

// (F9) An unseekable handle (a pipe read end) fails owned_file's
// probe pre-commit — the exchange untouched — and the failed attempt
// SPENDS the one-shot claim: a second send reports the spent source,
// not the probe failure. The spent handle is closed exactly once.
#ifndef _WIN32
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_file_unseekable_fails_precommit_and_spends)
    fake::filled_pipe pipe("pipe bytes");
    auto closes = std::make_shared<std::atomic<int>>(0);
    response_definition def;
    LT_CHECK(response_definition::owned_file(
        http::status::from_code(200), http::fields(), pipe.release(),
        fake::counting_close(closes), def).ok());

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report probe = run_send(x, ex, def, {});
    LT_CHECK(probe.status.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!probe.status.message().empty());
    LT_CHECK_EQ(sink.respond_calls, 0);
    LT_CHECK_EQ(out.push_calls(), 0);
    LT_CHECK(x.state() == exchange_state::head);
    LT_CHECK_EQ(closes->load(), 1);

    capturing_sink sink2;
    fake::scripted_body_sink out2;
    exchange y(make_head(), &sink2, 0, nullptr, &out2);
    manual_executor ex2;
    const send_report spent = run_send(y, ex2, def, {});
    LT_CHECK(spent.status.code() == http::outcome_code::invalid_state);
    LT_CHECK_EQ(sink2.respond_calls, 0);
    LT_CHECK_EQ(out2.push_calls(), 0);
    LT_CHECK_EQ(closes->load(), 1);
LT_END_AUTO_TEST(owned_file_unseekable_fails_precommit_and_spends)
#endif  // !_WIN32

// -----------------------------------------------------------------------
// P - owned_pipe: a transferred pipe endpoint is a strictly one-shot
// streaming source with unknown length (the pipe fixtures are
// POSIX-only).
// -----------------------------------------------------------------------
#ifndef _WIN32

// (P1) owned_pipe requires a non-null handle and a non-empty close
// operation; each failed call leaves `out` untouched.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_pipe_requires_handle)
    response_definition seeded;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("x"),
        seeded).ok());

    fake::filled_pipe pipe("p");
    response_definition def = seeded;
    http::outcome made = response_definition::owned_pipe(
        http::status::from_code(200), http::fields(), nullptr, def);
    LT_CHECK(!made.ok());
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!made.message().empty());
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");

    made = response_definition::owned_pipe(
        http::status::from_code(200), http::fields(), pipe.get(),
        httpserver::owned_close_fn{}, def);
    LT_CHECK(!made.ok());
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");
LT_END_AUTO_TEST(owned_pipe_requires_handle)

// (P2) The pipe body streams whole to EOF with NO Content-Length
// invented: the length is unknown, so the framing stays the
// transport's (chunked on HTTP/1.1, close-delimited on HTTP/1.0) —
// the non-replayable narrowing of REQ-029's contract.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_pipe_streams_to_eof_unpinned)
    const std::string payload = repeating(3000);
    auto closes = std::make_shared<std::atomic<int>>(0);

    response_definition def;
    LT_CHECK(response_definition::owned_pipe(
        http::status::from_code(200), http::fields(),
        fake::filled_pipe(payload).release(),
        fake::counting_close(closes), def).ok());
    LT_CHECK(def.valid());
    LT_CHECK(def.kind() == response_definition::source_kind::owned_pipe);
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(0));

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report report = run_send(x, ex, def, {});
    LT_CHECK(report.status.ok());
    LT_CHECK_EQ(report.body_bytes, payload.size());
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK_EQ(sink.responded.count("content-length"),
                static_cast<std::size_t>(0));
    LT_CHECK_EQ(out.drain(1u << 20), payload.size());
    LT_CHECK(of(out.drained_bytes()) == payload);
    LT_CHECK_EQ(out.end_calls(), 1);
    LT_CHECK_EQ(closes->load(), 1);
LT_END_AUTO_TEST(owned_pipe_streams_to_eof_unpinned)

// (P3) One endpoint, one read: a second send fails invalid_state
// BEFORE anything is written — no head, no body traffic — and closes
// nothing.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_pipe_second_send_rejected_before_writing)
    fake::filled_pipe pipe(repeating(128));
    auto closes = std::make_shared<std::atomic<int>>(0);
    response_definition def;
    LT_CHECK(response_definition::owned_pipe(
        http::status::from_code(200), http::fields(), pipe.release(),
        fake::counting_close(closes), def).ok());

    capturing_sink sink1;
    fake::scripted_body_sink out1;
    exchange first(make_head(), &sink1, 0, nullptr, &out1);
    manual_executor ex1;
    const send_report one = run_send(first, ex1, def, {});
    LT_CHECK(one.status.ok());
    LT_CHECK_EQ(closes->load(), 1);

    capturing_sink sink2;
    fake::scripted_body_sink out2;
    exchange second(make_head(), &sink2, 0, nullptr, &out2);
    manual_executor ex2;
    const send_report two = run_send(second, ex2, def, {});
    LT_CHECK(two.status.code() == http::outcome_code::invalid_state);
    LT_CHECK(!two.status.message().empty());
    LT_CHECK_EQ(sink2.respond_calls, 0);
    LT_CHECK_EQ(out2.push_calls(), 0);
    LT_CHECK_EQ(out2.end_calls(), 0);
    LT_CHECK(second.state() == exchange_state::head);
    LT_CHECK_EQ(closes->load(), 1);
LT_END_AUTO_TEST(owned_pipe_second_send_rejected_before_writing)

// (P4) Four threads race one pipe definition: the one-shot claim
// admits exactly ONE full body; every loser fails invalid_state typed;
// the endpoint closes exactly once across all of them.
LT_BEGIN_AUTO_TEST(response_sources_suite, threaded_pipe_sends_single_winner)
    constexpr int k_threads = 4;
    const std::string payload = repeating(8192);
    auto closes = std::make_shared<std::atomic<int>>(0);
    response_definition def;
    LT_CHECK(response_definition::owned_pipe(
        http::status::from_code(200), http::fields(),
        fake::filled_pipe(payload).release(),
        fake::counting_close(closes), def).ok());

    std::vector<int> ok(k_threads, 0);
    std::vector<int> rejected(k_threads, 0);
    std::vector<std::string> bodies(k_threads);
    std::vector<std::thread> workers;
    for (int k = 0; k < k_threads; ++k) {
        workers.emplace_back([&, k] {
            capturing_sink sink;
            fake::scripted_body_sink out;
            exchange x(make_head(), &sink, 0, nullptr, &out);
            manual_executor ex;
            int done = 0;
            spawn(ex, send_definition(x, def, {}),
                  [&](task_result<send_report> r) {
                      ++done;
                      if (!r.has_value()) return;
                      if (r.value().status.ok()) {
                          ok[k] = 1;
                      } else if (r.value().status.code()
                                 == http::outcome_code::invalid_state) {
                          rejected[k] = 1;
                      }
                  });
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(5);
            while (done == 0
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

    int winners = 0;
    int full = 0;
    for (int k = 0; k < k_threads; ++k) {
        winners += ok[k];
        if (bodies[k] == payload) ++full;
        LT_CHECK_EQ(rejected[k] + ok[k], 1);
    }
    LT_CHECK_EQ(winners, 1);
    LT_CHECK_EQ(full, 1);
    LT_CHECK_EQ(closes->load(), 1);
LT_END_AUTO_TEST(threaded_pipe_sends_single_winner)

// (P5) A declared Content-Length bounds the pipe read in BOTH
// directions: short of it is the engine's short-body diagnosis (after
// the committed head carries the declared length); over it truncates
// cleanly to the declared size.
LT_BEGIN_AUTO_TEST(response_sources_suite, owned_pipe_declared_length_bounds_read)
    const std::string payload = repeating(32);
    auto closes = std::make_shared<std::atomic<int>>(0);

    http::fields over;
    over.append("Content-Length", std::to_string(payload.size() + 10));
    response_definition long_def;
    LT_CHECK(response_definition::owned_pipe(
        http::status::from_code(200), over,
        fake::filled_pipe(payload).release(),
        fake::counting_close(closes), long_def).ok());

    capturing_sink sink;
    fake::scripted_body_sink out;
    exchange x(make_head(), &sink, 0, nullptr, &out);
    manual_executor ex;
    const send_report short_body = run_send(x, ex, long_def, {});
    LT_CHECK(short_body.status.code() == http::outcome_code::protocol_error);
    LT_CHECK(short_body.status.message().find(
                 "shorter than the declared Content-Length")
             != std::string::npos);
    LT_CHECK_EQ(sink.respond_calls, 1);
    LT_CHECK(sink.responded.first("content-length").value_or("")
             == std::to_string(payload.size() + 10));
    LT_CHECK_EQ(out.end_calls(), 0);
    LT_CHECK_EQ(closes->load(), 1);

    http::fields under;
    under.append("Content-Length", "3");
    response_definition trim_def;
    LT_CHECK(response_definition::owned_pipe(
        http::status::from_code(200), under,
        fake::filled_pipe(payload).release(),
        fake::counting_close(closes), trim_def).ok());

    capturing_sink sink2;
    fake::scripted_body_sink out2;
    exchange y(make_head(), &sink2, 0, nullptr, &out2);
    manual_executor ex2;
    const send_report trimmed = run_send(y, ex2, trim_def, {});
    LT_CHECK(trimmed.status.ok());
    LT_CHECK_EQ(trimmed.body_bytes, static_cast<std::size_t>(3));
    LT_CHECK_EQ(out2.drain(64), static_cast<std::size_t>(3));
    LT_CHECK(of(out2.drained_bytes()) == payload.substr(0, 3));
    LT_CHECK_EQ(out2.end_calls(), 1);
    LT_CHECK_EQ(closes->load(), 2);
LT_END_AUTO_TEST(owned_pipe_declared_length_bounds_read)

#endif  // !_WIN32

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
