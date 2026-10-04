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
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <utility>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/drain_scope.hpp>
#include <httpserver/detail/websocket_session_state.hpp>
#include "./io_loopback.hpp"
#include "./websocket_test_helpers.hpp"
#include "./littletest.hpp"
#include "./websocket_engine_helpers.hpp"
using namespace ws_engine_test;  // NOLINT(build/namespaces)
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
    std::atomic<int> closed{0}; std::atomic<bool> receive_ended{false}; rig r;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        auto result = co_await x.upgrade({});
        if (!result.session) co_return;
        auto registered = result.session->on_close([&](auto) { ++closed; });
        r.accepted = result.status.ok() && registered.ok();
        auto received = co_await result.session->receive();
        receive_ended = !received.status.ok();
    });
    LT_CHECK(r.start()); auto head = opening();
    io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    LT_CHECK(until([&] { return r.accepted.load(); }));
    LT_CHECK(until([&] { return h::detail::connection_engine_test_access::receive_parked(*r.engine); }));
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
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, new_output_after_quiet_idle_gets_fresh_deadline)
    rig r; r.config.timeouts.write_idle = 100ms; r.create_engine();
    auto driver = std::make_shared<h::detail::websocket_driver>(); auto session = driver->take_session();
    h::detail::connection_engine_test_access::install(*r.engine, driver);
    LT_CHECK(!h::detail::connection_engine_test_access::deadline(*r.engine));
    const auto queued = std::chrono::steady_clock::now();
    session.try_send(h::websocket::message_kind::binary, ws_test::bytes("new"));
    auto deadline = h::detail::connection_engine_test_access::deadline(*r.engine);
    LT_CHECK(deadline && *deadline >= queued + 100ms);
    h::detail::connection_engine_test_access::progress(*r.engine, false);
    LT_CHECK(h::detail::connection_engine_test_access::deadline(*r.engine) == deadline);
    const auto written = std::chrono::steady_clock::now();
    h::detail::connection_engine_test_access::progress(*r.engine, true);
    deadline = h::detail::connection_engine_test_access::deadline(*r.engine);
    LT_CHECK(deadline && *deadline >= written + 100ms);
    std::byte buffer[32]; driver->consume_output(driver->copy_output(buffer));
    LT_CHECK(!h::detail::connection_engine_test_access::deadline(*r.engine));
