/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <algorithm>
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include "./http3_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
namespace {
hd::http3_request_limits limits() {
    hd::http3_request_limits l;
    l.framing.qpack_capacity = 256;
    l.framing.qpack_blocked_streams = 4;
    return l;
}
void local(h3test::request_fixture& f) {
    for (auto role : {hd::http3_role::control, hd::http3_role::qpack_encoder, hd::http3_role::qpack_decoder}) {
        auto opened = f.flow.open_local(true);
        if (!opened) throw std::runtime_error("local open failed");
        auto s = std::make_unique<hd::quic_stream_state>(opened.id, hd::quic_endpoint_role::server, f.flow.ids(), hd::quic_stream_limits{}, f.pool.critical());
        if (f.engine->attach_local(role, *s)) throw std::runtime_error("local attach failed");
        f.streams.emplace(opened.id, std::move(s));
    }
}
void peer(h3test::request_fixture& f, unsigned capacity = 256, unsigned blocked = 4) {
    f.open(2);
    std::vector<std::byte> settings;
    h3test::integer(settings, 1); h3test::integer(settings, capacity);
    h3test::integer(settings, 7); h3test::integer(settings, blocked);
    auto wire = h3test::bytes({0});
    auto frame = h3test::frame(4, settings);
    wire.insert(wire.end(), frame.begin(), frame.end());
    if (!f.feed(2, wire)) throw std::runtime_error("peer settings failed");
    f.open(6); f.feed(6, h3test::bytes({2}));
    f.open(10); f.feed(10, h3test::bytes({3}));
}
std::vector<std::byte> instruction(hd::qpack_encoder_instruction i) {
    auto encoded = hd::qpack_encode_encoder_instruction(i, {4096, 4096, 4096});
    if (!encoded.status.ok()) throw std::runtime_error("instruction encode failed");
    return {reinterpret_cast<const std::byte*>(encoded.value.data()), reinterpret_cast<const std::byte*>(encoded.value.data() + encoded.value.size())};
}
std::vector<std::byte> dynamic_head(hd::qpack_dynamic_table& table, const std::vector<hd::qpack_field>& fields) {
    std::vector<hd::qpack_field_view> views;
    for (const auto& field : fields) views.push_back(field.view());
    auto encoded = hd::qpack_encoder{}.encode_dynamic(views, table, table.insert_count(), {65536, 65536, 256});
    if (!encoded.status.ok()) throw std::runtime_error("dynamic head encode failed");
    return h3test::frame(1, std::as_bytes(std::span(encoded.value)));
}
std::span<const std::uint8_t> first_section(const std::vector<std::byte>& wire) {
    auto type = hd::decode_quic_varint(wire);
    auto length = hd::decode_quic_varint(std::span(wire).subspan(type.consumed));
    return {reinterpret_cast<const std::uint8_t*>(wire.data() + type.consumed + length.consumed), static_cast<std::size_t>(length.value)};
}
void apply_output(const std::vector<std::byte>& wire, hd::qpack_dynamic_table& table) {
    std::size_t at = 1;
    while (at < wire.size()) {
        auto decoded = hd::qpack_decode_encoder_instruction({reinterpret_cast<const std::uint8_t*>(wire.data() + at), wire.size() - at}, {4096, 4096, 4096});
        if (!decoded.status.ok() || !hd::qpack_apply_encoder_instruction(decoded.instruction, {4096, 4096, 4096}, table).ok())
            throw std::runtime_error("encoder output rejected");
        at += decoded.consumed;
    }
}
bool transmit_with_delayed_ordinary_ack(h3test::request_fixture& f, std::size_t& lost, unsigned& retransmissions) {
    f.engine->begin_turn(); f.engine->pump_output();
    f.now += std::chrono::seconds(1);
    std::array<std::byte, 16> packet{}; hd::quic_send_request request;
    request.max_wire_bytes = packet.size(); request.protection_overhead = 0;
    auto plan = f.recovery.prepare_scheduled_packet(hd::quic_pn_space::application, packet, f.now, request, f.flow);
    if (!plan) return false;
    if (!f.recovery.check_scheduled_emission(plan.token, f.now, plan.bytes, plan.ack_eliciting, plan.ack_eliciting))
        throw std::runtime_error("transcript emission rejected");
    if (plan.stream) {
        auto frame = *plan.stream; auto& out = f.output[frame.stream];
        if (frame.offset == out.size()) {
            out.insert(out.end(), frame.data.begin(), frame.data.end());
        } else {
            if (frame.offset + frame.data.size() > out.size() || !std::ranges::equal(frame.data, std::span(out).subspan(frame.offset, frame.data.size())))
                throw std::runtime_error("retransmission changed retained bytes");
            ++retransmissions;
        }
        if (!f.streams.at(frame.stream)->record_stream_sent(frame.offset, frame.data.size(), frame.fin)) throw std::runtime_error("stream emission rejected");
    }
    if (!f.recovery.commit_sent(plan.token, f.now, plan.bytes, plan.ack_eliciting, plan.ack_eliciting)) throw std::runtime_error("transcript commit rejected");
    if (!plan.stream || plan.stream->stream != 16) {
        hd::quic_ack_frame ack; ack.largest = plan.packet_number;
        lost += f.recovery.receive_ack(hd::quic_pn_space::application, ack, f.now + std::chrono::milliseconds(1)).lost_bytes;
        while (auto done = f.recovery.take_completion()) f.engine->information_completed(*done);
    }
    f.executor.run_pending();
    return true;
}
}  // namespace
LT_BEGIN_SUITE(http3_qpack_scheduling_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(http3_qpack_scheduling_suite)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, response_uses_negotiated_dynamic_names_without_retaining_values)
    h3test::request_fixture f(limits()); local(f); peer(f, 512);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-repeat", "first"); fields.append("x-repeat", "second");
        x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending(); f.drain(8);
    LT_ASSERT(!f.output[0].empty());
    auto wire = first_section(f.output[0]);
    LT_CHECK_EQ(wire[0], 2U);
    hd::qpack_dynamic_table table(hd::qpack_table_role::decoder);
    LT_ASSERT(table.initialize(h3test::budget(), 512).ok());
    auto blocked = hd::qpack_decoder{}.decode_dynamic(wire, table, {65536, 65536, 256});
    LT_CHECK(blocked.status.state == hd::qpack_state::inserts_unavailable);
    apply_output(f.output[7], table);
    LT_CHECK_EQ(table.capacity(), 256U); LT_CHECK_EQ(table.size(), 1U);
    LT_ASSERT(table.lookup(0)); LT_CHECK(table.lookup(0)->value.empty());
    auto decoded = hd::qpack_decoder{}.decode_dynamic(wire, table, {65536, 65536, 256});
    LT_ASSERT(decoded.status.ok()); LT_ASSERT_EQ(decoded.fields.size(), 3U);
    LT_CHECK_EQ(decoded.fields[1].value, "first"); LT_CHECK_EQ(decoded.fields[2].value, "second");
    LT_CHECK(decoded.fields[1].never_indexed);
