/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <httpserver/detail/http2_request_state.hpp>

namespace httpserver::detail::http2_request_helpers {

void append_frame(std::vector<std::uint8_t>& out, std::uint8_t type, std::uint8_t flags, std::uint32_t stream, std::span<const std::uint8_t> payload) {
    const auto n = payload.size();
    out.insert(out.end(), {static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n), type, flags, static_cast<std::uint8_t>(stream >> 24),
                           static_cast<std::uint8_t>(stream >> 16), static_cast<std::uint8_t>(stream >> 8), static_cast<std::uint8_t>(stream)});
    out.insert(out.end(), payload.begin(), payload.end());
}

std::optional<std::string_view> canonical_length(std::string_view value) {
    if (value.empty()) return {};
    for (char c : value)
        if (c < '0' || c > '9') return {};
    const auto first = value.find_first_not_of('0');
    return first == std::string_view::npos ? std::string_view{} : value.substr(first);
}

bool valid_response_lengths(const http::fields& fields, bool metadata_only) {
    std::optional<std::string_view> prior;
    for (const auto& value : fields.all("content-length")) {
        const auto number = canonical_length(value);
        if (!number) return false;
        if (!metadata_only && !number->empty()) return false;
        if (prior && *prior != *number) return false;
        prior = number;
    }
    return true;
}

}  // namespace httpserver::detail::http2_request_helpers

namespace httpserver::detail {

// cpplint cannot track this constructor's nested qualifiers or braced initializers.
// NOLINTBEGIN(whitespace/indent_namespace)
http2_request_engine::state::stream::stream(state& s, std::uint32_t n, http::request_head h, server::resource_budget scope)
    : owner(s),
      budget(std::move(scope)),
      id(n),
      head(std::move(h)),
      body(budget),
      lifecycle(*this, s.bus(), head, &response_refused, s.native.tls),
      request(head, &lifecycle, s.connection_id, &body, this, s.peer, s.native.tls) {}
// NOLINTEND

auto http2_request_engine::state::stream::on_admit(const body_policy& policy) -> void {
    const auto n = policy.max_buffer_bytes ? std::min<std::uint64_t>(policy.max_buffer_bytes, owner.limits.body_buffer_bytes) : owner.limits.body_buffer_bytes;
    if (!body.admit(n)) {
        if (affinity) {
            native_abort = true;
            notify();
        } else {
            owner.reset(id, http2_error_code::refused_stream);
        }
        return;
    }
    if (head.head_fields.first("expect").value_or("") == "100-continue") {
        if (affinity) {
            native_continue = true;
            notify();
        } else {
            owner.continue_response(*this);
        }
    }
}

auto http2_request_engine::state::stream::on_respond(const http::status& status, const http::fields& fields) -> void {
    if (affinity)
        native_respond(status.code(), fields, false);
    else
        owner.respond(id, status.code(), fields, false);
}

auto http2_request_engine::state::stream::on_start_response(const http::status& status, const http::fields& fields) -> void {
    if (affinity)
        native_respond(status.code(), fields, true);
    else
        owner.respond(id, status.code(), fields, true);
}

auto http2_request_engine::state::stream::on_abort() -> void {
    if (affinity) {
        native_abort = true;
        notify();
    } else {
        owner.reset(id, http2_error_code::internal_error);
    }
}

auto http2_request_engine::state::stream::push(std::span<const std::byte> bytes) -> body_push_result {
    auto result = body.push(bytes);
    if (result.kind == body_push::failed) on_abort();
    return result;
}

auto http2_request_engine::state::stream::push_end(const http::fields& fields) -> body_push_result {
    auto result = body.push_end(fields);
    if (result.kind == body_push::failed) on_abort();
    return result;
}

http2_request_engine::state::stream::~stream() {
    if (affinity) return;
    owner.terminal(*this, !reset_pending && send_ended && body.receive_ended(), server::diagnostic_reason::local_stop);
    request.disconnect(http::outcome_code::connection_closed, "HTTP/2 stream ended");
    body.fail(http::outcome_code::connection_closed);
    handler.clear();
    complete_request(owner.bus(), request, lifecycle);
}

auto http2_request_engine::state::closed_reason(std::uint32_t id) const -> std::optional<closed_kind> {
    for (const auto& entry : closed)
        if (entry.id == id) return entry.kind;
    return {};
}

auto http2_request_engine::state::remember_closed(std::uint32_t id, closed_kind kind) -> void {
    for (auto& entry : closed)
        if (entry.id == id) {
            entry.kind = kind;
            return;
        }
    for (std::size_t offset = 0; offset < closed.size(); ++offset) {
        const auto slot = (closed_next + offset) % closed.size();
        // Never forget a local reset: late HEADERS must still decode, and
        // DATA must still debit connection credit without a duplicate reset.
        if (closed[slot].id && closed[slot].kind == closed_kind::local_reset) continue;
        closed[slot] = {id, kind};
        closed_next = (slot + 1) % closed.size();
        return;
    }
    fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded));
}

