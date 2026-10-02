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

// TASK-108 step 9: the native HTTP/1 end-to-end suite. One real
// native_server on an ephemeral loopback listener, one raw client
// connection per scenario (raw_http_client.hpp over the pollsys
// shims), responses asserted through the parity frame parser -- the
// wire is the interface. Cases 1-14 exercise the protocol posture
// (keep-alive, version-conditional framing and close, pipelining,
// error synthesis, watchdog timeouts, client aborts, Expect
// admission, rejection drain, suspension deadlines), 15-18 the server
// posture (concurrency, handler-safe stop, the validate gate, the
// connections budget). Every wait is deadline-bound; a pass is always
// observed bytes or an observed close, never a sleep.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <deque>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/response_definition.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/server.hpp>

#include "../integ/raw_http_client.hpp"
#include "./littletest.hpp"

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;
namespace raw = raw_http;

using httpserver::body_collect;
using httpserver::body_policy;
using httpserver::exchange;
using httpserver::resume_outcome;
using httpserver::resume_signal;
using httpserver::task;
using raw::observed_response;

// -- handlers ----------------------------------------------------------------

// GET /hello: streams a body with no explicit framing, so the engine
// picks the framing per protocol version (chunked on 1.1, close-
// delimited on 1.0).
task<void> hello_handler(exchange& x) {
    http::fields f;
    f.append("Content-Type", "text/plain");
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::string body = "hello";
    const std::byte* raw = reinterpret_cast<const std::byte*>(body.data());
    co_await x.writer().write(std::span<const std::byte>(raw, body.size()));
    co_await x.writer().finish();
}

// POST /echo: admits, collects bounded, echoes with explicit
// Content-Length framing.
task<void> echo_handler(exchange& x) {
    const http::outcome admitted = x.admit_body(body_policy{});
    if (!admitted.ok()) co_return;
    const body_collect collected = co_await x.body().collect(1 << 20);
    if (!collected.status.ok()) co_return;
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", std::to_string(collected.data.size()));
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::byte* raw =
        reinterpret_cast<const std::byte*>(collected.data.data());
    co_await x.writer().write(
        std::span<const std::byte>(raw, collected.data.size()));
    co_await x.writer().finish();
}

// POST /collect_slow: admits and parks in a body read (the
// abort-mid-body scenario).
task<void> collect_slow_handler(exchange& x) {
    static_cast<void>(x.admit_body(body_policy{}));
    std::byte piece[64];
    for (;;) {
        const httpserver::body_read read =
            co_await x.body().read_some(piece);
        if (!read.status.ok() || read.end_of_body) co_return;
    }
}

// GET /throwing: raises; run_route synthesizes the 500.
task<void> throwing_handler(exchange&) {
    throw std::runtime_error("e2e: handler asked to throw");
}

// TASK-112: GET /asset serves ONE shared immutable definition to
// every connection, each send decorated with its own overlay (the
// connection's id rides X-Conn-Idx, appended after the shared base
// fields). Built lazily: the definition is a plain value with a
// non-trivial factory, and function-local static init is thread-safe
// for the concurrent first sends.
httpserver::response_definition& asset_definition() {
    static httpserver::response_definition def = [] {
        httpserver::response_definition built;
        http::fields f;
        f.append("Content-Type", "application/octet-stream");
        static_cast<void>(httpserver::response_definition::owned_bytes(
            http::status::from_code(200), f,
            std::vector<std::byte>(1024, std::byte{0x61}), built));
        return built;
    }();
    return def;
}

task<void> asset_handler(exchange& x) {
    httpserver::response_overlay overlay;
    overlay.headers.append("X-Conn-Idx",
                           std::to_string(x.connection_id()));
    static_cast<void>(co_await httpserver::send_definition(
        x, asset_definition(), overlay));
}

// Rendezvous for the concurrency case: the response comes only once
// two handlers arrived, so the two connections provably interleave.
std::atomic<int> rendezvous_arrived{0};

task<void> rendezvous_handler(exchange& x) {
    rendezvous_arrived.fetch_add(1);
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (rendezvous_arrived.load() < 2) {
        if (std::chrono::steady_clock::now() >= deadline) co_return;
        std::this_thread::yield();
    }
    http::fields f;
    f.append("Content-Length", "3");
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::string body = "met";
    const std::byte* raw = reinterpret_cast<const std::byte*>(body.data());
    co_await x.writer().write(std::span<const std::byte>(raw, 3));
    co_await x.writer().finish();
}

// GET /stopper: calls request_stop() from inside the handler (the
// DR-V3-008 posture: initiation must return immediately, from any
// thread, inside a handler).
srv::native_server* stop_target = nullptr;
std::atomic<bool> stopper_returned{false};

