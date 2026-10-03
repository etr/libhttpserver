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

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/auth/basic_auth.hpp>
#include <httpserver/auth/digest_auth.hpp>
#include <httpserver/body_reader.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/forms/multipart.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/response_definition.hpp>
#include <httpserver/server/hooks.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/server.hpp>

#include "./digest_client.hpp"
#include "../integ/raw_http_client.hpp"
#include "../unit/response_source_rig.hpp"
#include "./littletest.hpp"

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;
namespace auth = httpserver::auth;
namespace dclient = httpserver_test;
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

// TASK-114: GET /secret behind make_basic_guard -- the guarded body.
// Authenticated requests are served the parity fixture's pinned
// secret response (text/plain, Content-Length: 9, "secret-ok").
task<void> secret_handler(exchange& x) {
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", "9");
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::string body = "secret-ok";
    const std::byte* raw = reinterpret_cast<const std::byte*>(body.data());
    co_await x.writer().write(std::span<const std::byte>(raw, body.size()));
    co_await x.writer().finish();
}

// TASK-115: GET /digest* behind make_digest_guard -- the parity
// fixture's Digest success response (text/plain, Content-Length: 9,
// "digest-ok").
task<void> digest_ok_handler(exchange& x) {
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", "9");
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::string body = "digest-ok";
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

// TASK-113: GET /one-shot-asset serves a TRANSFERRED std::FILE* to
// the first request only. That send claims the one-shot definition
// and streams it; every later request's send fails invalid_state
// BEFORE any head or body traffic, and the handler co_returns without
// a terminal action, so run_route synthesizes the 500 (the same
// posture as /throwing). The scratch asset is only the transfer's
// origin: once handed over, the library's handle is the memory's
// only owner that matters.
httpserver::response_definition& one_shot_definition() {
    static httpserver_test::temp_file asset{std::string(1536, 'z')};
    static httpserver::response_definition def = [] {
        httpserver::response_definition built;
        http::fields f;
        f.append("Content-Type", "application/octet-stream");
        static_cast<void>(httpserver::response_definition::owned_file(
            http::status::from_code(200), f,
            httpserver_test::open_for_read(asset), built));
        return built;
    }();
    return def;
}

task<void> one_shot_asset_handler(exchange& x) {
    static_cast<void>(co_await httpserver::send_definition(
        x, one_shot_definition(), {}));
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

// TASK-116 form-route state: /echo_form counts its invocations so the
// over-cap and malformed cases can prove the handler never ran.
std::atomic<int> form_echo_invocations{0};

// TASK-116: the /echo_form handler value -- every decoded entry echoed
// as name=value joined by ';' (for the parity body "a=1&b=two" this is
// exactly the v2 fixture's "a=1;b=two").
srv::sync_response form_echo_value(
    const httpserver::forms::form_fields& fields) {
    ++form_echo_invocations;
    std::string echo;
    bool first = true;
    for (const std::pair<std::string, std::string>& e :
         fields.entries()) {
        if (!first) echo += ";";
        first = false;
        echo += e.first + "=" + e.second;
    }
    srv::sync_response out;
    out.status = http::status::from_code(200);
    out.fields.append("Content-Type", "text/plain");
    const std::byte* raw = reinterpret_cast<const std::byte*>(echo.data());
    out.body.assign(raw, raw + echo.size());
    return out;
}

// TASK-117 multipart state. The disk scenarios point e2e_disk_dir() at
// a scratch directory before connecting (the suite runs sequentially).
std::atomic<int> upload_note_invocations{0};
std::atomic<int> upload_capped_invocations{0};
std::string& e2e_disk_dir() {
    static std::string dir;
    return dir;
}

// TASK-117: the /upload handler value -- the v2 fixture's posture
// (note=<v>, text/plain, length-framed).
srv::sync_response upload_note_value(
    const httpserver::forms::form_fields& fields) {
    ++upload_note_invocations;
    const std::string echo =
        "note=" + std::string(fields.value("note").value_or(""));
    srv::sync_response out;
    out.status = http::status::from_code(200);
    out.fields.append("Content-Type", "text/plain");
    const std::byte* raw = reinterpret_cast<const std::byte*>(echo.data());
    out.body.assign(raw, raw + echo.size());
    return out;
}

// TASK-117: /upload_capped -- the bounded-upload scenario's route,
// counting invocations so the 413 case can prove the handler never
// ran.
srv::sync_response upload_capped_value(
    const httpserver::forms::form_fields&) {
    ++upload_capped_invocations;
    srv::sync_response out;
    out.status = http::status::from_code(200);
    return out;
}

// TASK-117: /upload_disk -- a coroutine route driving read_multipart
// with temp_file_part_sink (the disk-upload round trip). The sink
// lives on the handler's frame: completed files are kept (the caller
// verifies the bytes) and removed at handler exit otherwise. The
// response body is the created file's path.
task<void> upload_disk_handler(exchange& x) {
    httpserver::forms::temp_file_options options;
    options.directory = e2e_disk_dir();
    options.should_keep =
        [](const std::string&, const std::string&,
           const httpserver::forms::part_file_info&) { return true; };
    httpserver::forms::temp_file_part_sink sink(std::move(options));
    const httpserver::forms::multipart_read read =
        co_await httpserver::forms::read_multipart(
            x, httpserver::forms::multipart_limits{16 << 20, 64,
                                                    16 << 20, 8192},
            sink);
    if (!read.ok()) co_return;
    const std::string path = sink.completed().empty()
        ? std::string()
        : sink.completed().front().file_system_file_name;
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", std::to_string(path.size()));
    static_cast<void>(x.start_response(http::status::from_code(200), f));
    const std::byte* raw = reinterpret_cast<const std::byte*>(path.data());
    co_await x.writer().write(
        std::span<const std::byte>(raw, path.size()));
    co_await x.writer().finish();
}

// TASK-117: /upload_slow -- the same streaming read without a keep
// callback, parked mid-body by the scenario's partial write; the
// client abort mid-file-part must unwind it and leave the scratch
// directory empty.
task<void> upload_slow_handler(exchange& x) {
    httpserver::forms::temp_file_part_sink sink(
        httpserver::forms::temp_file_options{e2e_disk_dir()});
    static_cast<void>(co_await httpserver::forms::read_multipart(
        x, httpserver::forms::multipart_limits{}, sink));
}

// TASK-117: one scratch directory per disk scenario (mkdtemp),
// removed recursively on destruction.
class e2e_scratch_dir {
 public:
    e2e_scratch_dir() {
        char tmpl[] = "/tmp/lht-e2e-upload-XXXXXX";
        char* made = mkdtemp(tmpl);
        if (made != nullptr) path_ = made;
    }

    ~e2e_scratch_dir() {
        if (path_.empty()) return;
        for (const std::string& name : entries()) {
            std::remove((path_ + "/" + name).c_str());
        }
        std::remove(path_.c_str());
    }

    const std::string& path() const noexcept { return path_; }

    bool empty() const { return entries().empty(); }

    std::vector<std::string> entries() const {
        std::vector<std::string> out;
        DIR* dir = opendir(path_.c_str());
        if (dir == nullptr) return out;
        while (dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name != "." && name != "..") out.push_back(name);
        }
        closedir(dir);
        return out;
    }

 private:
    std::string path_;
};

// TASK-117: deadline-bound poll for the scratch directory to become
// non-empty (the partial file appears) or empty again (the abort
// removed it).
bool wait_dir_state(const e2e_scratch_dir& dir, bool want_empty,
                    std::chrono::milliseconds budget =
                        std::chrono::milliseconds(5000)) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        if (dir.empty() == want_empty) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

std::string read_all(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return "<missing>";
    std::string out;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    std::fclose(f);
    return out;
}

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

// One Digest-guarded route under the parity fixture's posture (realm
// "transcript", bob/builder, the "digest required" challenge body),
// with the scenario's ttl and algorithm.
void route_digest(srv::native_server& server, const char* path,
                  std::chrono::seconds ttl,
                  auth::digest_algorithm algorithm) {
    auth::digest_auth_policy policy;
    auth::digest_auth_options options;
    options.algorithm = algorithm;
    options.nonce_ttl = ttl;
    options.challenge_body = "digest required";
    static_cast<void>(auth::digest_auth_policy::create(
        "transcript", "bob", "builder", options, policy));
    static_cast<void>(server.route(
        http::method::known(http::method_id::get), path,
        auth::make_digest_guard(std::move(policy), digest_ok_handler)));
}

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
        // TASK-114: /secret answers through the Basic auth guard (the
        // v2 parity fixture's credentials and realm).
        httpserver::auth::basic_auth_policy secret_policy;
        static_cast<void>(httpserver::auth::basic_auth_policy::create(
            "transcript", "alice", "wonderland", secret_policy));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::get), "/secret",
            httpserver::auth::make_basic_guard(
                std::move(secret_policy), secret_handler)));
        // TASK-115: the /digest family answers through the Digest auth
        // guard (the v2 parity fixture's credentials and realm; the
        // "digest required" challenge body). One route per posture:
        // the default MD5 policy, a ttl=0 stale-exercise policy, and a
        // SHA-256 policy.
        route_digest(server_, "/digest", std::chrono::seconds(300),
                     auth::digest_algorithm::md5);
        route_digest(server_, "/digest-stale", std::chrono::seconds(0),
                     auth::digest_algorithm::md5);
        route_digest(server_, "/digest-sha", std::chrono::seconds(300),
                     auth::digest_algorithm::sha_256);
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
            http::method::known(http::method_id::get), "/one-shot-asset",
            one_shot_asset_handler));
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
        // TASK-116: the bounded urlencoded form route (16-byte body
        // cap, the adapter's default field budget).
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/echo_form",
            httpserver::forms::make_urlencoded_route(
                httpserver::forms::urlencoded_limits{16, 64},
                [](const http::request_head&,
                   const httpserver::forms::form_fields& fields)
                    -> srv::sync_response {
                    return form_echo_value(fields);
                })));
        // TASK-117: the multipart routes -- the parity-posture field
        // echo (default budgets), the small-capped bounded-upload
        // probe, and the two streaming disk-upload coroutines.
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/upload",
            httpserver::forms::make_multipart_route(
                httpserver::forms::multipart_limits{},
                [](const http::request_head&,
                   const httpserver::forms::form_fields& fields)
                    -> srv::sync_response {
                    return upload_note_value(fields);
                })));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/upload_capped",
            httpserver::forms::make_multipart_route(
                httpserver::forms::multipart_limits{65536, 64, 65536,
                                                    8192},
                [](const http::request_head&,
                   const httpserver::forms::form_fields& fields)
                    -> srv::sync_response {
                    return upload_capped_value(fields);
                })));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/upload_disk",
            upload_disk_handler));
        static_cast<void>(server_.route(
            http::method::known(http::method_id::post), "/upload_slow",
            upload_slow_handler));
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
        form_echo_invocations.store(0);
        upload_note_invocations.store(0);
        upload_capped_invocations.store(0);
        e2e_disk_dir().clear();
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