auto http2_request_engine::state::observe(server::diagnostic_kind kind, std::uint64_t stream_id, http::outcome_code code, server::diagnostic_reason reason, server::resource resource_kind) -> void {
    server::diagnostic_event event{kind, reason, code};
    event.protocol = http::protocol::http_2;
    event.connection_id = connection_id;
    event.stream_id = stream_id;
    event.resource_kind = resource_kind;
    diagnostics_access::record(dispatch_context.diagnostics, event);
}

auto http2_request_engine::state::terminal(stream& value, bool complete, server::diagnostic_reason reason) -> void {
    if (!value.diagnostic_admitted || value.diagnostic_terminal) return;
    value.diagnostic_terminal = true;
    observe(complete ? server::diagnostic_kind::stream_completed : server::diagnostic_kind::stream_cancelled, value.id, complete ? http::outcome_code::ok : http::outcome_code::cancelled,
            complete ? server::diagnostic_reason::none : reason);
}

auto http2_request_engine::state::flow_stall(stream& value, bool blocked) -> void {
    if (blocked && !value.flow_blocked) observe(server::diagnostic_kind::flow_stalled, value.id, http::outcome_code::ok, server::diagnostic_reason::flow_control);
    value.flow_blocked = blocked;
}

// cpplint cannot track this constructor's nested qualifiers or braced initializers.
// NOLINTBEGIN(whitespace/indent_namespace)
http2_request_engine::state::state(server::resource_budget b, const server::route_registry& r, executor& e, http2_request_limits l, request_dispatch_context d, std::uint64_t id, net::peer_address p,
                                   http2_native_context context)
    : native(std::move(context)),
      budget(b),
      routes(r),
      dispatch_context(std::move(d)),
      connection_id(id),
      peer(p),
      owner(e),
      handlers(std::make_shared<route_executor>(e)),
      limits(l),
      connection(b, l.connection, receive_settings(l.max_streams), true, native.workers != nullptr),
      scope(native.drain ? native.drain : std::make_shared<drain_scope>(dispatch_context.diagnostics)) {
    connection_counted = !native.drain;
    if (connection_counted) scope->enter();
    if (native.workers) {
        if (!reserve(server::resource::body_buffer_bytes, limits.headers.max_compressed_bytes, block_charge)) {
            fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded));
        } else {
            try {
                block.reserve(limits.headers.max_compressed_bytes);
            } catch (const std::bad_alloc&) {
                fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded));
            }
        }
    }
}
// NOLINTEND

http2_request_engine::state::~state() {
    handlers->disable();
    for (auto& [id, value] : streams) cancel_websocket(*value, {http::outcome_code::connection_closed, "HTTP/2 engine destroyed"});
    streams.clear();
    leave_connection();
}

auto http2_request_engine::state::leave_connection() -> void {
    if (connection_counted) {
        connection_counted = false;
        scope->leave();
    }
}

auto http2_request_engine::state::retire_cancelled() -> void {
    if (phase == drain_phase::cancelled && !handlers->running() && !websocket_pumps) {
        if (native.workers) {
            for (auto& [id, value] : streams) value->native_cancel(http::outcome_code::timeout);
            std::erase_if(streams, [](const auto& entry) { return entry.second->native_retired.load(); });
            if (streams.empty()) leave_connection();
        } else {
            streams.clear();
            leave_connection();
        }
    }
}

