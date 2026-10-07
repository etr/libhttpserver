/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_request_state.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_STATE_HPP_
#include <algorithm>
#include <array>
#include <coroutine>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/http2_request_engine.hpp>
#include <httpserver/detail/http2_request_head.hpp>
#include <httpserver/detail/http2_body_stream.hpp>
#include <httpserver/detail/exchange_runner.hpp>
namespace httpserver::detail {
namespace http2_request_helpers {
// Bound simultaneous decoded fields, semantic fields and raw/derived targets.
constexpr std::size_t head_copy_allowance = 3;
inline http2_error connection_error(http2_error_code code, http::outcome_code outcome = http::outcome_code::protocol_error) {
    return {http2_error_scope::connection, code, 0, outcome, "HTTP/2 request engine failure"};
}
inline void append_frame(std::vector<std::uint8_t>& out, std::uint8_t type, std::uint8_t flags,
                  std::uint32_t stream, std::span<const std::uint8_t> payload) {
    const auto n = payload.size();
    out.insert(out.end(), {static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n), type, flags,
        static_cast<std::uint8_t>(stream >> 24), static_cast<std::uint8_t>(stream >> 16), static_cast<std::uint8_t>(stream >> 8), static_cast<std::uint8_t>(stream)});
    out.insert(out.end(), payload.begin(), payload.end());
}
inline std::optional<std::string_view> canonical_length(std::string_view value) {
    if (value.empty()) return {};
    for (char c : value) if (c < '0' || c > '9') return {};
    const auto first = value.find_first_not_of('0');
    return first == std::string_view::npos ? std::string_view{} : value.substr(first);
}
inline bool valid_response_lengths(const http::fields& fields, bool metadata_only) {
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
// A consumer keeps final_suspend alive, so the stream owns every nested
// handler frame until reaping. Queued resumes carry the task's witness and
// become no-ops after destruction, including application-owned signals.
class route_task {
 public:
    ~route_task() { clear(); }
    void start(executor& owner, task<void> task) {
        frame_ = task.release();
        auto& promise = frame_.promise();
        promise.try_consume();
        promise.set_affinity(&owner);
        promise.register_consumer(std::noop_coroutine(), &owner, {});
        owner.post([witness = promise.frame_witness_ptr(), frame = frame_] { guarded_resume(witness, frame); });
    }
    bool done() const { return frame_ && frame_.promise().state() == task_state::done; }
    void clear() {
        if (!frame_) return;
        frame_.promise().invalidate();
        frame_.destroy(); frame_ = {};
    }
 private:
    std::coroutine_handle<task<void>::promise_type> frame_;
};
}  // namespace http2_request_helpers
using namespace http2_request_helpers;  // NOLINT(build/namespaces)
struct http2_request_engine::state {
    struct stream final : exchange_sink, body_sink {
        state& owner;
        std::uint32_t id;
        http::request_head head;
        server::reservation count_charge, bytes_charge, fields_charge, trailer_charge, trailer_fields_charge, metadata_charge;
        http2_body_stream body;
        exchange request;
        route_task handler;
        std::int64_t credit_queued = 0;
        bool response_started = false, send_ended = false, reset_pending = false, headers_sent = false;
        stream(state& s, std::uint32_t n, http::request_head h)
            : owner(s), id(n), head(std::move(h)), body(s.budget), request(head, this, 0, &body, this) {}
        void on_admit(const body_policy& policy) override {
            const auto n = policy.max_buffer_bytes ? std::min<std::uint64_t>(policy.max_buffer_bytes, owner.limits.body_buffer_bytes) : owner.limits.body_buffer_bytes;
            if (!body.admit(n)) owner.reset(id, http2_error_code::refused_stream);
        }
        void on_respond(const http::status& status, const http::fields& fields) override { owner.respond(id, status.code(), fields, false); }
        void on_start_response(const http::status& status, const http::fields& fields) override { owner.respond(id, status.code(), fields, true); }
        websocket_upgrade_result on_upgrade(const ws_upgrade_options&) override {
            websocket_upgrade_result result; result.status = {http::outcome_code::invalid_state, "HTTP/2 upgrade unsupported"}; return result;
        }
        void on_abort() override { owner.reset(id, http2_error_code::internal_error); }
        body_push_result push(std::span<const std::byte> bytes) override {
            auto result = body.push(bytes);
            if (result.kind == body_push::failed) owner.reset(id, http2_error_code::internal_error);
            return result;
        }
        body_push_result push_end(const http::fields& fields) override {
            auto result = body.push_end(fields);
            if (result.kind == body_push::failed) owner.reset(id, http2_error_code::internal_error);
            return result;
        }
        const http::outcome& failure() const noexcept override { return body.failure(); }
        void park(body_write_wait& wait) override { body.park(wait); }
        void unpark(body_write_wait& wait) override { body.unpark(wait); }
        ~stream() override {
            request.disconnect(http::outcome_code::connection_closed, "HTTP/2 stream ended");
            body.fail(http::outcome_code::connection_closed); handler.clear();
        }
    };
    struct response {
        std::uint32_t stream = 0;
        bool ended = true;
        std::vector<hpack_field> fields;
        std::optional<http2_error_code> reset;
        server::reservation charge;
        std::size_t wire_limit = 0, framed_limit = 13;
    };
    server::resource_budget budget;
    const server::route_registry& routes;
    executor& owner;
    http2_request_limits limits;
    http2_connection connection;
    std::map<std::uint32_t, std::unique_ptr<stream>> streams;
    std::deque<response> pending;
    std::vector<std::uint8_t> block, active;
    server::reservation block_charge, active_charge;
    std::uint32_t assembling = 0, last_stream = 0;
    std::size_t active_used = 0;
    http2_window send_window, receive_window;
    std::uint64_t consumed = 0, connection_credit_queued = 0;
    std::uint32_t peer_initial = 65535, local_initial = 65535, selected = 0, active_stream = 0;
    std::size_t active_body = 0;
    bool active_end = false;
    bool end_stream = false, control_exposed = false;
    std::optional<http2_error_code> rejected;
    state(server::resource_budget b, const server::route_registry& r, executor& e, http2_request_limits l)
        : budget(b), routes(r), owner(e), limits(l), connection(b, {}, receive_settings(), true) {}
    static http2_settings receive_settings() { http2_settings settings; settings.initial_window_size = 0; return settings; }
    void discard(stream& value) {
        consumed += value.body.unread() + value.body.consumed; value.body.consumed = 0;
        value.body.discard_received();
        value.body.fail(http::outcome_code::connection_closed);
        value.request.disconnect(http::outcome_code::connection_closed, "HTTP/2 stream reset");
        value.reset_pending = true;
    }
    bool incomplete(const stream& value) const {
        return value.handler.done() && value.response_started && !value.body.finished() && !value.send_ended && !value.reset_pending;
    }
    bool retired(const stream& value) const {
        return value.reset_pending || (value.handler.done() && value.send_ended && value.body.receive_ended());
    }
    void reap() {
        if (connection.failure()) {
            streams.clear();
            return;
        }
        for (auto it = streams.begin(); it != streams.end();) {
            auto& value = *it->second;
            if (incomplete(value)) {
                reset(value.id, http2_error_code::internal_error);
            }
            if (retired(value)) {
                consumed += value.body.unread() + value.body.consumed;
                it = streams.erase(it);
            } else {
                ++it;
            }
        }
    }
    void sync_settings();
    void publish_credit();
    void publish_stream_credit(stream& value);
    void expose_credit(std::span<const std::uint8_t> control);
    bool data_ready(stream& value);
    void frame_headers(const response& value, std::span<const std::uint8_t> bytes);
    bool prepare_response(stream& value, std::uint16_t status, const http::fields& fields, bool streaming);
    bool reserve_response(response& value, std::size_t expanded, std::size_t fields);
    std::optional<http2_error> data_frame();
    std::optional<http2_error> update_window();
    bool encode_data();
    bool frame_data(stream& value);
    bool encode_trailers(stream& value);
    bool trailers(std::uint32_t id, bool ended, const std::vector<hpack_field>& fields);
    void clear_block() {
        std::vector<std::uint8_t>().swap(block);
        block_charge.release(); assembling = 0;
    }
    void fail(http2_error error) {
        connection.terminate(error);
        clear_block(); pending.clear();
        // A sink may fail while its handler is executing. Destruction must
        // wait for the next owner pump, after that resume has returned.
        // Active compression output is immutable after exposure. Finish its
        // field block before the terminal GOAWAY, retaining the borrowed span.
    }
    bool reserve(server::resource kind, std::size_t n, server::reservation& charge) {
        return n == 0 || budget.reserve(kind, n, charge).ok();
    }
    bool queue_room() {
        // Reset-only entries do not occupy live streams. Bound pending work as
        // well as bytes, so cancellation scans never grow with attacker input.
        if (pending.size() < limits.max_streams) return true;
        fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded));
        return false;
    }
    void reset(std::uint32_t id, http2_error_code code) {
        if (connection.failure()) return;
        if (auto found = streams.find(id); found != streams.end() && !found->second->reset_pending) {
            discard(*found->second);
        }
        // Drop only semantic, not yet encoded responses. Encoder state changes
        // in output() when this queue reaches the wire owner.
        std::erase_if(pending, [id](const response& r) { return r.stream == id; });
        if (!queue_room()) return;
        response value; value.stream = id; value.reset = code;
        if (!reserve(server::resource::response_queue_bytes, 13 + sizeof(response) + 64, value.charge)) {
            fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded)); return;
        }
        pending.push_back(std::move(value));
    }
    std::optional<std::size_t> response_size(std::uint16_t status, const http::fields& fields) const;
    bool response_fields(response& value, std::uint16_t status, const http::fields& fields);
    void respond(std::uint32_t id, std::uint16_t status, const http::fields& fields, bool streaming);
    std::optional<http2_error> assemble() {
        const auto h = connection.header();
        auto fragment = connection.payload();
        if (h.type == 1) {
            assembling = h.stream_id; end_stream = (h.flags & 1) != 0;
            if (!(assembling & 1)) return connection_error(http2_error_code::protocol_error);
            if (assembling <= last_stream && !streams.contains(assembling)) rejected = http2_error_code::stream_closed;
            last_stream = std::max(last_stream, assembling);
            if (!reserve(server::resource::body_buffer_bytes, limits.headers.max_compressed_bytes, block_charge)) {
                return connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
            }
            block.reserve(limits.headers.max_compressed_bytes);
            if (h.flags & 8) {
                const auto padding = fragment.front(); fragment = fragment.subspan(1);
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
    std::optional<http2_error> dispatch() {
        server::reservation temporary_bytes, temporary_fields;
        auto decode_limits = limits.headers;
        const auto free_bytes = budget.capacity(server::resource::header_bytes) - budget.in_use(server::resource::header_bytes);
        decode_limits.max_expanded_bytes = std::min(decode_limits.max_expanded_bytes, free_bytes / head_copy_allowance);
        decode_limits.max_fields = std::min(decode_limits.max_fields, budget.capacity(server::resource::header_fields) - budget.in_use(server::resource::header_fields));
        if (!reserve(server::resource::header_bytes, decode_limits.max_expanded_bytes * head_copy_allowance, temporary_bytes) ||
            !reserve(server::resource::header_fields, decode_limits.max_fields, temporary_fields)) {
            return connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
        }
        auto decoded = connection.compression().decoder().decode(block, decode_limits);
        const auto id = assembling; const auto ended = end_stream;
        clear_block();
        if (!decoded.status.ok()) return connection_error(http2_error_code::compression_error);
        if (rejected) {
            reset(id, *rejected); rejected.reset(); return {};
        }
        if (streams.contains(id)) {
            temporary_bytes.release(); temporary_fields.release();
            trailers(id, ended, decoded.fields); return {};
        }
        http::request_head head;
        if (!http2_convert_request(decoded.fields, head)) {
            reset(id, http2_error_code::protocol_error); return {};
        }
        std::optional<std::uint64_t> length;
        http2_content_length(head.head_fields, length);
        if (ended && length.value_or(0)) {
            reset(id, http2_error_code::protocol_error); return {};
        }
        if (head.request_method.id() == http::method_id::connect) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        temporary_bytes.release(); temporary_fields.release();
        return admit(id, std::move(head), decoded.fields, ended);
    }
    std::optional<http2_error> admit(std::uint32_t id, http::request_head head, const std::vector<hpack_field>& fields, bool ended) {
        reap();
        server::reservation count_charge;
        if (streams.size() >= limits.max_streams || !reserve(server::resource::streams, 1, count_charge)) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        std::size_t retained = 0;
        for (const auto& field : fields) retained += field.name.size() + field.value.size() + 32;
        server::reservation metadata;
        if (!reserve(server::resource::body_buffer_bytes, sizeof(stream) + 64, metadata)) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        auto value = std::make_unique<stream>(*this, id, std::move(head));
        value->metadata_charge = std::move(metadata);
        value->count_charge = std::move(count_charge);
        if (!reserve(server::resource::header_bytes, retained * head_copy_allowance, value->bytes_charge) ||
            !reserve(server::resource::header_fields, fields.size(), value->fields_charge)) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        std::optional<std::uint64_t> length;
        http2_content_length(value->head.head_fields, length);
        value->body.send_window.available = peer_initial;
        value->body.receive_window.available = local_initial;
        if (!value->body.prepare_receive(65535, ended, length)) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        auto* accepted = value.get();
        streams.emplace(id, std::move(value));
        connection.processed_stream(id);
        accepted->handler.start(owner, run_route(routes, accepted->request));
        return {};
    }
    std::optional<http2_error> process_frame() {
        auto h = connection.header();
        if (h.type == 1 || h.type == 9) return assemble();
        if (h.type == 3) {
            std::erase_if(pending, [h](const response& r) { return r.stream == h.stream_id; });
            if (auto found = streams.find(h.stream_id); found != streams.end()) {
                discard(*found->second);
                streams.erase(found);
            }
        } else if (h.type == 0) {
            return data_frame();
        } else if (h.type == 8) {
            return update_window();
        }
        return {};
    }
    bool encode_next();
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_STATE_HPP_
