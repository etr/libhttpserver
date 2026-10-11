/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <atomic>
#include <memory>
#include <new>
#include <utility>
#include <httpserver/detail/http3_request_state.hpp>
namespace httpserver::detail {
void http3_request_engine::state::observe_websocket(websocket_driver& driver, std::uint64_t id) {
    if (native.workers) {
        driver.observe_progress([weak = weak_from_this()] {
            if (auto self = weak.lock(); self && self->native.progress && *self->native.progress) (*self->native.progress)();
        });
        return;
    }
    auto queued = std::make_shared<std::atomic<bool>>(false);
    auto progress = [weak = weak_from_this(), id, queued] {
        if (queued->exchange(true)) return;
        if (auto self = weak.lock()) {
            self->owner.post([weak, id, queued] {
                // Keep notifications during this feed coalesced, including on
                // executors that invoke post inline.
                if (auto self = weak.lock()) {
                    pump_guard guard(*self);
                    auto it = self->records.find(id);
                    if (!self->disconnected && it != self->records.end() && !it->second->cancelled) {
                        self->pump_websocket_input(*it->second);
                    }
                }
                queued->store(false);
            });
        }
    };
    driver.observe_progress(std::move(progress));
}
void http3_request_engine::state::pump_websocket_input(record& r) {
    try {
        receive(r);
        websocket_terminal(r);
    } catch (const std::bad_alloc&) {
        fail(h3_error(0x107, "WebSocket transport allocation failed", r.transport->id()));
    }
}
bool http3_request_engine::state::deliver_websocket(record& r, const http3_event& event) {
    auto driver = r.semantic->websocket;
    const auto progress = driver->snapshot();
    if (progress.peer_close || progress.terminal) {
        websocket_terminal(r);
        return false;
    }
    if (r.websocket_input_blocked && !progress.input_ready) return false;
    auto fed = driver->feed(event.payload.subspan(r.event_position));
    if (r.cancelled) return false;
    r.event_position += fed.consumed;
    consumed(r, fed.consumed);
    r.websocket_input_blocked = fed.blocked && fed.status.ok();
    websocket_terminal(r);
    return !r.cancelled && !r.websocket_input_blocked && r.event_position == event.payload.size();
}
void http3_request_engine::state::websocket_terminal(record& r) {
    if (r.cancelled || !r.semantic || !r.semantic->websocket) return;
    const auto progress = r.semantic->websocket->snapshot();
    if (progress.terminal && !progress.clean) reset(r, 0x10c);
}
}  // namespace httpserver::detail