LT_END_AUTO_TEST(response_uses_negotiated_dynamic_names_without_retaining_values)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, encoder_input_resumes_siblings_and_pending_fin_once)
    for (unsigned split = 0; split < 10; ++split) {
        h3test::request_fixture f(limits()); local(f); peer(f);
        std::vector<unsigned> calls;
        LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            calls.push_back(x.head().head_fields.first("x-order").value_or("static") == "second" ? 2 : 1);
            x.respond(http::status::from_code(204), {}); co_return;
        }).ok());
        hd::qpack_dynamic_table table(hd::qpack_table_role::encoder);
        LT_ASSERT(table.initialize(h3test::budget(), 256).ok()); LT_ASSERT(table.set_capacity(256).ok());
        LT_ASSERT(table.insert("x-order", "first").ok());
        auto fields = h3test::get(); fields.push_back({"x-order", "first"}); auto first = dynamic_head(table, fields);
        LT_ASSERT(table.insert("x-order", "second").ok()); fields.back().value = "second"; auto second = dynamic_head(table, fields);
        f.open(0); f.open(4); f.open(8);
        LT_ASSERT(f.feed(0, second, true)); LT_ASSERT(f.feed(4, first, true));
        LT_ASSERT(f.feed(8, h3test::headers(h3test::get()), true)); f.executor.run_pending(); LT_CHECK_EQ(calls.size(), 1U);
        auto cap = instruction({hd::qpack_encoder_instruction_kind::capacity, 256}); LT_ASSERT(f.feed(6, cap));
        auto insert = instruction({hd::qpack_encoder_instruction_kind::insert_literal, 0, false, "x-order", "first"});
        auto at = std::min<std::size_t>(split, insert.size());
        LT_ASSERT(f.feed(6, std::span(insert).first(at))); LT_ASSERT(f.feed(6, std::span(insert).subspan(at)));
        f.executor.run_pending(); LT_CHECK_EQ(calls.size(), 2U);
        LT_ASSERT(f.feed(6, instruction({hd::qpack_encoder_instruction_kind::insert_literal, 0, false, "x-order", "second"})));
        f.executor.run_pending(); LT_CHECK_EQ(calls.size(), 3U);
        f.drain(); LT_CHECK_EQ(f.fins[0], 1U); LT_CHECK_EQ(f.fins[4], 1U);
    }