// (28) TASK-113: a transferred one-shot file handle serves exactly
// once over the wire: the first GET streams the exact body under the
// probed Content-Length; the second GET finds the definition spent,
// its send failing before anything is written, so the engine
// synthesizes the 500 on the SAME keep-alive connection.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, one_shot_definition_serves_once_then_500)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /one-shot-asset HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string(1536, 'z'));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK(client.send("GET /one-shot-asset HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].status, 500);
LT_END_AUTO_TEST(one_shot_definition_serves_once_then_500)

// (29) TASK-114: GET /secret behind make_basic_guard over the wire,
// all on ONE keep-alive connection: (a) no credentials answers the
// exact challenge framing (401, WWW-Authenticate: Basic
// realm="transcript", Content-Length: 0, empty body); (b) invalid
// credentials answer the same challenge; (c) valid credentials are
// served the secret; and the connection is still serving requests
// after a challenge (the 401 never poisons the pipeline).
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, basic_auth_guard_secret_round_trip)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    std::deque<observed_response> seen;
    // (a) absent Authorization.
    LT_CHECK(client.send("GET /secret HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 401);
        const std::string* challenge = nullptr;
        for (const auto& h : seen[0].headers) {
            if (lowered_header_name(h.name) == "www-authenticate") {
                challenge = &h.value;
            }
        }
        LT_CHECK(challenge != nullptr);
        if (challenge != nullptr) {
            LT_CHECK_EQ(*challenge, std::string("Basic realm=\"transcript\""));
        }
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
        LT_CHECK_EQ(seen[0].body, std::string(""));
    }
    // (b) invalid credentials: the same challenge, still keep-alive.
    LT_CHECK(client.send("GET /secret HTTP/1.1\r\nHost: h\r\n"
                         "Authorization: Basic aW52YWxpZDppbnZhbGlk\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) {
        LT_CHECK_EQ(seen[1].status, 401);
        const std::string* challenge = nullptr;
        for (const auto& h : seen[1].headers) {
            if (lowered_header_name(h.name) == "www-authenticate") {
                challenge = &h.value;
            }
        }
        LT_CHECK(challenge != nullptr);
        if (challenge != nullptr) {
            LT_CHECK_EQ(*challenge, std::string("Basic realm=\"transcript\""));
        }
    }
    // (c) valid credentials on the SAME connection: served the secret.
    LT_CHECK(client.send("GET /secret HTTP/1.1\r\nHost: h\r\n"
                         "Authorization: Basic YWxpY2U6d29uZGVybGFuZA==\r\n"
                         "\r\n"));
    LT_CHECK(client.receive(3, seen));
    LT_CHECK_EQ(seen.size(), 3u);
    if (seen.size() == 3) {
        LT_CHECK_EQ(seen[2].status, 200);
        LT_CHECK_EQ(seen[2].body, std::string("secret-ok"));
        LT_CHECK_EQ(seen[2].framing, std::string("content-length"));
    }
LT_END_AUTO_TEST(basic_auth_guard_secret_round_trip)

// TASK-115: the Digest e2e scenarios share one client-side posture:
// fetch the challenge with a bare request, answer it with the
// INDEPENDENT test-side RFC 7616 client, and ship the computed
// Authorization on the SAME keep-alive connection.
namespace {

const std::string* challenge_of(const observed_response& r) {
    for (const auto& h : r.headers) {
        if (lowered_header_name(h.name) == "www-authenticate") {
            return &h.value;
        }
    }
    return nullptr;
}

bool is_lower_hex(const std::string& s) {
    for (const char c : s) {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        if (!digit && !lower) return false;
    }
    return !s.empty();
}

// The Authorization value answering @p challenge as @p user/@p
// password under @p hash (one cnonce drawn for both the response and
// the header).
std::string answered_digest(const dclient::parsed_challenge& challenge,
                            dclient::digest_hash hash, const char* user,
                            const char* password, const char* uri) {
    const std::string cnonce = dclient::make_cnonce();
    const std::string response = dclient::compute_response_cleartext(
        challenge, hash, "GET", uri, user, password, cnonce, "00000001");
    return dclient::build_authorization_header(
        challenge, user, uri, cnonce, "00000001", response);
}

const char* const k_digest_prefix = "Digest realm=\"transcript\", qop=\"auth\", algorithm=";
const char* const k_digest_suffix = ", charset=UTF-8";
constexpr std::size_t k_opaque_marker_width = 11;  // closing quote + ", opaque=" + opening quote

// Asserts the pinned challenge shape on a 401: the six fields in the
// pinned order and quoting, a 112-hex nonce, a 32-hex opaque, no
// stale hint, the pinned framing and challenge body. Returns the
// first violated pin ("" = the shape held) -- littletest checks only
// run inside test bodies, so the helper reports instead.
std::string digest_challenge_diff(const observed_response& r,
                                  const char* algorithm) {
    if (r.status != 401) return "challenge status " + std::to_string(r.status);
    if (r.framing != "content-length") {
        return "challenge framing " + r.framing;
    }
    if (r.body != "digest required") return "challenge body " + r.body;
    const std::string* challenge = challenge_of(r);
    if (challenge == nullptr) return "no WWW-Authenticate on the 401";
    const std::string prefix =
        std::string(k_digest_prefix) + algorithm + ", nonce=\"";
    if (challenge->rfind(prefix, 0) != 0) {
        return "challenge does not open with the pinned prefix: "
            + *challenge;
    }
    const std::size_t opaque_marker = challenge->find("\", opaque=\"");
    const std::size_t suffix_at = challenge->find(k_digest_suffix);
    if (opaque_marker == std::string::npos
            || suffix_at == std::string::npos
            || suffix_at < opaque_marker) {
        return "challenge missing the opaque/charset fields: " + *challenge;
    }
    const std::size_t nonce_at = prefix.size();
    const std::size_t opaque_at = opaque_marker + k_opaque_marker_width;
    const std::string nonce =
        challenge->substr(nonce_at, opaque_marker - nonce_at);
    // The opaque's closing quote sits immediately before ", charset".
    const std::string opaque =
        challenge->substr(opaque_at, suffix_at - opaque_at - 1);
    if (nonce.size() != 112 || !is_lower_hex(nonce)) {
        return "nonce is not 112 lowercase hex: " + nonce;
    }
    if (opaque.size() != 32 || !is_lower_hex(opaque)) {
        return "opaque is not 32 lowercase hex: " + opaque;
    }
    if (challenge->substr(suffix_at) != k_digest_suffix) {
        return "challenge does not end at charset: " + *challenge;
    }
    if (challenge->find("stale") != std::string::npos) {
        return "challenge carries a stale hint: " + *challenge;
    }
    return "";
}

}  // namespace

// (30) TASK-115: the Digest valid flow on ONE keep-alive connection:
// the bare GET answers the pinned 401 challenge; the answered request
// is served the secret; the connection is still serving afterwards.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, digest_auth_guard_round_trip)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    std::deque<observed_response> seen;
    LT_CHECK(client.send("GET /digest HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        const std::string diff = digest_challenge_diff(seen[0], "MD5");
        if (!diff.empty()) std::cerr << "[digest e2e] " << diff << "\n";
        LT_CHECK(diff.empty());
        const std::string* value = challenge_of(seen[0]);
        if (value != nullptr) {
            const auto parsed = dclient::parse_www_authenticate(*value);
            LT_CHECK(parsed.has_value());
            if (parsed.has_value()) {
                LT_CHECK(client.send(
                    "GET /digest HTTP/1.1\r\nHost: h\r\nAuthorization: "
                    + answered_digest(*parsed, dclient::digest_hash::md5,
                                      "bob", "builder", "/digest")
                    + "\r\n\r\n"));
                LT_CHECK(client.receive(2, seen));
                LT_CHECK_EQ(seen.size(), 2u);
                if (seen.size() == 2) {
                    LT_CHECK_EQ(seen[1].status, 200);
                    LT_CHECK_EQ(seen[1].body, std::string("digest-ok"));
                    LT_CHECK_EQ(seen[1].framing,
                                std::string("content-length"));
                }
            }
        }
    }
    // The 401 never poisoned the pipeline: the connection still serves.
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(seen.size() + 1, seen));
    LT_CHECK_EQ(seen.back().body, std::string("hello"));
LT_END_AUTO_TEST(digest_auth_guard_round_trip)

// (31) TASK-115: a ttl=0 nonce is expired by the time the answer
// arrives: the re-challenge carries stale=TRUE and a FRESH nonce.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, digest_stale_nonce_rechallenges)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    std::deque<observed_response> seen;
    LT_CHECK(client.send("GET /digest-stale HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() != 1) return;
    const std::string* first_challenge = challenge_of(seen[0]);
    LT_CHECK(first_challenge != nullptr);
    if (first_challenge == nullptr) return;
    const auto parsed = dclient::parse_www_authenticate(*first_challenge);
    LT_CHECK(parsed.has_value());
    if (!parsed.has_value()) return;
    // ttl=0: the offered nonce expires when the wall clock advances.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    LT_CHECK(client.send(
        "GET /digest-stale HTTP/1.1\r\nHost: h\r\nAuthorization: "
        + answered_digest(*parsed, dclient::digest_hash::md5, "bob",
                          "builder", "/digest-stale")
        + "\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) {
        LT_CHECK_EQ(seen[1].status, 401);
        const std::string* re_challenge = challenge_of(seen[1]);
        LT_CHECK(re_challenge != nullptr);
        if (re_challenge != nullptr) {
            LT_CHECK(re_challenge->find("stale=TRUE")
                     != std::string::npos);
            LT_CHECK_EQ(re_challenge->substr(
                            re_challenge->size()
                            - std::string(", stale=TRUE").size()),
                        std::string(", stale=TRUE"));
            // The stale re-challenge offers a NEW nonce.
            LT_CHECK(*re_challenge != *first_challenge);
        }
    }
LT_END_AUTO_TEST(digest_stale_nonce_rechallenges)

// (32) TASK-115: identical Authorization bytes twice on one
// connection: the first authenticates, the second is a replay that
// answers 401 WITHOUT the stale hint.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, digest_replayed_nonce_rejected)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    std::deque<observed_response> seen;
    LT_CHECK(client.send("GET /digest HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() != 1) return;
    const std::string* value = challenge_of(seen[0]);
    LT_CHECK(value != nullptr);
    if (value == nullptr) return;
    const auto parsed = dclient::parse_www_authenticate(*value);
    LT_CHECK(parsed.has_value());
    if (!parsed.has_value()) return;
    const std::string authorization =
        answered_digest(*parsed, dclient::digest_hash::md5, "bob",
                        "builder", "/digest");
    LT_CHECK(client.send("GET /digest HTTP/1.1\r\nHost: h\r\nAuthorization: "
                         + authorization + "\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) {
        LT_CHECK_EQ(seen[1].status, 200);
        LT_CHECK_EQ(seen[1].body, std::string("digest-ok"));
    }
    // The replay: byte-identical Authorization.
    LT_CHECK(client.send("GET /digest HTTP/1.1\r\nHost: h\r\nAuthorization: "
                         + authorization + "\r\n\r\n"));
    LT_CHECK(client.receive(3, seen));
    LT_CHECK_EQ(seen.size(), 3u);
    if (seen.size() == 3) {
        LT_CHECK_EQ(seen[2].status, 401);
        const std::string* re_challenge = challenge_of(seen[2]);
        LT_CHECK(re_challenge != nullptr);
        if (re_challenge != nullptr) {
            LT_CHECK(re_challenge->find("stale") == std::string::npos);
        }
    }
LT_END_AUTO_TEST(digest_replayed_nonce_rejected)

// (33) TASK-115: a malformed Digest field answers the standard 401
// challenge (never a 400); a well-formed but WRONG password answers
// the same challenge without stale -- both on one connection.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, digest_malformed_and_wrong_password)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    std::deque<observed_response> seen;
    LT_CHECK(client.send("GET /digest HTTP/1.1\r\nHost: h\r\n"
                         "Authorization: Digest not-an-auth-param\r\n\r\n"));
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        const std::string diff = digest_challenge_diff(seen[0], "MD5");
        if (!diff.empty()) std::cerr << "[digest e2e] " << diff << "\n";
        LT_CHECK(diff.empty());
    }

    // A well-formed answer under the wrong password.
    LT_CHECK(client.send("GET /digest HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() != 2) return;
    const std::string* value = challenge_of(seen[1]);
    LT_CHECK(value != nullptr);
    if (value == nullptr) return;
    const auto parsed = dclient::parse_www_authenticate(*value);
    LT_CHECK(parsed.has_value());
    if (!parsed.has_value()) return;
    LT_CHECK(client.send(
        "GET /digest HTTP/1.1\r\nHost: h\r\nAuthorization: "
        + answered_digest(*parsed, dclient::digest_hash::md5, "bob",
                          "wrong", "/digest")
        + "\r\n\r\n"));
    LT_CHECK(client.receive(3, seen));
    LT_CHECK_EQ(seen.size(), 3u);
    if (seen.size() == 3) {
        LT_CHECK_EQ(seen[2].status, 401);
        LT_CHECK_EQ(seen[2].body, std::string("digest required"));
        const std::string* re_challenge = challenge_of(seen[2]);
        LT_CHECK(re_challenge != nullptr);
        if (re_challenge != nullptr) {
            LT_CHECK(re_challenge->find("stale") == std::string::npos);
        }
    }
LT_END_AUTO_TEST(digest_malformed_and_wrong_password)

// (34) TASK-115: the SHA-256 policy round-trips with the same
// contract: the challenge names algorithm=SHA-256 and the answered
// request is served the secret.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, digest_sha256_round_trip)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    std::deque<observed_response> seen;
    LT_CHECK(client.send("GET /digest-sha HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() != 1) return;
    const std::string sha_diff = digest_challenge_diff(seen[0], "SHA-256");
    if (!sha_diff.empty()) std::cerr << "[digest e2e] " << sha_diff << "\n";
    LT_CHECK(sha_diff.empty());
    const std::string* value = challenge_of(seen[0]);
    LT_CHECK(value != nullptr);
    if (value == nullptr) return;
    const auto parsed = dclient::parse_www_authenticate(*value);
    LT_CHECK(parsed.has_value());
    if (!parsed.has_value()) return;
    LT_CHECK(client.send(
        "GET /digest-sha HTTP/1.1\r\nHost: h\r\nAuthorization: "
        + answered_digest(*parsed, dclient::digest_hash::sha256, "bob",
                          "builder", "/digest-sha")
        + "\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) {
        LT_CHECK_EQ(seen[1].status, 200);
        LT_CHECK_EQ(seen[1].body, std::string("digest-ok"));
        LT_CHECK_EQ(seen[1].framing, std::string("content-length"));
    }
LT_END_AUTO_TEST(digest_sha256_round_trip)

// (35) TASK-116: the urlencoded form route round-trips over the wire
// -- decoded fields (repeats in order, %HH decoded) reach the handler
// and the echoed value is length-framed -- and the connection stays
// keep-alive.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_form_round_trip)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo_form HTTP/1.1\r\nHost: h\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n"
                         "Content-Length: 14\r\n\r\n"
                         "k=1&k=2&x=%2Fp"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        // Repeats in arrival order, the escape decoded.
        LT_CHECK_EQ(seen[0].body, std::string("k=1;k=2;x=/p"));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK_EQ(form_echo_invocations.load(), 1);
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].body, std::string("hello"));
LT_END_AUTO_TEST(http11_post_form_round_trip)

// (36) TASK-116: a form body past the adapter's byte cap answers 413
// without invoking the handler; the admitted-undrained settle discards
// the remainder and the connection is reused.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_form_over_cap_413)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo_form HTTP/1.1\r\nHost: h\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n"
                         "Content-Length: 19\r\n\r\n"
                         "a=xxxxxxxxxxxxxxxxx"));   // cap is 16
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) LT_CHECK_EQ(seen[0].status, 413);
    LT_CHECK_EQ(form_echo_invocations.load(), 0);
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].body, std::string("hello"));
    LT_CHECK_EQ(form_echo_invocations.load(), 0);
LT_END_AUTO_TEST(http11_post_form_over_cap_413)

// (37) TASK-116: a malformed %HH in a form body answers 400 with the
// length-framed empty body; the handler never runs.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_form_malformed_400)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo_form HTTP/1.1\r\nHost: h\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n"
                         "Content-Length: 5\r\n\r\n"
                         "a=%G1"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 400);
        LT_CHECK_EQ(seen[0].body, std::string());
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK_EQ(form_echo_invocations.load(), 0);
LT_END_AUTO_TEST(http11_post_form_malformed_400)

// (38) TASK-116: the body arrives as three separate TCP writes -- the
// escape accumulator must survive the segmentation end to end.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_form_segmented_body)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo_form HTTP/1.1\r\nHost: h\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n"
                         "Content-Length: 11\r\n\r\n"));
    LT_CHECK(client.send("a=1&b=tw"));
    LT_CHECK(client.send("%6F"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("a=1;b=two"));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK_EQ(form_echo_invocations.load(), 1);
LT_END_AUTO_TEST(http11_post_form_segmented_body)

// (39) TASK-116: a BODYLESS POST reaches the form handler with empty
// fields (the engine hands the exchange no body source).
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_form_bodyless)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo_form HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string());
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK_EQ(form_echo_invocations.load(), 1);
LT_END_AUTO_TEST(http11_post_form_bodyless)