auto http2_request_engine::state::receive_settings(std::size_t max_streams) -> http2_settings {
    http2_settings settings;
    settings.initial_window_size = 0;
    settings.enable_connect_protocol = 1;
    settings.max_concurrent_streams = static_cast<std::uint32_t>(std::min<std::size_t>(max_streams, 0xffffffff));
    return settings;
}

auto http2_request_engine::state::discard(stream& value) -> void {
    terminal(value, false, closed_reason(value.id) == closed_kind::peer_reset ? server::diagnostic_reason::peer_reset : server::diagnostic_reason::local_stop);
    value.reset_pending = true;
    cancel_websocket(value, {http::outcome_code::cancelled, "HTTP/2 stream reset"});
    connection.discard_stream_credit(value.id);
    value.credit_queued = 0;
    value.body.fail(http::outcome_code::connection_closed);
    consumed += value.body.unread() + value.body.consumed.exchange(0);
    value.body.discard_received();
    if (native.workers)
        value.native_cancel(http::outcome_code::connection_closed);
    else
        value.request.disconnect(http::outcome_code::connection_closed, "HTTP/2 stream reset");
    value.reset_pending = true;
}

auto http2_request_engine::state::incomplete(const stream& value) const -> bool {
    return !value.websocket && value.done() && value.response_started && !value.body.finished() && !value.send_ended && !value.reset_pending;
}

auto http2_request_engine::state::retired(stream& value) -> bool {
    if (!native.workers) return value.reset_pending || (value.done() && value.send_ended && value.body.receive_ended());
    if (!value.reset_pending && value.done() && value.send_ended && value.body.receive_ended()) value.native_cancel(http::outcome_code::connection_closed, true);
    return value.native_retired.load();
}

auto http2_request_engine::state::reap() -> void {
    if (handlers->running() || websocket_pumps) return;
    if (phase == drain_phase::cancelled) {
        retire_cancelled();
        return;
    }
    if (connection.failure()) {
        if (native.workers) {
            for (auto& [id, value] : streams) value->native_cancel(connection.failure()->outcome);
            std::erase_if(streams, [](const auto& entry) { return entry.second->native_retired.load(); });
        } else {
            streams.clear();
        }
        return;
    }
    for (auto it = streams.begin(); it != streams.end();) {
        auto& value = *it->second;
        if (!value.reset_pending && value.done() &&
            (native.workers ? value.native_refused.load() : (value.request.disconnected() && value.request.disconnect_reason().code() == http::outcome_code::peer_refused))) {
            reset(value.id, http2_error_code::refused_stream);
        }
        if (incomplete(value)) {
            reset(value.id, http2_error_code::internal_error);
        }
        if (retired(value)) {
            consumed += value.body.unread() + value.body.consumed.exchange(0);
            if (!value.reset_pending) remember_closed(value.id, closed_kind::complete);
            if (connection.failure()) return;
            if (native.workers) terminal(value, !value.reset_pending && value.send_ended && value.body.receive_ended(), server::diagnostic_reason::local_stop);
            it = streams.erase(it);
        } else {
            ++it;
        }
    }
}

auto http2_request_engine::state::clear_block() -> void {
    if (native.workers) {
        block.clear();
    } else {
        std::vector<std::uint8_t>().swap(block);
        block_charge.release();
    }
    assembling = 0;
}

auto http2_request_engine::state::fail(http2_error error) -> void {
    if (!diagnostic_failure) {
        diagnostic_failure = true;
        observe(server::diagnostic_kind::protocol_failure, 0, error.outcome, server::diagnostic_reason::malformed_input);
    }
    handlers->disable();
    connection.terminate(error);
    ++websocket_pumps;
    for (auto& [id, value] : streams) cancel_websocket(*value, {error.outcome, "HTTP/2 connection terminated"});
    --websocket_pumps;
    clear_block();
    pending.clear();
    // A sink may fail while its handler is executing. Destruction must
    // wait for the next owner pump, after that resume has returned.
    // Active compression output is immutable after exposure. Finish its
    // field block before the terminal GOAWAY, retaining the borrowed span.
}

auto http2_request_engine::state::queue_room() -> bool {
    // Reset-only entries do not occupy live streams. Bound pending work as
    // well as bytes, so cancellation scans never grow with attacker input.
    if (pending.size() < limits.max_streams) return true;
    fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded));
    return false;
}