LT_END_AUTO_TEST(encoder_input_resumes_siblings_and_pending_fin_once)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, critical_credit_survives_abandoned_packet_and_ordinary_saturation)
    auto l = limits(); l.max_pending_output_records = 1;
    h3test::request_fixture f(l, 65536, {}, 12); local(f); peer(f);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-dynamic", "long response value");
        x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    f.engine->begin_turn(); f.engine->pump_output();
    // Ordinary emission must leave all retained critical high-water ranges protected.
    auto ordinary = f.flow.send_allowance(0, 0, 65536);
    LT_CHECK(ordinary.bytes <= 2U);
    std::array<std::byte, 1200> packet{}; hd::quic_send_request request;
    request.max_wire_bytes = packet.size(); request.protection_overhead = 0;
    auto plan = f.recovery.prepare_scheduled_packet(hd::quic_pn_space::application, packet, f.now, request, f.flow);
    LT_ASSERT(plan); LT_ASSERT(plan.stream); LT_CHECK(plan.stream->stream != 0U);
    f.recovery.abandon_packet(plan.token);
    f.engine->pump_output();
    LT_CHECK(f.flow.send_allowance(0, 0, 65536).bytes <= 2U);
    f.drain(8); LT_CHECK(!f.output[3].empty()); LT_CHECK(!f.output[7].empty()); LT_CHECK(!f.output[11].empty());
    LT_ASSERT(f.flow.apply(hd::quic_flow_frame{hd::quic_flow_kind::max_data, 4096, 0})); f.drain(8);
    LT_CHECK_EQ(f.fins[0], 1U);
LT_END_AUTO_TEST(critical_credit_survives_abandoned_packet_and_ordinary_saturation)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, disconnect_releases_blocked_and_encoder_accounting_without_destroying_engine)
    h3test::request_fixture f(limits()); local(f); peer(f);
    f.open(0); LT_ASSERT(f.feed(0, h3test::bytes({1, 3, 2, 0, 0x80}), true));
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::blocked_headers), 1U);
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::qpack_table_bytes), 512U);
    f.engine->disconnect({http::outcome_code::connection_closed, "test disconnect"}); f.engine->disconnect({http::outcome_code::connection_closed, "test disconnect"});
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::blocked_headers), 0U);
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::qpack_table_bytes), 0U);
    f.engine->begin_turn(); f.engine->pump_output();
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::qpack_table_bytes), 0U);
LT_END_AUTO_TEST(disconnect_releases_blocked_and_encoder_accounting_without_destroying_engine)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, local_cancel_rolls_back_only_unpublished_response_sections)
    for (bool publish : {false, true}) {
        h3test::request_fixture f(limits(), publish ? 1 : 65536); local(f); peer(f);
        httpserver::resume_signal pause;
        LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            co_await pause.wait();
            http::fields fields; fields.append("x-hold", "value");
            x.respond(http::status::from_code(200), fields);
        }).ok());
        f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
        const auto baseline = f.pool.critical().budget.in_use(h3test::hs::resource::quic_reassembly_bytes);
        pause.signal(); f.executor.run_pending();
        if (publish) {
            f.engine->begin_turn(); f.engine->pump_output();
        }
        f.engine->stop_sending(0, 0x10c);
        f.drain();
        const auto retained = f.pool.critical().budget.in_use(h3test::hs::resource::quic_reassembly_bytes);
        if (publish) {
            LT_CHECK(retained > baseline);
            LT_ASSERT(f.feed(10, h3test::bytes({0x40})));
            LT_CHECK(f.pool.critical().budget.in_use(h3test::hs::resource::quic_reassembly_bytes) < retained);
        } else {
            LT_CHECK_EQ(retained, baseline);
        }
        f.engine->disconnect({http::outcome_code::connection_closed, "done"});
        LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::qpack_table_bytes), 0U);
    }
