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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <httpserver/server/server.hpp>
#include "../../examples/v3_host_poll.hpp"
#include "./raw_http_client.hpp"
#include "./littletest.hpp"

namespace srv = httpserver::server;
namespace http = httpserver::http;
namespace {
using clock_type = std::chrono::steady_clock;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

srv::server_options options() {
    srv::server_options out;
    out.loop() = srv::loop_mode::external;
    out.concurrency().workers = 2;
    out.add_listener({"127.0.0.1", 0, false});
    out.timeouts().header = 300ms;
    return out;
}

struct host_server {
    host_server() : server(options()) { }
    ~host_server() { stop(); }
    bool start() {
        if (!server.listen().ok()) return false;
        thread = std::thread([this] {
            try {
                const auto failure_deadline = clock_type::now() + 10s;
                while (!stopping.load()) {
                    auto snapshot = server.readiness()->interests();
                    {
                        std::lock_guard lock(mu);
                        if (replay_requested) {
                            for (const auto& socket : retained) {
                                const auto live = std::find_if(snapshot.sockets.begin(), snapshot.sockets.end(),
                                    [&](const auto& current) {
                                        return socket.key == current.key && socket.generation == current.generation;
                                    });
                                if (live == snapshot.sockets.end()) {
                                    replay.push_back({socket.key, socket.generation, true, true, true, true});
                                    ++stale_replays;
                                }
                            }
                            if (!replay.empty()) {
                                replay_requested = false;
                                retained.clear();
                            }
                        }
                        for (const auto& socket : snapshot.sockets) retained.push_back(socket);
                    }
                    auto events = v3_host::wait(snapshot, failure_deadline);
                    if (clock_type::now() >= failure_deadline) {
                        failed.store(true);
                        break;
                    }
                    if (events.empty()) ++empty_dispatches;
                    for (const auto& event : events) {
                        if (snapshot.wake && event.key == snapshot.wake->key) ++wake_dispatches;
                    }
                    {
                        std::lock_guard lock(mu);
                        events.insert(events.end(), replay.begin(), replay.end());
                        replay.clear();
                    }
                    const auto result = server.readiness()->dispatch(events, clock_type::now());
                    if (!result.ok() && !stopping.load()) {
                        failed.store(true);
                        break;
                    }
                }
            } catch (...) { failed.store(true); }
        });
        return true;
    }
    void queue_stale_callbacks() {
        std::lock_guard lock(mu);
        replay_requested = true;
    }
    void stop() {
        stopping.store(true);
        server.request_stop();
        if (thread.joinable()) thread.join();
        server.stop();
    }
    srv::native_server server;
    std::atomic_bool stopping{false};
    std::atomic_bool failed{false};
    std::atomic_int stale_replays{0};
    bool replay_requested = false;
    std::atomic_int empty_dispatches{0};
    std::atomic_int wake_dispatches{0};
    std::mutex mu;
    std::vector<srv::socket_interest> retained;
    std::vector<srv::readiness_event> replay;
    std::thread thread;
};

srv::sync_response response(std::string_view text) {
    srv::sync_response out;
    out.status = http::status::from_code(200);
    const auto* data = reinterpret_cast<const std::byte*>(text.data());
    out.body.assign(data, data + text.size());
    return out;
}
}  // namespace

LT_BEGIN_SUITE(external_http1_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(external_http1_suite)

LT_BEGIN_AUTO_TEST(external_http1_suite, public_host_get_post_keepalive_and_pipeline)
    host_server fx;
    LT_CHECK(fx.server.route_sync(http::method::known(http::method_id::get), "/hello",
        [](const http::request_head&, std::span<const std::byte>) { return response("hello"); }, 16).ok());
    LT_CHECK(fx.server.route_sync(http::method::known(http::method_id::post), "/echo",
        [](const http::request_head&, std::span<const std::byte> body) {
            return response({reinterpret_cast<const char*>(body.data()), body.size()});
        }, 16).ok());
    LT_ASSERT(fx.start());
    raw_http::connection client;
    LT_ASSERT(client.connect(fx.server.get_bound_port(0)));
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: local\r\n\r\n"
                        "POST /echo HTTP/1.1\r\nHost: local\r\nContent-Length: 4\r\n\r\necho"));
    std::deque<raw_http::observed_response> seen;
    LT_ASSERT(client.receive(2, seen));
    LT_CHECK_EQ(seen[0].status, 200);
    LT_CHECK_EQ(seen[0].body, std::string("hello"));
    LT_CHECK_EQ(seen[1].status, 200);
    LT_CHECK_EQ(seen[1].body, std::string("echo"));
    LT_CHECK(client.send("GET /hello HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n"));
    LT_ASSERT(client.receive(3, seen));
    LT_CHECK_EQ(seen[2].body, std::string("hello"));
    LT_CHECK(client.receive_close(seen));
    fx.stop();
    LT_CHECK(!fx.failed.load());
    LT_CHECK(!fx.server.readiness()->interests().wake);
    LT_CHECK(fx.server.readiness()->interests().sockets.empty());
