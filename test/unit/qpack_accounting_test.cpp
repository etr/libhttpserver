/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "./http3_fixture.hpp"
#include <httpserver/detail/quic_flow_control.hpp>
#include <httpserver/detail/quic_recovery.hpp>
#include "./littletest.hpp"
using namespace h3test;  // NOLINT(build/namespaces)
namespace {
hd::http3_limits limits() {
    hd::http3_limits l; l.qpack_capacity = 128; l.qpack_blocked_streams = 2; l.headers = {100, 100, 4}; return l;
}
void peer(hd::http3_connection& c, unsigned blocked = 2) {
    c.attach_stream(2); feed(c, 2, bytes({0, 4, 5, 1, 0x40, 0x80, 7, blocked}));
    c.attach_stream(6); feed(c, 6, bytes({3})); c.attach_stream(0); c.attach_stream(4);
}
bool insert(hd::http3_connection& c, const char* value) {
    return c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 128}).ok() &&
        c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::insert_literal, 0, false, "x", value}).ok();
}
}  // namespace
LT_BEGIN_SUITE(qpack_accounting_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(qpack_accounting_suite)
LT_BEGIN_AUTO_TEST(qpack_accounting_suite, section_fifo_shared_holds_and_cancellation)
    hd::quic_storage_pool pool(10000, 30000, budget()); hd::http3_connection c(pool.data(), pool.critical(), limits()); peer(c); LT_ASSERT(insert(c, "a"));
    std::array<hd::qpack_field_view, 1> fields{{{"x", "a"}}};
    auto a = c.encode_section(0, fields); auto b = c.encode_section(0, fields); auto other = c.encode_section(4, fields);
    LT_ASSERT(a.status.ok() && b.status.ok() && other.status.ok());
    LT_CHECK_EQ(static_cast<unsigned char>(a.value[0]), 2U);
    LT_CHECK(!feed(c, 6, bytes({0x80})).error);
    LT_CHECK(c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 0}).state == hd::qpack_state::eviction_prohibited);
    LT_CHECK(!feed(c, 6, bytes({0x80})).error);
    LT_CHECK(c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 0}).state == hd::qpack_state::eviction_prohibited);
    LT_CHECK(!feed(c, 6, bytes({0x44})).error);
    LT_CHECK(c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 0}).ok());
    LT_CHECK_EQ(feed(c, 6, bytes({0x80})).error->wire_code, 0x202U);
LT_END_AUTO_TEST(section_fifo_shared_holds_and_cancellation)
LT_BEGIN_AUTO_TEST(qpack_accounting_suite, cancellation_does_not_advance_known_received)
    hd::quic_storage_pool pool(10000, 30000, budget()); hd::http3_connection c(pool.data(), pool.critical(), limits()); peer(c); LT_ASSERT(insert(c, "a"));
    std::array<hd::qpack_field_view, 1> fields{{{"x", "a"}}}; LT_ASSERT(c.encode_section(0, fields).status.ok());
    LT_CHECK(!feed(c, 6, bytes({0x40, 0x40})).error);
    LT_CHECK(c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 0}).state == hd::qpack_state::eviction_prohibited);
    LT_CHECK(!feed(c, 6, bytes({1})).error);
    LT_CHECK(c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 0}).ok());
LT_END_AUTO_TEST(cancellation_does_not_advance_known_received)
LT_BEGIN_AUTO_TEST(qpack_accounting_suite, peer_blocked_allowance_counts_streams_and_increment_preserves_holds)
    for (unsigned allowance : {0U, 1U}) {
        hd::quic_storage_pool pool(10000, 30000, budget()); hd::http3_connection c(pool.data(), pool.critical(), limits()); peer(c, allowance); LT_ASSERT(insert(c, "a"));
        std::array<hd::qpack_field_view, 1> fields{{{"x", "a"}}};
        auto a = c.encode_section(0, fields); auto b = c.encode_section(0, fields); auto other = c.encode_section(4, fields);
        LT_ASSERT(a.status.ok() && b.status.ok() && other.status.ok());
        LT_CHECK_EQ(static_cast<unsigned char>(a.value[0]), allowance ? 2U : 0U);
        LT_CHECK_EQ(static_cast<unsigned char>(b.value[0]), allowance ? 2U : 0U);
        LT_CHECK_EQ(static_cast<unsigned char>(other.value[0]), 0U);
        LT_CHECK(!feed(c, 6, bytes({1})).error);
        LT_CHECK_EQ(static_cast<unsigned char>(c.encode_section(4, fields).value[0]), 2U);
        LT_CHECK(c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 0}).state == hd::qpack_state::eviction_prohibited);
    }