LT_END_AUTO_TEST(local_cancel_rolls_back_only_unpublished_response_sections)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, ordinary_turn_exhaustion_does_not_stop_encoder_progress)
    auto l = limits(); l.framing.frames_per_turn = 1;
    h3test::request_fixture f(l); local(f); peer(f);
    unsigned calls = 0;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        ++calls; x.respond(http::status::from_code(204), {}); co_return;
    }).ok());
    hd::qpack_dynamic_table table(hd::qpack_table_role::encoder);
    LT_ASSERT(table.initialize(h3test::budget(), 256).ok()); LT_ASSERT(table.set_capacity(256).ok()); LT_ASSERT(table.insert("x", "a").ok());
    auto fields = h3test::get(); fields.push_back({"x", "a"}); f.open(0); LT_ASSERT(f.feed(0, dynamic_head(table, fields), true));
    LT_ASSERT(f.feed(6, instruction({hd::qpack_encoder_instruction_kind::capacity, 256})));
    auto wire = instruction({hd::qpack_encoder_instruction_kind::insert_literal, 0, false, "x", "a"});
    LT_ASSERT(f.flow.receive(*f.streams.at(6), {6, f.incoming[6], wire, false})); f.incoming[6] += wire.size();
    f.open(4); auto ordinary = h3test::headers(h3test::get());
    LT_ASSERT(f.flow.receive(*f.streams.at(4), {4, 0, ordinary, true})); f.incoming[4] = ordinary.size();
    f.engine->begin_turn(); f.engine->pump_receive(4); f.executor.run_pending();
    LT_CHECK_EQ(calls, 2U);
LT_END_AUTO_TEST(ordinary_turn_exhaustion_does_not_stop_encoder_progress)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, bounded_continuations_rotate_past_sustained_ordinary_input)
    auto l = limits(); l.framing.frames_per_turn = 1;
    h3test::request_fixture f(l); peer(f);
    unsigned calls = 0;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        ++calls; x.respond(http::status::from_code(204), {}); co_return;
    }).ok());
    f.open(0); f.open(4);
    std::vector<std::byte> busy;
    for (unsigned i = 0; i < 8; ++i) {
        auto frame = h3test::frame(32, {}); busy.insert(busy.end(), frame.begin(), frame.end());
    }
    auto head = h3test::headers(h3test::get()); busy.insert(busy.end(), head.begin(), head.end());
    LT_ASSERT(f.flow.receive(*f.streams.at(0), {0, 0, busy, false}));
    LT_ASSERT(f.flow.receive(*f.streams.at(4), {4, 0, head, true}));
    for (unsigned i = 0; i < 2; ++i) {
        f.engine->begin_turn(); f.engine->pump_output(); f.executor.run_pending();
    }
    LT_CHECK_EQ(calls, 1U);
LT_END_AUTO_TEST(bounded_continuations_rotate_past_sustained_ordinary_input)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, blocked_trailers_resume_after_fin_and_paused_body_without_more_input)
    auto l = limits(); l.body_buffer_bytes = 2;
    h3test::request_fixture f(l); local(f); peer(f);
    httpserver::resume_signal pause; std::string body; unsigned ended = 0;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({2}); co_await pause.wait();
        std::array<std::byte, 2> bytes;
        for (;;) {
            auto part = co_await x.body().read_some(bytes); LT_CHECK(part.status.ok());
            if (part.end_of_body) break;
            body.append(reinterpret_cast<const char*>(part.data.data()), part.data.size());
            LT_CHECK(x.body().trailers().empty());
        }
        ++ended; LT_CHECK_EQ(*x.body().trailers().first("x-end"), "original");
        http::fields fields; fields.append("x-response", "first"); fields.append("x-response", "second");
        x.start_response(http::status::from_code(200), fields);
        co_await x.writer().write(std::as_bytes(std::span(body)));
        http::fields trailers; trailers.append("x-tail", "one"); trailers.append("x-tail", "two");
        co_await x.writer().finish(trailers);
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get("POST")))); f.executor.run_pending();
    hd::qpack_dynamic_table table(hd::qpack_table_role::encoder);
    LT_ASSERT(table.initialize(h3test::budget(), 256).ok()); LT_ASSERT(table.set_capacity(256).ok()); LT_ASSERT(table.insert("x-end", "original").ok());
    auto wire = h3test::data("abcd"); auto trailer = dynamic_head(table, {{"x-end", "original"}});
    wire.insert(wire.end(), trailer.begin(), trailer.end()); LT_ASSERT(f.feed(0, wire, true));
    LT_ASSERT(f.feed(6, instruction({hd::qpack_encoder_instruction_kind::capacity, 256})));
    pause.signal(); f.executor.run_pending(); f.engine->begin_turn(); f.engine->pump_output(); f.executor.run_pending();
    LT_CHECK_EQ(ended, 0U);
    auto insert = instruction({hd::qpack_encoder_instruction_kind::insert_literal, 0, false, "x-end", "original"});
    auto evict = instruction({hd::qpack_encoder_instruction_kind::capacity, 0}); insert.insert(insert.end(), evict.begin(), evict.end());
    LT_ASSERT(f.feed(6, insert)); f.executor.run_pending(); f.drain(16);
    LT_CHECK_EQ(body, "abcd"); LT_CHECK_EQ(ended, 1U); LT_CHECK_EQ(f.fins[0], 1U);
    hd::qpack_dynamic_table decoded_table(hd::qpack_table_role::decoder); LT_ASSERT(decoded_table.initialize(h3test::budget(), 256).ok());
    apply_output(f.output[7], decoded_table);
    std::size_t at = 0; unsigned sections = 0;
    while (at < f.output[0].size()) {
        auto type = hd::decode_quic_varint(std::span(f.output[0]).subspan(at)); at += type.consumed;
        auto size = hd::decode_quic_varint(std::span(f.output[0]).subspan(at)); at += size.consumed;
        if (type.value == 1) {
            auto payload = std::span(f.output[0]).subspan(at, size.value);
            auto decoded = hd::qpack_decoder{}.decode_dynamic(
                {reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()}, decoded_table, {65536, 65536, 256});
            LT_ASSERT(decoded.status.ok()); ++sections;
            if (sections == 2) {
                LT_ASSERT_EQ(decoded.fields.size(), 2U); LT_CHECK_EQ(decoded.fields[0].value, "one"); LT_CHECK_EQ(decoded.fields[1].value, "two");
            }
        }
        at += size.value;
    }
    LT_CHECK_EQ(sections, 2U); LT_ASSERT(f.feed(10, h3test::bytes({0x80, 0x80})));
