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
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/drain_scope.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/websocket_session_state.hpp>
#include "./io_loopback.hpp"
#include "./websocket_test_helpers.hpp"
#include "./littletest.hpp"

#ifndef TEST_UNIT_WEBSOCKET_ENGINE_HELPERS_HPP_
#define TEST_UNIT_WEBSOCKET_ENGINE_HELPERS_HPP_
// Internal seam keeps scheduling and copy-work assertions out of the public API.
namespace httpserver::detail {
struct connection_engine_test_access {
    struct queue_snapshot {
        std::size_t tail = 0, body = 0, output = 0, ws_input = 0, ws_output = 0;
        std::size_t total() const { return tail + body + output + ws_input + ws_output; }
    };
    static queue_snapshot queues(connection_engine& engine) {
        std::lock_guard lock(engine.mu_);
        queue_snapshot result;
        result.tail = engine.pending_tail_.size() + engine.early_bytes_.size();
        result.body = engine.body_ ? engine.body_->staged_bytes() : 0;
        result.output = engine.outbox_.queued_bytes();
        if (engine.websocket_) {
            auto usage = engine.websocket_->usage();
            result.ws_input = usage.incoming_bytes; result.ws_output = usage.output_bytes;
        }
        return result;
    }
    static bool http_idle(connection_engine& engine) {
        std::lock_guard lock(engine.mu_);
        return engine.current_ == nullptr && !engine.head_routed_;
    }
    static http1_response_sink& output_slot(connection_engine& engine) {
        return engine.outbox_.open(1, {});
    }

    static bool head_consumed_before_end(connection_engine& engine) {
        std::lock_guard lock(engine.mu_);
        return !engine.outbox_.empty() && engine.outbox_.queued_bytes() == 0
            && !engine.outbox_.front_complete();
    }
    static bool receive_parked(connection_engine& engine) {
        std::lock_guard lock(engine.mu_);
        auto driver = engine.websocket_;
        if (!driver) return false;
        std::lock_guard session_lock(driver->state_->mu);
        return driver->state_->callback_registered && driver->state_->receive_wait != nullptr;
    }
    static bool blocked_empty_input(connection_engine& engine) {
        std::lock_guard lock(engine.mu_);
        return engine.websocket_ && !engine.websocket_->snapshot().input_ready && engine.pending_tail_.empty();
    }
    static void install(connection_engine& engine, const std::shared_ptr<websocket_driver>& driver, std::string bytes = {}) {
        engine.websocket_ = driver;
        engine.phase_ = connection_engine::stream_phase::websocket;
        engine.head_routed_ = true;
        engine.pending_tail_ = std::move(bytes);
        engine.last_activity_ = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        engine.observe_websocket_driver(driver);
    }
    static bool upgrade_pending(connection_engine& engine) {
        std::lock_guard lock(engine.mu_);
        return engine.phase_ == connection_engine::stream_phase::upgrade_pending_flush;
    }
    static websocket_progress snapshot(connection_engine& engine) {
        std::lock_guard lock(engine.mu_);
        return engine.websocket_ ? engine.websocket_->snapshot() : websocket_progress{};
    }
    static std::optional<std::chrono::steady_clock::time_point> suspension_deadline(
            connection_engine& engine, exchange& current, bool transferred = false, bool pending = false) {
        std::lock_guard lock(engine.mu_);
        engine.current_ = &current;
        engine.phase_ = !transferred ? connection_engine::stream_phase::http
            : pending ? connection_engine::stream_phase::upgrade_pending_flush : connection_engine::stream_phase::websocket;
        return engine.suspension_deadline_locked();
    }
    static bool due(connection_engine& engine, std::chrono::steady_clock::time_point deadline) {
        return engine.watchdog_due(deadline);
    }
    static void feed(connection_engine& engine) { engine.feed_websocket_tail(); }
    static std::pair<const char*, std::size_t> retained(connection_engine& engine) {
        return {engine.pending_tail_.data(), engine.pending_tail_.size()};
    }
    static std::optional<std::chrono::steady_clock::time_point> deadline(connection_engine& engine) {
        std::lock_guard lock(engine.mu_);
        auto plan = engine.plan_watchdog_locked();
        return plan.defer || plan.exit ? std::nullopt : std::optional(plan.deadline);
    }
    static void progress(connection_engine& engine, bool written) { engine.note_transport_activity(written); }
    static void notify(connection_engine& engine) { engine.websocket_progressed(); }
    static void age_activity(connection_engine& engine) {
        std::lock_guard lock(engine.mu_);
        engine.last_activity_ = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    }
};
}  // namespace httpserver::detail
namespace h = httpserver;
namespace ws_engine_test {
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
    std::atomic<int> stop_calls{0};
    bool started = false;
    std::shared_ptr<h::detail::connection_engine> engine;
    rig() { h::server::route_registry::create(root, routes); }
    void create_engine() {
        engine = std::make_shared<h::detail::connection_engine>(backend, pool, routes, hooks, root, scope, config, 1, [this] { ++stop_calls; stopped = true; });
    }
    bool start() {
        if (!pair.ok()) return false;
        sys::set_nonblocking(pair.peer(), true);
        backend.adopt_connection(1, pair.detach_local());
        create_engine();
        engine->start(); started = true; return true;
    }
    ~rig() {
        if (engine) {
            engine->shutdown();
            if (started) {
                until([&] { return stopped.load(); });
            }
        }
    }
};
std::string opening(const std::string& protocol = {}) {
    std::string text = "GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";
    if (!protocol.empty()) text += "Sec-WebSocket-Protocol: " + protocol + "\r\n";
    return text + "\r\n";
}
}  // namespace ws_engine_test

#endif  // TEST_UNIT_WEBSOCKET_ENGINE_HELPERS_HPP_