// (40) TASK-116: the v2 content-type gate over the wire -- a
// non-matching Content-Type reaches the handler with EMPTY fields
// (v2 ran no form processing there), never 4xx.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_form_wrong_type_empty)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo_form HTTP/1.1\r\nHost: h\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: 9\r\n\r\n"
                         "a=1&b=two"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string());
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK_EQ(form_echo_invocations.load(), 1);
LT_END_AUTO_TEST(http11_post_form_wrong_type_empty)

// (41) TASK-116: framing independence -- the same form body carried
// with chunked transfer-coding decodes exactly like the
// length-framed one.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_form_chunked_body)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /echo_form HTTP/1.1\r\nHost: h\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n"
                         "Transfer-Encoding: chunked\r\n\r\n"
                         "5\r\na=1&b\r\n"
                         "4\r\n=two\r\n"
                         "0\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("a=1;b=two"));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK_EQ(form_echo_invocations.load(), 1);
LT_END_AUTO_TEST(http11_post_form_chunked_body)

// (42) TASK-117: the multipart parity posture over the wire -- the
// corpus body (one field part note=hello) answers exactly the way the
// v2 fixture did (200, text/plain, Content-Length: 10, note=hello,
// length framing) and the connection stays keep-alive.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_post_multipart_field_round_trip)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /upload HTTP/1.1\r\nHost: h\r\n"
                         "Content-Type: multipart/form-data; "
                         "boundary=PARITY096B\r\n"
                         "Content-Length: 84\r\n\r\n"
                         "--PARITY096B\r\n"
                         "Content-Disposition: form-data; name=\"note\"\r\n"
                         "\r\n"
                         "hello\r\n"
                         "--PARITY096B--\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        bool text_plain = false;
        for (const parity::observed_header& h : seen[0].headers) {
            if (h.name == "Content-Type" && h.value == "text/plain") {
                text_plain = true;
            }
        }
        LT_CHECK(text_plain);
        LT_CHECK_EQ(seen[0].body, std::string("note=hello"));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
    LT_CHECK_EQ(upload_note_invocations.load(), 1);
    // Keep-alive: the same connection serves the next request.
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].body, std::string("hello"));
LT_END_AUTO_TEST(http11_post_multipart_field_round_trip)