LT_END_AUTO_TEST(blocked_trailers_resume_after_fin_and_paused_body_without_more_input)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, peer_cannot_acknowledge_unpublished_response)
    h3test::request_fixture f(limits()); peer(f);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-name", "value");
        x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    LT_CHECK(!f.feed(10, h3test::bytes({0x80})));
    LT_ASSERT(f.engine->failure()); LT_CHECK_EQ(f.engine->failure()->wire_code, 0x202U);
LT_END_AUTO_TEST(peer_cannot_acknowledge_unpublished_response)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, stopped_connection_rejects_instruction_output_without_readmitting_storage)
    hd::quic_storage_pool pool(10000, 30000, h3test::budget()); hd::http3_connection c(pool.data(), pool.critical(), limits().framing);
    LT_ASSERT(!c.attach_stream(2));
    LT_ASSERT(!h3test::feed(c, 2, h3test::bytes({0, 4, 5, 1, 0x41, 0, 7, 4})).error);
    c.shutdown(); const auto before = pool.critical().budget.in_use(h3test::hs::resource::quic_reassembly_bytes);
    LT_CHECK(c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 0}).state == hd::qpack_state::invalid_argument);
    LT_CHECK_EQ(pool.critical().budget.in_use(h3test::hs::resource::quic_reassembly_bytes), before);
LT_END_AUTO_TEST(stopped_connection_rejects_instruction_output_without_readmitting_storage)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, late_transport_notifications_after_disconnect_are_inert)
    h3test::request_fixture f(limits()); local(f); peer(f); f.open(0);
    f.engine->disconnect({http::outcome_code::connection_closed, "done"});
    f.engine->terminal(0, {hd::quic_terminal_kind::reset, 0x10c});
    f.engine->stop_sending(0, 0x10c); f.engine->pump_receive(0); f.engine->pump_output();
    LT_CHECK(!f.engine->failure());
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::qpack_table_bytes), 0U);
LT_END_AUTO_TEST(late_transport_notifications_after_disconnect_are_inert)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, zero_admitted_capacity_keeps_response_static_without_optional_output_storage)
    auto l = limits(); l.framing.qpack_capacity = 0; l.framing.qpack_blocked_streams = 0;
    h3test::request_fixture f(l); local(f); peer(f);
    httpserver::resume_signal pause;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        co_await pause.wait(); http::fields fields; fields.append("x-static", "value");
        x.respond(http::status::from_code(200), fields);
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    const auto before = f.pool.critical().budget.in_use(h3test::hs::resource::quic_reassembly_bytes);
    pause.signal(); f.executor.run_pending();
    LT_CHECK_EQ(f.pool.critical().budget.in_use(h3test::hs::resource::quic_reassembly_bytes), before);
    f.drain(); LT_CHECK_EQ(first_section(f.output[0])[0], 0U);
    LT_CHECK(f.output[3] == h3test::bytes({0, 4, 4, 1, 0, 7, 0})); LT_CHECK(f.output[7] == h3test::bytes({2}));