task<void> stopper_handler(exchange& x) {
    http::fields f;
    f.append("Content-Length", "5");
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::string body = "adieu";
    const std::byte* raw = reinterpret_cast<const std::byte*>(body.data());
    co_await x.writer().write(std::span<const std::byte>(raw, 5));
    co_await x.writer().finish();
    if (stop_target != nullptr) stop_target->request_stop();
    stopper_returned.store(true);
}

// TASK-109 scenario state. The suite runs sequentially
// (AUTORUN_TESTS); each scenario resets what it reads before
// connecting (the previous fixture's stop joined its handlers).
resume_signal e2e_late_gate;   // /late_admit parks here, pre-admission
std::atomic<int> e2e_suspend_outcome{-1};   // resume_outcome as int

// TASK-111 sync-route state: /sync-echo counts its invocations so the
// over-cap case can prove the handler never ran.
std::atomic<int> sync_echo_invocations{0};

// POST /late_admit: parks on the shared gate BEFORE admitting (the
// wire must stay silent until admission), then collects + echoes. The
// test thread releases the gate, so the interim-vs-admission ordering
// is observable on the wire.
task<void> late_admit_handler(exchange& x) {
    const resume_outcome gate =
        co_await e2e_late_gate.wait_for(raw::kExchangeBudget);
    if (gate != resume_outcome::resumed) co_return;
    const http::outcome admitted = x.admit_body(body_policy{});
    if (!admitted.ok()) co_return;
    const body_collect collected = co_await x.body().collect(1 << 20);
    if (!collected.status.ok()) co_return;
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", std::to_string(collected.data.size()));
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::byte* raw =
        reinterpret_cast<const std::byte*>(collected.data.data());
    co_await x.writer().write(
        std::span<const std::byte>(raw, collected.data.size()));
    co_await x.writer().finish();
}

// POST /reject: answers 403 straight from the head -- the body is
// never admitted, so the rejection drain (not a handler) consumes it.
task<void> reject_handler(exchange& x) {
    http::fields f;
    f.append("Content-Length", "0");
    static_cast<void>(x.respond(http::status::from_code(403), f));
    co_return;
}

// POST /suspend_head: suspends the exchange at head state and parks;
// the engine resolves the wait (the suspension deadline cancels it).
task<void> suspend_head_handler(exchange& x) {
    resume_signal wait;
    if (!x.suspend(wait).ok()) co_return;
    const resume_outcome outcome =
        co_await wait.wait_for(raw::kExchangeBudget);
    e2e_suspend_outcome.store(static_cast<int>(outcome));
}

// TASK-110 drain scenario state. Same sequential-suite discipline as
// the TASK-109 state above: each scenario resets what it reads before
// connecting (the previous fixture's stop joined its handlers).
resume_signal e2e_drain_gate;   // /drain_gated parks here pre-response
std::atomic<bool> e2e_drain_entered{false};
std::atomic<bool> e2e_hang_entered{false};
std::atomic<int> e2e_hang_outcome{-1};   // resume_outcome as int
std::atomic<bool> e2e_caller_began_ok{false};
std::atomic<int> e2e_caller_wait_code{-1};   // outcome_code as int
std::atomic<bool> e2e_caller_returned{false};
srv::native_server* drain_begin_target = nullptr;
srv::drain_ticket* drain_begin_ticket = nullptr;

// GET /drain_gated: parks on the shared gate before responding, so the
// drain scenarios can hold the in-flight exchange open.
task<void> drain_gated_handler(exchange& x) {
    e2e_drain_entered.store(true);
    const resume_outcome gate =
        co_await e2e_drain_gate.wait_for(raw::kExchangeBudget);
    if (gate != resume_outcome::resumed) co_return;
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", "5");
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::string body = "gated";
    const std::byte* raw = reinterpret_cast<const std::byte*>(body.data());
    co_await x.writer().write(std::span<const std::byte>(raw, body.size()));
    co_await x.writer().finish();
}

// GET /hang_forever: suspends and parks on a never-signaled gate --
// work only a drain's deadline expiry (or a plain stop) can end. The
// suspension registration is what lets the hard stop cancel the park.
task<void> hang_forever_handler(exchange& x) {
    resume_signal wait;
    if (!x.suspend(wait).ok()) co_return;
    e2e_hang_entered.store(true);
    const resume_outcome outcome =
        co_await wait.wait_for(std::chrono::seconds(30));
    e2e_hang_outcome.store(static_cast<int>(outcome));
    co_return;   // cancelled: no response
}

