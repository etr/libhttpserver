/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <utility>
#include <httpserver/detail/http3_request_state.hpp>
namespace httpserver::detail {
http3_request_engine::state::stream::stream(state& s, record& r, http::request_head head)
: owner(s), transport(r), budget(child_budget(s.data.budget)), id(r.transport->id()), method(head.request_method.id()),
peer_section_limit(s.connection.peer_settings().max_field_section), body(budget), request(head, &lifecycle, s.connection_id, &body, this, s.peer, s.native.tls),
lifecycle(*this, s.bus(), request.head(), &response_refused) {
    if (!s.native.workers) {
        body.on_consumed([this](std::size_t n) { owner.consumed(transport, n); });
    }
}
http3_request_engine::state::stream::~stream() {
    handler.invalidate();
    request.disconnect(http::outcome_code::connection_closed, "HTTP/3 exchange ended");
    body.fail(http::outcome_code::connection_closed);
    handler.clear();
    // Native finish already completed hooks before releasing the state lease.
    // The affinity closure can keep this stream alive beyond that state.
    if (!native_retired.load()) complete_request(owner.bus(), request, lifecycle);
}
void http3_request_engine::state::stream::on_admit(const body_policy& policy) {
    const auto cap = policy.max_buffer_bytes ? std::min<std::uint64_t>(policy.max_buffer_bytes, owner.limits.body_buffer_bytes) : owner.limits.body_buffer_bytes;
    if (owner.native.workers) {
        if (!body.admit(cap)) native_abort = true;
        if (request.head().head_fields.first("expect").value_or("") == "100-continue") native_continue = true;
        notify(); return;
    }
    if (!body.admit(cap)) {
        owner.reset(transport, 0x107);
        return;
    }
    if (request.head().head_fields.first("expect").value_or("") == "100-continue") {
        if (!owner.section(transport, 100, {}, false)) owner.reset(transport, 0x102);
    }
}
void http3_request_engine::state::stream::on_respond(const http::status& status, const http::fields& fields) {
    if (owner.native.workers) native_respond(status.code(), fields, false);
    else owner.respond(transport, status.code(), fields, false);
}
void http3_request_engine::state::stream::on_start_response(const http::status& status, const http::fields& fields) {
    if (owner.native.workers) native_respond(status.code(), fields, true);
    else owner.respond(transport, status.code(), fields, true);
}
void http3_request_engine::state::stream::on_abort() {
    if (owner.native.workers) {
        native_abort = true; notify();
    } else {
        owner.reset(transport, 0x102);
    }
}
body_push_result http3_request_engine::state::stream::push(std::span<const std::byte> bytes) {
    auto result = body.push(bytes);
    if (result.kind == body_push::failed) {
        if (owner.native.workers) {
            native_abort = true; notify();
        }
        else owner.reset(transport, 0x102);
    }
    return result;
}
body_push_result http3_request_engine::state::stream::push_end(const http::fields& trailers) {
    if (!owner.reserve_trailers(*this, trailers)) {
        body.fail(http::outcome_code::limit_exceeded);
        if (owner.native.workers) {
            native_abort = true; notify();
        }
        else owner.reset(transport, 0x107);
        return {body_push::failed, 0};
    }
    auto result = body.push_end(trailers);
    if (result.kind == body_push::failed) {
        if (owner.native.workers) {
            native_abort = true; notify();
        }
        else owner.reset(transport, 0x102);
    }
    return result;
}
}  // namespace httpserver::detail
