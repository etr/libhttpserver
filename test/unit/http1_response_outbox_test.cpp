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

// TASK-107: HTTP/1 response outbox tests (PRD-V3N-REQ-026/027,
// DR-V3-006).
//
//   step 6 pins the ordered persistence core: per-response staging
//   slots drained strictly in request order (a response that completes
//   early still waits its turn — bytes physically cannot interleave),
//   the shared byte budget with partial-copy pushes, the end-marker
//   room rule, and the immediate oversized-head failure;
//
//   step 7 pins the body_sink seam under the real response writer
//   (park/room/failed/cancelled), abandon, the write_front io bridge
//   over the fake backend, and the pipelined_keepalive corpus replay
//   (routing.tseq): two pipelined responses drain strictly in request
//   order as one wire stream, with the corpus's keep-alive connection
//   verdicts asserted against http1_response_keepalive.
//
// Tests parse drained bytes through the parity response-frame parser
// (compiled into this program per the Makefile block).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/detail/fake_io_backend.hpp>
#include <httpserver/detail/http1_response_outbox.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <parity/response_frame.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;

using httpserver::body_finish;
using httpserver::body_write;
using httpserver::detail::body_push;
using httpserver::detail::body_push_result;
using httpserver::detail::fake_io_backend;
using httpserver::detail::http1_keepalive;
using httpserver::detail::http1_outbox_budget;
using httpserver::detail::http1_response_mode;
using httpserver::detail::http1_response_outbox;
using httpserver::detail::http1_response_sink;
using httpserver::detail::io_connection_owner;
using httpserver::detail::io_result;
using httpserver::detail::op_state;
using httpserver::detail::recording_sink;
using httpserver::exchange;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::stop_source;
using httpserver::task;
using httpserver::task_result;
namespace hd = httpserver::detail;
namespace hh = httpserver::http;
using parity::observed_response;
using parity::response_frame_parser;

http::request_head request(http::method_id id, http::protocol version,
                           const std::string& target = "/x") {
    http::request_head head;
    head.raw_target = target;
    head.route_path = target;
    head.request_method = http::method::known(id);
    head.request_protocol = version;
    return head;
}

http::request_head get_11() {
    return request(http::method_id::get, http::protocol::http_1_1);
}

http::fields length_fields(std::size_t n) {
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", std::to_string(n));
    return f;
}

std::span<const std::byte> as_bytes(const std::string& s) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), s.size());
}

std::string as_string(std::span<const std::byte> raw) {
    return std::string(reinterpret_cast<const char*>(raw.data()),
                       raw.size());
}

// Starts a plain 200 length-framed response head on `sink`.
http::outcome start_ok(http1_response_sink& sink, std::size_t body_bytes) {
    return sink.start(get_11(), http::status::from_code(200),
                      length_fields(body_bytes));
}

// Drains everything currently queued (only ended-and-drained slots
// pop, so this reads the front slot's buffered bytes).
std::string drain_buffered(http1_response_outbox& outbox) {
    std::string out;
    std::vector<std::byte> buf(65536);
    for (;;) {
        const std::size_t n = outbox.copy_front(buf);
        if (n == 0) break;
        out.append(as_string(std::span<const std::byte>(buf.data(), n)));
        outbox.consume_front(n);
    }
    return out;
}

// Drains until the outbox is empty, exposing ended front slots by
// consuming them fully.
std::string drain_all(http1_response_outbox& outbox) {
    std::string out;
    std::vector<std::byte> buf(65536);
    while (!outbox.empty()) {
        std::size_t n = outbox.copy_front(buf);
        if (n == 0 && !outbox.front_complete()) break;
        out.append(as_string(std::span<const std::byte>(buf.data(), n)));
        outbox.consume_front(n);
    }
    return out;
}