// (43) TASK-117: a bounded upload -- a multipart body four times the
// route's 64KiB byte cap answers 413 without invoking the handler,
// long before the client finishes writing; the drain then consumes
// the remainder and the connection serves the next request (the
// bounded claim: the server read only cap-plus-epsilon before
// rejecting).
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, http11_multipart_over_cap_413)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    const std::size_t declared = 256 << 10;
    LT_CHECK(client.send("POST /upload_capped HTTP/1.1\r\nHost: h\r\n"
                         "Content-Type: multipart/form-data; "
                         "boundary=BIGB\r\n"
                         "Content-Length: " + std::to_string(declared)
                         + "\r\n\r\n"));
    // Write slices past the cap until the 413 lands (the send may
    // stall once the server stops reading -- the deadline bounds it).
    std::deque<observed_response> seen;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(5000);
    const std::string slice(16 << 10, 'x');
    std::size_t sent = 0;
    while (seen.empty()
            && std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(client.send(slice,
                                      std::chrono::milliseconds(300)));
        sent += slice.size();
        static_cast<void>(client.receive(
            1, seen, std::chrono::milliseconds(100)));
    }
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 413);
        LT_CHECK_EQ(seen[0].body, std::string());
    }
    LT_CHECK_EQ(upload_capped_invocations.load(), 0);
    // The declared remainder, then the next request pipelined behind
    // it: the drain consumes the bytes and the connection is reused.
    while (sent < declared) {
        const std::size_t n = std::min<std::size_t>(
            64 << 10, declared - sent);
        const std::string rest(n, 'y');
        LT_CHECK(client.send(rest));
        sent += n;
    }
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) LT_CHECK_EQ(seen[1].body, std::string("hello"));
    LT_CHECK_EQ(upload_capped_invocations.load(), 0);
