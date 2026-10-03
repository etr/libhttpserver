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

#include <sys/socket.h>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/drain_scope.hpp>
#include "./io_loopback.hpp"
#include "./websocket_test_helpers.hpp"
#include "./littletest.hpp"
namespace h = httpserver;
namespace {
namespace sys = h::detail::pollsys;
using namespace std::chrono_literals;  // NOLINT(build/namespaces)
template<class Predicate> bool until(Predicate pred, std::chrono::milliseconds budget = 2s) {
    auto end = std::chrono::steady_clock::now() + budget;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::yield();
    }
    return true;
}
struct rig {
    h::server::resource_budget root = h::server::resource_budget::root({});
    h::server::route_registry routes;
    h::server::hook_bus hooks;
    h::detail::drain_scope scope;
    h::detail::worker_pool pool{2};
    h::detail::io_poll_backend backend;
    io_loopback::pair pair = io_loopback::pair::make();
    h::detail::connection_engine_config config;
    std::atomic<bool> stopped{false}, accepted{false};
    std::shared_ptr<h::detail::connection_engine> engine;
    rig() { h::server::route_registry::create(root, routes); }
    bool start() {
        if (!pair.ok()) return false;
        sys::set_nonblocking(pair.peer(), true);
        backend.adopt_connection(1, pair.detach_local());
        engine = std::make_shared<h::detail::connection_engine>(backend, pool, routes, hooks, root, scope, config, 1, [this] { stopped = true; });
        engine->start(); return true;
    }
    ~rig() {
        if (engine) {
            engine->shutdown(); until([&] { return stopped.load(); });
        }
    }
};
std::string opening(const std::string& protocol = {}) {
    std::string text = "GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";
    if (!protocol.empty()) text += "Sec-WebSocket-Protocol: " + protocol + "\r\n";
    return text + "\r\n";
}
}  // namespace
LT_BEGIN_SUITE(upgrade_transport_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(upgrade_transport_suite)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, abandoned_session_during_stalled_101_still_has_deadline)
    rig r; std::string protocol(32768, 'p');
    int small = 1024;
    LT_CHECK(::setsockopt(r.pair.local(), SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0);
    r.config.timeouts.handshake = 30ms; r.config.timeouts.write_idle = 10s;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        h::ws_upgrade_options options; options.subprotocols = {protocol};
        auto result = co_await x.upgrade(options);
        r.accepted = result.status.ok();
        co_return;  // handle destruction before the large 101 can flush
    });
    LT_CHECK(r.start());
    auto head = opening(protocol);
    io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    LT_CHECK(until([&] { return r.accepted.load(); }));
    LT_CHECK(until([&] { return r.stopped.load(); }, 500ms));
LT_END_AUTO_TEST(abandoned_session_during_stalled_101_still_has_deadline)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, shutdown_wakes_receive_and_releases_engine_without_cycle)
    rig r; std::atomic<int> closed{0}; std::atomic<bool> receive_ended{false};
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        auto result = co_await x.upgrade({});
        r.accepted = result.status.ok();
        if (!result.session) co_return;
        result.session->on_close([&](auto) { ++closed; });
        auto received = co_await result.session->receive();
        receive_ended = !received.status.ok();
    });
    LT_CHECK(r.start()); auto head = opening();
    io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    LT_CHECK(until([&] { return r.accepted.load(); }));
    r.engine->shutdown(); r.engine->shutdown();
    LT_CHECK(until([&] { return r.stopped.load(); }));
    LT_CHECK(receive_ended.load()); LT_CHECK_EQ(closed.load(), 1);
    std::weak_ptr<h::detail::connection_engine> witness = r.engine;
    r.engine.reset(); LT_CHECK(until([&] { return witness.expired(); }));
LT_END_AUTO_TEST(shutdown_wakes_receive_and_releases_engine_without_cycle)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, prior_http_and_complete_101_precede_frame_under_partial_writes)
    rig r; std::string protocol(32768, 'p'); int small = 1024;
    LT_CHECK(::setsockopt(r.pair.local(), SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0);
    r.routes.route(h::http::method::known(h::http::method_id::get), "/before", [](h::exchange& x) -> h::task<void> {
        h::http::fields fields; fields.append("Content-Length", "3");
        x.start_response(h::http::status::from_code(200), fields);
        auto body = ws_test::bytes("old"); co_await x.writer().write(body); co_await x.writer().finish();
    });
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        h::ws_upgrade_options options; options.subprotocols = {protocol};
        auto result = co_await x.upgrade(options); r.accepted = result.status.ok();
        if (!result.session) co_return;
        auto payload = ws_test::bytes("afterhead");
        result.session->try_send(h::websocket::message_kind::text, payload);
        co_await result.session->receive();
    });
    LT_CHECK(r.start());
    auto request = std::string("GET /before HTTP/1.1\r\nHost: localhost\r\n\r\n") + opening(protocol);
    io_loopback::write_all(r.pair.peer(), request.data(), request.size());
    LT_CHECK(until([&] { return r.accepted.load(); }));
    std::string expected = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nold"
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\nSec-WebSocket-Protocol: " + protocol + "\r\n\r\n";
    auto wire = ws_test::frame(1, ws_test::bytes("afterhead"), true, false);
    expected.append(reinterpret_cast<const char*>(wire.data()), wire.size());
    std::vector<std::byte> received(expected.size());
    LT_CHECK(io_loopback::read_exact(r.pair.peer(), received.data(), received.size()));
    LT_CHECK(std::string(reinterpret_cast<const char*>(received.data()), received.size()) == expected);
    r.engine->shutdown(); LT_CHECK(until([&] { return r.stopped.load(); }));
LT_END_AUTO_TEST(prior_http_and_complete_101_precede_frame_under_partial_writes)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, peer_close_releases_all_loops_and_owned_engine)
    rig r;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        auto result = co_await x.upgrade({}); r.accepted = result.status.ok();
        if (!result.session) co_return;
        h::resume_signal closed;
        result.session->on_close([closed](auto) mutable { closed.signal(); });
        co_await result.session->receive();
        co_await closed.wait_for(1s);
    });
    LT_CHECK(r.start()); auto request = opening();
    auto close = ws_test::frame(8, ws_test::bytes("\x03\xe8"));
    request.append(reinterpret_cast<const char*>(close.data()), close.size());
    io_loopback::write_all(r.pair.peer(), request.data(), request.size());
    LT_CHECK(until([&] { return r.accepted.load(); }));
    LT_CHECK(until([&] { return r.stopped.load(); }));
    std::weak_ptr<h::detail::connection_engine> witness = r.engine;
    r.engine.reset(); LT_CHECK(until([&] { return witness.expired(); }));
LT_END_AUTO_TEST(peer_close_releases_all_loops_and_owned_engine)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