LT_END_AUTO_TEST(zero_admitted_capacity_keeps_response_static_without_optional_output_storage)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, resetting_blocked_request_sends_one_cancellation_and_never_dispatches)
    h3test::request_fixture f(limits()); local(f); peer(f); unsigned calls = 0;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        ++calls; x.respond(http::status::from_code(204), {}); co_return;
    }).ok());
    hd::qpack_dynamic_table table(hd::qpack_table_role::encoder);
    LT_ASSERT(table.initialize(h3test::budget(), 256).ok()); LT_ASSERT(table.set_capacity(256).ok()); LT_ASSERT(table.insert("x", "a").ok());
    auto fields = h3test::get(); fields.push_back({"x", "a"});
    f.open(0); LT_ASSERT(f.feed(0, dynamic_head(table, fields), true));
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::blocked_headers), 1U);
    f.engine->stop_sending(0, 0x10c); f.engine->stop_sending(0, 0x10c);
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::blocked_headers), 0U);
    LT_ASSERT(f.feed(6, instruction({hd::qpack_encoder_instruction_kind::capacity, 256})));
    LT_ASSERT(f.feed(6, instruction({hd::qpack_encoder_instruction_kind::insert_literal, 0, false, "x", "a"})));
    f.executor.run_pending(); f.drain(); LT_CHECK_EQ(calls, 0U);
    LT_CHECK(f.output[11] == h3test::bytes({3, 0x40, 1}));
