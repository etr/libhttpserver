/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <httpserver/detail/quic_flow_control.hpp>
#include <httpserver/detail/quic_recovery.hpp>
#include "../littletest.hpp"
namespace hd = httpserver::detail;
using space = hd::quic_pn_space;
namespace {
auto budget() { return httpserver::server::resource_budget::root({}); }
constexpr std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
hd::quic_transport_parameters parameters(std::uint64_t limit) {
    hd::quic_transport_parameters p;
    p.initial_max_data = p.initial_max_stream_data_bidi_remote = limit;
    p.initial_max_streams_bidi = 2;
    return p;
}
hd::quic_frame decode(std::span<const std::byte> bytes, space s = space::application) {
    hd::quic_frame_cursor cursor;
    hd::quic_frame_context context;
    context.packet = s == space::initial ? hd::quic_packet_kind::initial : hd::quic_packet_kind::one_rtt;
    auto result = hd::next_quic_frame(bytes, cursor, context);
    if (result.code != hd::quic_codec_code::ok) throw std::runtime_error("Bad prepared frame");
    return result.value;
}
hd::quic_ack_frame ack(std::uint64_t pn) { return {pn, 0, 0, 0, {}, {}}; }
}  // namespace
LT_BEGIN_SUITE(flow_recovery_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(flow_recovery_suite)
LT_BEGIN_AUTO_TEST(flow_recovery_suite, preparation_splits_to_credit_and_commit_charges_only_successful_emission)
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(4), parameters(4), 10, budget());
    LT_ASSERT(f.open_local(false));
    hd::quic_recovery r({}, budget());
    LT_ASSERT(r.retain_stream({1, 0, data, true}));
    std::array<std::byte, 100> out{};
    auto p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p);
    auto frame = std::get<hd::quic_stream_frame>(decode(std::span(out).first(p.bytes)));
    LT_CHECK(frame.data.size() == 4 && !frame.fin && f.sent() == 0);
    LT_ASSERT(p.stream);
    LT_CHECK(p.stream->offset == 0 && p.stream->data.size() == 4);
    LT_CHECK(r.commit_sent(p.token, {}, 0, true, true).code == hd::quic_recovery_code::invalid);
    LT_CHECK(f.sent() == 0);
    LT_ASSERT(r.abandon_packet(p.token));
    p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_CHECK(f.sent() == 4);
    LT_CHECK(r.prepare_packet(space::application, out, {}, f).code == hd::quic_recovery_code::no_data);
    p = r.prepare_packet(space::application, out, {}, f, true);
    LT_ASSERT(p);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_CHECK(f.sent() == 4);
    LT_ASSERT(f.apply({hd::quic_flow_kind::max_data, 8}));
    LT_ASSERT(f.apply({hd::quic_flow_kind::max_stream_data, 8, 1}));
    p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p);
    frame = std::get<hd::quic_stream_frame>(decode(std::span(out).first(p.bytes)));
    LT_CHECK(frame.offset == 4 && frame.fin && frame.data.size() == 4);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_CHECK(f.sent() == 8);
LT_END_AUTO_TEST(preparation_splits_to_credit_and_commit_charges_only_successful_emission)
LT_BEGIN_AUTO_TEST(flow_recovery_suite, data_saturation_preserves_crypto_controls_ack_and_reset)
    hd::quic_recovery_config c;
    c.max_information = 5;
    c.critical_information = 3;
    c.max_retained_bytes = 24;
    c.critical_retained_bytes = 8;
    c.max_sent_packets = 4;
    c.critical_sent_packets = 2;
    hd::quic_recovery r(c, budget());
    LT_ASSERT(r.retain_stream({1, 0, data}));
    LT_ASSERT(r.retain_stream({5, 0, data}));
    LT_CHECK(r.retain_stream({9, 0, data}).code == hd::quic_recovery_code::capacity);
    std::array<std::byte, 100> out{};
    for (unsigned i = 0; i < 2; ++i) {
        auto p = r.prepare_packet(space::application, out, {});
        LT_ASSERT(p);
        LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    }
    LT_ASSERT(r.retain_crypto(space::application, 0, data));
    LT_ASSERT(r.retain_flow({hd::quic_flow_kind::max_data, 20}));
    LT_ASSERT(r.retain_reset({1, 9, 8}));
    auto p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_CHECK(std::holds_alternative<hd::quic_crypto_frame>(decode(std::span(out).first(p.bytes))));
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_CHECK(std::get<hd::quic_flow_frame>(decode(std::span(out).first(p.bytes))).limit == 20);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), {}));
    p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_CHECK(std::holds_alternative<hd::quic_reset_stream_frame>(decode(std::span(out).first(p.bytes))));
    LT_ASSERT(r.abandon_packet(p.token));
    LT_ASSERT(r.receive_packet(space::initial, 0, true, {}));
    p = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(p);
    LT_CHECK(!p.ack_eliciting && std::holds_alternative<hd::quic_ack_frame>(decode(std::span(out).first(p.bytes), space::initial)));
    LT_ASSERT(r.commit_sent(p.token, {}, 100, false, false));