// GET /drain_caller: commits its response first, then begins a drain
// and waits on the ticket from inside the handler. The wait must
// refuse would_deadlock at once (the handler is counted work), leaving
// the ticket itself usable for the external waiter.
task<void> drain_caller_handler(exchange& x) {
    http::fields f;
    f.append("Content-Length", "2");
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::string body = "ok";
    const std::byte* raw = reinterpret_cast<const std::byte*>(body.data());
    co_await x.writer().write(std::span<const std::byte>(raw, body.size()));
    co_await x.writer().finish();
    if (drain_begin_target != nullptr && drain_begin_ticket != nullptr) {
        const http::outcome began = drain_begin_target->begin_drain(
            std::chrono::milliseconds(5000), *drain_begin_ticket);
        e2e_caller_began_ok.store(began.ok());
        srv::drain_result observed;
        const http::outcome waited =
            drain_begin_ticket->wait(observed);
        e2e_caller_wait_code.store(static_cast<int>(waited.code()));
    }
    e2e_caller_returned.store(true);
}

// -- fixtures ----------------------------------------------------------------

srv::server_options base_options() {
    srv::server_options options;
    srv::listener_options listener;
    listener.address = "127.0.0.1";
    listener.port = 0;
    options.add_listener(listener);
    options.concurrency().workers = 4;
    return options;
}

// One running server on an ephemeral port. Destruction stops it.
class server_fixture {
 public:
    explicit server_fixture(srv::server_options options)
        : server_(std::move(options)) {
        static_cast<void>(server_.route(
            http::method::known(http::method_id::get), "/hello",
            hello_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/echo",
            echo_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/collect_slow",
            collect_slow_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::get), "/throwing",
            throwing_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::get), "/asset",
            asset_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::get), "/rendezvous",
            rendezvous_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::get), "/stopper",
            stopper_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/late_admit",
            late_admit_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/reject",
            reject_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/suspend_head",
            suspend_head_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::get), "/drain_gated",
            drain_gated_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::get), "/hang_forever",
            hang_forever_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::get), "/drain_caller",
            drain_caller_handler));
        // TASK-111: the bounded sync value route -- a plain lambda, no
        // coroutine code -- with a 16-byte body cap.
        static_cast<void>(server_.route_sync(
            http::method::known(http::method_id::post), "/sync-echo",
            [](const http::request_head& h,
               std::span<const std::byte> body) -> srv::sync_response {
                ++sync_echo_invocations;
                const std::string text(
                    reinterpret_cast<const char*>(body.data()), body.size());
                const std::string echo = h.route_path + ":" + text;
                srv::sync_response out;
                out.status = http::status::from_code(200);
                const std::byte* raw =
                    reinterpret_cast<const std::byte*>(echo.data());
                out.body.assign(raw, raw + echo.size());
                return out;
            },
            16));
        static_cast<void>(server_.listen());
    }

    ~server_fixture() { server_.stop(); }

    srv::native_server& server() noexcept { return server_; }

    std::uint16_t port() const noexcept { return server_.get_bound_port(0); }

 private:
    srv::native_server server_;
};

bool wait_until(const std::atomic<bool>& flag,
                std::chrono::milliseconds budget =
                    std::chrono::milliseconds(5000)) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (!flag.load()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

// ASCII-lowercased copy of a header name (TASK-112: case-insensitive
// response header lookup).
std::string lowered_header_name(const std::string& name) {
    std::string out = name;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

}  // namespace

LT_BEGIN_SUITE(native_http1_e2e_suite)
    void set_up() {
        rendezvous_arrived.store(0);
        stopper_returned.store(false);
        e2e_late_gate = resume_signal{};
        e2e_suspend_outcome.store(-1);
        sync_echo_invocations.store(0);
        e2e_drain_gate = resume_signal{};
        e2e_drain_entered.store(false);
        e2e_hang_entered.store(false);
        e2e_hang_outcome.store(-1);
        e2e_caller_began_ok.store(false);
        e2e_caller_wait_code.store(-1);
        e2e_caller_returned.store(false);
        drain_begin_target = nullptr;
        drain_begin_ticket = nullptr;
    }

    void tear_down() {
    }
LT_END_SUITE(native_http1_e2e_suite)

// (1) HTTP/1.1: two GETs on one keep-alive connection; the engine
// frames the un-framed streaming body as chunked.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_two_gets_keepalive_chunked)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("hello"));
        LT_CHECK_EQ(seen[0].framing, std::string("chunked"));
    }
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) {
        LT_CHECK_EQ(seen[1].status, 200);
        LT_CHECK_EQ(seen[1].body, std::string("hello"));
    }
LT_END_AUTO_TEST(http11_two_gets_keepalive_chunked)

