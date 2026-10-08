/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <new>
#include <memory>
#include <utility>
#include <vector>
#include <httpserver/detail/http3_request_state.hpp>
namespace httpserver::detail {
namespace {
bool valid_limits(const http3_request_limits& limits) {
    return limits.max_active_requests && limits.max_active_requests <= limits.framing.stream_records && limits.body_buffer_bytes && limits.body_buffer_bytes <= 65536 &&
           limits.response_buffer_bytes && limits.response_buffer_bytes <= 65536 && limits.max_receipt_records >= 2 && limits.max_receipt_records <= 1024 &&
           limits.max_pending_output_records && limits.max_pending_output_records <= 4096;
}
std::optional<std::size_t> expanded_size(std::span<const qpack_field> fields, std::size_t limit) {
    auto remaining = limit;
    for (const auto& field : fields)
        if (!qpack_admit_field(field.name.size(), field.value.size(), remaining)) return {};
    return limit - remaining;
}
}  // namespace
http3_request_engine::http3_request_engine(quic_storage_lease data, quic_storage_lease critical, quic_flow_control& flow, quic_recovery& recovery,
                                           const server::route_registry& routes, executor& owner, http3_request_limits limits, std::uint64_t id, net::peer_address peer)
    : state_(std::make_shared<state>(std::move(data), std::move(critical), flow, recovery, routes, owner, limits, id, peer)) {
    state_->handlers->after_resume([weak = std::weak_ptr<state>(state_)] {
        if (auto s = weak.lock()) s->reap();
    });
    if (!valid_limits(limits)) state_->fail(h3_error(0x107, "Invalid request bridge limits"));
}
http3_request_engine::~http3_request_engine() {
    disconnect({http::outcome_code::connection_closed, "HTTP/3 engine destroyed"});
    if (state_->handlers->running()) {
        // A handler may destroy its engine. Retain the state until the outermost
        // executor resume returns, then break this temporary ownership cycle.
        state_->handlers->after_resume([keep = state_] {
            keep->handlers->after_resume({});
            keep->reap();
        });
    }
}
http3_request_engine::state::~state() {
    handlers->disable();
    records.clear();
}
http3_request_engine::state::stream::stream(state& s, record& r, http::request_head head)
    : owner(s), transport(r), body(s.data.budget), request(head, this, s.connection_id, &body, this, s.peer) {
    body.on_consumed([this](std::size_t n) { owner.consumed(transport, n); });
}
http3_request_engine::state::stream::~stream() {
    handler.invalidate();
    request.disconnect(http::outcome_code::connection_closed, "HTTP/3 exchange ended");
    body.fail(http::outcome_code::connection_closed);
    handler.clear();
}
void http3_request_engine::state::stream::on_admit(const body_policy& policy) {
    const auto cap = policy.max_buffer_bytes ? std::min<std::uint64_t>(policy.max_buffer_bytes, owner.limits.body_buffer_bytes) : owner.limits.body_buffer_bytes;
    if (!body.admit(cap)) {
        owner.reset(transport, 0x107);
        return;
    }
    if (request.head().head_fields.first("expect").value_or("") == "100-continue") {
        if (!owner.section(transport, 100, {}, false)) owner.reset(transport, 0x102);
    }
}
void http3_request_engine::state::stream::on_respond(const http::status& status, const http::fields& fields) {
    owner.respond(transport, status.code(), fields, false);
}
void http3_request_engine::state::stream::on_start_response(const http::status& status, const http::fields& fields) {
    owner.respond(transport, status.code(), fields, true);
}
websocket_upgrade_result http3_request_engine::state::stream::on_upgrade(const ws_upgrade_options&) {
    websocket_upgrade_result out;
    out.status = {http::outcome_code::not_supported, "HTTP/3 CONNECT unsupported"};
    return out;
}
void http3_request_engine::state::stream::on_abort() {
    owner.reset(transport, 0x102);
}
body_push_result http3_request_engine::state::stream::push(std::span<const std::byte> bytes) {
    auto result = body.push(bytes);
    if (result.kind == body_push::failed) owner.reset(transport, 0x102);
    return result;
}
body_push_result http3_request_engine::state::stream::push_end(const http::fields& trailers) {
    if (!owner.reserve_trailers(*this, trailers)) {
        body.fail(http::outcome_code::limit_exceeded);
        owner.reset(transport, 0x107);
        return {body_push::failed, 0};
    }
    auto result = body.push_end(trailers);
    if (result.kind == body_push::failed) owner.reset(transport, 0x102);
    return result;
}
void http3_request_engine::state::fail(http3_error failure) {
    if (error) return;
    error = failure;
    disconnected = true;
    handlers->disable();
    for (auto& [id, r] : records)
        cancel(*r, failure.outcome);
}
void http3_request_engine::state::cancel(record& r, http::outcome_code reason) {
    if (r.cancelled) return;
    r.cancelled = true;
    for (auto id : r.information) {
        recovery.cancel_information(id);
        --(r.local ? retained_critical : retained);
    }
    r.information.clear();
    r.sections.clear();
    std::vector<std::byte>().swap(r.output);
    r.output_charge.release();
    r.scratch_position = r.scratch_size = r.receipt_count = 0;
    if (r.semantic) {
        r.semantic->handler.invalidate();
        r.semantic->request.disconnect(reason, "HTTP/3 request cancelled");
        r.semantic->body.discard();
        r.semantic->body.fail(reason);
    }
}
void http3_request_engine::state::reset(record& r, std::uint64_t code) {
    if (r.cancelled) return;
    if (!r.receive_done) r.action.stop_sending = quic_stop_sending_frame{r.transport->id(), code};
    r.transport->receive(quic_stop_sending_frame{r.transport->id(), code});
    r.action.reset = r.transport->take_reset_request();
    connection.terminal(r.transport->id(), {quic_terminal_kind::reset, code});
    cancel(r, http::outcome_code::protocol_error);
}
void http3_request_engine::state::abandon(record& r) {
    if (r.abandoned || r.receive_done) return;
    r.abandoned = true;
    r.action.stop_sending = quic_stop_sending_frame{r.transport->id(), 0x10c};
    // Do not credit unread body or cancel the valid response send half.
    connection.terminal(r.transport->id(), {quic_terminal_kind::reset, 0x10c});
    r.scratch_position = r.scratch_size = r.receipt_count = 0;
    r.semantic->body.discard();
}
bool http3_request_engine::state::retired(const record& r) const {
    return r.semantic->handler.done() && r.fin_retained && (r.receive_done || r.abandoned);
}
void http3_request_engine::state::reap(record& r) {
    if (!r.semantic) return;
    auto& s = *r.semantic;
    if (!r.cancelled && s.handler.done() && s.streaming && !s.body.finished()) reset(r, 0x102);
    if (r.cancelled || retired(r)) {
        r.semantic.reset();
        --active;
    }
}
void http3_request_engine::state::reap() {
    if (handlers->running() || pumps) return;
    for (auto& [id, r] : records)
        reap(*r);
}

bool http3_request_engine::state::attach(quic_stream_state& transport, std::optional<http3_role> role) {
    if (error) return false;
    const auto id = transport.id();
    if (!flow.ids().opened(id) || records.contains(id)) {
        fail(h3_error(0x103, "Invalid or recreated transport stream", id));
        return false;
    }
    auto failed = role ? connection.attach_local_stream(*role, id) : connection.attach_stream(id);
    if (failed) {
        fail(*failed);
        return false;
    }
    auto& storage = role || (id & 2) ? critical : data;
    server::reservation charge;
    const auto scratch = std::min<std::size_t>(16384, std::max<std::size_t>(1, limits.framing.data_chunk));
    const auto output_records = role ? critical_output_records : limits.max_pending_output_records;
    const auto bytes =
        sizeof(record) + 1024 + scratch + limits.max_receipt_records * sizeof(http3_request_helpers::receipt) + output_records * sizeof(quic_information_id);
    if (!storage.budget.reserve(server::resource::quic_reassembly_bytes, bytes, charge).ok()) {
        fail(h3_error(0x107, "Bridge record storage exhausted", id));
        return false;
    }
    auto r = std::make_unique<record>();
    r->transport = &transport;
    r->local = role.has_value();
    r->local_role = role.value_or(http3_role::pending);
    r->charge = std::move(charge);
    r->scratch.resize(scratch);
    r->receipts.resize(limits.max_receipt_records);
    r->information.reserve(output_records);
    records.emplace(id, std::move(r));
    return true;
}
std::optional<http3_error> http3_request_engine::attach_peer(quic_stream_state& stream) {
    auto s = state_;
    state::pump_guard guard(*s);
    try {
        s->attach(stream, {});
    } catch (const std::bad_alloc&) {
        s->fail(h3_error(0x107, "Bridge record allocation failed"));
    }
    return s->error;
}
std::optional<http3_error> http3_request_engine::attach_local(http3_role role, quic_stream_state& stream) {
    auto s = state_;
    state::pump_guard guard(*s);
    try {
        s->attach(stream, role);
    } catch (const std::bad_alloc&) {
        s->fail(h3_error(0x107, "Local bridge allocation failed"));
    }
    return s->error;
}
bool http3_request_engine::state::receipt(record& r, std::uint64_t begin, std::uint64_t end, bool body) {
    if (begin == end) return true;
    if (r.receipt_count == r.receipts.size()) return false;
    auto& entry = r.receipts[(r.receipt_head + r.receipt_count++) % r.receipts.size()];
    entry = {begin, end, body ? begin : end, body};
    r.accounted = end;
    return true;
}
void http3_request_engine::state::flush_receipts(record& r) {
    while (r.receipt_count && !r.cancelled && !r.abandoned) {
        auto& entry = r.receipts[r.receipt_head];
        auto result = entry.body ? flow.consume_body(*r.transport, entry.consumed) : flow.consume_protocol(*r.transport, entry.end);
        if (!result) {
            fail(h3_error(0x102, "Invalid transport consumption receipt", r.transport->id()));
            return;
        }
        if (entry.consumed != entry.end) return;
        r.receipt_head = (r.receipt_head + 1) % r.receipts.size();
        --r.receipt_count;
    }
}
void http3_request_engine::state::consumed(record& r, std::size_t count) {
    for (std::size_t i = 0; i < r.receipt_count && count; ++i) {
        auto& entry = r.receipts[(r.receipt_head + i) % r.receipts.size()];
        if (!entry.body) continue;
        const auto n = std::min<std::uint64_t>(count, entry.end - entry.consumed);
        entry.consumed += n;
        count -= n;
    }
    flush_receipts(r);
}
bool http3_request_engine::state::head(record& r, std::span<const qpack_field> fields) {
    if (active == limits.max_active_requests) {
        reset(r, 0x107);
        return false;
    }
    const auto expanded = expanded_size(fields, limits.framing.headers.max_expanded_bytes);
    if (!expanded) {
        reset(r, 0x107);
        return false;
    }
    // Reserve semantic heads and derived targets before materializing; the core
    // independently charges the simultaneous decoded QPACK field section.
    const auto storage = 6 * *expanded + (fields.size() + 1) * 256 + sizeof(stream) + limits.body_buffer_bytes + 3 * limits.response_buffer_bytes + 512;
    server::reservation bytes, descriptors, memory;
    if (!data.budget.reserve(server::resource::header_bytes, std::max<std::size_t>(1, 5 * *expanded), bytes).ok() ||
        !data.budget.reserve(server::resource::header_fields, fields.size() + 1, descriptors).ok() ||
        !data.budget.reserve(server::resource::quic_reassembly_bytes, storage, memory).ok()) {
        reset(r, 0x107);
        return false;
    }
    http::request_head head;
    if (!http3_convert_request(fields, head)) {
        reset(r, 0x10e);
        return false;
    }
    std::optional<std::uint64_t> length;
    http3_content_length(head.head_fields, length);
    auto value = std::make_unique<stream>(*this, r, std::move(head));
    value->head_charge = std::move(bytes);
    value->fields_charge = std::move(descriptors);
    value->storage_charge = std::move(memory);
    if (!value->body.prepare_receive(limits.body_buffer_bytes, length)) {
        reset(r, 0x107);
        return false;
    }
    r.semantic = std::move(value);
    ++active;
    r.semantic->handler.start(*handlers, run_route(routes, r.semantic->request));
    return true;
}
bool http3_request_engine::state::stage_trailers(record& r, std::span<const qpack_field> fields) {
    server::reservation charge;
    const auto expanded = expanded_size(fields, limits.framing.headers.max_expanded_bytes);
    if (!expanded) {
        reset(r, 0x107);
        return false;
    }
    const auto bytes = 3 * *expanded + fields.size() * 128;
    if (!data.budget.reserve(server::resource::quic_reassembly_bytes, std::max<std::size_t>(1, bytes), charge).ok()) {
        reset(r, 0x107);
        return false;
    }
    http::fields trailers;
    if (!http3_convert_trailers(fields, trailers)) {
        reset(r, 0x10e);
        return false;
    }
    r.semantic->trailers = std::move(trailers);
    r.semantic->trailer_charge = std::move(charge);
    return true;
}
void http3_request_engine::state::record_event(record& r, const http3_event& event) {
    if (r.event_recorded) return;
    if (event.type == 0) {
        receipt(r, r.accounted, event.payload_begin, false);
        receipt(r, event.payload_begin, event.payload_end, true);
    } else {
        receipt(r, r.accounted, connection.offset(r.transport->id()), false);
    }
    r.event_recorded = true;
    flush_receipts(r);
}
bool http3_request_engine::state::deliver_body(record& r, const http3_event& event) {
    if (!r.semantic) {
        reset(r, 0x10e);
        return false;
    }
    const auto n = std::min(event.payload.size() - r.event_position, r.semantic->body.room());
    if (n && !r.semantic->body.receive(event.payload.subspan(r.event_position, n))) {
        reset(r, 0x10e);
        return false;
    }
    // Inline wakeups may cancel the exchange or abandon its receive half.
    if (r.cancelled || r.abandoned) return false;
    r.event_position += n;
    return r.event_position == event.payload.size();
}
bool http3_request_engine::state::event(record& r) {
    const auto id = r.transport->id();
    const auto* ready = connection.event(id);
    if (!ready) return true;
    const auto event = *ready;
    record_event(r, event);
    if (event.type == 1) {
        const bool ok = r.semantic ? stage_trailers(r, event.fields) : head(r, event.fields);
        if (!ok) return false;
    } else if (!deliver_body(r, event)) {
        return false;
    }
    if (r.cancelled || r.abandoned) return false;
    connection.release_event(id);
    r.event_position = 0;
    r.event_recorded = false;
    return true;
}

bool http3_request_engine::state::read_scratch(record& r) {
    if (r.scratch_position < r.scratch_size) return true;
    r.scratch_position = r.scratch_size = 0;
    auto read = r.transport->read(r.scratch);
    r.scratch_size = read.bytes;
    if (!read.bytes) {
        if (auto terminal = r.transport->take_terminal()) r.terminal = terminal;
        finish_receive(r);
        return false;
    }
    return true;
}
bool http3_request_engine::state::receive_step(record& r) {
    if (!event(r) || r.cancelled || r.abandoned) return false;
    if (r.receipt_count + 2 > r.receipts.size()) return false;
    if (!read_scratch(r)) return false;
    const auto id = r.transport->id();
    const auto result = connection.feed(id, std::span(r.scratch).subspan(r.scratch_position, r.scratch_size - r.scratch_position), connection.offset(id));
    r.scratch_position += result.consumed;
    if (result.error) {
        fail(*result.error);
        return false;
    }
    if (!connection.event(id)) {
        receipt(r, r.accounted, connection.offset(id), false);
        flush_receipts(r);
    }
    if (result.progress == http3_progress::yield) return false;
    return result.consumed || connection.event(id);
}
void http3_request_engine::state::notice_terminal(record& r) {
    if (auto terminal = r.transport->take_terminal()) {
        if (terminal->kind == quic_terminal_kind::reset)
            peer_reset(r, *terminal);
        else
            r.terminal = terminal;
    }
}
http3_progress http3_request_engine::state::receive(record& r) {
    if (r.local) return http3_progress::input;
    notice_terminal(r);
    if (error) return http3_progress::failed;
    if (r.receive_done || r.cancelled || r.abandoned) return http3_progress::input;
    // The core's frame budget bounds each turn; scratch is never overwritten
    // while a DATA event still borrows it. Siblings are pumped independently.
    while (!error && receive_step(r)) {}
    return error ? http3_progress::failed : http3_progress::input;
}
void http3_request_engine::state::finish_receive(record& r) {
    if (!r.terminal || r.receive_done || connection.event(r.transport->id())) return;
    const auto terminal = *r.terminal;
    if (terminal.kind == quic_terminal_kind::reset) {
        peer_reset(r, terminal);
        return;
    }
    if (auto error = connection.terminal(r.transport->id(), terminal)) {
        fail(*error);
        return;
    }
    r.receive_done = true;
    if (r.semantic && !r.semantic->body.end_receive(std::move(r.semantic->trailers))) reset(r, 0x10e);
}
http3_progress http3_request_engine::pump_receive(std::uint64_t id) {
    auto s = state_;
    state::pump_guard guard(*s);
    const auto it = s->records.find(id);
    if (it == s->records.end()) {
        s->fail(h3_error(0x103, "Unknown receive stream", id));
        return http3_progress::failed;
    }
    try {
        return s->receive(*it->second);
    } catch (const std::bad_alloc&) {
        s->fail(h3_error(0x107, "Request allocation failed", id));
        return http3_progress::failed;
    }
}
void http3_request_engine::state::peer_reset(record& r, quic_stream_terminal terminal) {
    const auto id = r.transport->id();
    if (!r.reset_settled) {
        if (!flow.settle_reset(*r.transport)) {
            fail(h3_error(0x102, "Invalid RESET settlement", id));
            return;
        }
        r.reset_settled = true;
    }
    if (auto error = connection.terminal(id, terminal)) {
        fail(*error);
        return;
    }
    r.receive_done = true;
    // A peer honoring our receive-abandonment STOP_SENDING leaves the valid
    // response half usable. An independently initiated RESET cancels the route.
    if (!r.abandoned || terminal.error != 0x10c) cancel(r, http::outcome_code::cancelled);
}
void http3_request_engine::terminal(std::uint64_t id, quic_stream_terminal terminal) {
    auto s = state_;
    state::pump_guard guard(*s);
    auto it = s->records.find(id);
    if (it == s->records.end()) return;
    auto& r = *it->second;
    if (r.local) {
        if (auto error = s->connection.terminal(id, terminal)) s->fail(*error);
        return;
    }
    if (terminal.kind == quic_terminal_kind::reset) {
        s->peer_reset(r, terminal);
        return;
    }
    if (!r.receive_done && !r.cancelled) {
        r.terminal = terminal;
        s->receive(r);
    }
}
void http3_request_engine::stop_sending(std::uint64_t id, std::uint64_t error) {
    auto s = state_;
    state::pump_guard guard(*s);
    auto it = s->records.find(id);
    if (it == s->records.end()) return;
    auto& r = *it->second;
    if (r.local) {
        if (auto failure = s->connection.terminal(id, {quic_terminal_kind::reset, error})) s->fail(*failure);
        return;
    }
    s->reset(r, error);
}
std::optional<http3_request_action> http3_request_engine::take_action() {
    for (auto& [id, r] : state_->records) {
        if (!r->action.reset && !r->action.stop_sending) continue;
        auto result = r->action;
        r->action = {};
        return result;
    }
    return {};
}
void http3_request_engine::disconnect(http::outcome reason) {
    auto s = state_;
    state::pump_guard guard(*s);
    if (s->disconnected) return;
    s->disconnected = true;
    s->handlers->disable();
    for (auto& [id, r] : s->records)
        s->cancel(*r, reason.code());
}
void http3_request_engine::begin_turn() {
    state_->reap();
    state_->connection.begin_turn();
}
const std::optional<http3_error>& http3_request_engine::failure() const {
    return state_->error;
}
}  // namespace httpserver::detail