// Parses one complete response out of drained wire bytes.
observed_response parse_one(const std::string& wire) {
    response_frame_parser parser;
    std::vector<observed_response> done = parser.feed(wire);
    for (observed_response& r : parser.finish()) {
        done.push_back(std::move(r));
    }
    return done.empty() ? observed_response{} : done.front();
}

}  // namespace

LT_BEGIN_SUITE(outbox_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(outbox_suite)

LT_BEGIN_AUTO_TEST(outbox_suite, budget_defaults_project_from_server)
    const http1_outbox_budget defaults;
    LT_CHECK_EQ(defaults.max_queue_bytes, 4194304u);
    LT_CHECK_EQ(defaults.max_head_bytes, 65536u);

    httpserver::server::budget_limits limits;
    limits.set(httpserver::server::resource::response_queue_bytes, 8192);
    limits.set(httpserver::server::resource::header_bytes, 512);
    const http1_outbox_budget projected =
        http1_outbox_budget::from_budget_limits(limits);
    LT_CHECK_EQ(projected.max_queue_bytes, 8192u);
    LT_CHECK_EQ(projected.max_head_bytes, 512u);
LT_END_AUTO_TEST(budget_defaults_project_from_server)

LT_BEGIN_AUTO_TEST(outbox_suite, bytes_emerge_in_request_order)
    http1_response_outbox outbox;
    http1_response_sink& first = outbox.open(0, {});
    LT_CHECK(start_ok(first, 5).ok());
    http1_response_sink& second = outbox.open(1, {});
    LT_CHECK(start_ok(second, 3).ok());

    // The second response completes entirely while the first is still
    // open: its bytes wait behind the first slot.
    LT_CHECK(second.push(as_bytes("abc")).kind == body_push::accepted);
    LT_CHECK(second.push_end(http::fields()).kind == body_push::accepted);
    LT_CHECK(!outbox.empty());

    // The front bytes are the FIRST response's head, then its body.
    const std::string front = drain_buffered(outbox);
    LT_CHECK(front.find("HTTP/1.1 200 OK\r\n") == 0);
    LT_CHECK(front.find("Content-Length: 5\r\n") != std::string::npos);
    LT_CHECK(front.find("abc") == std::string::npos);
LT_END_AUTO_TEST(bytes_emerge_in_request_order)

LT_BEGIN_AUTO_TEST(outbox_suite, front_advances_only_on_consume)
    http1_response_outbox outbox;
    http1_response_sink& slot = outbox.open(0, {});
    LT_CHECK(start_ok(slot, 5).ok());
    LT_CHECK(slot.push(as_bytes("hello")).kind == body_push::accepted);

    std::vector<std::byte> buf(65536);
    const std::size_t total = outbox.copy_front(buf);
    const std::string head = as_string(std::span<const std::byte>(buf.data(), total));
    LT_CHECK_EQ(total, outbox.queued_bytes());

    // Consuming a prefix leaves the rest readable from the new offset.
    LT_CHECK_EQ(outbox.consume_front(4), 4u);
    std::vector<std::byte> again(65536);
    const std::size_t rest = outbox.copy_front(again);
    LT_CHECK(as_string(std::span<const std::byte>(again.data(), rest))
             == head.substr(4));
    LT_CHECK(!outbox.front_complete());

    // The last byte of an unended front slot does not pop it.
    LT_CHECK_EQ(outbox.consume_front(rest), rest);
    LT_CHECK(!outbox.empty());
    LT_CHECK(!outbox.front_complete());
    LT_CHECK_EQ(outbox.copy_front(again), 0u);

    // Once ended, the final consume pops the slot.
    LT_CHECK(slot.push_end(http::fields()).kind == body_push::accepted);
    LT_CHECK(outbox.front_complete());
    LT_CHECK_EQ(outbox.consume_front(0), 0u);
    LT_CHECK(outbox.empty());
LT_END_AUTO_TEST(front_advances_only_on_consume)

LT_BEGIN_AUTO_TEST(outbox_suite, second_response_emerges_after_first_pops)
    http1_response_outbox outbox;
    http1_response_sink& first = outbox.open(0, {});
    LT_CHECK(start_ok(first, 5).ok());
    LT_CHECK(first.push(as_bytes("hello")).kind == body_push::accepted);
    LT_CHECK(first.push_end(http::fields()).kind == body_push::accepted);
    http1_response_sink& second = outbox.open(1, {});
    LT_CHECK(start_ok(second, 2).ok());
    LT_CHECK(second.push(as_bytes("hi")).kind == body_push::accepted);
    LT_CHECK(second.push_end(http::fields()).kind == body_push::accepted);

    const std::string wire = drain_all(outbox);
    LT_CHECK(!wire.empty());
    response_frame_parser parser;
    std::vector<observed_response> parsed = parser.feed(wire);
    for (observed_response& r : parser.finish()) parsed.push_back(std::move(r));
    LT_CHECK(!parser.failed());
    LT_CHECK_EQ(parsed.size(), 2u);
    if (parsed.size() == 2) {
        LT_CHECK(parsed[0].body == "hello");
        LT_CHECK(parsed[1].body == "hi");
    }
LT_END_AUTO_TEST(second_response_emerges_after_first_pops)

LT_BEGIN_AUTO_TEST(outbox_suite, shared_budget_copies_partially_then_full)
    http1_response_outbox outbox(http1_outbox_budget{128, 128});
    http1_response_sink& slot = outbox.open(0, {});
    LT_CHECK(start_ok(slot, 1000).ok());
    const std::size_t head_bytes = outbox.queued_bytes();
    LT_CHECK(head_bytes <= 128);

    // The 1000-byte body fills the remaining budget partially.
    const httpserver::detail::body_push_result pushed =
        slot.push(as_bytes(std::string(1000, 'x')));
    LT_CHECK(pushed.kind == body_push::accepted);
    LT_CHECK_EQ(pushed.copied, 128u - head_bytes);
    LT_CHECK_EQ(outbox.queued_bytes(), 128u);

    // The budget is exhausted: the next push reports full, copies
    // nothing.
    const httpserver::detail::body_push_result blocked =
        slot.push(as_bytes(std::string(16, 'y')));
    LT_CHECK(blocked.kind == body_push::full);
    LT_CHECK_EQ(blocked.copied, 0u);
    LT_CHECK_EQ(outbox.queued_bytes(), 128u);

    // Room appears only from the drain side.
    LT_CHECK_EQ(outbox.consume_front(10), 10u);
    const httpserver::detail::body_push_result resumed =
        slot.push(as_bytes(std::string(16, 'z')));
    LT_CHECK(resumed.kind == body_push::accepted);
    LT_CHECK(resumed.copied <= 10u);
LT_END_AUTO_TEST(shared_budget_copies_partially_then_full)

LT_BEGIN_AUTO_TEST(outbox_suite, end_marker_needs_queue_room)
    // CL "63": the serialized head (with its terminator) is 65 bytes,
    // so the 63-byte body fills the 128-byte budget exactly and the
    // body stays completable.
    http1_response_outbox outbox(http1_outbox_budget{128, 128});
    http1_response_sink& slot = outbox.open(0, {});
    LT_CHECK(slot.start(get_11(), http::status::from_code(200),
                        length_fields(63)).ok());
    LT_CHECK(slot.push(as_bytes(std::string(63, 'x'))).copied == 63);
    LT_CHECK_EQ(outbox.queued_bytes(), 128u);

    // The end marker occupies queue room: a full budget reports full.
    LT_CHECK(slot.push_end(http::fields()).kind == body_push::full);

    // After room frees, the re-push ends the slot.
    outbox.consume_front(1);
    LT_CHECK(slot.push_end(http::fields()).kind == body_push::accepted);
    LT_CHECK(outbox.front_complete() == false);  // head bytes queued
    LT_CHECK(!outbox.empty());
LT_END_AUTO_TEST(end_marker_needs_queue_room)

LT_BEGIN_AUTO_TEST(outbox_suite, oversized_head_fails_immediately)
    http1_response_outbox outbox(http1_outbox_budget{4096, 16});
    http1_response_sink& slot = outbox.open(0, {});
    const http::outcome started = start_ok(slot, 5);
    LT_CHECK(started.code() == http::outcome_code::limit_exceeded);
    LT_CHECK(!started.message().empty());
    LT_CHECK_EQ(outbox.queued_bytes(), 0u);
    // The failed slot still occupies its request-order position (the
    // engine closes the connection; it never drains).
    LT_CHECK(!outbox.empty());
    // Sticky: the sink is unusable after the failed head.
    const std::string probe = "x";
    LT_CHECK(slot.push(as_bytes(probe)).kind == body_push::failed);
    LT_CHECK(slot.failure().code() == http::outcome_code::limit_exceeded);
LT_END_AUTO_TEST(oversized_head_fails_immediately)

LT_BEGIN_AUTO_TEST(outbox_suite, rejected_mode_fails_the_sink)
    http1_response_outbox outbox;
    http1_response_sink& slot = outbox.open(0, {});
    http::fields bad;
    bad.append("Content-Length", "5a");
    const http::outcome started = slot.start(
        get_11(), http::status::from_code(200), bad);
    LT_CHECK(started.code() == http::outcome_code::protocol_error);
    LT_CHECK_EQ(outbox.queued_bytes(), 0u);
    const std::string probe = "x";
    LT_CHECK(slot.push(as_bytes(probe)).kind == body_push::failed);
    LT_CHECK(slot.failure().code() == http::outcome_code::protocol_error);
LT_END_AUTO_TEST(rejected_mode_fails_the_sink)

LT_BEGIN_AUTO_TEST(outbox_suite, double_start_fails_typed)
    http1_response_outbox outbox;
    http1_response_sink& slot = outbox.open(0, {});
    LT_CHECK(start_ok(slot, 5).ok());
    const http::outcome again = start_ok(slot, 3);
    LT_CHECK(again.code() == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(double_start_fails_typed)

LT_BEGIN_AUTO_TEST(outbox_suite, interim_bytes_precede_the_final_head)
    http1_response_outbox outbox;
    http1_response_sink& slot = outbox.open(0, {});
    LT_CHECK(slot.interim(100).ok());
    LT_CHECK(start_ok(slot, 2).ok());
    LT_CHECK(slot.push(as_bytes("hi")).kind == body_push::accepted);
    LT_CHECK(slot.push_end(http::fields()).kind == body_push::accepted);

    const std::string wire = drain_all(outbox);
    LT_CHECK(wire.find("HTTP/1.1 100 Continue\r\n\r\n") == 0);
    LT_CHECK(wire.find("HTTP/1.1 200 OK\r\n") != std::string::npos);
    // The interim surfaces as its own observed response; the final
    // response follows it.
    response_frame_parser parser;
    std::vector<observed_response> parsed = parser.feed(wire);
    for (observed_response& r : parser.finish()) parsed.push_back(std::move(r));
    LT_CHECK(!parser.failed());
    LT_CHECK_EQ(parsed.size(), 2u);
    if (parsed.size() == 2) {
        LT_CHECK(parsed[0].status == 100);
        LT_CHECK(parsed[1].status == 200);
        LT_CHECK(parsed[1].body == "hi");
    }
LT_END_AUTO_TEST(interim_bytes_precede_the_final_head)

LT_BEGIN_AUTO_TEST(outbox_suite, queued_bytes_tracks_buffered_bytes)
    http1_response_outbox outbox;
    LT_CHECK(outbox.empty());
    LT_CHECK_EQ(outbox.queued_bytes(), 0u);
    http1_response_sink& slot = outbox.open(0, {});
    LT_CHECK(start_ok(slot, 5).ok());
    const std::size_t head = outbox.queued_bytes();
    LT_CHECK(head > 0);
    LT_CHECK(!outbox.empty());
    LT_CHECK(slot.push(as_bytes("hello")).kind == body_push::accepted);
    LT_CHECK_EQ(outbox.queued_bytes(), head + 5);
    outbox.consume_front(head + 5);
    LT_CHECK_EQ(outbox.queued_bytes(), 0u);
    LT_CHECK(slot.push_end(http::fields()).kind == body_push::accepted);
    LT_CHECK(outbox.front_complete());
LT_END_AUTO_TEST(queued_bytes_tracks_buffered_bytes)

LT_BEGIN_SUITE(seam_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(seam_suite)

http::request_head streaming_head() {
    return request(http::method_id::get, http::protocol::http_1_1,
                   "/stream");
}

// Drains the executor until `flag` turns non-zero (failure bound only).
bool drain_until(httpserver::manual_executor& ex, int& flag, int spins) {
    for (int i = 0; i < spins && flag == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ex.run_pending();
    }
    return flag != 0;
}

// Engine stand-in: awaits the exchange's stop and abandons the outbox,
// exactly the disconnect fan-out the connection loop performs.
task<void> watch_stop_and_abandon(httpserver::exchange& x,
                                              http1_response_outbox& outbox) {
    try {
        co_await x.cancellation().cancelled();
    } catch (const httpserver::cancelled_exception&) {
        outbox.abandon();
    }
}

// Streaming handler: commits the head on `x`, writes `payload` in
// `chunk_bytes` chunks through the real writer, finishes with a total
// trailer. Fills `flag` when the handler task ends.
httpserver::task<void> stream_through(httpserver::exchange& x,
                                      http1_response_sink& sink,
                                      const std::string& payload,
                                      std::size_t chunk_bytes, int& flag) {
    if (!x.start_response(http::status::from_code(200),
                          length_fields(payload.size())).ok()) {
        co_return;
    }
    // The engine drives sink.start() where on_respond fired.
    if (!sink.start(x.head(), http::status::from_code(200),
                    length_fields(payload.size())).ok()) {
        co_return;
    }
    for (std::size_t offset = 0; offset < payload.size();
         offset += chunk_bytes) {
        const body_write r = co_await x.writer().write(
            as_bytes(payload.substr(offset, chunk_bytes)));
        if (!r.status.ok()) co_return;
    }
    static_cast<void>(co_await x.writer().finish(http::fields()));
    flag = 1;
}

LT_BEGIN_AUTO_TEST(seam_suite, writer_parks_until_drain_frees_room)
    // CL "195": the serialized head is 66 bytes, so the 190-byte direct
    // fill tops the 256-byte budget exactly while 5 declared body bytes
    // remain for the writer chunk.
    http1_response_outbox outbox(http1_outbox_budget{256, 128});
    stop_source done_token;
    http1_response_sink& slot = outbox.open(0, done_token.get_token());
    recording_sink decisions;
    exchange x(streaming_head(), &decisions, 0, nullptr, &slot);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              length_fields(195)).ok());
    LT_CHECK(slot.start(x.head(), http::status::from_code(200),
                        length_fields(195)).ok());
    LT_CHECK(slot.push(as_bytes(std::string(190, 'a'))).copied == 190);
    LT_CHECK_EQ(outbox.queued_bytes(), 256u);

    // A writer chunk parks on the full queue.
    body_write seen;
    int deliveries = 0;
    const std::string chunk = "bcdef";
    spawn(ex, x.writer().write(as_bytes(chunk)),
          [&](task_result<body_write> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    LT_CHECK(slot.parked());
    LT_CHECK_EQ(deliveries, 0);

    // Engine progress: each one-byte drain wakes the writer, which
    // refills the budget exactly; the loop drives the chunk to
    // completion with the queue ending full.
    for (int i = 0; i < 100 && deliveries == 0; ++i) {
        outbox.consume_front(1);
        ex.run_pending();
    }
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.status.ok());
    LT_CHECK_EQ(seen.accepted, std::size_t{5});
    LT_CHECK(slot.parked() == false);
    LT_CHECK_EQ(outbox.queued_bytes(), 256u);

    // finish needs queue room too: with the budget exactly full it
    // parks, and the drain drives the end marker through.
    body_finish finished;
    int finish_done = 0;
    spawn(ex, x.writer().finish(http::fields()),
          [&](task_result<body_finish> r) {
              ++finish_done;
              if (r.has_value()) finished = r.value();
          });
    ex.run_pending();
    LT_CHECK(slot.parked());
    for (int i = 0; i < 100 && finish_done == 0; ++i) {
        outbox.consume_front(1);
        ex.run_pending();
    }
    LT_CHECK_EQ(finish_done, 1);
    LT_CHECK(finished.status.ok());
    while (!outbox.empty()) outbox.consume_front(4096);
    LT_CHECK(outbox.empty());
LT_END_AUTO_TEST(writer_parks_until_drain_frees_room)

LT_BEGIN_AUTO_TEST(seam_suite, disconnect_then_park_completes_cancelled)
    // The writer is inside its loop when the disconnect lands: the wake
    // from the drain finds the stop fired, the re-push parks, and the
    // park completes immediately as cancelled.
    http1_response_outbox outbox(http1_outbox_budget{256, 128});
    // The engine opens response slots against its connection-level stop
    // source and requests the stop when the exchange disconnects.
    stop_source connection_stop;
    http1_response_sink& slot = outbox.open(0, connection_stop.get_token());
    recording_sink decisions;
    exchange x(streaming_head(), &decisions, 0, nullptr, &slot);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              length_fields(195)).ok());
    LT_CHECK(slot.start(x.head(), http::status::from_code(200),
                        length_fields(195)).ok());
    LT_CHECK(slot.push(as_bytes(std::string(190, 'a'))).copied == 190);

    body_write seen;
    int deliveries = 0;
    const std::string chunk = "bcdef";
    spawn(ex, x.writer().write(as_bytes(chunk)),
          [&](task_result<body_write> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    LT_CHECK(slot.parked());

    // Free one byte, then disconnect BEFORE the parked write runs: the
    // re-push sees one byte of room, copies it, parks again, and the
    // park completes cancelled on the spot.
    LT_CHECK_EQ(outbox.consume_front(1), 1u);
    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "peer went away").ok());
    connection_stop.request_stop();
    LT_CHECK(drain_until(ex, deliveries, 400));

    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.status.code() == http::outcome_code::cancelled);
    LT_CHECK_EQ(seen.accepted, std::size_t{1});
    LT_CHECK(slot.parked() == false);