LT_END_AUTO_TEST(data_saturation_preserves_crypto_controls_ack_and_reset)
LT_BEGIN_AUTO_TEST(flow_recovery_suite, max_snapshots_survive_loss_abandon_supersession_and_late_ack)
    hd::quic_recovery r({}, budget());
    LT_ASSERT(r.retain_flow({hd::quic_flow_kind::max_data, 10}));
    std::array<std::byte, 100> out{};
    auto p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_ASSERT(r.abandon_packet(p.token));
    auto original = r.prepare_packet(space::application, out, {});
    LT_ASSERT(original);
    LT_ASSERT(r.commit_sent(original.token, {}, 100, true, true));
    for (unsigned i = 0; i < 3; ++i) {
        p = r.reserve_packet(space::application);
        LT_ASSERT(p);
        LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    }
    LT_CHECK(r.receive_ack(space::application, ack(p.packet_number), {}).lost_bytes == 100);
    auto replacement = r.prepare_packet(space::application, out, {});
    LT_ASSERT(replacement);
    LT_CHECK(std::get<hd::quic_flow_frame>(decode(std::span(out).first(replacement.bytes))).limit == 10);
    LT_ASSERT(r.abandon_packet(replacement.token));
    auto newer = r.retain_flow({hd::quic_flow_kind::max_data, 20});
    LT_ASSERT(newer);
    LT_ASSERT(r.receive_ack(space::application, ack(original.packet_number), {}));
    p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_CHECK(std::get<hd::quic_flow_frame>(decode(std::span(out).first(p.bytes))).limit == 20);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), {}));
    LT_CHECK(r.prepare_packet(space::application, out, {}).code == hd::quic_recovery_code::no_data);
    LT_CHECK(r.retain_flow({hd::quic_flow_kind::data_blocked, 20}).code == hd::quic_recovery_code::invalid);
LT_END_AUTO_TEST(max_snapshots_survive_loss_abandon_supersession_and_late_ack)
LT_BEGIN_AUTO_TEST(flow_recovery_suite, completed_max_history_is_monotonic_across_other_admission)
    hd::quic_recovery r({}, budget());
    LT_ASSERT(r.retain_flow({hd::quic_flow_kind::max_data, 20}));
    std::array<std::byte, 64> out{};
    auto p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), {}));
    auto completion = r.take_completion();
    LT_ASSERT(completion && completion->flow);
    LT_CHECK(completion->flow->limit == 20);
    auto stream = r.retain_stream({1, 0, data});
    LT_ASSERT(stream);
    LT_ASSERT(r.cancel_information(stream.id));
    LT_ASSERT(r.retain_flow({hd::quic_flow_kind::max_data, 10}));
    LT_CHECK(r.prepare_packet(space::application, out, {}).code == hd::quic_recovery_code::no_data);
LT_END_AUTO_TEST(completed_max_history_is_monotonic_across_other_admission)
LT_BEGIN_AUTO_TEST(flow_recovery_suite, ack_work_progresses_when_pending_data_fills_output_or_packet_quota)
    hd::quic_recovery_config config;
    config.max_sent_packets = 2;
    config.critical_sent_packets = 1;
    hd::quic_recovery r(config, budget());
    LT_ASSERT(r.retain_stream({1, 0, data}));
    std::array<std::byte, 8> out{};
    LT_ASSERT(r.receive_packet(space::application, 0, true, {}));
    auto p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_CHECK(std::holds_alternative<hd::quic_ack_frame>(decode(std::span(out).first(p.bytes))));
    LT_ASSERT(r.commit_sent(p.token, {}, 100, p.ack_eliciting, p.ack_eliciting));
    p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_ASSERT(r.receive_packet(space::application, 1, true, {}));
    p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_CHECK(!p.ack_eliciting);
    LT_ASSERT(r.abandon_packet(p.token));
LT_END_AUTO_TEST(ack_work_progresses_when_pending_data_fills_output_or_packet_quota)
LT_BEGIN_AUTO_TEST(flow_recovery_suite, reset_eligibility_precedes_emission_and_retransmits_without_new_credit)
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(4), parameters(4), 8, budget());
    LT_ASSERT(f.open_local(false));
    hd::quic_recovery r({}, budget());
    auto invalid = r.retain_reset({1, 9, 4});
    LT_ASSERT(invalid);
    std::array<std::byte, 64> out{};
    LT_CHECK(r.prepare_packet(space::application, out, {}, f).code == hd::quic_recovery_code::no_data);
    LT_ASSERT(r.cancel_information(invalid.id));
    LT_ASSERT(f.record_stream_sent(1, 0, 4, false));
    LT_ASSERT(r.retain_reset({1, 9, 4}));
    auto p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p && p.reset);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_CHECK(f.sent() == 4);
    p = r.prepare_packet(space::application, out, {}, f, true);
    LT_ASSERT(p && p.reset);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_CHECK(f.sent() == 4);