auto http2_request_engine::state::reset(std::uint32_t id, http2_error_code code) -> void {
    if (connection.failure()) return;
    remember_closed(id, closed_kind::local_reset);
    if (connection.failure()) return;
    if (auto found = streams.find(id); found != streams.end() && !found->second->reset_pending) {
        discard(*found->second);
    }
    // Drop only semantic, not yet encoded responses. Encoder state changes
    // in output() when this queue reaches the wire owner.
    std::erase_if(pending, [id](const response& r) { return r.stream == id; });
    if (!queue_room()) return;
    response value;
    value.stream = id;
    value.reset = code;
    if (!reserve(server::resource::response_queue_bytes, 13 + sizeof(response) + 64, value.charge)) {
        fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded));
        return;
    }
    pending.push_back(std::move(value));
}

auto http2_request_engine::state::field_block_error(bool decoded) const -> std::optional<http2_error> {
    // RFC 9113 section 4.3 requires decompression even for discarded fields.
    if (!decoded) return connection_error(http2_error_code::compression_error);
    if (!closed_headers || closed_reason(assembling) == closed_kind::local_reset) return {};
    return connection_error(closed_reason(assembling) ? http2_error_code::stream_closed : http2_error_code::protocol_error);
}

auto http2_request_engine::state::assemble() -> std::optional<http2_error> {
    const auto h = connection.header();
    auto fragment = connection.payload();
    if (h.type == 1) {
        connect_eligible = connection.connect_protocol_at_frame_start();
        assembling = h.stream_id;
        end_stream = (h.flags & 1) != 0;
        if (!(assembling & 1)) return connection_error(http2_error_code::protocol_error);
        closed_headers = assembling <= last_stream && !streams.contains(assembling);
        last_stream = std::max(last_stream, assembling);
        if (!native.workers && !reserve(server::resource::body_buffer_bytes, limits.headers.max_compressed_bytes, block_charge)) {
            return connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
        }
        block.reserve(limits.headers.max_compressed_bytes);
        if (h.flags & 8) {
            const auto padding = fragment.front();
            fragment = fragment.subspan(1);
            fragment = fragment.first(fragment.size() - padding);
        }
        if (h.flags & 32) fragment = fragment.subspan(5);
    }
    if (fragment.size() > limits.headers.max_compressed_bytes - block.size()) {
        return connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
    }
    block.insert(block.end(), fragment.begin(), fragment.end());
    if (h.flags & 4) return dispatch();
    return {};
}

auto http2_request_engine::state::discard_headers(std::uint32_t id) -> bool {
    const bool draining_opening = phase != drain_phase::running && !streams.contains(id);
    if (!closed_headers && !rejected && !draining_opening) return false;
    if (!closed_headers) reset(id, rejected.value_or(http2_error_code::refused_stream));
    closed_headers = false;
    rejected.reset();
    return true;
}

auto http2_request_engine::state::dispatch() -> std::optional<http2_error> {
    auto section_budget = native.workers ? (streams.contains(assembling) ? streams.at(assembling)->budget : child_budget(budget)) : budget;
    resource_charge temporary_bytes(section_budget, server::resource::header_bytes);
    resource_charge temporary_fields(section_budget, server::resource::header_fields);
    auto decode_limits = limits.headers;
    const auto free_bytes = budget.capacity(server::resource::header_bytes) - budget.in_use(server::resource::header_bytes);
    decode_limits.max_expanded_bytes = std::min(decode_limits.max_expanded_bytes, free_bytes / head_copy_allowance);
    decode_limits.max_fields = std::min(decode_limits.max_fields, budget.capacity(server::resource::header_fields) - budget.in_use(server::resource::header_fields));
    if (!temporary_bytes.grow(decode_limits.max_expanded_bytes * head_copy_allowance) || !temporary_fields.grow(decode_limits.max_fields)) {
        return connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
    }
    auto decoded = connection.compression().decoder().decode(block, decode_limits);
    const auto id = assembling;
    const auto ended = end_stream;
    const auto error = field_block_error(decoded.status.ok());
    clear_block();
    if (error) return error;
    if (discard_headers(id)) return {};
    if (streams.contains(id)) {
        trailers(id, ended, decoded.fields, std::move(temporary_bytes), std::move(temporary_fields));
        return {};
    }
    http::request_head head;
    http2_connect_metadata connect;
    if (!http2_convert_request(decoded.fields, head, connect)) {
        reset(id, http2_error_code::protocol_error);
        return {};
    }
    std::optional<std::uint64_t> length;
    http2_content_length(head.head_fields, length);
    if (ended && length.value_or(0)) {
        reset(id, http2_error_code::protocol_error);
        return {};
    }
    if (!connect_allowed(id, head, connect)) return {};
    return admit(id, std::move(head), decoded.fields, ended, std::move(connect), std::move(section_budget), std::move(temporary_bytes), std::move(temporary_fields));
}