LT_END_AUTO_TEST(peer_blocked_allowance_counts_streams_and_increment_preserves_holds)
LT_BEGIN_AUTO_TEST(qpack_accounting_suite, feedback_literal_bytes_partial_output_and_abandonment)
    hd::quic_storage_pool pool(10000, 30000, budget()); hd::http3_connection c(pool.data(), pool.critical(), limits());
    c.attach_stream(0); c.attach_stream(4); c.attach_stream(2);
    feed(c, 0, bytes({1, 3, 2, 0, 0x80})); feed(c, 4, bytes({1, 3, 3, 0, 0x80}));
    LT_CHECK(!c.terminal(4, {hd::quic_terminal_kind::reset}));
    LT_CHECK(!feed(c, 2, bytes({2, 0x3f, 0x61, 0x41, 'x', 1, 'a'})).error);
    auto role = hd::http3_role::qpack_decoder;
    LT_CHECK(std::ranges::equal(c.local_prefix(role), bytes({3})));
    LT_ASSERT(c.advance_local_prefix(role, 1));
    LT_CHECK(std::ranges::equal(c.local_prefix(role), bytes({0x44, 0x80})));
    LT_CHECK(std::ranges::equal(c.local_prefix(role), bytes({0x44, 0x80})));
    LT_ASSERT(c.advance_local_prefix(role, 1)); LT_CHECK(std::ranges::equal(c.local_prefix(role), bytes({0x80})));
    LT_CHECK(!feed(c, 2, bytes({0})).error);
    LT_CHECK(std::ranges::equal(c.local_prefix(role), bytes({0x80, 1})));
    LT_CHECK(!c.advance_local_prefix(role, 3)); LT_ASSERT(c.advance_local_prefix(role, 2));
    LT_CHECK(c.local_prefix(role).empty());
LT_END_AUTO_TEST(feedback_literal_bytes_partial_output_and_abandonment)
LT_BEGIN_AUTO_TEST(qpack_accounting_suite, critical_ranges_protect_connection_credit_and_retransmit)
    hd::quic_transport_parameters p; p.initial_max_data = 12; p.initial_max_stream_data_uni = 10;
    p.initial_max_stream_data_bidi_remote = 12; p.initial_max_streams_uni = 3; p.initial_max_streams_bidi = 2;
    hd::quic_flow_control flow(hd::quic_endpoint_role::server, p, p, 8, budget());
    auto encoder = flow.open_local(true); auto decoder = flow.open_local(true); auto ordinary = flow.open_local(false);
    LT_ASSERT(encoder && decoder && ordinary);
    LT_ASSERT(flow.reserve_critical(encoder.id, 0, 3)); LT_ASSERT(flow.reserve_critical(decoder.id, 0, 2));
    LT_CHECK_EQ(flow.send_allowance(ordinary.id, 0, 12).bytes, 7U);
    LT_ASSERT(flow.record_stream_sent(ordinary.id, 0, 7, false));
    LT_CHECK(flow.send_allowance(ordinary.id, 7, 1).code == hd::quic_flow_code::blocked);
    LT_ASSERT(flow.record_stream_sent(encoder.id, 0, 1, false));
    LT_ASSERT(flow.record_stream_sent(encoder.id, 0, 1, false)); LT_CHECK_EQ(flow.sent(), 8U);
    LT_ASSERT(flow.record_stream_sent(decoder.id, 0, 2, false));
    LT_ASSERT(flow.record_stream_sent(encoder.id, 1, 2, false)); LT_CHECK_EQ(flow.sent(), 12U);
    LT_ASSERT(flow.record_stream_sent(encoder.id, 0, 3, false)); LT_CHECK_EQ(flow.sent(), 12U);
    LT_CHECK(flow.reserve_critical(encoder.id, 3, 1).code == hd::quic_flow_code::blocked);
    LT_ASSERT(flow.apply({hd::quic_flow_kind::max_data, 15}));
    LT_ASSERT(flow.reserve_critical(encoder.id, 3, 3));
    LT_ASSERT(flow.reserve_critical(encoder.id, 3, 1));  // replacement returns unused credit
    LT_CHECK_EQ(flow.send_allowance(ordinary.id, 7, 8).bytes, 2U);
    flow.abandon_critical(encoder.id); LT_CHECK_EQ(flow.send_allowance(ordinary.id, 7, 8).bytes, 3U);
    LT_CHECK(flow.reserve_critical(ordinary.id, 7, 1).code == hd::quic_flow_code::stream_state_error);
