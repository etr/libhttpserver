/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/http2_request_state.hpp>
namespace httpserver::detail {
std::optional<http2_connection::time_point> http2_request_engine::state::native_deadline(http2_connection::time_point now) {
    if (!native.workers || connection.failure()) return {};
    auto next = connection.settings_deadline();
    const auto include = [&](std::optional<http2_connection::time_point> candidate) {
        if (candidate && (!next || *candidate < *next)) next = candidate;
    };
    include(native_drain_deadline);
    if (native_header_anchor) include(*native_header_anchor + limits.timeouts.header);
    for (auto& [id, value] : streams) {
        if (value->reset_pending || value->native_closed.load()) continue;
        if (value->native_suspended.load()) {
            if (!value->native_suspension_anchor) value->native_suspension_anchor = now;
            include(*value->native_suspension_anchor + limits.timeouts.suspension);
        } else {
            value->native_suspension_anchor.reset();
        }
        if (value->body.admitted() && !value->native_body_admitted) {
            value->native_body_admitted = true;
            value->native_body_anchor = now;
        }
        if (value->native_body_admitted && !value->body.receive_ended() && !value->websocket) include(value->native_body_anchor + limits.timeouts.body_idle);
        if (value->body.queued() || (value->response_started && !value->headers_sent)) {
            if (!value->native_write_anchor) value->native_write_anchor = now;
            include(*value->native_write_anchor + limits.timeouts.write_idle);
        } else {
            value->native_write_anchor.reset();
        }
        if (value->websocket) {
            auto progress = value->websocket->snapshot();
            if (progress.closing_since) include(*progress.closing_since + limits.timeouts.ws_close);
            if (progress.output_pending) {
                if (!value->websocket_write_anchor) value->websocket_write_anchor = progress.output_pending_since;
                if (value->websocket_write_anchor) include(*value->websocket_write_anchor + limits.timeouts.write_idle);
            }
        }
    }
    return next;
}
void http2_request_engine::state::expire_native(http2_connection::time_point now) {
    if (!native.workers || connection.failure()) return;
    if (auto error = connection.check_timeout(now)) {
        fail(*error);
        return;
    }
    if (native_header_anchor && now >= *native_header_anchor + limits.timeouts.header) {
        fail(connection_error(http2_error_code::no_error, http::outcome_code::timeout)); return;
    }
    if (native_drain_deadline && now >= *native_drain_deadline) {
        scope->expire_if_due(now); cancel_drain(); return;
    }
    static_cast<void>(native_deadline(now));
    for (auto& [id, value] : streams) {
        if (value->reset_pending || value->native_closed.load()) continue;
        const auto due = [now](auto anchor, auto timeout) { return anchor && now >= *anchor + timeout; };
        if (due(value->native_suspension_anchor, limits.timeouts.suspension) ||
            (value->body.admitted() && !value->body.receive_ended() && !value->websocket && now >= value->native_body_anchor + limits.timeouts.body_idle) ||
            due(value->native_write_anchor, limits.timeouts.write_idle)) {
            value->native_cancel(http::outcome_code::timeout);
            reset(id, http2_error_code::cancel);
        }
    }
}
}  // namespace httpserver::detail