LT_END_AUTO_TEST(http11_multipart_over_cap_413)

// (44) TASK-117: a client abort mid-file-part -- the streaming read
// unwinds, the temp-file sink's partial file is removed, and the next
// connection is served normally.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, multipart_client_abort_removes_partial)
    server_fixture s(base_options());
    e2e_scratch_dir dir;
    LT_CHECK(!dir.path().empty());
    e2e_disk_dir() = dir.path();

    raw::connection aborter;
    LT_CHECK(aborter.connect(s.port()));
    LT_CHECK(aborter.send("POST /upload_slow HTTP/1.1\r\nHost: h\r\n"
                          "Content-Type: multipart/form-data; "
                          "boundary=AB\r\n"
                          "Content-Length: 65536\r\n\r\n"
                          "--AB\r\n"
                          "Content-Disposition: form-data; "
                          "name=\"f\"; filename=\"p.bin\"\r\n"
                          "\r\npartial file bytes"));
    // The partial file appears once the part began.
    LT_CHECK(wait_dir_state(dir, false));
    // The abort: close without finishing the body.
    aborter.close();
    // The abort removed the partial; the directory is empty again.
    LT_CHECK(wait_dir_state(dir, true));

    // The next connection is served normally.
    raw::connection next;
    LT_CHECK(next.connect(s.port()));
    LT_CHECK(next.send("POST /upload HTTP/1.1\r\nHost: h\r\n"
                       "Content-Type: multipart/form-data; "
                       "boundary=PARITY096B\r\n"
                       "Content-Length: 84\r\n\r\n"
                       "--PARITY096B\r\n"
                       "Content-Disposition: form-data; name=\"note\"\r\n"
                       "\r\n"
                       "hello\r\n"
                       "--PARITY096B--\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(next.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) LT_CHECK_EQ(seen[0].body, std::string("note=hello"));
LT_END_AUTO_TEST(multipart_client_abort_removes_partial)

// (45) TASK-117: the disk-upload round trip -- a coroutine route
// drives read_multipart with temp_file_part_sink, the uploaded bytes
// land on disk byte-identical (kept through the should_keep hook for
// verification), and an upload whose sink keeps nothing leaves the
// directory empty again.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, multipart_disk_upload_round_trip)
    server_fixture s(base_options());
    e2e_scratch_dir dir;
    LT_CHECK(!dir.path().empty());
    e2e_disk_dir() = dir.path();

    // A 2 MiB deterministic payload through a 512 KiB part body.
    std::string payload;
    payload.reserve(2 << 20);
    for (std::size_t i = 0; i < (2 << 20); ++i) {
        payload.push_back(static_cast<char>('a' + (i % 26)));
    }
    const std::string head =
        "--D\r\nContent-Disposition: form-data; name=\"f\"; "
        "filename=\"big.bin\"\r\nContent-Type: "
        "application/octet-stream\r\n\r\n";
    const std::string tail = "\r\n--D--\r\n";
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /upload_disk HTTP/1.1\r\nHost: h\r\n"
                         "Content-Type: multipart/form-data; "
                         "boundary=D\r\n"
                         "Content-Length: "
                         + std::to_string(head.size() + payload.size()
                                          + tail.size())
                         + "\r\n\r\n"
                         + head));
    // The payload in slices (keep the socket fed).
    for (std::size_t at = 0; at < payload.size(); at += (64 << 10)) {
        const std::size_t n = std::min<std::size_t>(
            64 << 10, payload.size() - at);
        LT_CHECK(client.send(std::string_view(payload).substr(at, n)));
    }
    LT_CHECK(client.send(tail));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        const std::string path = seen[0].body;
        LT_CHECK(path.size() > dir.path().size());
        LT_CHECK(read_all(path) == payload);
        std::remove(path.c_str());
    }

    // The slow route keeps nothing: the same upload through
    // /upload_slow (a sink with no keep callback) ends with the
    // handler removing the completed file; the quiet handler's exit
    // synthesizes a response, then the directory must be empty.
    raw::connection quiet_client;
    LT_CHECK(quiet_client.connect(s.port()));
    LT_CHECK(quiet_client.send("POST /upload_slow HTTP/1.1\r\nHost: h\r\n"
                               "Content-Type: multipart/form-data; "
                               "boundary=D\r\n"
                               "Content-Length: "
                               + std::to_string(head.size()
                                                + payload.size()
                                                + tail.size())
                               + "\r\n\r\n"
                               + head));
    for (std::size_t at = 0; at < payload.size(); at += (64 << 10)) {
        const std::size_t n = std::min<std::size_t>(
            64 << 10, payload.size() - at);
        LT_CHECK(quiet_client.send(std::string_view(payload).substr(at, n)));
    }
    LT_CHECK(quiet_client.send(tail));
    std::deque<observed_response> quiet_seen;
    static_cast<void>(quiet_client.receive(1, quiet_seen));
    LT_CHECK(wait_dir_state(dir, true));