LT_END_AUTO_TEST(critical_ranges_protect_connection_credit_and_retransmit)
LT_BEGIN_AUTO_TEST(qpack_accounting_suite, critical_stream_limit_and_retry_after_max_stream_data)
    hd::quic_transport_parameters p; p.initial_max_data = 10; p.initial_max_stream_data_uni = 1; p.initial_max_streams_uni = 1;
    hd::quic_flow_control flow(hd::quic_endpoint_role::server, p, p, 2, budget()); auto stream = flow.open_local(true); LT_ASSERT(stream);
    LT_CHECK(flow.reserve_critical(stream.id, 0, 2).code == hd::quic_flow_code::blocked);
    LT_CHECK_EQ(flow.send_allowance(stream.id, 0, 10).bytes, 1U);
    LT_ASSERT(flow.apply({hd::quic_flow_kind::max_stream_data, 2, stream.id})); LT_ASSERT(flow.reserve_critical(stream.id, 0, 2));
    LT_ASSERT(flow.record_stream_sent(stream.id, 0, 2, false));
    LT_CHECK(flow.send_allowance(stream.id, 2, 1).code == hd::quic_flow_code::blocked);
LT_END_AUTO_TEST(critical_stream_limit_and_retry_after_max_stream_data)
LT_BEGIN_AUTO_TEST(qpack_accounting_suite, feedback_and_encoder_ranges_survive_saturated_data_and_recovery)
    hd::quic_recovery_config config; config.max_information = 6; config.critical_information = 3;
    config.max_receive_ranges = 4; config.max_sent_packets = 4; config.max_retained_bytes = 16; config.critical_retained_bytes = 8;
    auto root = budget(); hd::quic_storage_pool pool(10000, hd::quic_recovery::storage_capacity(config) + 30000, root);
    hd::quic_recovery recovery(config, pool.data(), pool.critical());
    hd::quic_transport_parameters p; p.initial_max_data = 12; p.initial_max_stream_data_uni = 8;
    p.initial_max_stream_data_bidi_remote = 12; p.initial_max_streams_uni = 2; p.initial_max_streams_bidi = 1;
    hd::quic_flow_control flow(hd::quic_endpoint_role::server, p, p, 8, pool.critical());
    auto enc = flow.open_local(true); auto dec = flow.open_local(true); auto data = flow.open_local(false);
    LT_ASSERT(enc && dec && data);
    hd::http3_connection c(pool.data(), pool.critical(), limits()); c.attach_stream(0); c.attach_stream(2);
    LT_CHECK(!feed(c, 0, bytes({1, 3, 2, 0, 0x80})).error);
    std::array<std::byte, 8> ordinary{}; LT_ASSERT(recovery.retain_stream({data.id, 9, ordinary}));
    LT_CHECK(!feed(c, 2, bytes({2, 0x3f, 0x61, 0x41, 'x', 1, 'a'})).error); LT_ASSERT(c.event(0));
    hs::reservation saturation;
    LT_ASSERT(pool.data().budget.reserve(hs::resource::quic_reassembly_bytes,
        10000 - pool.data().budget.in_use(hs::resource::quic_reassembly_bytes), saturation).ok());
    LT_ASSERT(flow.reserve_critical(enc.id, 0, 1)); LT_ASSERT(flow.reserve_critical(dec.id, 0, 2));
    LT_ASSERT(flow.record_stream_sent(data.id, 0, 9, false));
    auto role = hd::http3_role::qpack_decoder;
    LT_ASSERT(recovery.retain_stream({dec.id, 0, c.local_prefix(role)}, hd::quic_stream_retention::critical));
    LT_ASSERT(c.advance_local_prefix(role, 1));
    LT_ASSERT(recovery.retain_stream({dec.id, 1, c.local_prefix(role)}, hd::quic_stream_retention::critical));
    LT_ASSERT(c.advance_local_prefix(role, 1));
    LT_ASSERT(recovery.retain_stream({enc.id, 0, c.local_prefix(hd::http3_role::qpack_encoder)}, hd::quic_stream_retention::critical));
    std::array<std::byte, 100> output{};
    for (unsigned i = 0; i < 3; ++i) {
        auto plan = recovery.prepare_packet(hd::quic_pn_space::application, output, {}, flow);
        LT_ASSERT(plan && plan.stream); LT_CHECK(plan.stream->stream == enc.id || plan.stream->stream == dec.id);
        LT_ASSERT(recovery.commit_sent(plan.token, {}, 20, true, true));
    }
    LT_CHECK_EQ(flow.sent(), 12U);