LT_END_AUTO_TEST(resetting_blocked_request_sends_one_cancellation_and_never_dispatches)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, concurrent_transcript_preserves_critical_progress_and_exact_body_credit)
    auto l = limits(); l.body_buffer_bytes = 2; l.max_pending_output_records = 1;
    hd::quic_recovery_config config; config.max_information = 4; config.critical_information = 3;
    config.max_retained_bytes = 64; config.critical_retained_bytes = 32;
    h3test::request_fixture f(l, 65536, config); local(f); peer(f);
    httpserver::resume_signal pause; unsigned calls = 0, ended = 0; std::string body;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        ++calls; http::fields fields; fields.append("x-reply", "first"); fields.append("x-reply", "second");
        x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        ++calls; x.admit_body({2}); co_await pause.wait();
        std::array<std::byte, 2> bytes;
        for (;;) {
            auto part = co_await x.body().read_some(bytes); LT_CHECK(part.status.ok());
            if (part.end_of_body) break;
            body.append(reinterpret_cast<const char*>(part.data.data()), part.data.size());
        }
        ++ended; LT_CHECK_EQ(*x.body().trailers().first("x-req"), "b");
        http::fields fields; fields.append("x-reply", "post"); x.start_response(http::status::from_code(200), fields);
        co_await x.writer().write(std::as_bytes(std::span(body)));
        http::fields trailers; trailers.append("x-tail", "done"); co_await x.writer().finish(trailers);
    }).ok());
    hd::qpack_dynamic_table table(hd::qpack_table_role::encoder);
    LT_ASSERT(table.initialize(h3test::budget(), 256).ok()); LT_ASSERT(table.set_capacity(256).ok()); LT_ASSERT(table.insert("x-req", "a").ok());
    auto fields = h3test::get(); fields.push_back({"x-req", "a"}); auto first = dynamic_head(table, fields);
    LT_ASSERT(table.insert("x-req", "b").ok()); fields = h3test::get("POST"); fields.push_back({"x-req", "b"});
    auto post = dynamic_head(table, fields); const auto head_size = post.size();
    auto data = h3test::data("abcd"); post.insert(post.end(), data.begin(), data.end());
    auto trailer = dynamic_head(table, {{"x-req", "b"}}); post.insert(post.end(), trailer.begin(), trailer.end());
    f.open(0); f.open(4); f.open(8); f.open(12); f.open(16);
    LT_ASSERT(f.feed(0, post, true)); LT_ASSERT(f.feed(4, first)); LT_ASSERT(f.feed(12, dynamic_head(table, fields), true));
    LT_ASSERT(f.feed(8, h3test::headers(h3test::get()), true)); f.executor.run_pending(); LT_CHECK_EQ(calls, 1U);
    f.engine->stop_sending(12, 0x10c); f.engine->stop_sending(12, 0x10c);
    LT_ASSERT(f.feed(6, instruction({hd::qpack_encoder_instruction_kind::capacity, 256})));
    auto insert = instruction({hd::qpack_encoder_instruction_kind::insert_literal, 0, false, "x-req", "a"});
    for (auto byte : insert) LT_ASSERT(f.feed(6, std::span(&byte, 1)));
    f.executor.run_pending(); LT_CHECK_EQ(calls, 2U);
    LT_ASSERT(f.flow.receive(*f.streams.at(4), hd::quic_reset_stream_frame{4, 0x10c, f.incoming[4]}));
    auto reset = f.streams.at(4)->take_terminal(); LT_ASSERT(reset); f.engine->terminal(4, *reset);
    insert = instruction({hd::qpack_encoder_instruction_kind::insert_literal, 0, false, "x-req", "b"});
    LT_ASSERT(f.feed(6, insert)); f.executor.run_pending(); LT_CHECK_EQ(calls, 3U); LT_CHECK_EQ(ended, 0U);
    LT_CHECK_EQ(f.credit(0), 65536U + head_size + 2U);
    std::array<std::byte, 32> filler{}; auto held = f.recovery.retain_stream({16, 0, filler, false}); LT_ASSERT(held);
    auto storage = f.pool.data().budget; h3test::hs::reservation saturation;
    const auto resource = h3test::hs::resource::quic_reassembly_bytes;
    LT_ASSERT(storage.reserve(resource, storage.capacity(resource) - storage.in_use(resource), saturation).ok());
    std::size_t lost = 0; unsigned retransmissions = 0;
    for (unsigned turn = 0; turn < 16; ++turn) transmit_with_delayed_ordinary_ack(f, lost, retransmissions);
    LT_CHECK(!f.output[3].empty()); LT_CHECK(f.output[7].size() > 1U); LT_CHECK(f.output[11].size() > 1U);
    LT_CHECK(f.output[4].empty()); LT_CHECK(f.output[8].empty()); LT_CHECK_EQ(f.credit(0), 65536U + head_size + 2U);
    LT_CHECK(lost > 0U); LT_CHECK(retransmissions > 0U);
    f.recovery.cancel_information(held.id);
    saturation.release(); pause.signal(); f.executor.run_pending();
    for (unsigned turn = 0; turn < 8; ++turn) {
        f.engine->begin_turn(); f.engine->pump_output(); f.executor.run_pending();
    }
    f.drain(16);
    LT_CHECK_EQ(body, "abcd"); LT_CHECK_EQ(ended, 1U); LT_CHECK_EQ(f.fins[0], 1U); LT_CHECK_EQ(f.fins[4], 1U); LT_CHECK_EQ(f.fins[8], 1U);
    LT_CHECK_EQ(f.credit(0), 65536U + post.size()); LT_CHECK(f.output[12].empty());
    hd::qpack_dynamic_table decoder(hd::qpack_table_role::decoder); LT_ASSERT(decoder.initialize(h3test::budget(), 256).ok());
    auto blocked = hd::qpack_decoder{}.decode_dynamic(first_section(f.output[4]), decoder, {65536, 65536, 256});
    LT_CHECK(blocked.status.state == hd::qpack_state::inserts_unavailable);
    apply_output(f.output[7], decoder);
    auto decoded = hd::qpack_decoder{}.decode_dynamic(first_section(f.output[4]), decoder, {65536, 65536, 256});
    LT_ASSERT(decoded.status.ok()); LT_ASSERT_EQ(decoded.fields.size(), 3U);
    LT_CHECK_EQ(decoded.fields[1].value, "first"); LT_CHECK_EQ(decoded.fields[2].value, "second");
    LT_CHECK_EQ(std::count(f.output[11].begin(), f.output[11].end(), std::byte{0x4c}), 1);
    LT_ASSERT(f.feed(10, h3test::bytes({0x80, 0x80, 0x84, 0x88})));
    f.engine->disconnect({http::outcome_code::connection_closed, "transcript done"});
    LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::qpack_table_bytes), 0U); LT_CHECK_EQ(f.root.in_use(h3test::hs::resource::blocked_headers), 0U);
LT_END_AUTO_TEST(concurrent_transcript_preserves_critical_progress_and_exact_body_credit)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, zero_blocked_allowance_uses_dynamic_names_only_after_insert_receipt)
    h3test::request_fixture f(limits()); local(f); peer(f, 256, 0);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-name", "value"); x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending(); f.drain();
    LT_CHECK_EQ(first_section(f.output[0])[0], 0U);
    LT_ASSERT(f.feed(10, h3test::bytes({1})));
    f.open(4); LT_ASSERT(f.feed(4, h3test::headers(h3test::get()), true)); f.executor.run_pending(); f.drain();
    LT_CHECK_EQ(first_section(f.output[4])[0], 2U);
    hd::qpack_dynamic_table decoder(hd::qpack_table_role::decoder); LT_ASSERT(decoder.initialize(h3test::budget(), 256).ok());
    apply_output(f.output[7], decoder); LT_CHECK_EQ(decoder.size(), 1U);
    LT_CHECK(hd::qpack_decoder{}.decode_dynamic(first_section(f.output[4]), decoder, {65536, 65536, 256}).status.ok());
    LT_ASSERT(f.feed(10, h3test::bytes({0x84})));