LT_END_AUTO_TEST(new_output_after_quiet_idle_gets_fresh_deadline)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, delayed_drain_and_requeue_notifications_preserve_fresh_interval)
    for (bool send_first : {false, true}) {
        rig r; r.config.timeouts.write_idle = 100ms; r.create_engine();
        auto driver = std::make_shared<h::detail::websocket_driver>(); auto session = driver->take_session();
        h::detail::connection_engine_test_access::install(*r.engine, driver);
        LT_CHECK(session.try_send(h::websocket::message_kind::binary, ws_test::bytes("old")).status.ok());
        const auto admitted_deadline = h::detail::connection_engine_test_access::deadline(*r.engine);
        std::byte buffer[32]; const auto offered = driver->copy_output(buffer);
        // Mirror the writer's successful socket write, bookkeeping, then consume.
        LT_CHECK_EQ(::send(r.pair.local(), buffer, 1, 0), 1);
        h::detail::connection_engine_test_access::progress(*r.engine, true);
        LT_CHECK(driver->consume_output(1).ok());
        const auto partial_deadline = h::detail::connection_engine_test_access::deadline(*r.engine);
        LT_CHECK(partial_deadline && partial_deadline > admitted_deadline);
        LT_CHECK_EQ(::send(r.pair.local(), buffer + 1, offered - 1, 0), static_cast<ssize_t>(offered - 1));
        h::detail::connection_engine_test_access::progress(*r.engine, true);
        const auto written_deadline = h::detail::connection_engine_test_access::deadline(*r.engine);

        // Gate delivery after each real mutation, independently of the session
        // mutex, so either notification can reach the engine first.
        std::atomic<int> arrived{0}; std::atomic<bool> release[2]{}, delivered[2]{};
        const std::weak_ptr<h::detail::connection_engine> weak = r.engine;
        driver->observe_progress([&] {
            const auto index = arrived.fetch_add(1);
            while (!release[index].load()) std::this_thread::yield();
            if (auto engine = weak.lock()) h::detail::connection_engine_test_access::notify(*engine);
            delivered[index] = true;
        });
        std::thread consume([&] { driver->consume_output(offered - 1); });
        LT_CHECK(until([&] { return arrived.load() == 1 && !driver->snapshot().output_pending; }));
        std::this_thread::sleep_for(30ms);
        const auto queued = std::chrono::steady_clock::now();
        std::thread send([&] { session.try_send(h::websocket::message_kind::binary, ws_test::bytes("new")); });
        LT_CHECK(until([&] { return arrived.load() == 2 && driver->snapshot().output_pending; }));
        const auto first = send_first ? 1 : 0, second = send_first ? 0 : 1;
        release[first] = true; LT_CHECK(until([&] { return delivered[first].load(); }));
        const auto first_deadline = h::detail::connection_engine_test_access::deadline(*r.engine);
        release[second] = true; consume.join(); send.join();
        const auto fresh = h::detail::connection_engine_test_access::deadline(*r.engine);
        std::cout << "send_first=" << send_first << " anchor_same=" << (fresh == written_deadline)
            << " fresh_interval_ms=" << std::chrono::duration<double, std::milli>(*fresh - queued).count() << "\n";
        LT_CHECK(fresh && *fresh >= queued + 100ms);
        LT_CHECK(fresh == first_deadline);
        // Restore ordinary delivery before testing continuously pending output.
        driver->observe_progress([weak] {
            if (auto engine = weak.lock()) h::detail::connection_engine_test_access::notify(*engine);
        });
        h::detail::connection_engine_test_access::notify(*r.engine);
        h::detail::connection_engine_test_access::progress(*r.engine, false);
        LT_CHECK(h::detail::connection_engine_test_access::deadline(*r.engine) == fresh);
        std::this_thread::sleep_until(*fresh);
        h::detail::connection_engine_test_access::notify(*r.engine);
        LT_CHECK(h::detail::connection_engine_test_access::deadline(*r.engine) == fresh);
        LT_CHECK(driver->consume_output(driver->copy_output(buffer)).ok());
        LT_CHECK(!h::detail::connection_engine_test_access::deadline(*r.engine));
    }
LT_END_AUTO_TEST(delayed_drain_and_requeue_notifications_preserve_fresh_interval)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, coalesced_empty_frames_resume_without_more_peer_input)
    std::atomic<int> received{0}; h::resume_signal release; rig r;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        h::ws_upgrade_options options; options.limits.incoming_messages = 1;
        auto result = co_await x.upgrade(options); r.accepted = result.status.ok();
        if (!result.session) co_return;
        co_await release.wait_for(2s);
        for (int i = 0; i < 2; ++i) {
            auto message = co_await result.session->receive();
            if (message.status.ok() && message.value->data.empty()) ++received;
        }
        co_await result.session->receive();
    });
    LT_CHECK(r.start()); auto request = opening();
    auto frame = ws_test::frame(2, {});
    request.append(reinterpret_cast<const char*>(frame.data()), frame.size());
    request.append(reinterpret_cast<const char*>(frame.data()), frame.size());
    io_loopback::write_all(r.pair.peer(), request.data(), request.size());
    LT_CHECK(until([&] { return r.accepted.load(); }));
    LT_CHECK(until([&] { return h::detail::connection_engine_test_access::blocked_empty_input(*r.engine); }));
    release.signal();
    LT_CHECK(until([&] { return received.load() == 2; }, 500ms));