LT_END_AUTO_TEST(multipart_disk_upload_round_trip)

// TASK-118 step 4: the route-family and lifecycle-hook wire scenarios.
// Each case builds a scenario-local server (the shared fixture pins
// fixed routes; hooks and page factories are per-scenario state) and
// asserts on the wire through the parity frame parser.

// A scenario-local native server over the full route/hook/page
// surface. Destruction stops it.
class hooks_server {
 public:
    explicit hooks_server(srv::server_options options)
        : server_(std::move(options)) { }

    ~hooks_server() { server_.stop(); }

    // Registration closes at listen(): routes and hooks install first,
    // start() binds.
    srv::native_server& get() noexcept { return server_; }

    bool start() { return server_.listen().ok(); }

    std::uint16_t port() const noexcept { return server_.get_bound_port(0); }

 private:
    srv::native_server server_;
};

// GET handler streaming @p body with the length pinned (the corpus
// fixture's value-response shape: HEAD declares the GET body length
// on the wire).
srv::route_handler stream_body(const std::string& body) {
    return srv::route_handler([out = body](exchange& x) -> task<void> {
        http::fields f;
        f.append("Content-Type", "text/plain");
        f.append("Content-Length", std::to_string(out.size()));
        static_cast<void>(x.start_response(http::status::from_code(200), f));
        const std::byte* raw = reinterpret_cast<const std::byte*>(out.data());
        co_await x.writer().write(std::span<const std::byte>(raw, out.size()));
        co_await x.writer().finish();
    });
}

// The observed value of one response header, "" when absent.
std::string header_of(const raw::observed_response& r, const char* name) {
    for (const parity::observed_header& h : r.headers) {
        if (h.name == name) return h.value;
    }
    return "";
}

bool has_header(const raw::observed_response& r, const char* name) {
    return !header_of(r, name).empty();
}

// (a) The method mismatch answers 405 with Allow and the v2 default
// body on the wire.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, route_families_405_allow_on_wire)
    hooks_server s(base_options());
    LT_CHECK(s.get().route(http::method::known(http::method_id::get),
                           "/get_only", stream_body("get-only"))
                 .ok());
    LT_CHECK(s.start());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("POST /get_only HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 0\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 405);
        LT_CHECK_EQ(header_of(seen[0], "Allow"), std::string("GET"));
        LT_CHECK_EQ(seen[0].body, std::string("Method not Allowed"));
        LT_CHECK_EQ(seen[0].framing, std::string("content-length"));
    }
LT_END_AUTO_TEST(route_families_405_allow_on_wire)

