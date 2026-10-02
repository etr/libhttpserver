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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