LT_END_AUTO_TEST(coalesced_empty_frames_resume_without_more_peer_input)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, staged_one_message_capacity_has_linear_relocation_work)
    for (std::size_t count : {256, 1024, 4096}) {
        rig r; r.create_engine(); auto limits = ws_test::small(); limits.incoming_messages = 1;
        auto driver = std::make_shared<h::detail::websocket_driver>(limits); auto session = driver->take_session();
        auto frame = ws_test::frame(2, ws_test::bytes("x")); std::string batch;
        for (std::size_t i = 0; i < count; ++i) batch.append(reinterpret_cast<const char*>(frame.data()), frame.size());
        const auto input_bytes = batch.size();
        h::detail::connection_engine_test_access::install(*r.engine, driver, std::move(batch));
        std::size_t relocated_bytes = 0, delivered = 0; h::manual_executor ex;
        for (std::size_t i = 0; i < count; ++i) {
            const auto before = h::detail::connection_engine_test_access::retained(*r.engine);
            h::detail::connection_engine_test_access::feed(*r.engine);
            const auto after = h::detail::connection_engine_test_access::retained(*r.engine);
            // Relocation or prefix compaction must move the retained suffix.
            // A consumption offset leaves the backing address/size unchanged.
            if (before.first != after.first || before.second != after.second) relocated_bytes += after.second;
            h::spawn(ex, session.receive(), [&](auto result) {
                if (result.value().status.ok() && result.value().value->data == ws_test::bytes("x")) ++delivered;
            });
            ex.run_pending();
        }
        std::cout << "staged input=" << input_bytes << " relocated=" << relocated_bytes << " delivered=" << delivered << "\n";
        LT_CHECK_EQ(delivered, count); LT_CHECK(relocated_bytes <= input_bytes * 2);
    }
LT_END_AUTO_TEST(staged_one_message_capacity_has_linear_relocation_work)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, raw_versioned_upgrade_alternatives_accept)
    for (bool repeated : {false, true}) {
        rig r;
        r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
            auto result = co_await x.upgrade({}); r.accepted = result.status.ok();
            if (result.session) co_await result.session->receive();
        });
        LT_CHECK(r.start()); auto request = opening();
        request.replace(request.find("Upgrade: websocket"), std::string("Upgrade: websocket").size(),
            repeated ? "Upgrade: other/1\r\nUpgrade: websocket" : "Upgrade: other/1, websocket");
        io_loopback::write_all(r.pair.peer(), request.data(), request.size());
        LT_CHECK(until([&] { return r.accepted.load(); }, 500ms));
        std::byte prefix[12]; LT_CHECK(io_loopback::read_exact(r.pair.peer(), prefix, sizeof prefix));
        LT_CHECK(std::string(reinterpret_cast<const char*>(prefix), sizeof prefix) == "HTTP/1.1 101");
    }
LT_END_AUTO_TEST(raw_versioned_upgrade_alternatives_accept)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, ready_peer_reads_output_after_quiet_idle)
    h::resume_signal send; rig r; r.config.timeouts.write_idle = 100ms;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        auto result = co_await x.upgrade({}); r.accepted = result.status.ok();
        if (!result.session) co_return;
        co_await send.wait_for(2s);
        result.session->try_send(h::websocket::message_kind::binary, ws_test::bytes("fresh"));
        co_await result.session->receive();
    });
    LT_CHECK(r.start()); auto request = opening();
    io_loopback::write_all(r.pair.peer(), request.data(), request.size());
    const std::string head = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    std::vector<std::byte> response(head.size());
    LT_CHECK(io_loopback::read_exact(r.pair.peer(), response.data(), response.size()));
    h::detail::connection_engine_test_access::age_activity(*r.engine); send.signal();
    auto expected = ws_test::frame(2, ws_test::bytes("fresh"), true, false);
    response.resize(expected.size());
    LT_CHECK(io_loopback::read_exact(r.pair.peer(), response.data(), response.size()));
    LT_CHECK(response == expected); LT_CHECK(!r.stopped.load());