// (b) HEAD on a GET+HEAD registration answers headers-only: no body
// bytes, the declared Content-Length rides the head.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, route_families_head_headers_only)
    hooks_server s(base_options());
    http::method_set get_head;
    get_head.set(http::method_id::get);
    get_head.set(http::method_id::head);
    LT_CHECK(s.get().route(get_head, "/both", stream_body("both-ok")).ok());
    LT_CHECK(s.start());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    // The client parser must know the request was HEAD: a HEAD response
    // never carries body bytes however the head frames them.
    client.set_head_only(true);
    LT_CHECK(client.send("HEAD /both HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string(""));
        LT_CHECK_EQ(header_of(seen[0], "Content-Length"), std::string("7"));
        LT_CHECK_EQ(seen[0].framing, std::string("none"));
    }
LT_END_AUTO_TEST(route_families_head_headers_only)

// (c) A before_handler short-circuit answers its 403 and the
// after_handler hook does NOT stamp its header (the pinned v2
// suppression).
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, hooks_before_403_suppresses_after)
    hooks_server s(base_options());
    LT_CHECK(s.get().route(http::method::known(http::method_id::del),
                           "/admin", stream_body("admin-ok"))
                 .ok());
    (void)s.get().hooks().add<srv::hook_phase::before_handler>(
        [](srv::before_handler_ctx&) -> srv::hook_action {
            srv::hook_response page;
            page.status = http::status::from_code(403);
            page.fields.append("Content-Type", "text/plain");
            const char* body = "hooked403";
            const std::byte* raw =
                reinterpret_cast<const std::byte*>(body);
            page.body.assign(raw, raw + 9);
            return srv::hook_action::respond_with(std::move(page));
        }).detach();
    (void)s.get().hooks().add<srv::hook_phase::after_handler>(
        [](srv::after_handler_ctx& ctx) -> srv::hook_action {
            ctx.fields.append("X-Hook", "after");
            return srv::hook_action::pass();
        }).detach();

    LT_CHECK(s.start());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("DELETE /admin HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 0\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 403);
        LT_CHECK_EQ(seen[0].body, std::string("hooked403"));
        LT_CHECK(!has_header(seen[0], "X-Hook"));
    }
LT_END_AUTO_TEST(hooks_before_403_suppresses_after)

// (d) after_handler mutates a streamed response's head on the wire.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, hooks_after_header_on_stream)
    hooks_server s(base_options());
    LT_CHECK(s.get().route(http::method::known(http::method_id::get),
                           "/hello", stream_body("hello"))
                 .ok());
    (void)s.get().hooks().add<srv::hook_phase::after_handler>(
        [](srv::after_handler_ctx& ctx) -> srv::hook_action {
            ctx.fields.append("X-Hook", "after");
            return srv::hook_action::pass();
        }).detach();

    LT_CHECK(s.start());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK_EQ(seen[0].body, std::string("hello"));
        LT_CHECK_EQ(header_of(seen[0], "X-Hook"), std::string("after"));
    }
LT_END_AUTO_TEST(hooks_after_header_on_stream)

// (e) The construction-time custom 404 factory supplies the body.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, hooks_custom_404_page)
    srv::server_options options = base_options();
    options.not_found_response([](const http::request_head&) {
        srv::hook_response page;
        page.status = http::status::from_code(404);
        page.fields.append("Content-Type", "text/plain");
        const char* body = "custom-not-found";
        const std::byte* raw = reinterpret_cast<const std::byte*>(body);
        page.body.assign(raw, raw + 16);
        return page;
    });
    hooks_server s(std::move(options));
    LT_CHECK(s.get().route(http::method::known(http::method_id::get),
                           "/hello", stream_body("hello"))
                 .ok());
    LT_CHECK(s.start());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /nope HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 404);
        LT_CHECK_EQ(seen[0].body, std::string("custom-not-found"));
    }
LT_END_AUTO_TEST(hooks_custom_404_page)

// (f) Family precedence on the wire: an exact route beats the prefix,
// the prefix beats the catch-all.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, route_families_prefix_precedence)
    hooks_server s(base_options());
    const http::method get = http::method::known(http::method_id::get);
    http::method_set gets;
    gets.set(http::method_id::get);
    LT_CHECK(s.get().route(get, "/api/exact", stream_body("exact")).ok());
    LT_CHECK(s.get().route_prefix(gets, "/api", stream_body("api")).ok());
    LT_CHECK(s.get().route_prefix(gets, "/", stream_body("root")).ok());

    LT_CHECK(s.start());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /api/exact HTTP/1.1\r\nHost: h\r\n\r\n"
                         "GET /api/other HTTP/1.1\r\nHost: h\r\n\r\n"
                         "GET /zzz HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(3, seen));
    LT_CHECK_EQ(seen.size(), 3u);
    if (seen.size() == 3) {
        LT_CHECK_EQ(seen[0].body, std::string("exact"));
        LT_CHECK_EQ(seen[1].body, std::string("api"));
        LT_CHECK_EQ(seen[2].body, std::string("root"));
    }
LT_END_AUTO_TEST(route_families_prefix_precedence)