LT_END_AUTO_TEST(disconnect_then_park_completes_cancelled)

LT_BEGIN_AUTO_TEST(seam_suite, abandon_fails_parked_writers)
    http1_response_outbox outbox(http1_outbox_budget{128, 128});
    stop_source token;
    http1_response_sink& slot = outbox.open(0, token.get_token());
    recording_sink decisions;
    exchange x(streaming_head(), &decisions, 0, nullptr, &slot);
    manual_executor ex;

    LT_CHECK(x.start_response(http::status::from_code(200),
                              length_fields(63)).ok());
    LT_CHECK(slot.start(x.head(), http::status::from_code(200),
                        length_fields(63)).ok());
    LT_CHECK(slot.push(as_bytes(std::string(63, 'a'))).copied == 63);

    httpserver::body_write seen;
    int deliveries = 0;
    spawn(ex, watch_stop_and_abandon(x, outbox),
          [&](task_result<void>) { });
    spawn(ex, x.writer().write(as_bytes("bcdefg")),
          [&](httpserver::task_result<httpserver::body_write> r) {
              ++deliveries;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();  // the watcher awaits the stop; the write parks
    LT_CHECK(slot.parked());

    LT_CHECK(x.disconnect(http::outcome_code::connection_closed,
                          "peer went away").ok());
    LT_CHECK(drain_until(ex, deliveries, 400));

    // abandon() fails the parker with the typed connection_closed
    // diagnostic; the failure is sticky on the sink.
    LT_CHECK_EQ(deliveries, 1);
    LT_CHECK(seen.status.code() == http::outcome_code::connection_closed);
    LT_CHECK(seen.status.message().find("peer went away")
             == std::string::npos);
    LT_CHECK(slot.failure().code() == http::outcome_code::connection_closed);
    const std::string probe = "x";
    LT_CHECK(slot.push(as_bytes(probe)).kind == body_push::failed);
LT_END_AUTO_TEST(abandon_fails_parked_writers)

LT_BEGIN_AUTO_TEST(seam_suite, write_front_over_fake_backend)
    fake_io_backend backend;
    manual_executor ex;
    io_connection_owner owner(ex);
    http1_response_outbox outbox;
    http1_response_sink& slot = outbox.open(0, {});
    LT_CHECK(slot.start(get_11(), http::status::from_code(200),
                        length_fields(5)).ok());
    LT_CHECK(slot.push(as_bytes("hello")).kind == body_push::accepted);
    LT_CHECK(slot.push_end(http::fields()).kind == body_push::accepted);

    task<io_result> job = outbox.write_front(backend, owner, 1);
    io_result seen;
    int done = 0;
    spawn(ex, std::move(job),
          [&](task_result<io_result> r) {
              ++done;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();  // the coroutine submits the first write op
    LT_CHECK_EQ(done, 0);
    LT_CHECK_EQ(backend.pending_count(), std::size_t{1});

    // The first write carries the whole front slot (head + body).
    const std::vector<std::shared_ptr<hd::op_state>> first =
        backend.pending_ops();
    LT_CHECK_EQ(first.size(), std::size_t{1});
    const std::size_t first_bytes = std::get<hd::write_payload>(
        first[0]->payload()).bytes.size();
    LT_CHECK_EQ(first_bytes, outbox.queued_bytes());

    // Scripted partial transfer: 2 bytes go out; the bridge consumes
    // them and re-copies the rest.
    LT_CHECK(backend.complete(*first[0],
                              hd::io_result{hh::outcome_code::ok, 2, 0}));
    ex.run_pending();
    const std::vector<std::shared_ptr<hd::op_state>> second =
        backend.pending_ops();
    LT_CHECK_EQ(second.size(), std::size_t{1});
    LT_CHECK(second[0] != first[0]);
    const std::size_t second_bytes = std::get<hd::write_payload>(
        second[0]->payload()).bytes.size();
    LT_CHECK_EQ(second_bytes, first_bytes - 2u);
    LT_CHECK(!outbox.empty());

    // The remainder goes out; the ended front slot pops and the bridge
    // returns the total.
    LT_CHECK(backend.complete(*second[0],
                              hd::io_result{hh::outcome_code::ok,
                                            second_bytes, 0}));
    ex.run_pending();
    ex.run_pending();  // the second apply resumes the bridge frame
    LT_CHECK_EQ(done, 1);
    LT_CHECK(seen.code == hh::outcome_code::ok);
    LT_CHECK_EQ(seen.transferred, first_bytes);
    LT_CHECK(outbox.empty());
    LT_CHECK_EQ(backend.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(write_front_over_fake_backend)

LT_BEGIN_AUTO_TEST(seam_suite, write_front_returns_io_failures)
    fake_io_backend backend;
    manual_executor ex;
    io_connection_owner owner(ex);
    http1_response_outbox outbox;
    http1_response_sink& slot = outbox.open(0, {});
    LT_CHECK(slot.start(get_11(), http::status::from_code(200),
                        length_fields(5)).ok());
    LT_CHECK(slot.push(as_bytes("hello")).kind == body_push::accepted);

    task<io_result> job = outbox.write_front(backend, owner, 1);
    io_result seen;
    int done = 0;
    spawn(ex, std::move(job),
          [&](task_result<io_result> r) {
              ++done;
              if (r.has_value()) seen = r.value();
          });
    ex.run_pending();
    const std::vector<std::shared_ptr<hd::op_state>> pending =
        backend.pending_ops();
    LT_CHECK_EQ(pending.size(), std::size_t{1});
    // TASK-108 owns retry/close; the bridge reports the failure as-is.
    LT_CHECK(backend.complete(
        *pending[0],
        hd::io_result{hh::outcome_code::connection_closed, 0, 0}));
    ex.run_pending();
    LT_CHECK_EQ(done, 1);
    LT_CHECK(seen.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(seen.transferred, std::size_t{0});
    // Nothing was consumed on failure.
    LT_CHECK(!outbox.empty());
LT_END_AUTO_TEST(write_front_returns_io_failures)

LT_BEGIN_AUTO_TEST(seam_suite, pipelined_keepalive_replay)
    // routing.tseq pipelined_keepalive: two GET /hello requests
    // back-to-back on one connection; both answered in order, both
    // keep-alive. The exchanges + writers + sinks are the real v3
    // stack; the drain is the engine loop.
    http1_response_outbox outbox;
    stop_source connection;
    recording_sink decisions;
    http1_response_sink& first = outbox.open(0, connection.get_token());
    http1_response_sink& second = outbox.open(1, connection.get_token());
    exchange x0(streaming_head(), &decisions, 0, nullptr, &first);
    exchange x1(streaming_head(), &decisions, 0, nullptr, &second);
    manual_executor ex;

    int done = 0;
    spawn(ex, stream_through(x0, first, "OK", 2, done),
          [&](task_result<void>) { });
    ex.run_pending();
    int done2 = 0;
    spawn(ex, stream_through(x1, second, "OK", 2, done2),
          [&](task_result<void>) { });
    ex.run_pending();

    for (int i = 0; i < 1000 && (done == 0 || done2 == 0); ++i) {
        outbox.consume_front(64);
        ex.run_pending();
    }
    LT_CHECK_EQ(done, 1);
    LT_CHECK_EQ(done2, 1);

    const std::string wire = drain_all(outbox);
    response_frame_parser parser;
    std::vector<observed_response> parsed = parser.feed(wire);
    for (observed_response& r : parser.finish()) parsed.push_back(std::move(r));
    LT_CHECK(!parser.failed());
    LT_CHECK_EQ(parsed.size(), 2u);
    if (parsed.size() == 2) {
        // Both responses carry the same shape, in request order.
        LT_CHECK(parsed[0].status == 200);
        LT_CHECK(parsed[0].body == "OK");
        LT_CHECK(parsed[0].framing == "content-length");
        LT_CHECK(parsed[1].status == 200);
        LT_CHECK(parsed[1].body == "OK");
        // The corpus connection verdicts, resolved through the
        // http1_response_keepalive rule (HTTP/1.1, no close token).
        const http1_response_mode mode0 = http1_response_mode::compute(
            x0.head(), http::status::from_code(200), length_fields(2));
        LT_CHECK(httpserver::detail::http1_response_keepalive(
                     x0.head(), mode0.kind, mode0.close_policy)
                 == http1_keepalive::keep_alive);
        const http1_response_mode mode1 = http1_response_mode::compute(
            x1.head(), http::status::from_code(200), length_fields(2));
        LT_CHECK(httpserver::detail::http1_response_keepalive(
                     x1.head(), mode1.kind, mode1.close_policy)
                 == http1_keepalive::keep_alive);
    }
LT_END_AUTO_TEST(pipelined_keepalive_replay)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