// (2) HTTP/1.0: no keep-alive, no chunked -- the body is delimited by
// the server's close.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http10_get_closes_delimited)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /hello HTTP/1.0\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK(client.receive_close(seen));
    LT_CHECK(client.peer_closed());
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("hello"));
        LT_CHECK_EQ(seen[0].framing, std::string("none"));
    }
LT_END_AUTO_TEST(http10_get_closes_delimited)

// (3) HTTP/1.0 with Connection: keep-alive: honored (two requests, one
// connection); the framing stays explicit.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http10_keepalive_requested_honored)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo HTTP/1.0\r\nConnection: keep-alive\r\n"
                         "Content-Length: 4\r\n\r\nping"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("ping"));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK(client.send("POST /echo HTTP/1.0\r\nConnection: keep-alive\r\n"
                         "Content-Length: 4\r\n\r\npong"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].body, std::string("pong"));
LT_END_AUTO_TEST(http10_keepalive_requested_honored)

// (4) HTTP/1.1 POST with Content-Length: admit + collect + explicit
// Content-Length echo.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_content_length_echo)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 11\r\n\r\nhello world"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("hello world"));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
LT_END_AUTO_TEST(http11_post_content_length_echo)

// (5) HTTP/1.1 chunked POST through the same exchange body.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_chunked_echo)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo HTTP/1.1\r\nHost: h\r\n"
                         "Transfer-Encoding: chunked\r\n\r\n"
                         "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("hello world"));
    }
LT_END_AUTO_TEST(http11_post_chunked_echo)

// (6) Error answers: 404 keeps the connection alive; a throwing
// handler answers 500; an unsupported transfer coding answers 501 and
// closes.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, error_answers_and_close_policy)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /missing HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) LT_CHECK_EQ(seen[0].status, 404);
    // The connection survives a 404: the next request answers too.
    LT_CHECK(client.send("GET /throwing HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].status, 500);

    // 501 closes per the policy: a coding before chunked is refused.
    raw::connection refused;
    LT_CHECK(refused.connect(s.port()));
    LT_CHECK(refused.send("POST /echo HTTP/1.1\r\nHost: h\r\n"
                          "Transfer-Encoding: gzip, chunked\r\n\r\n"
                          "0\r\n\r\n"));
    std::deque<observed_response> refused_seen;
    LT_CHECK(refused.receive(1, refused_seen));
    LT_CHECK(refused.receive_close(refused_seen));
    LT_CHECK(refused.peer_closed());
    LT_CHECK_EQ(refused_seen.size(), 1u);
    if (refused_seen.size() == 1) {
        LT_CHECK_EQ(refused_seen[0].status, 501);
    }
LT_END_AUTO_TEST(error_answers_and_close_policy)

// (7) A malformed request line: 400, then close.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, malformed_line_400_close)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /no-version\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK(client.receive_close(seen));
    LT_CHECK(client.peer_closed());
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) LT_CHECK_EQ(seen[0].status, 400);
LT_END_AUTO_TEST(malformed_line_400_close)

// (8) A head over the configured budget: 431, then close.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, head_over_budget_431_close)
    srv::server_options options = base_options();
    options.budgets().set(srv::resource::header_bytes, 128);
    server_fixture s(options);
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    const std::string request =
        "GET /" + std::string(256, 'a') + " HTTP/1.1\r\nHost: h\r\n\r\n";
    LT_CHECK(client.send(request));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK(client.receive_close(seen));
    LT_CHECK(client.peer_closed());
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) LT_CHECK_EQ(seen[0].status, 431);
LT_END_AUTO_TEST(head_over_budget_431_close)

// (9) Pipelining: two GETs in one write, two ordered responses.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, pipelined_gets_answered_in_order)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 3\r\n\r\none"
                         "POST /echo HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 3\r\n\r\ntwo"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) {
        LT_CHECK_EQ(seen[0].body, std::string("one"));
        LT_CHECK_EQ(seen[1].body, std::string("two"));
    }
LT_END_AUTO_TEST(pipelined_gets_answered_in_order)

// (10) A client abort mid-POST-body: the server unwinds and serves the
// next connection.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, client_abort_mid_body_unwinds)
    server_fixture s(base_options());
    raw::connection aborter;
    LT_CHECK(aborter.connect(s.port()));
    LT_CHECK(aborter.send("POST /collect_slow HTTP/1.1\r\nHost: h\r\n"
                          "Content-Length: 64\r\n\r\npartial"));
    // The abort: close without finishing the body.
    aborter.close();
    // The next connection is served normally.
    raw::connection next;
    LT_CHECK(next.connect(s.port()));
    LT_CHECK(next.send("POST /echo HTTP/1.1\r\nHost: h\r\n"
                       "Content-Length: 5\r\n\r\nafter"));
    std::deque<observed_response> seen;
    LT_CHECK(next.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("after"));
    }
