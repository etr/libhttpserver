/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <httpserver/server/server.hpp>
#include <httpserver/detail/quic_connection_lifecycle.hpp>
#include "./http3_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace http = httpserver::http;
namespace hs = httpserver::server;
using httpserver::exchange;
using httpserver::task;
namespace {
using clock_type = std::chrono::steady_clock;
struct destroy_on_post_executor : httpserver::executor {
    std::unique_ptr<hd::http3_request_engine>* engine = nullptr;
    bool destroy = false;
    void post(handler work) override {
        if (destroy) {
            destroy = false; engine->reset();
        }
        work();
    }
    bool is_current() const noexcept override { return false; }
};
void local(h3test::request_fixture& f) {
    for (auto role : {hd::http3_role::control, hd::http3_role::qpack_encoder, hd::http3_role::qpack_decoder}) {
        auto opened = f.flow.open_local(true);
        if (!opened) throw std::runtime_error("critical open failed");
        auto s = std::make_unique<hd::quic_stream_state>(opened.id, hd::quic_endpoint_role::server, f.flow.ids(), hd::quic_stream_limits{}, f.pool.critical());
        if (f.engine->attach_local(role, *s)) throw std::runtime_error("critical attach failed");
        f.streams.emplace(opened.id, std::move(s));
    }
}
std::vector<std::uint64_t> goaways(const std::vector<std::byte>& wire) {
    hd::quic_cursor cursor(wire);
    if (cursor.varint() != 0) throw std::runtime_error("missing control type");
    std::vector<std::uint64_t> ids;
    bool settings = false;
    while (cursor.remaining()) {
        auto type = cursor.varint(), length = cursor.varint();
        auto payload = cursor.take(length);
        if (!settings && type != 4) throw std::runtime_error("SETTINGS not first");
        settings = true;
        if (type == 7) ids.push_back(hd::decode_quic_varint(payload).value);
    }
    return ids;
}
bool transmit(h3test::request_fixture& f, std::vector<hd::quic_information_completion>& held, bool hold_control, bool hold_response, bool hold_encoder = false) {
    f.engine->begin_turn(); f.engine->pump_output(); f.now += std::chrono::seconds(1);
    std::array<std::byte, 24> packet{}; hd::quic_send_request request;
    request.max_wire_bytes = packet.size(); request.protection_overhead = 0;
    auto plan = f.recovery.prepare_scheduled_packet(hd::quic_pn_space::application, packet, f.now, request, f.flow);
    if (!plan) return false;
    if (!f.recovery.check_scheduled_emission(plan.token, f.now, plan.bytes, plan.ack_eliciting, plan.ack_eliciting)) throw std::runtime_error("emission refused");
    if (plan.stream) {
        auto frame = *plan.stream; auto& output = f.output[frame.stream];
        if (frame.offset != output.size()) throw std::runtime_error("unexpected retransmission");
        output.insert(output.end(), frame.data.begin(), frame.data.end());
        if (!f.streams.at(frame.stream)->record_stream_sent(frame.offset, frame.data.size(), frame.fin)) throw std::runtime_error("sent offset refused");
        if (frame.fin) ++f.fins[frame.stream];
    }
    if (!f.recovery.commit_sent(plan.token, f.now, plan.bytes, plan.ack_eliciting, plan.ack_eliciting)) throw std::runtime_error("commit refused");
    hd::quic_ack_frame ack; ack.largest = plan.packet_number;
    f.recovery.receive_ack(hd::quic_pn_space::application, ack, f.now + std::chrono::milliseconds(1));
    while (auto done = f.recovery.take_completion()) {
        if ((done->stream == 3 && hold_control) || (done->stream == 0 && hold_response) || (done->stream == 7 && hold_encoder)) held.push_back(*done);
        else f.engine->information_completed(*done);
    }
    f.executor.run_pending();
    return true;
}
}  // namespace
LT_BEGIN_SUITE(http3_drain_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(http3_drain_suite)
LT_BEGIN_AUTO_TEST(http3_drain_suite, settings_precede_ack_gated_nonincreasing_goaway)
    auto budget = h3test::budget();
    hd::quic_storage_pool pool(200000, 30000, budget);
    hd::http3_connection c(pool.data(), pool.critical());
    auto settings = c.local_prefix(hd::http3_role::control);
    const auto settings_size = settings.size();
    LT_ASSERT(c.begin_goaway(4).ok());
    LT_CHECK_EQ(c.local_prefix(hd::http3_role::control).size(), settings_size);
    LT_ASSERT(c.advance_local_prefix(hd::http3_role::control, settings_size));
    auto first = c.local_prefix(hd::http3_role::control);
    LT_ASSERT_EQ(first.size(), 10U);
    LT_CHECK_EQ(std::to_integer<unsigned>(first[0]), 7U);
    LT_CHECK_EQ(hd::decode_quic_varint(first.subspan(2)).value, hd::k_quic_max_integer - 3);
    LT_ASSERT(c.advance_local_prefix(hd::http3_role::control, 3));
    LT_CHECK_EQ(c.local_prefix(hd::http3_role::control).size(), 7U);
    LT_ASSERT(c.advance_local_prefix(hd::http3_role::control, 7));
    LT_CHECK(c.local_prefix(hd::http3_role::control).empty());
    c.acknowledge_control(settings_size + 9);
    LT_CHECK(c.local_prefix(hd::http3_role::control).empty());
    c.acknowledge_control(settings_size + 10);
    auto final = c.local_prefix(hd::http3_role::control);
    LT_ASSERT_EQ(final.size(), 3U);
    LT_CHECK_EQ(hd::decode_quic_varint(final.subspan(2)).value, 4U);
    LT_CHECK(!c.begin_goaway(8).ok());
    LT_ASSERT(c.advance_local_prefix(hd::http3_role::control, 3));
    c.acknowledge_control(settings_size + 13);
    LT_CHECK(c.goaway_retired());
LT_END_AUTO_TEST(settings_precede_ack_gated_nonincreasing_goaway)
LT_BEGIN_AUTO_TEST(http3_drain_suite, accepted_request_survives_and_later_holes_are_rejected)
    h3test::request_fixture f; local(f);
    httpserver::resume_signal pause;
    unsigned calls = 0;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        ++calls; co_await pause.wait(); x.respond(http::status::from_code(200), {});
    }).ok());
    f.open(8); LT_ASSERT(f.feed(8, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    f.open(12); LT_ASSERT(f.feed(12, h3test::bytes({1})));
    hs::drain_ticket ticket;
    LT_ASSERT(f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    for (auto id : {12U, 0U, 16U}) {
        if (id != 12) f.open(id);
        auto action = f.engine->take_action();
        LT_ASSERT(action); LT_ASSERT(action->reset);
        LT_CHECK_EQ(action->reset->stream, id); LT_CHECK_EQ(action->reset->error, 0x10bU);
        f.pump(id); LT_CHECK(!f.engine->take_action());
    }
    f.drain(8); LT_CHECK_EQ(calls, 1U);
    LT_CHECK(!f.engine->take_connection_close());
    pause.signal(); f.executor.run_pending(); f.drain(8);
    LT_CHECK_EQ(f.fins[8], 1U);
    auto ids = goaways(f.output[3]); LT_ASSERT_EQ(ids.size(), 2U); LT_CHECK_EQ(ids[1], 12U);
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
    auto close = f.engine->take_connection_close(); LT_ASSERT(close); LT_CHECK(close->application); LT_CHECK_EQ(close->error, 0x100U);
    LT_CHECK(!f.engine->take_connection_close()); LT_CHECK(!f.engine->failure());
LT_END_AUTO_TEST(accepted_request_survives_and_later_holes_are_rejected)
LT_BEGIN_AUTO_TEST(http3_drain_suite, handler_initiation_counts_itself_and_wait_is_rejected)
    h3test::request_fixture f; local(f);
    hs::drain_ticket ticket; http::outcome_code waited = http::outcome_code::ok;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        auto started = f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket);
        if (!started.ok()) throw std::runtime_error("handler drain refused");
        hs::drain_result result; waited = ticket.wait(result).code();
        x.respond(http::status::from_code(204), {}); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    LT_CHECK(waited == http::outcome_code::would_deadlock);
    f.drain(); auto ids = goaways(f.output[3]); LT_ASSERT_EQ(ids.size(), 2U); LT_CHECK_EQ(ids[1], 4U);
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
LT_END_AUTO_TEST(handler_initiation_counts_itself_and_wait_is_rejected)
LT_BEGIN_AUTO_TEST(http3_drain_suite, blocked_output_expires_once_and_quic_retains_terminal_state)
    h3test::request_fixture f({}, 0, {}, 0); local(f);
    httpserver::resume_signal pause; httpserver::stop_token stop;
    unsigned resumed = 0;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        stop = x.cancellation(); co_await pause.wait(); ++resumed; x.respond(http::status::from_code(204), {});
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    auto now = clock_type::now(), deadline = now + std::chrono::hours(1); hs::drain_ticket ticket;
    LT_ASSERT(f.engine->begin_drain(deadline, ticket).ok()); LT_CHECK(f.engine->drain_deadline() == deadline);
    f.engine->check_drain(deadline - std::chrono::nanoseconds(1)); LT_CHECK(!stop.stop_requested());
    f.engine->pump_output(); LT_CHECK(!f.engine->take_connection_close());
    f.engine->check_drain(deadline); LT_CHECK(stop.stop_requested());
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::deadline_expired); LT_CHECK_EQ(result.remaining, 2U);
    auto close = f.engine->take_connection_close(); LT_ASSERT(close);
    hd::quic_connection_lifecycle lifecycle({}, {}, now);
    LT_ASSERT(lifecycle.local_close(*close, deadline, std::chrono::seconds(1)));
    LT_CHECK(lifecycle.state() == hd::quic_lifecycle_state::closing); LT_CHECK(lifecycle.response_due(deadline));
    LT_CHECK(!lifecycle.expire(deadline + std::chrono::seconds(2), std::chrono::seconds(1)));
    LT_CHECK(lifecycle.expire(deadline + std::chrono::seconds(3), std::chrono::seconds(1)));
    f.engine->check_drain(deadline + std::chrono::hours(1)); LT_CHECK(!f.engine->take_connection_close());
    pause.signal(); f.executor.run_pending(); LT_CHECK_EQ(resumed, 0U);
    hs::drain_result again; LT_ASSERT(ticket.wait(again).ok()); LT_CHECK(again.status == result.status); LT_CHECK_EQ(again.remaining, result.remaining);
    LT_CHECK_EQ(f.pool.data().budget.in_use(hs::resource::header_bytes), 0U);
LT_END_AUTO_TEST(blocked_output_expires_once_and_quic_retains_terminal_state)
LT_BEGIN_AUTO_TEST(http3_drain_suite, invalid_repeated_and_resource_refused_initiation_are_atomic)
    h3test::request_fixture f; local(f);
    hs::drain_ticket ticket;
    LT_CHECK(!f.engine->begin_drain(clock_type::now(), ticket).ok());
    hs::reservation pressure;
    auto budget = f.pool.critical().budget;
    const auto available = 1048576 - budget.in_use(hs::resource::quic_reassembly_bytes);
    LT_ASSERT(budget.reserve(hs::resource::quic_reassembly_bytes, available, pressure).ok());
    LT_CHECK(!f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    hs::drain_result empty; LT_CHECK(ticket.wait(empty).code() == http::outcome_code::invalid_state); LT_CHECK(!f.engine->drain_deadline());
    pressure.release(); LT_ASSERT(f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    LT_CHECK(!f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    f.drain(8); auto ids = goaways(f.output[3]); LT_ASSERT_EQ(ids.size(), 2U); LT_CHECK_EQ(ids[1], 0U);
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
LT_END_AUTO_TEST(invalid_repeated_and_resource_refused_initiation_are_atomic)
LT_BEGIN_AUTO_TEST(http3_drain_suite, terminal_cleanup_releases_goaway_reservation)
    hd::quic_storage_pool pool(200000, 30000, h3test::budget());
    hd::http3_connection c(pool.data(), pool.critical());
    LT_ASSERT(c.begin_goaway(0).ok()); c.shutdown();
    LT_CHECK_EQ(pool.critical().budget.in_use(hs::resource::quic_reassembly_bytes), 0U);
LT_END_AUTO_TEST(terminal_cleanup_releases_goaway_reservation)
LT_BEGIN_AUTO_TEST(http3_drain_suite, owned_ack_retirement_gates_stages_and_last_response_completion)
    h3test::request_fixture f; local(f);
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-large", std::string(300, 'a'));
        x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    hs::drain_ticket ticket; LT_ASSERT(f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    std::vector<hd::quic_information_completion> held;
    for (unsigned turn = 0; turn < 100 && transmit(f, held, true, true); ++turn) {}
    LT_ASSERT(!held.empty()); LT_CHECK(!f.engine->take_connection_close());
    // SETTINGS ACK alone enables output retention, but does not skip GOAWAY's ACK barrier.
    auto settings = held.front(); LT_CHECK_EQ(settings.stream, 3U);
    auto unrelated = settings; unrelated.id += 1000000; unrelated.through_offset = hd::k_quic_max_integer;
    f.engine->information_completed(unrelated); LT_CHECK(!f.engine->take_connection_close());
    f.engine->information_completed(settings); f.engine->information_completed(settings); held.erase(held.begin());
    for (unsigned turn = 0; turn < 100 && transmit(f, held, true, true); ++turn) {}
    auto ids = goaways(f.output[3]); LT_ASSERT_EQ(ids.size(), 1U);
    for (unsigned turn = 0; turn < 3; ++turn) {
        f.engine->begin_turn(); f.engine->pump_output();
    }
    LT_CHECK_EQ(goaways(f.output[3]).size(), 1U);
    auto initial = std::find_if(held.begin(), held.end(), [](auto c) { return c.stream == 3; }); LT_ASSERT(initial != held.end());
    auto initial_done = *initial; held.erase(initial); f.engine->information_completed(initial_done); f.engine->information_completed(initial_done);
    for (unsigned turn = 0; turn < 100 && transmit(f, held, false, true); ++turn) {}
    LT_CHECK_EQ(goaways(f.output[3]).size(), 2U); LT_CHECK_EQ(f.fins[0], 1U);
    LT_CHECK(!f.engine->take_connection_close());
    std::atomic<unsigned> completed{0};
    auto waiter = [&] { hs::drain_result result; if (ticket.wait(result).ok() && result.status == hs::drain_status::completed) ++completed; };
    std::thread first(waiter), second(waiter);
    std::reverse(held.begin(), held.end());
    for (auto done : held) {
        f.engine->information_completed(done); f.engine->information_completed(done);
    }
    // No output poll is needed after the last owned information completion.
    auto close = f.engine->take_connection_close();
    if (!close) f.engine->check_drain(clock_type::now() + std::chrono::hours(2));
    first.join(); second.join(); LT_ASSERT(close); LT_CHECK_EQ(completed.load(), 2U);
    f.engine->check_drain(clock_type::now() + std::chrono::hours(2)); hs::drain_result result;
    LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::completed);
LT_END_AUTO_TEST(owned_ack_retirement_gates_stages_and_last_response_completion)
LT_BEGIN_AUTO_TEST(http3_drain_suite, abandoned_packet_and_flow_credit_preserve_control_prefix)
    h3test::request_fixture f({}, 1, {}, 1); local(f);
    hs::drain_ticket ticket; LT_ASSERT(f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    f.engine->pump_output(); std::array<std::byte, 24> packet{}; hd::quic_send_request request;
    request.max_wire_bytes = packet.size(); request.protection_overhead = 0;
    auto first = f.recovery.prepare_scheduled_packet(hd::quic_pn_space::application, packet, f.now, request, f.flow);
    LT_ASSERT(first); LT_ASSERT(first.stream); const auto offset = first.stream->offset;
    std::vector<std::byte> saved(first.stream->data.begin(), first.stream->data.end()); f.recovery.abandon_packet(first.token);
    LT_CHECK(!f.engine->take_connection_close());
    auto retry = f.recovery.prepare_scheduled_packet(hd::quic_pn_space::application, packet, f.now, request, f.flow);
    LT_ASSERT(retry); LT_ASSERT(retry.stream); LT_CHECK_EQ(retry.stream->offset, offset); LT_CHECK(std::ranges::equal(retry.stream->data, saved));
    f.recovery.abandon_packet(retry.token); f.drain(); LT_CHECK(!f.engine->take_connection_close());
    LT_ASSERT(f.flow.apply(hd::quic_flow_frame{hd::quic_flow_kind::max_data, 4096, 0}));
    for (auto id : {3U, 7U, 11U}) LT_ASSERT(f.flow.apply(hd::quic_flow_frame{hd::quic_flow_kind::max_stream_data, 4096, id}));
    f.drain(8); auto ids = goaways(f.output[3]); LT_ASSERT_EQ(ids.size(), 2U); LT_CHECK_EQ(ids[1], 0U); LT_ASSERT(f.engine->take_connection_close());
LT_END_AUTO_TEST(abandoned_packet_and_flow_credit_preserve_control_prefix)
LT_BEGIN_AUTO_TEST(http3_drain_suite, disconnect_and_late_completions_keep_incomplete_result)
    h3test::request_fixture f; local(f); httpserver::resume_signal pause;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        co_await pause.wait(); x.respond(http::status::from_code(204), {});
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    hs::drain_ticket ticket; LT_ASSERT(f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    f.engine->disconnect({http::outcome_code::connection_closed, "peer close"});
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::deadline_expired); LT_CHECK_EQ(result.remaining, 2U);
    f.engine.reset(); pause.signal(); f.executor.run_pending();
    hs::drain_result again; LT_ASSERT(ticket.wait(again).ok()); LT_CHECK_EQ(again.remaining, result.remaining);
LT_END_AUTO_TEST(disconnect_and_late_completions_keep_incomplete_result)
LT_BEGIN_AUTO_TEST(http3_drain_suite, accepted_maximum_request_id_refuses_an_unrepresentable_cutoff)
    h3test::request_fixture f({}, 65536, {}, 1048576, std::uint64_t{1} << 60); local(f);
    f.open(hd::k_quic_max_integer - 3);
    LT_ASSERT(f.feed(hd::k_quic_max_integer - 3, h3test::headers(h3test::get()), true));
    hs::drain_ticket ticket;
    auto status = f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket);
    LT_CHECK(status.code() == http::outcome_code::limit_exceeded); LT_CHECK(!f.engine->drain_deadline());
LT_END_AUTO_TEST(accepted_maximum_request_id_refuses_an_unrepresentable_cutoff)
LT_BEGIN_AUTO_TEST(http3_drain_suite, required_qpack_instructions_retire_before_terminal_close)
    hd::http3_request_limits limits; limits.framing.qpack_capacity = 256; limits.framing.qpack_blocked_streams = 4;
    h3test::request_fixture f(limits); local(f); f.open(2);
    LT_ASSERT(f.feed(2, h3test::bytes({0, 4, 5, 1, 0x41, 0, 7, 4})));
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("x-new-name", "dynamic response"); x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    hs::drain_ticket ticket; LT_ASSERT(f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    std::vector<hd::quic_information_completion> held;
    for (unsigned n = 0; n < 100 && transmit(f, held, false, false, true); ++n) {}
    LT_ASSERT(!held.empty()); LT_CHECK(!f.engine->take_connection_close());
    for (auto done : held) f.engine->information_completed(done);
    f.drain(8); LT_CHECK(f.output[7].size() > 1U); LT_ASSERT(f.engine->take_connection_close());
LT_END_AUTO_TEST(required_qpack_instructions_retire_before_terminal_close)
LT_BEGIN_AUTO_TEST(http3_drain_suite, recovery_saturation_keeps_partial_goaway_progress_protected)
    hd::quic_recovery_config config; config.max_information = 4; config.critical_information = 3; config.max_retained_bytes = 16; config.critical_retained_bytes = 8;
    h3test::request_fixture f({}, 0, config); local(f); f.open(0);
    for (auto id : {3U, 7U, 11U}) LT_ASSERT(f.flow.apply({hd::quic_flow_kind::max_stream_data, 4096, id}));
    std::array<std::byte, 8> filler{}; auto ordinary = f.recovery.retain_stream({0, 0, filler, false}); LT_ASSERT(ordinary);
    LT_CHECK(!f.recovery.retain_stream({0, 8, filler, false}));
    hs::drain_ticket ticket; LT_ASSERT(f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    f.drain(8); auto ids = goaways(f.output[3]); LT_ASSERT_EQ(ids.size(), 2U); LT_CHECK_EQ(ids[1], 0U);
    LT_ASSERT(f.engine->take_connection_close()); LT_ASSERT(f.recovery.cancel_information(ordinary.id));
LT_END_AUTO_TEST(recovery_saturation_keeps_partial_goaway_progress_protected)
LT_BEGIN_AUTO_TEST(http3_drain_suite, lost_goaway_retransmits_the_same_offset_and_cutoff_before_advancing)
    h3test::request_fixture f; local(f); f.drain();
    hs::drain_ticket ticket; LT_ASSERT(f.engine->begin_drain(clock_type::now() + std::chrono::hours(1), ticket).ok());
    f.engine->pump_output(); std::array<std::byte, 64> packet{}; hd::quic_send_request request;
    request.max_wire_bytes = packet.size(); request.protection_overhead = 0;
    f.now += std::chrono::seconds(1);
    auto lost = f.recovery.prepare_scheduled_packet(hd::quic_pn_space::application, packet, f.now, request, f.flow);
    LT_ASSERT(lost); LT_ASSERT(lost.stream); LT_CHECK_EQ(lost.stream->stream, 3U);
    const auto offset = lost.stream->offset; std::vector<std::byte> saved(lost.stream->data.begin(), lost.stream->data.end());
    LT_ASSERT(f.recovery.check_scheduled_emission(lost.token, f.now, lost.bytes, true, true));
    LT_ASSERT(f.streams.at(3)->record_stream_sent(offset, saved.size(), false));
    LT_ASSERT(f.recovery.commit_sent(lost.token, f.now, lost.bytes, true, true));
    std::uint64_t latest = 0;
    for (unsigned n = 0; n < 3; ++n) {
        auto ping = f.recovery.reserve_packet(hd::quic_pn_space::application); LT_ASSERT(ping); latest = ping.packet_number;
        LT_ASSERT(f.recovery.commit_sent(ping.token, f.now, 1, true, true));
    }
    hd::quic_ack_frame ack; ack.largest = latest;
    auto event = f.recovery.receive_ack(hd::quic_pn_space::application, ack, f.now + std::chrono::milliseconds(1)); LT_ASSERT(event); LT_CHECK(event.lost_bytes > 0U);
    LT_CHECK(!f.recovery.take_completion()); LT_CHECK(!f.engine->take_connection_close());
    f.now += std::chrono::seconds(1);
    auto retry = f.recovery.prepare_scheduled_packet(hd::quic_pn_space::application, packet, f.now, request, f.flow);
    LT_ASSERT(retry); LT_ASSERT(retry.stream); LT_CHECK_EQ(retry.stream->offset, offset); LT_CHECK(std::ranges::equal(retry.stream->data, saved));
    LT_ASSERT(f.recovery.check_scheduled_emission(retry.token, f.now, retry.bytes, true, true));
    LT_ASSERT(f.streams.at(3)->record_stream_sent(offset, saved.size(), false));
    LT_ASSERT(f.recovery.commit_sent(retry.token, f.now, retry.bytes, true, true));
    f.output[3].insert(f.output[3].end(), saved.begin(), saved.end()); ack.largest = retry.packet_number;
    LT_ASSERT(f.recovery.receive_ack(hd::quic_pn_space::application, ack, f.now + std::chrono::milliseconds(1)));
    while (auto done = f.recovery.take_completion()) f.engine->information_completed(*done);
    f.drain(8); auto ids = goaways(f.output[3]); LT_ASSERT_EQ(ids.size(), 2U); LT_CHECK_EQ(ids[0], hd::k_quic_max_integer - 3); LT_CHECK_EQ(ids[1], 0U);
    LT_ASSERT(f.engine->take_connection_close());
LT_END_AUTO_TEST(lost_goaway_retransmits_the_same_offset_and_cutoff_before_advancing)
LT_BEGIN_AUTO_TEST(http3_drain_suite, ticket_expiry_notifies_without_mutating_owner_work)
    h3test::request_fixture f; local(f); httpserver::resume_signal pause; httpserver::stop_token stop;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        stop = x.cancellation(); co_await pause.wait(); x.respond(http::status::from_code(204), {});
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    hs::drain_ticket ticket; auto deadline = clock_type::now() + std::chrono::milliseconds(20);
    LT_ASSERT(f.engine->begin_drain(deadline, ticket).ok());
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::deadline_expired); LT_CHECK_EQ(result.remaining, 2U);
    LT_CHECK(!stop.stop_requested()); LT_CHECK(!f.engine->take_connection_close());
    f.engine->check_drain(deadline); LT_CHECK(stop.stop_requested()); LT_ASSERT(f.engine->take_connection_close());
    pause.signal(); f.executor.run_pending(); f.engine->information_completed({1000000, hd::quic_information_kind::stream, hd::quic_pn_space::application, 3, hd::k_quic_max_integer});
    hs::drain_result again; LT_ASSERT(ticket.wait(again).ok()); LT_CHECK(again.status == result.status); LT_CHECK_EQ(again.remaining, result.remaining);
LT_END_AUTO_TEST(ticket_expiry_notifies_without_mutating_owner_work)
LT_BEGIN_AUTO_TEST(http3_drain_suite, resetting_accepted_work_never_reports_completed)
    h3test::request_fixture f; local(f); httpserver::resume_signal pause;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        co_await pause.wait(); x.respond(http::status::from_code(204), {});
    }).ok());
    f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); f.executor.run_pending();
    hs::drain_ticket ticket; auto deadline = clock_type::now() + std::chrono::hours(1);
    LT_ASSERT(f.engine->begin_drain(deadline, ticket).ok()); f.engine->stop_sending(0, 42); f.drain(8);
    LT_CHECK(!f.engine->take_connection_close()); f.engine->check_drain(deadline); LT_ASSERT(f.engine->take_connection_close());
    hs::drain_result result; LT_ASSERT(ticket.wait(result).ok()); LT_CHECK(result.status == hs::drain_status::deadline_expired);
LT_END_AUTO_TEST(resetting_accepted_work_never_reports_completed)
LT_BEGIN_AUTO_TEST(http3_drain_suite, owner_entry_keeps_state_during_inline_cancellation)
    for (bool completion : {true, false}) {
        h3test::request_fixture f; destroy_on_post_executor executor; executor.engine = &f.engine;
        f.engine = std::make_unique<hd::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, executor);
        local(f); unsigned entered = 0;
        LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            ++entered; co_await x.cancellation().cancelled();
        }).ok());
        f.open(0); LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true)); LT_CHECK_EQ(entered, 1U);
        std::vector<hd::quic_information_completion> held;
        for (unsigned n = 0; n < 100 && transmit(f, held, true, true); ++n) {}
        LT_ASSERT(!held.empty());
        hs::drain_ticket ticket; auto deadline = clock_type::now() + std::chrono::milliseconds(20);
        LT_ASSERT(f.engine->begin_drain(deadline, ticket).ok());
        hs::drain_result result; LT_ASSERT(ticket.wait(result).ok());
        executor.destroy = true;
        if (completion) f.engine->information_completed(held.front());
        else f.engine->begin_turn();
        LT_CHECK(!f.engine); LT_CHECK(result.status == hs::drain_status::deadline_expired);
        LT_CHECK_EQ(f.pool.data().budget.in_use(hs::resource::header_bytes), 0U);
    }
LT_END_AUTO_TEST(owner_entry_keeps_state_during_inline_cancellation)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