// (g) A request_received short-circuit answers 413 at head-complete
// before the body; the declared remainder drains and the connection
// serves the pipelined follow-up (PRD-V3N-REQ-023).
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, hooks_request_received_413_drains)
    hooks_server s(base_options());
    LT_CHECK(s.get().route(http::method::known(http::method_id::post),
                           "/big", stream_body("big-ok"))
                 .ok());
    LT_CHECK(s.get().route(http::method::known(http::method_id::get),
                           "/hello", stream_body("hello"))
                 .ok());
    (void)s.get().hooks().add<srv::hook_phase::request_received>(
        [](srv::request_received_ctx& ctx) -> srv::hook_action {
            if (ctx.request.route_path != "/big") {
                return srv::hook_action::pass();
            }
            srv::hook_response page;
            page.status = http::status::from_code(413);
            page.fields.append("Content-Type", "text/plain");
            const char* body = "too large";
            const std::byte* raw =
                reinterpret_cast<const std::byte*>(body);
            page.body.assign(raw, raw + 9);
            return srv::hook_action::respond_with(std::move(page));
        }).detach();

    LT_CHECK(s.start());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    const std::string declared(20, 'x');
    LT_CHECK(client.send("POST /big HTTP/1.1\r\nHost: h\r\n"
                         "Content-Length: 20\r\n\r\n"
                         + declared
                         + "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(2, seen));
    LT_CHECK_EQ(seen.size(), 2u);
    if (seen.size() == 2) {
        LT_CHECK_EQ(seen[0].status, 413);
        LT_CHECK_EQ(seen[0].body, std::string("too large"));
        LT_CHECK_EQ(seen[1].status, 200);
        LT_CHECK_EQ(seen[1].body, std::string("hello"));
    }
LT_END_AUTO_TEST(hooks_request_received_413_drains)

// (12) request_completed fires exactly once on a forced settle: the
// suspension deadline disconnects a parked exchange, the tail reports
// succeeded=false with the typed end reason.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, hooks_request_completed_on_abort)
    srv::server_options options = base_options();
    options.timeouts().suspension = std::chrono::milliseconds(300);
    hooks_server s(std::move(options));
    LT_CHECK(s.get().route(http::method::known(http::method_id::get),
                           "/hang",
                           [](exchange& x) -> task<void> {
                               resume_signal wait;
                               if (!x.suspend(wait).ok()) co_return;
                               const resume_outcome outcome =
                                   co_await wait.wait_for(
                                       raw::kExchangeBudget);
                               static_cast<void>(outcome);
                           })
                 .ok());
    // The completion hook fires on an engine thread while this test
    // thread reads: a mutex-guarded recorder.
    struct recorder {
        std::mutex mu;
        std::vector<std::pair<bool, int>> entries;
        void record(bool succeeded, int end) {
            std::lock_guard<std::mutex> lock(mu);
            entries.emplace_back(succeeded, end);
        }
        std::vector<std::pair<bool, int>> take() {
            std::lock_guard<std::mutex> lock(mu);
            return entries;
        }
    };
    auto completions = std::make_shared<recorder>();
    (void)s.get().hooks().add<srv::hook_phase::request_completed>(
        [completions](srv::request_completed_ctx& ctx) -> srv::hook_action {
            completions->record(ctx.succeeded,
                                static_cast<int>(ctx.end.code()));
            return srv::hook_action::pass();
        }).detach();

    LT_CHECK(s.start());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /hang HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive_close(seen));
    LT_CHECK(client.peer_closed());
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    std::vector<std::pair<bool, int>> observed;
    do {
        observed = completions->take();
        if (!observed.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    LT_CHECK_EQ(observed.size(), std::size_t{1});
    if (!observed.empty()) {
        LT_CHECK(!observed.front().first);
        LT_CHECK(observed.front().second
                 != static_cast<int>(http::outcome_code::ok));
    }
LT_END_AUTO_TEST(hooks_request_completed_on_abort)

// (28) TASK-119 (D2): accept-time refusal. reject_all closes the
// kernel-completed connection with zero application bytes -- the same
// deterministic shape as the connections-budget refusal (the v2 deny
// wire was timing-dependent and is a named migration exception).
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, reject_all_refuses_with_zero_bytes)
    srv::server_options options = base_options();
    options.peer_policy().mode = srv::peer_policy_mode::reject_all;
    server_fixture s(options);
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    std::deque<observed_response> none;
    LT_CHECK(client.receive_close(none));
    LT_CHECK(client.peer_closed());
    LT_CHECK(none.empty());
LT_END_AUTO_TEST(reject_all_refuses_with_zero_bytes)

// (29) TASK-119: the pinned ip_controls profile live -- REJECT
// everything with loopback allow-listed serves the ordinary 200.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, allow_listed_loopback_serves)
    srv::server_options options = base_options();
    options.peer_policy().mode = srv::peer_policy_mode::reject_all;
    options.peer_policy().allow.push_back("127.0.0.1");
    server_fixture s(options);
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) {
        LT_CHECK_EQ(seen[0].status, 200);
        LT_CHECK(seen[0].body == "hello");
    }
LT_END_AUTO_TEST(allow_listed_loopback_serves)

// (30) TASK-119 (D1 point 2): a runtime deny takes effect on the NEXT
// request of an established keep-alive connection -- the in-flight
// first exchange completed; the second head settles with zero bytes.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, runtime_deny_closes_next_request)
    server_fixture s(base_options());
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> first;
    LT_CHECK(client.receive(1, first));
    LT_CHECK_EQ(first.size(), 1u);
    const http::outcome denied =
        s.server().peer_policy().deny("127.0.0.1");
    LT_CHECK(denied.ok());
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> none;
    LT_CHECK(client.receive_close(none));
    LT_CHECK(client.peer_closed());
    LT_CHECK(none.empty());
LT_END_AUTO_TEST(runtime_deny_closes_next_request)

// (31) TASK-119 (D6): accept_decision observes the refusal after the
// verdict is fixed: accepted=false, reason=denied, the loopback peer.
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, accept_decision_observes_refusal)
    srv::server_options options = base_options();
    options.peer_policy().deny.push_back("127.0.0.1");
    server_fixture s(options);
    std::mutex gate;
    int fired = 0;
    bool accepted_seen = true;
    int reason = -1;
    std::string peer_text;
    (void)s.server().hooks().add<srv::hook_phase::accept_decision>(
        [&gate, &fired, &accepted_seen, &reason, &peer_text](
            srv::accept_decision_ctx& c) -> srv::hook_action {
            std::lock_guard<std::mutex> lock(gate);
            ++fired;
            accepted_seen = c.accepted;
            reason = static_cast<int>(c.reason);
            peer_text = c.peer.address.to_string();
            return srv::hook_action::pass();
        }).detach();
    raw::connection client;
    LT_CHECK(client.connect(s.port()));
    std::deque<observed_response> none;
    LT_CHECK(client.receive_close(none));
    LT_CHECK(client.peer_closed());
    LT_CHECK(none.empty());
    // The hook fires before the transport is released, so the close
    // the client observed already ordered the observation.
    std::lock_guard<std::mutex> lock(gate);
    LT_CHECK_EQ(fired, 1);
    LT_CHECK(!accepted_seen);
    LT_CHECK_EQ(reason, static_cast<int>(srv::peer_refusal::denied));
    LT_CHECK(peer_text == "127.0.0.1");
LT_END_AUTO_TEST(accept_decision_observes_refusal)

// (32) TASK-119: the IPv6 loopback. ::1 allow-listed serves; reject_all
// closes with zero bytes. Fails visibly when ::1 is unavailable (the
// check-skip-rationales discipline: no silent skips).
LT_BEGIN_AUTO_TEST(native_http1_e2e_suite, ipv6_loopback_policy)
    srv::server_options allow_options;
    srv::listener_options v6;
    v6.address = "::1";
    v6.port = 0;
    allow_options.add_listener(v6);
    allow_options.concurrency().workers = 2;
    allow_options.peer_policy().mode = srv::peer_policy_mode::reject_all;
    allow_options.peer_policy().allow.push_back("::1");
    server_fixture s(allow_options);
    raw::connection client{raw::connection::ipv6};
    LT_CHECK(client.connect_v6(s.port()));
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"));
    std::deque<observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), 1u);
    if (seen.size() == 1) LT_CHECK_EQ(seen[0].status, 200);

    srv::server_options reject_options = allow_options;
    reject_options.peer_policy().allow.clear();
    server_fixture t(reject_options);
    raw::connection refused{raw::connection::ipv6};
    LT_CHECK(refused.connect_v6(t.port()));
    std::deque<observed_response> none;
    LT_CHECK(refused.receive_close(none));
    LT_CHECK(refused.peer_closed());
    LT_CHECK(none.empty());
LT_END_AUTO_TEST(ipv6_loopback_policy)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
