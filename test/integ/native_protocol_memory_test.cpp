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

#if defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#include <fstream>
#endif
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "../unit/websocket_engine_helpers.hpp"
using namespace ws_engine_test;  // NOLINT(build/namespaces)
using engine_access = h::detail::connection_engine_test_access;
namespace {
void check(bool okay, const char* what) { if (!okay) throw std::runtime_error(what); }
std::size_t resident_bytes() {
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    check(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS,
          "process memory measurement unavailable");
    return info.resident_size;
#else
    std::ifstream stat("/proc/self/statm");
    std::size_t total = 0, resident = 0;
    check(static_cast<bool>(stat >> total >> resident), "process memory measurement unavailable");
    return resident * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
}
void small_sockets(rig& r) {
    int small = 2048;
    check(setsockopt(r.pair.local(), SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0, "local send buffer");
    check(setsockopt(r.pair.peer(), SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0, "peer send buffer");
    r.config.body.max_staged_bytes = 1024;
    r.config.outbox.max_queue_bytes = 1024;
    r.config.timeouts.body_idle = 10s; r.config.timeouts.write_idle = 10s;
}
// Attempted bytes use a reusable buffer. A blocked kernel write makes
// the admission stop observable without creating a growing client queue.
std::size_t attempt_input(rig& r, std::span<const std::byte> buffer, std::size_t attempts) {
    std::size_t sent = 0;
    for (std::size_t n = 0; n < attempts; n += buffer.size()) {
        auto result = sys::write_some(r.pair.peer(), buffer.data(), buffer.size());
        if (result.status == sys::sys_status::ok) {
            sent += result.transferred;
        } else {
            check(result.status == sys::sys_status::would_block, "input connection failed");
        }
    }
    return sent;
}
void plateau(rig& r, const char* name, const std::function<void(std::size_t)>& workload) {
    workload(1024 * 1024);
    auto first = engine_access::queues(*r.engine);
    auto memory = resident_bytes();
    workload(8 * 1024 * 1024);
    auto second = engine_access::queues(*r.engine);
    auto after = resident_bytes();
    // Two fixed transport buffers (16KiB read + 4KiB write), queue
    // budgets and frame overhead; process slack is separately bounded.
    check(first.tail <= 16384 + 1024 && second.tail <= 16384 + 1024, "pending transport tail bound");
    check(first.body <= 1024 && second.body <= 1024, "HTTP input budget");
    check(first.output <= 2048 && second.output <= 2048, "HTTP outbox bound including fixed head");
    check(first.ws_input <= 512 && second.ws_input <= 512, "WebSocket input budget");
    check(first.ws_output <= 522 && second.ws_output <= 522, "WebSocket output budget");
    // RSS includes kernel-independent allocator slack and sanitizer
    // metadata/quarantine. No custom allocation operators are installed.
    constexpr std::size_t slack = 1024 * 1024;
    check(after <= memory + slack, "retained process memory grew past fixed slack");
    std::cout << "PLATEAU " << name << " retained=" << first.total() << '/' << second.total()
              << " RSS=" << memory << '/' << after << " slack=" << slack << '\n';
}
void terminal(rig& r) {
    r.engine->shutdown(); r.engine->shutdown();
    check(until([&] { return r.stopped.load(); }), "engine shutdown deadline");
    check(r.scope.active() == 0 && r.stop_calls == 1, "drained connection and exactly-once stop");
}
void release_engine(rig& r) {
    std::weak_ptr<h::detail::connection_engine> released = r.engine;
    r.engine.reset();
    check(until([&] { return released.expired(); }), "released connection state and retained allocations");
}
void http_upload() {
    h::resume_signal resume;
    std::atomic<bool> entered{false}, delivered{false};
    std::atomic<std::size_t> received{0};
    rig r; small_sockets(r);
    constexpr std::size_t length = 1024 * 1024;
    r.routes.route(h::http::method::known(h::http::method_id::post), "/", [&](h::exchange& x) -> h::task<void> {
        x.admit_body({}); entered = true;
        co_await resume.wait_for(10s);
        std::byte scratch[1024];
        for (;;) {
            auto read = co_await x.body().read_some(scratch);
            if (!read.status.ok()) co_return;
            for (std::size_t i = 0; i < read.data.size(); ++i) {
                if (scratch[i] != std::byte{'u'}) co_return;
            }
            received += read.data.size();
            if (read.end_of_body) break;
        }
        delivered = received == length;
        h::http::fields fields; fields.append("Content-Length", "0");
        x.start_response(h::http::status::from_code(200), fields); co_await x.writer().finish();
    });
    check(r.start(), "upload start");
    std::string head = "POST / HTTP/1.1\r\nHost: h\r\nContent-Length: 1048576\r\n\r\n";
    io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    check(until([&] { return entered.load(); }), "upload admission");
    std::array<std::byte, 1024> data; data.fill(std::byte{'u'});
    std::size_t sent = 0;
    check(until([&] { sent += attempt_input(r, data, data.size()); return engine_access::queues(*r.engine).body == 1024; }), "upload staging full");
    check(until([&] { auto progress = attempt_input(r, data, data.size()); sent += progress; return progress == 0; }), "upload transport backpressure");
    plateau(r, "HTTP upload", [&](std::size_t n) { sent += attempt_input(r, data, n); });
    check(sent < length && received == 0, "application stall stopped progress");
    resume.signal();
    while (sent < length) {
        auto part = std::span(data).first(std::min(data.size(), length - sent));
        auto result = sys::write_some(r.pair.peer(), part.data(), part.size());
        if (result.transferred) {
            sent += result.transferred;
        } else {
            bool resumed = until([&] { auto result = sys::write_some(r.pair.peer(), part.data(), part.size()); sent += result.transferred; return result.transferred > 0; });
            check(resumed, "upload resume");
        }
    }
    check(until([&] { return delivered.load(); }), "ordered upload delivery");
    check(until([&] { return engine_access::http_idle(*r.engine); }), "upload exchange settled before abort");
    terminal(r);
    release_engine(r);
}
void http_output() {
    rig r; small_sockets(r); check(r.start(), "output start");
    auto& sink = engine_access::output_slot(*r.engine);
    h::http::request_head request; request.request_protocol = h::http::protocol::http_1_1;
    h::http::fields fields; fields.append("Content-Length", "1048576");
    check(sink.start(request, h::http::status::from_code(200), fields).ok(), "output head");
    std::array<std::byte, 1024> data; data.fill(std::byte{'o'});
    std::size_t accepted = 0;
    auto produce = [&](std::size_t attempts) {
        for (std::size_t n = 0; n < attempts; n += data.size()) {
            auto result = sink.push(data); accepted += result.copied;
            check(result.kind != h::detail::body_push::failed, "output push");
        }
        engine_access::notify(*r.engine);
    };
    auto steady = std::chrono::steady_clock::now();
    check(until([&] {
        auto before = accepted; produce(1024);
        if (accepted != before) steady = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(1ms);
        return engine_access::queues(*r.engine).output == 1024 && std::chrono::steady_clock::now() - steady >= 20ms;
    }), "stalled peer stops outbox admission");
    plateau(r, "HTTP output", produce);
    check(sink.push(data).kind == h::detail::body_push::full, "output admission stopped");
    std::string wire;
    std::array<std::byte, 4096> scratch;
    check(until([&] {
        auto result = sys::read_some(r.pair.peer(), scratch.data(), scratch.size());
        if (result.transferred) wire.append(reinterpret_cast<const char*>(scratch.data()), result.transferred);
        auto end = wire.find("\r\n\r\n");
        return end != std::string::npos && wire.size() - end - 4 == accepted;
    }), "output resumes");
    // Retain only the small admitted prefix, never the attempted windows.
    auto end = wire.find("\r\n\r\n");
    check(end != std::string::npos, "response head delivered");
    check(wire.size() - end - 4 == accepted && wire.substr(end + 4) == std::string(wire.size() - end - 4, 'o'), "ordered output bytes");
    check(accepted < 1048576, "bounded output admission");
    terminal(r);
    check(engine_access::queues(*r.engine).output == 0, "outbox release");
    release_engine(r);
}
void websocket_input() {
    h::resume_signal resume, closed;
    std::atomic<bool> entered{false}, invalid{false}, clean{false}; std::atomic<int> closes{0};
    std::atomic<std::size_t> received{0};
    rig r; small_sockets(r);
    r.config.websocket_limits.max_message_bytes = 64; r.config.websocket_limits.incoming_bytes = 512;
    r.config.websocket_limits.incoming_messages = 8;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        auto upgraded = co_await x.upgrade({});
        if (!upgraded.session) co_return;
        upgraded.session->on_close([&](auto info) { clean = info.clean; ++closes; closed.signal(); }); entered = true;
        co_await resume.wait_for(10s);
        for (;;) {
            auto result = co_await upgraded.session->receive();
            if (!result.value) break;
            if (result.value->data != std::vector<std::byte>(64, std::byte{'w'})) invalid = true;
            received += result.value->data.size();
        }
        co_await closed.wait_for(5s);
    });
    check(r.start(), "WebSocket input start"); auto head = opening();
    io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    check(until([&] { return entered.load(); }), "WebSocket upgrade");
    std::string handshake;
    std::array<std::byte, 512> head_bytes;
    check(until([&] {
        auto result = sys::read_some(r.pair.peer(), head_bytes.data(), head_bytes.size());
        handshake.append(reinterpret_cast<const char*>(head_bytes.data()), result.transferred);
        check(handshake.size() <= 1024, "bounded upgrade response");
        return handshake.find("\r\n\r\n") != std::string::npos;
    }), "upgrade response drained before input stall");
    check(handshake.starts_with("HTTP/1.1 101 "), "wire upgrade status");
    auto wire = ws_test::frame(2, std::vector<std::byte>(64, std::byte{'w'}));
    std::size_t sent = 0, partial = 0;
    auto produce = [&](std::size_t attempts) {
        for (std::size_t n = 0; n < attempts; n += wire.size()) {
            auto result = sys::write_some(r.pair.peer(), wire.data() + partial, wire.size() - partial);
            sent += result.transferred; partial = (partial + result.transferred) % wire.size();
            check(result.status == sys::sys_status::ok || result.status == sys::sys_status::would_block, "WebSocket input write");
        }
    };
    check(until([&] { produce(1024); return engine_access::queues(*r.engine).ws_input == 512; }), "WebSocket receiver stall");
    plateau(r, "WebSocket input", produce);
    check(received == 0, "stalled application receives nothing");
    resume.signal();
    if (partial) {
        io_loopback::write_all(r.pair.peer(), wire.data() + partial, wire.size() - partial); sent += wire.size() - partial; }
    check(until([&] { return received == sent / wire.size() * 64; }), "ordered WebSocket input resume");
    auto close = ws_test::frame(8, ws_test::bytes("\x03\xe8"));
    io_loopback::write_all(r.pair.peer(), close.data(), close.size());
    check(until([&] { return r.stopped.load(); }), "WebSocket clean close");
    check(!invalid && clean && closes == 1 && r.stop_calls == 1 && r.scope.active() == 0, "WebSocket terminal notification once");
    check(engine_access::queues(*r.engine).ws_input == 0, "WebSocket input released");
    release_engine(r);
}
void websocket_output() {
    rig r; small_sockets(r);
    auto limits = ws_test::small(); limits.max_message_bytes = 64; limits.incoming_bytes = 512; limits.output_bytes = 522; limits.outgoing_messages = 8;
    auto driver = std::make_shared<h::detail::websocket_driver>(limits);
    auto session = driver->take_session(); std::atomic<int> closes{0}; session.on_close([&](auto) { ++closes; });
    r.create_engine(); engine_access::install(*r.engine, driver);
    sys::set_nonblocking(r.pair.peer(), true); r.backend.adopt_connection(1, r.pair.detach_local());
    r.engine->start(); r.started = true;
    std::array<std::byte, 64> data; data.fill(std::byte{'s'});
    std::size_t messages = 0;
    auto produce = [&](std::size_t attempts) {
        for (std::size_t n = 0; n < attempts; n += data.size()) {
            auto result = session.try_send(h::websocket::message_kind::binary, data);
            if (result.disposition == h::websocket::send_disposition::accepted) {
                ++messages;
            } else {
                check(result.disposition == h::websocket::send_disposition::backpressured, "WebSocket output send");
            }
        }
    };
    auto steady = std::chrono::steady_clock::now();
    check(until([&] {
        auto before = messages; produce(1024);
        if (messages != before) steady = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(1ms);
        return driver->usage().output_bytes >= 462 && std::chrono::steady_clock::now() - steady >= 20ms;
    }), "stalled peer stops WebSocket output admission");
    plateau(r, "WebSocket output", produce);
    check(session.try_send(h::websocket::message_kind::binary, data).disposition == h::websocket::send_disposition::backpressured,
          "WebSocket output admission stopped");
    std::string wire;
    std::array<std::byte, 4096> scratch;
    check(messages * 66 <= 1024 * 1024, "bounded receiver buffer");
    check(until([&] {
        auto result = sys::read_some(r.pair.peer(), scratch.data(), scratch.size());
        wire.append(reinterpret_cast<const char*>(scratch.data()), result.transferred);
        return wire.size() == messages * 66;
    }), "WebSocket output resumes");
    for (std::size_t n = 0; n < messages; ++n) {
        check(static_cast<unsigned char>(wire[n * 66]) == 0x82 && wire[n * 66 + 1] == 64,
              "ordered WebSocket frame header");
        check(wire.substr(n * 66 + 2, 64) == std::string(64, 's'), "ordered WebSocket frame payload");
    }
    terminal(r); check(closes == 1 && driver->usage().output_bytes == 0, "WebSocket output released once");
    release_engine(r);
}
}  // namespace
int main() {
    try {
        http_upload(); http_output(); websocket_input(); websocket_output();
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
