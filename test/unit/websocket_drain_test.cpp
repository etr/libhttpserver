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

#include <memory>
#include <string>
#include <httpserver/concurrency/resume_signal.hpp>
#include "./websocket_engine_helpers.hpp"
using namespace ws_engine_test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(drain_transport_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(drain_transport_suite)
LT_BEGIN_AUTO_TEST(drain_transport_suite, quiesce_sends_close_and_keeps_receive_alive_until_peer_reply)
    std::atomic<int> closes{0}; std::atomic<bool> clean{false};
    h::resume_signal terminal; rig r;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        auto result = co_await x.upgrade({});
        if (!result.session) co_return;
        result.session->on_close([&](auto info) { clean = info.clean; ++closes; terminal.signal(); });
        r.accepted = true;
        co_await result.session->receive();
        co_await terminal.wait_for(2s);
    });
    LT_CHECK(r.start()); auto head = opening(); io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    LT_CHECK(until([&] { return h::detail::connection_engine_test_access::receive_parked(*r.engine); }));
    r.engine->quiesce();
    std::string wire; char bytes[256];
    LT_CHECK(until([&] {
        auto n = sys::read_some(r.pair.peer(), reinterpret_cast<std::byte*>(bytes), sizeof bytes);
        if (n.transferred > 0) wire.append(bytes, n.transferred);
        return wire.size() >= 16 && wire.find("server drain") != std::string::npos;
    }));
    LT_CHECK(!r.stopped.load());
    auto close = ws_test::frame(8, ws_test::bytes(std::string("\x03\xe9server drain")));
    io_loopback::write_all(r.pair.peer(), close.data(), close.size());
    LT_CHECK(until([&] { return r.stopped.load(); }));
    LT_CHECK(clean.load()); LT_CHECK_EQ(closes.load(), 1); LT_CHECK_EQ(r.scope.active(), std::size_t{0});
LT_END_AUTO_TEST(quiesce_sends_close_and_keeps_receive_alive_until_peer_reply)
LT_BEGIN_AUTO_TEST(drain_transport_suite, drain_deadline_without_ticket_records_sticky_expiry)
    std::atomic<int> closes{0}; std::atomic<bool> receive_timeout{false}; std::string reason; rig r;
    r.config.timeouts.ws_close = 2s;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        auto result = co_await x.upgrade({});
        if (!result.session) co_return;
        result.session->on_close([&](auto info) { reason = info.status.message(); ++closes; });
        r.accepted = true;
        auto received = co_await result.session->receive();
        receive_timeout = received.status.code() == h::http::outcome_code::timeout;
    });
    LT_CHECK(r.start()); auto head = opening(); io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    LT_CHECK(until([&] { return h::detail::connection_engine_test_access::receive_parked(*r.engine); }));
    auto deadline = std::chrono::steady_clock::now() + 100ms;
    std::weak_ptr<h::detail::connection_engine> weak = r.engine;
    r.scope.arm(deadline, {}, [weak] {
        if (auto engine = weak.lock()) engine->shutdown(h::http::outcome_code::timeout);
     });
    r.engine->quiesce(deadline);
    r.engine->quiesce(std::chrono::steady_clock::now() + 2s);
    LT_CHECK(h::detail::connection_engine_test_access::deadline(*r.engine) == deadline);
    LT_CHECK(until([&] { return r.stopped.load(); }));
    LT_CHECK(receive_timeout.load()); LT_CHECK_EQ(closes.load(), 1); LT_CHECK_EQ(reason, "WebSocket drain deadline");
    h::server::drain_result late; LT_CHECK(r.scope.wait(late).ok());
    LT_CHECK(late.status == h::server::drain_status::deadline_expired);
    LT_CHECK_EQ(late.remaining, std::size_t{2}); LT_CHECK_EQ(r.scope.active(), std::size_t{0});
    r.engine.reset(); LT_CHECK(until([&] { return weak.expired(); }));
LT_END_AUTO_TEST(drain_deadline_without_ticket_records_sticky_expiry)
LT_BEGIN_AUTO_TEST(drain_transport_suite, local_close_timeout_does_not_expire_longer_server_drain)
    std::atomic<int> closes{0}; std::atomic<bool> receive_timeout{false}; std::string reason; rig r;
    r.config.timeouts.ws_close = 30ms;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        auto result = co_await x.upgrade({});
        if (!result.session) co_return;
        result.session->on_close([&](auto info) { reason = info.status.message(); ++closes; });
        r.accepted = true;
        auto received = co_await result.session->receive();
        receive_timeout = received.status.code() == h::http::outcome_code::timeout;
    });
    LT_CHECK(r.start()); auto head = opening(); io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    LT_CHECK(until([&] { return h::detail::connection_engine_test_access::receive_parked(*r.engine); }));
    auto deadline = std::chrono::steady_clock::now() + 2s;
    int global_cancels = 0;
    r.scope.arm(deadline, {}, [&] { ++global_cancels; });
    r.engine->quiesce(deadline);
    LT_CHECK(until([&] { return r.stopped.load(); }));
    LT_CHECK(receive_timeout.load()); LT_CHECK_EQ(closes.load(), 1); LT_CHECK_EQ(reason, "WebSocket Close timeout");
    h::server::drain_result result; LT_CHECK(r.scope.wait(result).ok());
    LT_CHECK(result.status == h::server::drain_status::completed); LT_CHECK_EQ(global_cancels, 0);
LT_END_AUTO_TEST(local_close_timeout_does_not_expire_longer_server_drain)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