LT_END_AUTO_TEST(client_abort_mid_body_unwinds)

// (11) A partial head that never completes: the header watchdog closes
// the connection (observed as the close within a tight deadline).
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, partial_head_header_timeout_close)
    srv::server_options options = base_options();
    options.timeouts().header = std::chrono::milliseconds(300);
    server_fixture s(options);
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /he"));
    std::deque<observed_response> seen;
    const auto began = std::chrono::steady_clock::now();
    LT_CHECK(client.receive_close(
        seen, std::chrono::milliseconds(5000)));
    const auto elapsed = std::chrono::steady_clock::now() - began;
    LT_CHECK(client.peer_closed());
    LT_CHECK(seen.empty());
    LT_CHECK(elapsed < std::chrono::milliseconds(4000));
LT_END_AUTO_TEST(partial_head_header_timeout_close)

// (12) TASK-109: Expect: 100-continue is gated on admission. The wire
// stays silent while the handler has not admitted (no interim, no
// close); releasing the handler yields the 100 first, then the final
// response once the body follows it.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, expect_continue_gated_on_admission)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /late_admit HTTP/1.1\r\nHost: h\r\n"
                         "Expect: 100-continue\r\n"
                         "Content-Length: 4\r\n\r\n"));
    // Pre-admission silence: neither the interim nor a close.
    LT_CHECK(client.quiet_for(std::chrono::milliseconds(300)));
    e2e_late_gate.signal();
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) LT_CHECK_EQ(seen[0].status, 100);
    // The body follows the interim; the final echo closes the exchange.
    LT_CHECK(client.send("ping"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) {
        LT_CHECK_EQ(seen[1].status, 200);
        LT_CHECK_EQ(seen[1].body, std::string("ping"));
    }
LT_END_AUTO_TEST(expect_continue_gated_on_admission)

// (13) TASK-109: a rejected length-framed body drains to its counted
// remainder and the connection is reused -- the 403 commits, the
// remainder discards, the same connection answers the next request.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, reject_drains_and_reuses_connection)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    // Head plus a partial body: the 403 commits while the drain still
    // counts the missing suffix.
    LT_CHECK(client.send("POST /reject HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 16\r\n\r\n"
                         "0123456789"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) LT_CHECK_EQ(seen[0].status, 403);
    // The drain holds the connection open: no close while the counted
    // remainder is outstanding.
    LT_CHECK(client.quiet_for(std::chrono::milliseconds(200)));
    // The remainder, then the next request pipelined behind it.
    LT_CHECK(client.send("abcdef"
                         "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].body, std::string("hello"));
LT_END_AUTO_TEST(reject_drains_and_reuses_connection)

// (14) TASK-109: a handler suspended at head state hits its suspension
// deadline exactly once: the close lands inside the deadline window,
// not one response byte precedes it, and the parked wait resolves as
// cancelled.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, suspended_head_timeout_closes_once)
    srv::server_options options = base_options();
    options.timeouts().suspension = std::chrono::milliseconds(300);
    server_fixture s(options);
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /suspend_head HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 8\r\n\r\n"));
    std::deque<observed_response> seen;
    const auto began = std::chrono::steady_clock::now();
    LT_CHECK(client.receive_close(
        seen, std::chrono::milliseconds(5000)));
    const auto elapsed = std::chrono::steady_clock::now() - began;
    LT_CHECK(client.peer_closed());
    LT_CHECK(seen.empty());
    LT_CHECK(elapsed < std::chrono::milliseconds(2500));
    // The single enforcement cancelled the parked handler wait.
    const auto cancelled = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(5000);
    while (e2e_suspend_outcome.load()
               != static_cast<int>(resume_outcome::cancelled)) {
        if (std::chrono::steady_clock::now() >= cancelled) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    LT_CHECK_EQ(e2e_suspend_outcome.load(),
                static_cast<int>(resume_outcome::cancelled));
LT_END_AUTO_TEST(suspended_head_timeout_closes_once)

// (15) Two concurrent connections interleave: each /rendezvous response
// arrives only after both handlers were running.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, two_clients_interleave)
    server_fixture s(base_options());
    raw::connection first;
    raw::connection second;
    LT_CHECK(first.connect(s.port()));
    LT_CHECK(second.connect(s.port()));
    LT_CHECK(first.send("GET /rendezvous HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(second.send("GET /rendezvous HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen_first;
    std::deque<observed_response> seen_second;
    LT_CHECK(first.receive(1, seen_first));
    LT_CHECK(second.receive(1, seen_second));
    LT_CHECK_EQ(seen_first.size(), 1u);
    LT_CHECK_EQ(seen_second.size(), 1u);
    if (seen_first.size() == 1) LT_CHECK_EQ(seen_first[0].body,
                                            std::string("met"));
    if (seen_second.size() == 1) LT_CHECK_EQ(seen_second[0].body,
                                             std::string("met"));
LT_END_AUTO_TEST(two_clients_interleave)

// (16) DR-V3-008: request_stop() from inside a handler returns (the
// handler finishes), and stop() joins everything promptly.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, request_stop_inside_handler)
    server_fixture s(base_options());
    stop_target = &s.server();
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /stopper HTTP/1.1\r\nHost: h\r\n\r\n"));
    // The handler ran to completion: request_stop() returned to it.
    LT_CHECK(wait_until(stopper_returned));
    stop_target = nullptr;
    const auto began = std::chrono::steady_clock::now();
    s.server().stop();
    const auto elapsed = std::chrono::steady_clock::now() - began;
    LT_CHECK(elapsed < std::chrono::milliseconds(5000));
    LT_CHECK(!s.server().is_running());
LT_END_AUTO_TEST(request_stop_inside_handler)

// (17) REQ-016: invalid options fail typed from listen(); nothing ran.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, invalid_options_fail_typed)
    srv::server_options empty;
    srv::native_server server(empty);
    const http::outcome listened = server.listen();
    LT_CHECK(!listened.ok());
    LT_CHECK(listened.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!server.is_running());
    server.stop();
LT_END_AUTO_TEST(invalid_options_fail_typed)

// (18) Connections budget = 1: while one connection holds its seat, a
// second is closed without a response.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, connections_budget_refuses_second)
    srv::server_options options = base_options();
    options.budgets().set(srv::resource::connections, 1);
    server_fixture s(options);
    raw::connection holder;
    LT_CHECK(holder.connect(s.port()));
    LT_CHECK(holder.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    // The response proves the first engine took the seat; the
    // keep-alive connection then idles ON the seat.
    std::deque<observed_response> holder_seen;
    LT_CHECK(holder.receive(1, holder_seen));
    LT_CHECK_EQ(holder_seen.size(), 1u);
    raw::connection refused;
    LT_CHECK(refused.connect(s.port()));
    std::deque<observed_response> none;
    LT_CHECK(refused.receive_close(none));
    LT_CHECK(refused.peer_closed());
    LT_CHECK(none.empty());
LT_END_AUTO_TEST(connections_budget_refuses_second)

// (19) TASK-110: a drain over a live in-flight exchange completes once
// that work ends -- the held-open response flushes in full, the client
// observes the whole body then the close, and the ticket reports
// completed with nothing remaining.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, drain_completes_after_inflight_work_ends)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /drain_gated HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(wait_until(e2e_drain_entered));
    srv::drain_ticket ticket;
    const http::outcome began = s.server().begin_drain(
        std::chrono::milliseconds(5000), ticket);
    LT_CHECK(began.ok());
    e2e_drain_gate.signal();
    srv::drain_result observed;
    const http::outcome waited = ticket.wait(observed);
    LT_CHECK(waited.ok());
    LT_CHECK(observed.status == srv::drain_status::completed);
    LT_CHECK_EQ(observed.remaining, std::size_t{0});
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK(client.receive_close(seen));
    LT_CHECK(client.peer_closed());
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("gated"));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    const auto began_stop = std::chrono::steady_clock::now();
    s.server().stop();
    const auto elapsed = std::chrono::steady_clock::now() - began_stop;
    LT_CHECK(elapsed < std::chrono::milliseconds(5000));
LT_END_AUTO_TEST(drain_completes_after_inflight_work_ends)

// (20) TASK-110: a drain whose work never ends expires at its
// deadline -- the pre-cancel remaining is reported, the cancel hook
// hard-stops the hung exchange (its parked wait resolves cancelled),
// and the client observes the close.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, drain_deadline_expires_and_cancels_hung_work)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /hang_forever HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(wait_until(e2e_hang_entered));
    srv::drain_ticket ticket;
    const http::outcome began = s.server().begin_drain(
        std::chrono::milliseconds(300), ticket);
    LT_CHECK(began.ok());
    srv::drain_result observed;
    const http::outcome waited = ticket.wait(observed);
    LT_CHECK(waited.ok());
    LT_CHECK(observed.status == srv::drain_status::deadline_expired);
    LT_CHECK(observed.remaining >= 1);
    std::deque<observed_response> none;
    LT_CHECK(client.receive_close(none));
    LT_CHECK(client.peer_closed());
    LT_CHECK(none.empty());
    const auto cancelled_by = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(5000);
    while (e2e_hang_outcome.load()
               != static_cast<int>(resume_outcome::cancelled)) {
        if (std::chrono::steady_clock::now() >= cancelled_by) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    LT_CHECK_EQ(e2e_hang_outcome.load(),
                static_cast<int>(resume_outcome::cancelled));
    s.server().stop();
LT_END_AUTO_TEST(drain_deadline_expires_and_cancels_hung_work)

// (21) TASK-110: a handler may begin a drain and try to wait on it --
// begin_drain returns ok (nonblocking), the wait refuses
// would_deadlock at once, the handler returns, and the very same
// ticket waited externally afterwards reports completed.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, drain_wait_from_handler_is_would_deadlock)
    server_fixture s(base_options());
    srv::drain_ticket ticket;
    drain_begin_target = &s.server();
    drain_begin_ticket = &ticket;
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /drain_caller HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(wait_until(e2e_caller_returned));
    drain_begin_target = nullptr;
    drain_begin_ticket = nullptr;
    LT_CHECK(e2e_caller_began_ok.load());
    LT_CHECK_EQ(e2e_caller_wait_code.load(),
                static_cast<int>(http::outcome_code::would_deadlock));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK(client.receive_close(seen));
    LT_CHECK(client.peer_closed());
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("ok"));
    }
    srv::drain_result observed;
    const http::outcome waited = ticket.wait(observed);
    LT_CHECK(waited.ok());
    LT_CHECK(observed.status == srv::drain_status::completed);
    LT_CHECK_EQ(observed.remaining, std::size_t{0});
    s.server().stop();
LT_END_AUTO_TEST(drain_wait_from_handler_is_would_deadlock)

// (22) TASK-110: one drain closes both shapes at once -- the idle
// keep-alive connection (nothing in flight) and the busy one (an
// exchange held open until released); both observe their close, the
// held response arrives in full first, and the ticket completes.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, drain_closes_idle_and_busy_connections)
    server_fixture s(base_options());
    raw::connection idle_client;
    LT_CHECK(idle_client.connect(s.port()));
    LT_CHECK(idle_client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> idle_seen;
    LT_CHECK(idle_client.receive(1, idle_seen));
    LT_CHECK_EQ(idle_seen.size(), 1u);
    if (idle_seen.size() == 1) LT_CHECK_EQ(idle_seen[0].body,
                                           std::string("hello"));
    raw::connection busy_client;
    LT_CHECK(busy_client.connect(s.port()));
    LT_CHECK(busy_client.send("GET /drain_gated HTTP/1.1\r\nHost: h\r\n"
                              "\r\n"));
    LT_CHECK(wait_until(e2e_drain_entered));
    srv::drain_ticket ticket;
    const http::outcome began = s.server().begin_drain(
        std::chrono::milliseconds(5000), ticket);
    LT_CHECK(began.ok());
    e2e_drain_gate.signal();
    std::deque<observed_response> idle_close;
    LT_CHECK(idle_client.receive_close(idle_close));
    LT_CHECK(idle_client.peer_closed());
    LT_CHECK(idle_close.empty());
    std::deque<observed_response> busy_seen;
    LT_CHECK(busy_client.receive(1, busy_seen));
    LT_CHECK(busy_client.receive_close(busy_seen));
    LT_CHECK(busy_client.peer_closed());
    LT_CHECK_EQ(busy_seen.size(), 1u);
    if (busy_seen.size() == 1) {
        LT_CHECK_EQ(busy_seen[0].status, 200);
        LT_CHECK_EQ(busy_seen[0].body, std::string("gated"));
    }
    srv::drain_result observed;
    const http::outcome waited = ticket.wait(observed);
    LT_CHECK(waited.ok());
    LT_CHECK(observed.status == srv::drain_status::completed);
    LT_CHECK_EQ(observed.remaining, std::size_t{0});
    s.server().stop();
LT_END_AUTO_TEST(drain_closes_idle_and_busy_connections)

// (23) TASK-110: the stop halves stay split -- after a request_stop()
// no drain may begin, and the failed begin leaves the ticket empty.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, begin_drain_after_request_stop_invalid_state)
    server_fixture s(base_options());
    s.server().request_stop();
    srv::drain_ticket ticket;
    const http::outcome began = s.server().begin_drain(
        std::chrono::milliseconds(1000), ticket);
    LT_CHECK(began.code() == http::outcome_code::invalid_state);
    srv::drain_result observed;
    LT_CHECK(ticket.wait(observed).code()
             == http::outcome_code::invalid_state);
    s.server().stop();
LT_END_AUTO_TEST(begin_drain_after_request_stop_invalid_state)

// (24) TASK-111: a synchronous value-returning route -- a plain
// lambda, no coroutine code -- serves a POST within its cap over the
// wire (head fidelity and body echo included) and the connection stays
// keep-alive for the next request.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_sync_route_within_cap)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /sync-echo HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 11\r\n\r\nhello sync!"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        // The handler saw the head and the exact body bytes.
        LT_CHECK_EQ(seen[0].body, std::string("/sync-echo:hello sync!"));
        // The adapter pinned the value's Content-Length.
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK_EQ(sync_echo_invocations.load(), 1);
    // Keep-alive: the same connection answers the next request.
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].body, std::string("hello"));
    LT_CHECK_EQ(sync_echo_invocations.load(), 1);
LT_END_AUTO_TEST(http11_post_sync_route_within_cap)

// (25) TASK-111: a body over the sync route's cap answers 413 without
// invoking the handler; the admitted-undrained settle (TASK-109)
// discards the remainder and the connection is reused.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_sync_route_over_cap_413)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /sync-echo HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 32\r\n\r\n"
                         + std::string(32, 'x')));   // cap is 16
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) LT_CHECK_EQ(seen[0].status, 413);
    // The handler is provably never invoked.
    LT_CHECK_EQ(sync_echo_invocations.load(), 0);
    // The settle drained the counted remainder: the same connection
    // answers the next request.
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].body, std::string("hello"));
    LT_CHECK_EQ(sync_echo_invocations.load(), 0);