LT_END_AUTO_TEST(public_host_get_post_keepalive_and_pipeline)

LT_BEGIN_AUTO_TEST(external_http1_suite, delayed_worker_response_uses_wake_without_periodic_poll)
    host_server fx;
    std::mutex gate_mu;
    std::condition_variable gate_cv;
    bool entered = false, released = false;
    LT_CHECK(fx.server.route_sync(http::method::known(http::method_id::get), "/delayed",
        [&](const http::request_head&, std::span<const std::byte>) {
            std::unique_lock lock(gate_mu);
            entered = true;
            gate_cv.notify_all();
            gate_cv.wait_for(lock, 5s, [&] { return released; });
            return response("released");
        }, 16).ok());
    LT_ASSERT(fx.start());
    raw_http::connection client;
    LT_ASSERT(client.connect(fx.server.get_bound_port(0)));
    LT_CHECK(client.send("GET /delayed HTTP/1.1\r\nHost: local\r\n\r\n"));
    {
        std::unique_lock lock(gate_mu);
        LT_CHECK(gate_cv.wait_for(lock, 5s, [&] { return entered; }));
    }
    const int wakes = fx.wake_dispatches.load();
    LT_CHECK(client.quiet_for(20ms));
    {
        std::lock_guard lock(gate_mu);
        released = true;
    }
    gate_cv.notify_all();
    std::deque<raw_http::observed_response> seen;
    LT_ASSERT(client.receive(1, seen));
    LT_CHECK_EQ(seen[0].body, std::string("released"));
    LT_CHECK(fx.wake_dispatches.load() > wakes);
    fx.stop();
    LT_CHECK(!fx.failed.load());
LT_END_AUTO_TEST(delayed_worker_response_uses_wake_without_periodic_poll)

LT_BEGIN_AUTO_TEST(external_http1_suite, published_header_deadline_drives_empty_dispatch)
    host_server fx;
    LT_ASSERT(fx.start());
    raw_http::connection client;
    LT_ASSERT(client.connect(fx.server.get_bound_port(0)));
    LT_CHECK(client.send("GET /incomplete"));
    std::deque<raw_http::observed_response> seen;
    LT_CHECK(client.receive_close(seen));
    LT_CHECK(seen.empty());
    LT_CHECK(fx.empty_dispatches.load() > 0);
    fx.stop();
    LT_CHECK(!fx.failed.load());
LT_END_AUTO_TEST(published_header_deadline_drives_empty_dispatch)

LT_BEGIN_AUTO_TEST(external_http1_suite, reconnect_replayed_callbacks_and_host_driven_drain)
    host_server fx;
    LT_CHECK(fx.server.route_sync(http::method::known(http::method_id::get), "/hello",
        [](const http::request_head&, std::span<const std::byte>) { return response("fresh"); }, 16).ok());
    LT_ASSERT(fx.start());
    {
        raw_http::connection old;
        LT_ASSERT(old.connect(fx.server.get_bound_port(0)));
        LT_CHECK(old.send("GET /hello HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n"));
        std::deque<raw_http::observed_response> seen;
        LT_CHECK(old.receive_close(seen));
        LT_ASSERT(seen.size() == 1);
        LT_CHECK_EQ(seen.front().body, std::string("fresh"));
    }
    fx.queue_stale_callbacks();
    raw_http::connection fresh;
    LT_ASSERT(fresh.connect(fx.server.get_bound_port(0)));
    LT_CHECK(fresh.send("GET /hello HTTP/1.1\r\nHost: local\r\n\r\n"));
    std::deque<raw_http::observed_response> seen;
    LT_ASSERT(fresh.receive(1, seen));
    LT_CHECK_EQ(seen.front().body, std::string("fresh"));
    LT_CHECK(fx.stale_replays.load() > 0);
    srv::drain_ticket ticket;
    LT_CHECK(fx.server.begin_drain(2s, ticket).ok());
    LT_CHECK(fresh.receive_close(seen));
    srv::drain_result result;
    LT_CHECK(ticket.wait(result).ok());
    LT_CHECK(result.status == srv::drain_status::completed);
    LT_CHECK_EQ(result.remaining, std::size_t{0});
    fx.stop();
    LT_CHECK(!fx.failed.load());
LT_END_AUTO_TEST(reconnect_replayed_callbacks_and_host_driven_drain)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