auto http2_request_engine::state::connect_allowed(std::uint32_t id, const http::request_head& head, const http2_connect_metadata& connect) -> bool {
    if (connect.protocol && !connect_eligible) {
        reset(id, http2_error_code::protocol_error);
        return false;
    }
    if (head.request_method.id() == http::method_id::connect && connect.protocol != "websocket") {
        reset(id, http2_error_code::refused_stream);
        return false;
    }
    return true;
}

auto http2_request_engine::state::admit(std::uint32_t id, http::request_head head, const std::vector<hpack_field>& fields, bool ended, http2_connect_metadata connect,
                                        server::resource_budget stream_budget, resource_charge bytes, resource_charge count) -> std::optional<http2_error> {
    reap();
    server::reservation count_charge;
    if (streams.size() >= limits.max_streams) {
        observe(server::diagnostic_kind::resource_refused, id, http::outcome_code::limit_exceeded, server::diagnostic_reason::capacity);
        reset(id, http2_error_code::refused_stream);
        return {};
    }
    if (!stream_budget.reserve(server::resource::streams, 1, count_charge).ok()) {
        reset(id, http2_error_code::refused_stream);
        return {};
    }
    std::size_t retained = 0;
    for (const auto& field : fields) retained += field.name.size() + field.value.size() + 32;
    server::reservation metadata;
    if (!stream_budget.reserve(server::resource::body_buffer_bytes, sizeof(stream) + sizeof(http2_dispatch) + sizeof(route_executor) + 512, metadata).ok()) {
        reset(id, http2_error_code::refused_stream);
        return {};
    }
    auto value = std::make_shared<stream>(*this, id, std::move(head), std::move(stream_budget));
    value->connect = std::move(connect);
    value->metadata_charge = std::move(metadata);
    value->count_charge = std::move(count_charge);
    bytes.shrink(bytes.units() - retained * head_copy_allowance);
    count.shrink(count.units() - fields.size());
    value->bytes_charge = std::move(bytes);
    value->fields_charge = std::move(count);
    std::optional<std::uint64_t> length;
    http2_content_length(value->head.head_fields, length);
    value->body.send_window.available = peer_initial;
    value->body.receive_window.available = local_initial;
    if (!value->body.prepare_receive(65535, ended, value->connect.protocol ? std::nullopt : length)) {
        reset(id, http2_error_code::refused_stream);
        return {};
    }
    auto* accepted = value.get();
    streams.emplace(id, std::move(value));
    accepted->diagnostic_admitted = true;
    observe(server::diagnostic_kind::stream_admitted, id);
    accepted_stream = std::max(accepted_stream, id);
    accepted->drain_unit.emplace(*scope);
    connection.processed_stream(id);
    if (native.workers)
        accepted->native_start();
    else
        accepted->handler.start(*handlers, dispatch_request(routes, bus(), dispatch_context.pages, accepted->lifecycle, accepted->request, dispatch_context.peers));
    return {};
}

auto http2_request_engine::state::admit_opening(http2_connection::time_point now) -> std::optional<http2_error> {
    const auto h = connection.header();
    if (h.type == 1 && (h.stream_id & 1) && h.stream_id > last_stream) return connection.open_stream(now);
    return {};
}

auto http2_request_engine::state::process_frame() -> std::optional<http2_error> {
    auto h = connection.header();
    if (h.type == 1 || h.type == 9) return assemble();
    if (h.type == 3) return peer_reset(h.stream_id);
    if (h.type == 0) {
        return data_frame();
    } else if (h.type == 8) {
        return update_window();
    }
    return {};
}

}  // namespace httpserver::detail
