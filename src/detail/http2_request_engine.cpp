/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
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
#include <httpserver/detail/exchange_runner.hpp>
namespace httpserver::detail {
namespace {
// Bound simultaneous decoded fields, semantic fields and raw/derived targets.
constexpr std::size_t head_copy_allowance = 3;
http2_error connection_error(http2_error_code code, http::outcome_code outcome = http::outcome_code::protocol_error) {
    return {http2_error_scope::connection, code, 0, outcome, "HTTP/2 request engine failure"};
}
void append_frame(std::vector<std::uint8_t>& out, std::uint8_t type, std::uint8_t flags,
                  std::uint32_t stream, std::span<const std::uint8_t> payload) {
    const auto n = payload.size();
    out.insert(out.end(), {static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n), type, flags,
        static_cast<std::uint8_t>(stream >> 24), static_cast<std::uint8_t>(stream >> 16), static_cast<std::uint8_t>(stream >> 8), static_cast<std::uint8_t>(stream)});
    out.insert(out.end(), payload.begin(), payload.end());
}
std::optional<std::string_view> canonical_length(std::string_view value) {
    if (value.empty()) return {};
    for (char c : value) if (c < '0' || c > '9') return {};
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
}  // namespace
struct http2_request_engine::state {
    struct stream final : exchange_sink, body_sink {
        state& owner;
        std::uint32_t id;
        http::request_head head;
        server::reservation count_charge, bytes_charge, fields_charge;
        exchange request;
        route_task handler;
        http::outcome unsupported{http::outcome_code::invalid_state, "HTTP/2 DATA responses require TASK-141"};
        stream(state& s, std::uint32_t n, http::request_head h) : owner(s), id(n), head(std::move(h)), request(head, this, 0, nullptr, this) {}
        void on_admit(const body_policy&) override {}
        void on_respond(const http::status& status, const http::fields& fields) override { owner.respond(id, status.code(), fields); }
        websocket_upgrade_result on_upgrade(const ws_upgrade_options&) override {
            websocket_upgrade_result result; result.status = unsupported; return result;
        }
        void on_abort() override { owner.reset(id, http2_error_code::internal_error); }
        body_push_result push(std::span<const std::byte>) override { owner.reset(id, http2_error_code::internal_error); return {body_push::failed, 0}; }
        body_push_result push_end(const http::fields&) override { owner.reset(id, http2_error_code::internal_error); return {body_push::failed, 0}; }
        const http::outcome& failure() const noexcept override { return unsupported; }
        void park(body_write_wait&) override {}
        void unpark(body_write_wait&) override {}
        ~stream() override {
            request.disconnect(http::outcome_code::connection_closed, "HTTP/2 stream ended");
            handler.clear();
        }
    };
    struct response {
        std::uint32_t stream = 0;
        std::vector<hpack_field> fields;
        std::optional<http2_error_code> reset;
        server::reservation charge;
        std::size_t wire_limit = 0;
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
    bool end_stream = false, control_exposed = false;
    std::optional<http2_error_code> rejected;
    state(server::resource_budget b, const server::route_registry& r, executor& e, http2_request_limits l)
        : budget(b), routes(r), owner(e), limits(l), connection(b) {}
    void reap() {
        if (connection.failure()) {
            streams.clear();
            return;
        }
        for (auto it = streams.begin(); it != streams.end();) {
            if (it->second->handler.done()) {
                it = streams.erase(it);
            } else {
                ++it;
            }
        }
    }
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
    void reset(std::uint32_t id, http2_error_code code) {
        if (connection.failure()) return;
        response value; value.stream = id; value.reset = code;
        if (!reserve(server::resource::response_queue_bytes, 13, value.charge)) {
            fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded)); return;
        }
        // Drop only semantic, not yet encoded responses. Encoder state changes
        // in output() when this queue reaches the wire owner.
        std::erase_if(pending, [id](const response& r) { return r.stream == id; });
        pending.push_back(std::move(value));
    }
    std::optional<std::size_t> response_size(std::uint16_t status, const http::fields& fields) const {
        if (status < 200 || status > 599 || fields.size() >= limits.headers.max_fields) return {};
        std::size_t expanded = 42;
        for (auto field : fields.entries()) {
            const auto size = field.name.size() + field.value.size() + 32;
            if (size > limits.headers.max_expanded_bytes || expanded > limits.headers.max_expanded_bytes - size) return {};
            expanded += size;
        }
        return expanded;
    }
    bool response_fields(response& value, std::uint16_t status, const http::fields& fields) {
        value.fields.push_back({":status", std::to_string(status), hpack_indexing::without_indexing});
        for (auto field : fields.entries()) {
            std::string name(field.name);
            for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (!http2_regular_field(name, field.value)) return false;
            value.fields.push_back({std::move(name), std::string(field.value), hpack_indexing::without_indexing});
        }
        return true;
    }
    void respond(std::uint32_t id, std::uint16_t status, const http::fields& fields) {
        if (connection.failure()) return;
        const auto found = streams.find(id);
        const bool metadata_only = status == 304 ||
            (found != streams.end() && found->second->head.request_method.id() == http::method_id::head);
        if (!valid_response_lengths(fields, metadata_only)) {
            reset(id, http2_error_code::internal_error); return;
        }
        const auto expanded = response_size(status, fields);
        if (!expanded) {
            reset(id, http2_error_code::internal_error); return;
        }
        response value; value.stream = id;
        // Plain literals use fewer octets than their HPACK expanded charge.
        // Include pending table updates and frame headers before encoding.
        value.wire_limit = *expanded + 32;
        auto frames = value.wire_limit / 16384 + 1;
        if (!reserve(server::resource::response_queue_bytes, *expanded + value.wire_limit + frames * 9, value.charge)) {
            fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded)); return;
        }
        if (!response_fields(value, status, fields)) {
            reset(id, http2_error_code::internal_error); return;
        }
        pending.push_back(std::move(value));
    }
    std::optional<http2_error> assemble() {
        const auto h = connection.header();
        auto fragment = connection.payload();
        if (h.type == 1) {
            assembling = h.stream_id; end_stream = (h.flags & 1) != 0;
            if (!(assembling & 1)) return connection_error(http2_error_code::protocol_error);
            if (assembling <= last_stream) rejected = http2_error_code::stream_closed;
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
        http::request_head head;
        if (!http2_convert_request(decoded.fields, head)) {
            reset(id, http2_error_code::protocol_error); return {};
        }
        if (!ended || head.request_method.id() == http::method_id::connect) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        temporary_bytes.release(); temporary_fields.release();
        return admit(id, std::move(head), decoded.fields);
    }
    std::optional<http2_error> admit(std::uint32_t id, http::request_head head, const std::vector<hpack_field>& fields) {
        reap();
        server::reservation count_charge;
        if (streams.size() >= limits.max_streams || !reserve(server::resource::streams, 1, count_charge)) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        std::size_t retained = 0;
        for (const auto& field : fields) retained += field.name.size() + field.value.size() + 32;
        auto value = std::make_unique<stream>(*this, id, std::move(head));
        value->count_charge = std::move(count_charge);
        if (!reserve(server::resource::header_bytes, retained * head_copy_allowance, value->bytes_charge) ||
            !reserve(server::resource::header_fields, fields.size(), value->fields_charge)) {
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
            streams.erase(h.stream_id);
        } else if (h.type == 0) {
            reset(h.stream_id, http2_error_code::stream_closed);
        }
        return {};
    }
    bool encode_next() {
        auto value = std::move(pending.front()); pending.pop_front();
        active.clear(); active_used = 0;
        active_charge = std::move(value.charge);
        active.reserve(active_charge.units());
        if (value.reset) {
            auto n = static_cast<std::uint32_t>(*value.reset);
            std::array<std::uint8_t, 4> code{static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n)};
            append_frame(active, 3, 0, value.stream, code); return true;
        }
        auto maximum = connection.peer_settings().max_header_list_size;
        auto expanded = limits.headers.max_expanded_bytes;
        if (maximum) expanded = std::min(expanded, static_cast<std::size_t>(*maximum));
        std::size_t size = 0;
        for (const auto& field : value.fields) size += field.name.size() + field.value.size() + 32;
        if (size > expanded) {
            active_charge.release();
            reset(value.stream, http2_error_code::internal_error);
            return false;
        }
        auto encoded = connection.compression().encoder().encode_section(value.fields, {value.wire_limit, expanded, limits.headers.max_fields});
        if (!encoded.status.ok()) {
            active_charge.release();
            fail(connection_error(http2_error_code::internal_error)); return false;
        }
        auto bytes = hpack_octets(encoded.value);
        const auto frame_size = connection.peer_settings().max_frame_size;
        bool initial = true;
        do {
            auto n = std::min(bytes.size(), static_cast<std::size_t>(frame_size));
            auto flags = static_cast<std::uint8_t>((initial ? 1 : 0) | (bytes.size() == n ? 4 : 0));
            append_frame(active, initial ? 1 : 9, flags, value.stream, bytes.first(n));
            bytes = bytes.subspan(n); initial = false;
        } while (!bytes.empty());
        return true;
    }
};
http2_request_engine::http2_request_engine(server::resource_budget budget, const server::route_registry& routes,
                                          executor& owner, http2_request_limits limits)
    : state_(std::make_unique<state>(budget, routes, owner, limits)) {
    const auto& h = limits.headers;
    if (!h.max_compressed_bytes || h.max_compressed_bytes > server::max_capacity(server::resource::body_buffer_bytes) ||
        !h.max_expanded_bytes || h.max_expanded_bytes > server::max_capacity(server::resource::header_bytes) / head_copy_allowance ||
        !h.max_fields || h.max_fields > server::max_capacity(server::resource::header_fields) || !limits.max_streams) {
        state_->fail(connection_error(http2_error_code::internal_error, http::outcome_code::invalid_argument));
    }
}
http2_request_engine::~http2_request_engine() = default;
void http2_request_engine::begin_turn() {
    state_->reap(); state_->connection.begin_turn();
}
http2_feed_result http2_request_engine::feed(std::span<const std::uint8_t> bytes, http2_connection::time_point now) {
    auto result = state_->connection.feed(bytes, now);
    if (result.error && result.error->scope == http2_error_scope::connection) {
        state_->fail(*result.error); state_->reap(); return result;
    }
    if (result.error && state_->connection.header().type != 1) {
        state_->reset(result.error->stream_id, result.error->wire_code);
        state_->connection.release_frame(); return result;
    }
    if (result.error) state_->rejected = result.error->wire_code;
    if (!result.error && result.progress != http2_progress::frame_ready) return result;
    try {
        auto error = state_->process_frame();
        state_->connection.release_frame();
        if (error) {
            state_->fail(*error); state_->reap();
            return {http2_progress::failed, result.consumed, error};
        }
    } catch (const std::bad_alloc&) {
        auto error = connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
        state_->fail(error); state_->reap(); return {http2_progress::failed, result.consumed, error};
    }
    return result;
}
http2_feed_result http2_request_engine::eof() {
    auto result = state_->connection.eof();
    if (result.error) {
        state_->fail(*result.error); state_->reap();
    }
    return result;
}
std::span<const std::uint8_t> http2_request_engine::output(http2_connection::time_point now) {
    state_->reap();
    if (!state_->active.empty()) return std::span(state_->active).subspan(state_->active_used);
    for (;;) {
        auto control = state_->connection.output(now);
        if (!control.empty()) {
            state_->control_exposed = true; return control;
        }
        if (state_->pending.empty()) return {};
        if (state_->encode_next()) return state_->active;
    }
}
bool http2_request_engine::advance_output(std::size_t count) {
    if (state_->control_exposed) {
        auto n = state_->connection.output().size();
        if (!state_->connection.advance_output(count)) return false;
        if (count == n) state_->control_exposed = false;
        return true;
    }
    if (count > state_->active.size() - state_->active_used) return false;
    state_->active_used += count;
    if (state_->active_used == state_->active.size()) {
        std::vector<std::uint8_t>().swap(state_->active);
        state_->active_used = 0; state_->active_charge.release();
    }
    return true;
}
const std::optional<http2_error>& http2_request_engine::failure() const { return state_->connection.failure(); }
}  // namespace httpserver::detail
