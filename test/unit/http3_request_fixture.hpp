/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_HTTP3_REQUEST_FIXTURE_HPP_
#define TEST_UNIT_HTTP3_REQUEST_FIXTURE_HPP_
#include <utility>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <httpserver/detail/http3_request_engine.hpp>
#include "./http3_fixture.hpp"
namespace h3test {
inline hd::quic_transport_parameters parameters(std::uint64_t send = 65536) {
    hd::quic_transport_parameters p;
    p.initial_max_data = 1048576;
    p.initial_max_stream_data_bidi_local = send;
    p.initial_max_stream_data_bidi_remote = send;
    p.initial_max_stream_data_uni = send;
    p.initial_max_streams_bidi = 64;
    p.initial_max_streams_uni = 8;
    return p;
}
inline std::vector<hd::qpack_field> get(std::string method = "GET", std::string path = "/items/../hello?x=1") {
    return {{":method", method}, {":scheme", "https"}, {":path", path}, {":authority", "example.test"}};
}
inline std::vector<std::byte> encode(const std::vector<hd::qpack_field>& fields) {
    auto out = hd::qpack_encoder{}.encode_section(fields, {65536, 65536, 256});
    if (!out.status.ok()) throw std::runtime_error("fixture QPACK encode failed");
    auto span = std::as_bytes(std::span(out.value));
    return {span.begin(), span.end()};
}
inline std::vector<std::byte> headers(const std::vector<hd::qpack_field>& fields) {
    return frame(1, encode(fields));
}
inline std::vector<std::byte> data(std::string_view value) {
    return frame(0, std::as_bytes(std::span(value)));
}
struct response {
    std::vector<std::vector<hd::qpack_field>> heads;
    std::string body;
};
inline response decode_response(const std::vector<std::byte>& wire) {
    response result;
    std::size_t at = 0;
    hd::qpack_decoder decoder;
    while (at < wire.size()) {
        auto type = hd::decode_quic_varint(std::span(wire).subspan(at));
        at += type.consumed;
        auto length = hd::decode_quic_varint(std::span(wire).subspan(at));
        at += length.consumed;
        if (length.value > wire.size() - at) throw std::runtime_error("truncated H3 response");
        auto payload = std::span(wire).subspan(at, length.value);
        at += length.value;
        if (type.value == 0) result.body.append(reinterpret_cast<const char*>(payload.data()), payload.size());
        if (type.value == 1) {
            auto fields = decoder.decode_allocated({reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()}, {65536, 65536, 256});
            if (!fields.status.ok()) throw std::runtime_error("bad response QPACK");
            result.heads.push_back(std::move(fields.fields));
        }
    }
    return result;
}
struct request_fixture {
    hs::resource_budget root;
    hd::quic_storage_pool pool;
    hs::route_registry routes;
    httpserver::manual_executor executor;
    hd::quic_flow_control flow;
    hd::quic_recovery recovery;
    std::map<std::uint64_t, std::unique_ptr<hd::quic_stream_state>> streams;
    std::unique_ptr<hd::http3_request_engine> engine;
    std::map<std::uint64_t, std::vector<std::byte>> output;
    std::map<std::uint64_t, unsigned> fins;
    std::map<std::uint64_t, std::uint64_t> incoming;
    hd::quic_recovery::time_point now{};
    static hs::resource_budget large_budget() {
        hs::budget_limits l;
        l.set(hs::resource::quic_reassembly_bytes, 16777216);
        l.set(hs::resource::header_fields, 4096);
        return hs::resource_budget::root(l);
    }
    explicit request_fixture(hd::http3_request_limits limits = {}, std::uint64_t send = 65536, hd::quic_recovery_config config = {})
        : root(large_budget()),
          pool(4194304, 1048576, root),
          flow(hd::quic_endpoint_role::server, parameters(), parameters(send), 128, pool.data()),
          recovery(config, pool.data(), pool.critical()) {
        if (!hs::route_registry::create(root, routes).ok()) throw std::runtime_error("route fixture failed");
        engine = std::make_unique<hd::http3_request_engine>(pool.data(), pool.critical(), flow, recovery, routes, executor, limits, 77);
    }
    hd::quic_stream_state& open(std::uint64_t id) {
        if (!flow.observe_peer(id)) throw std::runtime_error("peer open failed");
        auto s = std::make_unique<hd::quic_stream_state>(id, hd::quic_endpoint_role::server, flow.ids(), hd::quic_stream_limits{}, pool.data());
        auto& result = *s;
        streams.emplace(id, std::move(s));
        if (engine->attach_peer(result)) throw std::runtime_error("adapter attach failed");
        return result;
    }
    bool feed(std::uint64_t id, std::span<const std::byte> wire, bool fin = false) {
        auto& s = *streams.at(id);
        if (!flow.receive(s, {id, incoming[id], wire, fin})) return false;
        incoming[id] += wire.size();
        engine->begin_turn();
        engine->pump_receive(id);
        return !engine->failure();
    }
    void pump(std::uint64_t id) {
        engine->begin_turn();
        engine->pump_receive(id);
    }
    void drain(std::size_t packet_size = 1200, unsigned turns = 4096) {
        std::vector<std::byte> packet(packet_size);
        for (unsigned i = 0; i < turns; ++i) {
            engine->begin_turn();
            engine->pump_output();
            now += std::chrono::seconds(1);
            hd::quic_send_request request;
            request.max_wire_bytes = packet_size;
            request.protection_overhead = 0;
            auto plan = recovery.prepare_scheduled_packet(hd::quic_pn_space::application, packet, now, request, flow);
            if (!plan) break;
            if (!recovery.check_scheduled_emission(plan.token, now, plan.bytes, plan.ack_eliciting, plan.ack_eliciting)) throw std::runtime_error("emission refused");
            if (plan.stream) {
                auto f = *plan.stream;
                auto& out = output[f.stream];
                if (f.offset != out.size()) throw std::runtime_error("duplicated response offset");
                out.insert(out.end(), f.data.begin(), f.data.end());
                if (!streams.at(f.stream)->record_stream_sent(f.offset, f.data.size(), f.fin)) throw std::runtime_error("stream emission refused");
                if (f.fin) ++fins[f.stream];
            }
            if (!recovery.commit_sent(plan.token, now, plan.bytes, plan.ack_eliciting, plan.ack_eliciting)) throw std::runtime_error("commit failed");
            hd::quic_ack_frame ack;
            ack.largest = plan.packet_number;
            recovery.receive_ack(hd::quic_pn_space::application, ack, now + std::chrono::milliseconds(1));
            while (auto done = recovery.take_completion())
                engine->information_completed(*done);
            executor.run_pending();
        }
    }
    std::uint64_t credit(std::uint64_t id) const {
        auto c = flow.pending_credit(hd::quic_flow_kind::max_stream_data, id);
        return c ? c->limit : parameters().initial_max_stream_data_bidi_remote;
    }
};
}  // namespace h3test
#endif  // TEST_UNIT_HTTP3_REQUEST_FIXTURE_HPP_