LT_END_AUTO_TEST(ready_peer_reads_output_after_quiet_idle)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, stalled_output_after_quiet_idle_gets_full_write_interval)
    std::atomic<bool> timed_out{false}; h::resume_signal send; rig r; r.config.timeouts.write_idle = 100ms;
    int small = 1024;
    LT_CHECK(::setsockopt(r.pair.local(), SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0);
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        auto result = co_await x.upgrade({});
        if (!result.session) co_return;
        result.session->on_close([&](auto info) { timed_out = info.status.code() == h::http::outcome_code::timeout; });
        r.accepted = result.status.ok(); co_await send.wait_for(2s);
        std::vector<std::byte> payload(512 * 1024);
        result.session->try_send(h::websocket::message_kind::binary, payload);
        co_await result.session->receive();
    });
    LT_CHECK(r.start()); auto request = opening();
    io_loopback::write_all(r.pair.peer(), request.data(), request.size());
    std::vector<std::byte> head(129);  // complete default 101 response
    LT_CHECK(io_loopback::read_exact(r.pair.peer(), head.data(), head.size()));
    LT_CHECK(until([&] { return r.accepted.load(); }));
    h::detail::connection_engine_test_access::age_activity(*r.engine);
    const auto queued = std::chrono::steady_clock::now(); send.signal();
    LT_CHECK(until([&] { return r.stopped.load(); }, 1s));
    LT_CHECK(std::chrono::steady_clock::now() - queued >= 100ms); LT_CHECK(timed_out.load());
LT_END_AUTO_TEST(stalled_output_after_quiet_idle_gets_full_write_interval)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, quiesce_initiates_close_without_aborting_upgraded_transport)
    rig r; r.create_engine();
    auto driver = std::make_shared<h::detail::websocket_driver>(); auto session = driver->take_session();
    h::detail::connection_engine_test_access::install(*r.engine, driver);
    r.engine->quiesce();
    LT_CHECK(driver->snapshot().closing); LT_CHECK(driver->snapshot().output_pending);
    LT_CHECK(!driver->snapshot().terminal);
LT_END_AUTO_TEST(quiesce_initiates_close_without_aborting_upgraded_transport)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, close_deadline_survives_quiet_output_and_delayed_observer)
    rig r; r.config.timeouts.ws_close = 30ms; r.config.timeouts.write_idle = 10s; r.create_engine();
    auto driver = std::make_shared<h::detail::websocket_driver>(); auto session = driver->take_session();
    h::detail::connection_engine_test_access::install(*r.engine, driver);
    driver->observe_progress({});
    LT_CHECK(session.close().ok()); auto anchor = driver->snapshot().closing_since;
    LT_CHECK(anchor.has_value());
    auto due = h::detail::connection_engine_test_access::deadline(*r.engine);
    LT_CHECK(due == *anchor + 30ms);
    std::byte output[32]; auto count = driver->copy_output(output); driver->consume_output(count);
    h::detail::connection_engine_test_access::progress(*r.engine, true);
    LT_CHECK(h::detail::connection_engine_test_access::deadline(*r.engine) == due);
    LT_CHECK(!driver->snapshot().output_pending);
    LT_CHECK(driver->snapshot().closing_since == anchor);
LT_END_AUTO_TEST(close_deadline_survives_quiet_output_and_delayed_observer)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, prior_app_close_anchor_survives_drain_and_stale_timer_is_rejected)
    rig r; r.config.timeouts.ws_close = 30ms; r.config.timeouts.write_idle = 10s; r.create_engine();
    auto driver = std::make_shared<h::detail::websocket_driver>(); auto session = driver->take_session();
    h::detail::connection_engine_test_access::install(*r.engine, driver);
    session.close(1000, "original");
    const auto anchor = driver->snapshot().closing_since;
    r.engine->quiesce(std::chrono::steady_clock::now() + 2s);
    LT_CHECK(driver->snapshot().closing_since == anchor);
    LT_CHECK(h::detail::connection_engine_test_access::deadline(*r.engine) == *anchor + 30ms);
    LT_CHECK(!h::detail::connection_engine_test_access::due(*r.engine, *anchor + 29ms));
    LT_CHECK(until([&] { return h::detail::connection_engine_test_access::due(*r.engine, *anchor + 30ms); }));