LT_END_AUTO_TEST(http11_post_sync_route_over_cap_413)

// (26) TASK-111: a BODYLESS POST (no Content-Length: a legal request
// with no body framing -- the engine hands the exchange no body source)
// is served by the sync route, not 500'd: the handler sees an empty
// span and the connection stays keep-alive.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_sync_route_bodyless)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /sync-echo HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        // The handler saw the head and an empty body.
        LT_CHECK_EQ(seen[0].body, std::string("/sync-echo:"));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK_EQ(sync_echo_invocations.load(), 1);
    // Keep-alive: the same connection answers the next request.
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].body, std::string("hello"));
    LT_CHECK_EQ(sync_echo_invocations.load(), 1);
LT_END_AUTO_TEST(http11_post_sync_route_bodyless)

// (27) TASK-112: one shared immutable definition serves two CONCURRENT
// connections (DR-V3-005): every body is the same shared bytes, every
// head carries the shared base fields plus its own overlay
// (X-Conn-Idx) appended after them, and each send's framing is the
// definition's pinned Content-Length.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, shared_definition_serves_two_connections)
    server_fixture s(base_options());
    raw::connection first;
    raw::connection second;
    LT_CHECK(first.connect(s.port()));
    LT_CHECK(second.connect(s.port()));
    LT_CHECK(first.send("GET /asset HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(second.send("GET /asset HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen_first;
    std::deque<observed_response> seen_second;
    LT_CHECK(first.receive(1, seen_first));
    LT_CHECK(second.receive(1, seen_second));

    const std::string expected(1024, 'a');
    const observed_response* replies[2] = {nullptr, nullptr};
    if (seen_first.size() == 1) replies[0] = &seen_first[0];
    if (seen_second.size() == 1) replies[1] = &seen_second[0];
    std::string conn_tags[2];
    for (int i = 0; i < 2; ++i) {
        const observed_response* const r = replies[i];
        if (r == nullptr) continue;
        LT_CHECK_EQ(r->status, 200);
        LT_CHECK_EQ(r->body, expected);
        LT_CHECK_EQ(r->framing, std::string("content-length"));
        // Shared base field present in both, exactly one overlay
        // header, ordered after it.
        const std::string* content_type = nullptr;
        std::size_t content_type_at = r->headers.size();
        const std::string* conn_idx = nullptr;
        std::size_t conn_idx_at = r->headers.size();
        for (std::size_t h = 0; h < r->headers.size(); ++h) {
            const std::string name =
                lowered_header_name(r->headers[h].name);
            if (name == "content-type") {
                content_type = &r->headers[h].value;
                content_type_at = h;
            } else if (name == "x-conn-idx") {
                conn_idx = &r->headers[h].value;
                conn_idx_at = h;
            }
        }
        LT_CHECK(content_type != nullptr);
        if (content_type != nullptr) {
            LT_CHECK_EQ(*content_type,
                        std::string("application/octet-stream"));
        }
        LT_CHECK(conn_idx != nullptr);
        if (conn_idx != nullptr) {
            LT_CHECK(!conn_idx->empty());
            conn_tags[i] = *conn_idx;
            LT_CHECK(conn_idx_at > content_type_at);
        }
    }
    // The per-send overlays are per connection: two distinct tags.
    LT_CHECK(!conn_tags[0].empty());
    LT_CHECK(!conn_tags[1].empty());
    LT_CHECK(conn_tags[0] != conn_tags[1]);
LT_END_AUTO_TEST(shared_definition_serves_two_connections)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