LT_END_AUTO_TEST(reset_eligibility_precedes_emission_and_retransmits_without_new_credit)
LT_BEGIN_AUTO_TEST(flow_recovery_suite, semantic_consumption_queues_independent_reliable_max_generations)
    auto local = parameters(8);
    local.initial_max_stream_data_bidi_remote = 8;
    hd::quic_flow_control f(hd::quic_endpoint_role::server, local, parameters(8), 8, budget());
    LT_ASSERT(f.observe_peer(0));
    hd::quic_stream_state s(0, hd::quic_endpoint_role::server, f.ids(), {}, budget());
    LT_ASSERT(f.receive(s, hd::quic_stream_frame{0, 0, data}));
    std::array<std::byte, 8> staging{};
    LT_CHECK(s.read(staging).bytes == 8);
    LT_CHECK(f.receive(s, hd::quic_stream_frame{0, 8, std::span(data).first(1)}).code == hd::quic_flow_code::flow_control_error);
    LT_ASSERT(f.consume_body(s, 3));
    auto connection = f.pending_credit(hd::quic_flow_kind::max_data);
    auto stream = f.pending_credit(hd::quic_flow_kind::max_stream_data, 0);
    LT_ASSERT(connection && stream);
    LT_CHECK(connection->limit == 11 && stream->limit == 11);
    hd::quic_recovery r({}, budget());
    LT_ASSERT(r.retain_flow(*connection));
    LT_ASSERT(r.retain_flow(*stream));
    std::array<std::byte, 64> out{};
    auto p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p && p.flow);
    LT_ASSERT(r.abandon_packet(p.token));
    LT_CHECK(f.pending_credit(hd::quic_flow_kind::max_stream_data, 0)->limit == 11);
    p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    f.credit_emitted(*p.flow);
    LT_ASSERT(f.consume_body(s, 5));
    LT_ASSERT(r.retain_flow(*f.pending_credit(hd::quic_flow_kind::max_data)));
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), {}));
    LT_CHECK(f.pending_credit(hd::quic_flow_kind::max_data)->limit == 13);
    p = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(p && p.flow);
    LT_CHECK(p.flow->kind == hd::quic_flow_kind::max_data && p.flow->limit == 13);
LT_END_AUTO_TEST(semantic_consumption_queues_independent_reliable_max_generations)
LT_BEGIN_AUTO_TEST(flow_recovery_suite, lost_stream_extent_retransmits_at_exhausted_connection_credit)
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(4), parameters(4), 8, budget());
    LT_ASSERT(f.open_local(false));
    hd::quic_recovery r({}, budget());
    LT_ASSERT(r.retain_stream({1, 0, std::span(data).first(4), true}));
    std::array<std::byte, 64> out{};
    auto original = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(original);
    LT_ASSERT(r.commit_sent(original.token, {}, 100, true, true));
    for (unsigned i = 0; i < 3; ++i) {
        auto p = r.reserve_packet(space::application);
        LT_ASSERT(p);
        LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    }
    LT_CHECK(r.receive_ack(space::application, ack(3), {}).lost_bytes == 100);
    auto replacement = r.prepare_packet(space::application, out, {}, f);
    LT_ASSERT(replacement && replacement.stream);
    LT_CHECK(replacement.stream->offset == 0 && replacement.stream->data.size() == 4 && replacement.stream->fin);
    LT_ASSERT(r.receive_ack(space::application, ack(original.packet_number), {}));
    LT_ASSERT(r.commit_sent(replacement.token, {}, 100, true, true));
    LT_CHECK(f.sent() == 4);
LT_END_AUTO_TEST(lost_stream_extent_retransmits_at_exhausted_connection_credit)
LT_BEGIN_AUTO_TEST(flow_recovery_suite, malformed_controls_and_invalid_quota_configuration_are_bounded)
    hd::quic_recovery_config config;
    config.max_information = 2;
    config.critical_information = 2;
    hd::quic_recovery r(config, budget());
    LT_CHECK(r.retain_flow({hd::quic_flow_kind::max_stream_data, 8, 3}).code == hd::quic_recovery_code::invalid);
    LT_CHECK(r.retain_flow({hd::quic_flow_kind::max_streams_uni, (std::uint64_t{1} << 60) + 1}).code == hd::quic_recovery_code::invalid);
    LT_ASSERT(r.retain_flow({hd::quic_flow_kind::max_data, 8}));
    LT_ASSERT(r.retain_flow({hd::quic_flow_kind::max_stream_data, 8, 0}));
    LT_CHECK(r.retain_flow({hd::quic_flow_kind::max_stream_data, 8, 4}).code == hd::quic_recovery_code::capacity);
    for (unsigned i = 9; i < 100; ++i) LT_ASSERT(r.retain_flow({hd::quic_flow_kind::max_stream_data, i, 0}));
    config.critical_information = 3;
    bool invalid = false;
    try {
        hd::quic_recovery rejected(config, budget());
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    LT_CHECK(invalid);
LT_END_AUTO_TEST(malformed_controls_and_invalid_quota_configuration_are_bounded)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