LT_END_AUTO_TEST(prior_app_close_anchor_survives_drain_and_stale_timer_is_rejected)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, close_during_stalled_101_expires_without_promotion_reset)
    std::atomic<int> closes{0}; std::string reason; rig r; std::string protocol(32768, 'p');
    int small = 1024;
    LT_CHECK(::setsockopt(r.pair.local(), SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0);
    r.config.timeouts.ws_close = 30ms; r.config.timeouts.handshake = 2s; r.config.timeouts.write_idle = 10s;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        h::ws_upgrade_options options; options.subprotocols = {protocol};
        auto result = co_await x.upgrade(options);
        if (!result.session) co_return;
        result.session->on_close([&](auto info) { reason = info.status.message(); ++closes; });
        r.accepted = true; co_await result.session->receive();
    });
    LT_CHECK(r.start()); auto head = opening(protocol); io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    LT_CHECK(until([&] { return h::detail::connection_engine_test_access::receive_parked(*r.engine); }));
    LT_CHECK(h::detail::connection_engine_test_access::upgrade_pending(*r.engine));
    r.engine->quiesce(std::chrono::steady_clock::now() + 1s);
    const auto anchor = h::detail::connection_engine_test_access::snapshot(*r.engine).closing_since;
    LT_CHECK(anchor.has_value());
    LT_CHECK(h::detail::connection_engine_test_access::deadline(*r.engine) == *anchor + 30ms);
    LT_CHECK(until([&] { return r.stopped.load(); }));
    LT_CHECK_EQ(closes.load(), 1); LT_CHECK_EQ(reason, "WebSocket Close timeout");
LT_END_AUTO_TEST(close_during_stalled_101_expires_without_promotion_reset)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, quiesce_before_upgrade_commit_refuses_new_websocket_work)
    std::atomic<bool> entered{false}, refused{false}, head_committed{false};
    h::resume_signal gate, finish_gate; rig r;
    r.routes.route(h::http::method::known(h::http::method_id::get), "/", [&](h::exchange& x) -> h::task<void> {
        entered = true; co_await gate.wait_for(2s);
        auto result = co_await x.upgrade({});
        refused = result.status.code() == h::http::outcome_code::connection_closed && !result.session;
        h::http::fields fields; fields.append("Content-Length", "0");
        x.respond(h::http::status::from_code(400), fields);
        head_committed = true;
        co_await finish_gate.wait_for(2s);
    });
    LT_CHECK(r.start()); auto head = opening(); io_loopback::write_all(r.pair.peer(), head.data(), head.size());
    LT_CHECK(until([&] { return entered.load(); }));
    r.engine->quiesce(std::chrono::steady_clock::now() + 1s); gate.signal();
    // Force the writer to consume the head before the zero-byte end arrives.
    // The post-respond witness excludes an empty, still-uncommitted slot.
    LT_CHECK(until([&] {
        return head_committed.load()
            && h::detail::connection_engine_test_access::head_consumed_before_end(*r.engine);
    }));
    finish_gate.signal();
    LT_CHECK(until([&] { return r.stopped.load(); })); LT_CHECK(refused.load());
    std::string wire; char bytes[256];
    LT_CHECK(until([&] {
        auto read = sys::read_some(r.pair.peer(), reinterpret_cast<std::byte*>(bytes), sizeof bytes);
        if (read.transferred) wire.append(bytes, read.transferred);
        return read.status == sys::sys_status::closed_reset;
    }));
    LT_CHECK(wire.find("HTTP/1.1 400") == 0); LT_CHECK(wire.find("101 Switching") == std::string::npos);
LT_END_AUTO_TEST(quiesce_before_upgrade_commit_refuses_new_websocket_work)
LT_BEGIN_AUTO_TEST(upgrade_transport_suite, transfer_removes_http_suspension_before_exchange_cleanup)
    h::http::request_head head; h::exchange current(head, nullptr); h::resume_signal resume; rig r;
    r.create_engine();
    LT_CHECK(current.suspend(resume).ok());
    LT_CHECK(h::detail::connection_engine_test_access::suspension_deadline(*r.engine, current).has_value());
    // The sink commits stream ownership before exchange::upgrade clears its flag.
    LT_CHECK(!h::detail::connection_engine_test_access::suspension_deadline(*r.engine, current, true, true));
    LT_CHECK(!h::detail::connection_engine_test_access::suspension_deadline(*r.engine, current, true));
LT_END_AUTO_TEST(transfer_removes_http_suspension_before_exchange_cleanup)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