LT_END_AUTO_TEST(feedback_and_encoder_ranges_survive_saturated_data_and_recovery)
LT_BEGIN_AUTO_TEST(qpack_accounting_suite, outgoing_budget_and_ledger_refusal_roll_back_references)
    for (bool data_refusal : {false, true}) {
        hd::quic_storage_pool pool(10000, 30000, budget()); auto l = limits(); l.qpack_outstanding_sections = 0;
        hd::http3_connection c(pool.data(), pool.critical(), l); peer(c); LT_ASSERT(insert(c, "a"));
        std::array<hd::qpack_field_view, 1> fields{{{"x", "a"}}}; hs::reservation saturation;
        if (data_refusal) LT_ASSERT(pool.data().budget.reserve(hs::resource::quic_reassembly_bytes, 10000, saturation).ok());
        LT_CHECK(c.encode_section(0, fields).status.state == hd::qpack_state::limit_exceeded);
        saturation.release(); LT_CHECK_EQ(pool.data().budget.in_use(hs::resource::quic_reassembly_bytes), 0U);
        LT_CHECK(!feed(c, 6, bytes({1})).error);
        LT_CHECK(c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 0}).ok());
    }
LT_END_AUTO_TEST(outgoing_budget_and_ledger_refusal_roll_back_references)
LT_BEGIN_AUTO_TEST(qpack_accounting_suite, outgoing_wire_scratch_is_admitted_before_publishing_references)
    hd::quic_storage_pool pool(10000, 30000, budget()); hd::http3_connection c(pool.data(), pool.critical(), limits()); peer(c); LT_ASSERT(insert(c, "a"));
    hs::reservation saturation; LT_ASSERT(pool.data().budget.reserve(hs::resource::quic_reassembly_bytes, 9850, saturation).ok());
    std::array<hd::qpack_field_view, 1> fields{{{"x", "a"}}};
    LT_CHECK(c.encode_section(0, fields).status.state == hd::qpack_state::limit_exceeded);
    saturation.release(); LT_CHECK(!feed(c, 6, bytes({1})).error);
    LT_CHECK(c.queue_encoder_instruction({hd::qpack_encoder_instruction_kind::capacity, 0}).ok());
LT_END_AUTO_TEST(outgoing_wire_scratch_is_admitted_before_publishing_references)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
