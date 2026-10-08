/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http3_request_state.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP3_REQUEST_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP3_REQUEST_STATE_HPP_
#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <vector>
#include <utility>
#include <httpserver/detail/http3_request_engine.hpp>
#include <httpserver/detail/http3_request_head.hpp>
#include <httpserver/detail/http3_body_stream.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/detail/request_handler_frames.hpp>
namespace httpserver::detail {
namespace http3_request_helpers {
using request_handler_frames::route_executor;
using request_handler_frames::route_task;
struct receipt {
    std::uint64_t begin = 0, end = 0, consumed = 0;
    bool body = false;
};
struct wire_section {
    std::vector<std::byte> bytes;
    server::reservation charge;
    bool fin = false;
};
}  // namespace http3_request_helpers
struct http3_request_engine::state : std::enable_shared_from_this<state> {
    struct record;
    struct stream final : exchange_sink, body_sink {
        state& owner;
        record& transport;
        http3_body_stream body;
        exchange request;
        http3_request_helpers::route_task handler;
        server::reservation head_charge, fields_charge, storage_charge, trailer_charge, sender_trailer_charge;
        http::fields trailers;
        bool streaming = false;
        stream(state& s, record& r, http::request_head head);
        ~stream() override;
        void on_admit(const body_policy& policy) override;
        void on_respond(const http::status& status, const http::fields& fields) override;
        void on_start_response(const http::status& status, const http::fields& fields) override;
        websocket_upgrade_result on_upgrade(const ws_upgrade_options&) override;
        void on_abort() override;
        body_push_result push(std::span<const std::byte> bytes) override;
        body_push_result push_end(const http::fields& trailers) override;
        const http::outcome& failure() const noexcept override { return body.failure(); }
        void park(body_write_wait& wait) override { body.park(wait); }
        void unpark(body_write_wait& wait) override { body.unpark(wait); }
    };
    struct record {
        quic_stream_state* transport = nullptr;
        http3_role local_role = http3_role::pending;
        bool local = false, cancelled = false, abandoned = false, receive_done = false, fin_retained = false, reset_settled = false;
        bool event_recorded = false;
        std::optional<quic_stream_terminal> terminal;
        http3_request_action action;
        server::reservation charge, output_charge;
        std::unique_ptr<stream> semantic;
        std::vector<std::byte> scratch;
        std::size_t scratch_position = 0, scratch_size = 0, event_position = 0;
        std::vector<http3_request_helpers::receipt> receipts;
        std::size_t receipt_head = 0, receipt_count = 0;
        std::uint64_t accounted = 0, retained_offset = 0;
        std::vector<quic_information_id> information;
        std::deque<http3_request_helpers::wire_section> sections;
        std::vector<std::byte> output;
        std::size_t output_position = 0, output_body_begin = 0;
        bool output_fin = false, output_trailers = false;
    };
    quic_storage_lease data, critical;
    quic_flow_control& flow;
    quic_recovery& recovery;
    const server::route_registry& routes;
    http3_request_limits limits;
    http3_connection connection;
    std::shared_ptr<http3_request_helpers::route_executor> handlers;
    std::uint64_t connection_id;
    net::peer_address peer;
    std::map<std::uint64_t, std::unique_ptr<record>> records;
    std::optional<http3_error> error;
    // Three local roles share reserved retention slots, separate from data.
    static constexpr std::size_t critical_output_records = 3;
    std::size_t active = 0, retained = 0, retained_critical = 0, pumps = 0;
    std::optional<std::uint64_t> ordinary_after;
    bool disconnected = false;
    state(quic_storage_lease d, quic_storage_lease c, quic_flow_control& f, quic_recovery& q, const server::route_registry& r, executor& e, http3_request_limits l,
          std::uint64_t id, net::peer_address p)
        : data(std::move(d)),
          critical(std::move(c)),
          flow(f),
          recovery(q),
          routes(r),
          limits(l),
          connection(data, critical, l.framing),
          handlers(std::make_shared<http3_request_helpers::route_executor>(e)),
          connection_id(id),
          peer(p) {}
    ~state();
    struct pump_guard {
        state& owner;
        explicit pump_guard(state& s) : owner(s) { ++owner.pumps; }
        ~pump_guard() {
            --owner.pumps;
            owner.reap();
        }
    };
    void fail(http3_error failure);
    void cancel(record& r, http::outcome_code reason);
    void reset(record& r, std::uint64_t code);
    void abandon(record& r);
    void reap();
    void reap(record& r);
    bool retired(const record& r) const;
    bool attach(quic_stream_state& transport, std::optional<http3_role> role);
    bool receipt(record& r, std::uint64_t begin, std::uint64_t end, bool body);
    void flush_receipts(record& r);
    void consumed(record& r, std::size_t count);
    bool head(record& r, std::span<const qpack_field> fields);
    bool event(record& r);
    void record_event(record& r, const http3_event& event);
    bool deliver_body(record& r, const http3_event& event);
    void finish_receive(record& r);
    void peer_reset(record& r, quic_stream_terminal terminal);
    void notice_terminal(record& r);
    http3_progress receive(record& r);
    bool receive_step(record& r);
    bool read_scratch(record& r);
    bool stage_trailers(record& r, std::span<const qpack_field> fields);
    bool section(record& r, std::uint16_t status, const http::fields& fields, bool fin, bool trailers = false);
    void respond(record& r, std::uint16_t status, const http::fields& fields, bool streaming);
    bool prepare_output(record& r);
    bool stream_output(record& r);
    bool local_output(record& r);
    void retire_output(record& r, std::size_t start);
    bool reserve_trailers(stream& s, const http::fields& fields);
    bool submit(record& r, std::span<const std::byte> bytes, bool fin);
    bool output(record& r);
    void ordinary_output();
    void complete(const quic_information_completion& completion);
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP3_REQUEST_STATE_HPP_
