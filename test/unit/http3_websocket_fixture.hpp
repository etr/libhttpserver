/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_HTTP3_WEBSOCKET_FIXTURE_HPP_
#define TEST_UNIT_HTTP3_WEBSOCKET_FIXTURE_HPP_
#include <memory>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "./http3_request_fixture.hpp"
#include "./websocket_test_helpers.hpp"
namespace h3ws {
namespace h = httpserver;
namespace http = h::http;
inline std::vector<h::detail::qpack_field> connect(std::string path = "/chat") {
    return {{":method", "CONNECT"}, {":protocol", "websocket"}, {":scheme", "https"},
        {":authority", "example.test"}, {":path", std::move(path)}, {"sec-websocket-version", "13"}};
}
struct fixture : h3test::request_fixture {
    std::vector<h::websocket::session> sessions;
    h::ws_upgrade_options options;
    std::string selected;
    unsigned refusals = 0;
    http::outcome_code refusal = http::outcome_code::ok;
    bool repeat = false;
    std::function<void()> before_upgrade, after_upgrade;
    http::outcome_code second = http::outcome_code::ok, response_after = http::outcome_code::ok;
    explicit fixture(h::detail::http3_request_limits limits = {}, std::uint64_t send = 65536, std::uint64_t connection_send = 1048576, h::server::resource_budget enclosing = {})
        : request_fixture(limits, send, {}, connection_send, 64, std::move(enclosing)) {
        routes.route(http::method::known(http::method_id::connect), "/chat", [this](h::exchange& x) -> h::task<void> {
            auto pending = x.upgrade(options);
            if (before_upgrade) before_upgrade();
            auto result = co_await std::move(pending);
            if (after_upgrade) after_upgrade();
            if (result.status.ok()) {
                selected = result.selected_subprotocol;
                sessions.push_back(std::move(*result.session));
                if (repeat) {
                    second = (co_await x.upgrade(options)).status.code();
                    response_after = x.respond(http::status::from_code(204), {}).code();
                }
            } else {
                ++refusals; refusal = result.status.code();
                x.respond(result.rejection_status, result.rejection_fields);
            }
        });
        routes.route(http::method::known(http::method_id::get), "/hello", [](h::exchange& x) -> h::task<void> {
            x.respond(http::status::from_code(204), {}); co_return;
        });
    }
    bool settings(std::optional<unsigned> enabled = 1, std::optional<unsigned> max_section = {}) {
        open(2);
        std::vector<std::byte> payload;
        if (enabled) {
            h3test::integer(payload, 8); h3test::integer(payload, *enabled);
        }
        if (max_section) {
            h3test::integer(payload, 6); h3test::integer(payload, *max_section);
        }
        auto wire = h3test::bytes({0}); auto frame = h3test::frame(4, payload);
        wire.insert(wire.end(), frame.begin(), frame.end());
        return feed(2, wire);
    }
    bool request(std::uint64_t id = 0, std::vector<h::detail::qpack_field> fields = connect(), bool fin = false) {
        open(id); auto ok = feed(id, h3test::headers(fields), fin); executor.run_pending(); return ok;
    }
    std::shared_ptr<std::optional<h::websocket::receive_result>> take(std::size_t index = 0) {
        auto result = std::make_shared<std::optional<h::websocket::receive_result>>();
        h::spawn(executor, sessions.at(index).receive(), [result](auto received) { result->emplace(std::move(received.value())); });
        executor.run_pending(); return result;
    }
    bool data(std::span<const std::byte> bytes, std::uint64_t id = 0, bool fin = false) {
        return feed(id, h3test::frame(0, bytes), fin);
    }
    void local() {
        for (auto role : {h::detail::http3_role::control, h::detail::http3_role::qpack_encoder, h::detail::http3_role::qpack_decoder}) {
            auto opened = flow.open_local(true);
            if (!opened) throw std::runtime_error("critical open failed");
            auto stream = std::make_unique<h::detail::quic_stream_state>(opened.id, h::detail::quic_endpoint_role::server,
                flow.ids(), h::detail::quic_stream_limits{}, pool.critical());
            if (engine->attach_local(role, *stream)) throw std::runtime_error("critical attach failed");
            streams.emplace(opened.id, std::move(stream));
        }
    }
    bool transmit_held(std::vector<h::detail::quic_information_completion>& held) {
        engine->begin_turn(); engine->pump_output(); now += std::chrono::seconds(1);
        std::array<std::byte, 1200> packet{}; h::detail::quic_send_request request;
        request.max_wire_bytes = packet.size(); request.protection_overhead = 0;
        auto plan = recovery.prepare_scheduled_packet(h::detail::quic_pn_space::application, packet, now, request, flow);
        if (!plan) return false;
        if (!recovery.check_scheduled_emission(plan.token, now, plan.bytes, plan.ack_eliciting, plan.ack_eliciting)) throw std::runtime_error("emission refused");
        if (plan.stream) {
            auto frame = *plan.stream; auto& out = output[frame.stream];
            if (frame.offset != out.size()) throw std::runtime_error("duplicate output");
            out.insert(out.end(), frame.data.begin(), frame.data.end());
            if (!streams.at(frame.stream)->record_stream_sent(frame.offset, frame.data.size(), frame.fin)) throw std::runtime_error("sent offset refused");
            if (frame.fin) ++fins[frame.stream];
        }
        if (!recovery.commit_sent(plan.token, now, plan.bytes, plan.ack_eliciting, plan.ack_eliciting)) throw std::runtime_error("commit refused");
        h::detail::quic_ack_frame ack; ack.largest = plan.packet_number;
        recovery.receive_ack(h::detail::quic_pn_space::application, ack, now + std::chrono::milliseconds(1));
        while (auto done = recovery.take_completion()) held.push_back(*done);
        executor.run_pending(); return true;
    }
    h3test::response reply(std::uint64_t id = 0) { drain(); return h3test::decode_response(output[id]); }
};
inline std::vector<std::byte> masked(unsigned op, std::string_view text, bool fin = true) {
    return ws_test::frame(op, std::as_bytes(std::span(text)), fin);
}
inline std::string text(const h::websocket::receive_result& received) {
    return received.value ? std::string(reinterpret_cast<const char*>(received.value->data.data()), received.value->data.size()) : std::string{};
}
}  // namespace h3ws
#endif  // TEST_UNIT_HTTP3_WEBSOCKET_FIXTURE_HPP_