LT_END_AUTO_TEST(zero_blocked_allowance_uses_dynamic_names_only_after_insert_receipt)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, partial_critical_stream_credit_resumes_after_max_stream_data)
    h3test::request_fixture f(limits(), 3); local(f); peer(f);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-name", "value"); x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending(); f.drain();
    LT_CHECK_EQ(f.output[3].size(), 3U); LT_CHECK_EQ(f.output[7].size(), 3U); LT_CHECK_EQ(f.output[11].size(), 1U); LT_CHECK_EQ(f.fins[0], 0U);
    for (unsigned id : {0U, 3U, 7U, 11U}) LT_ASSERT(f.flow.apply({hd::quic_flow_kind::max_stream_data, 4096, id}));
    f.drain(8); LT_CHECK_EQ(f.fins[0], 1U);
    hd::qpack_dynamic_table decoder(hd::qpack_table_role::decoder); LT_ASSERT(decoder.initialize(h3test::budget(), 256).ok());
    apply_output(f.output[7], decoder);
    LT_CHECK(hd::qpack_decoder{}.decode_dynamic(first_section(f.output[0]), decoder, {65536, 65536, 256}).status.ok());
LT_END_AUTO_TEST(partial_critical_stream_credit_resumes_after_max_stream_data)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, pending_instructions_keep_credit_while_encoder_prefix_is_retained)
    h3test::request_fixture f(limits(), 65536, {}, 12); local(f); peer(f);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-name", "value"); x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    f.engine->begin_turn(); f.engine->pump_output();
    LT_CHECK_EQ(f.flow.send_allowance(0, 0, 65536).bytes, 0U);
    f.engine->disconnect({http::outcome_code::connection_closed, "release pending credit"});
    LT_CHECK_EQ(f.flow.send_allowance(0, 0, 65536).bytes, 12U);
LT_END_AUTO_TEST(pending_instructions_keep_credit_while_encoder_prefix_is_retained)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, new_critical_roles_classify_despite_sustained_control_input)
    auto l = limits(); l.framing.frames_per_turn = 1;
    h3test::request_fixture f(l); local(f);
    f.open(2); LT_ASSERT(f.feed(2, h3test::bytes({0, 4, 5, 1, 0x41, 0, 7, 4})));
    f.open(6); f.open(10);
    std::vector<std::byte> controls;
    for (unsigned n = 0; n < 8; ++n) {
        auto unknown = h3test::frame(32, {}); controls.insert(controls.end(), unknown.begin(), unknown.end());
    }
    auto encoder = h3test::bytes({2}); auto cap = instruction({hd::qpack_encoder_instruction_kind::capacity, 256});
    encoder.insert(encoder.end(), cap.begin(), cap.end()); auto decoder = h3test::bytes({3, 0x40});
    LT_ASSERT(f.flow.receive(*f.streams.at(2), {2, f.incoming[2], controls, false}));
    LT_ASSERT(f.flow.receive(*f.streams.at(6), {6, 0, encoder, false}));
    LT_ASSERT(f.flow.receive(*f.streams.at(10), {10, 0, decoder, false}));
    for (unsigned n = 0; n < 2; ++n) {
        f.engine->begin_turn(); f.engine->pump_output();
    }
    LT_CHECK_EQ(f.credit(6), 65536U + encoder.size()); LT_CHECK_EQ(f.credit(10), 65536U + decoder.size());
LT_END_AUTO_TEST(new_critical_roles_classify_despite_sustained_control_input)
LT_BEGIN_AUTO_TEST(http3_qpack_scheduling_suite, recovery_byte_limits_admit_partial_encoder_and_response_ranges)
    hd::quic_recovery_config config; config.max_retained_bytes = 16; config.critical_retained_bytes = 8;
    h3test::request_fixture f(limits(), 65536, config); local(f); peer(f);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append(std::string(64, 'x'), "value"); x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending(); f.drain(16);
    LT_CHECK(f.output[7].size() > 16U); LT_CHECK_EQ(f.fins[0], 1U);
    hd::qpack_dynamic_table decoder(hd::qpack_table_role::decoder); LT_ASSERT(decoder.initialize(h3test::budget(), 256).ok());
    apply_output(f.output[7], decoder);
    LT_ASSERT(!f.output[0].empty());
    LT_CHECK(hd::qpack_decoder{}.decode_dynamic(first_section(f.output[0]), decoder, {65536, 65536, 256}).status.ok());
LT_END_AUTO_TEST(recovery_byte_limits_admit_partial_encoder_and_response_ranges)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
