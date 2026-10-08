/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <memory>
#include <vector>
#include <httpserver/detail/quic_recovery.hpp>
#include <httpserver/detail/quic_datagram_dispatch.hpp>
#include <httpserver/server/peer_policy.hpp>
#include "./littletest.hpp"
#include <httpserver/detail/quic_connection_identity.hpp>
namespace hd = httpserver::detail;
using namespace std::chrono_literals;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(identity_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(identity_suite)
namespace {
hd::quic_cid cid(unsigned n) {
    hd::quic_cid c;
    c.size = 8;
    c.bytes[0] = std::byte(n);
    return c;
}
hd::quic_path_identity path(unsigned port = 4000, const char* ip = "127.0.0.1") {
    hd::quic_path_identity p;
    p.peer.peer = {*httpserver::net::parse_address(ip), static_cast<std::uint16_t>(port)};
    p.socket_id = 1;
    return p;
}
struct sink final : hd::datagram_sink {
    unsigned packets = 0;
    void on_datagram(std::shared_ptr<const hd::io_datagram>) noexcept override { ++packets; }
};
struct fixture {
    static httpserver::server::resource_budget large_budget() {
        httpserver::server::budget_limits limits;
        limits.set(httpserver::server::resource::quic_reassembly_bytes, 4194304);
        return httpserver::server::resource_budget::root(limits);
    }
    httpserver::server::resource_budget root = large_budget();
    hd::quic_storage_pool storage{1048576, 1048576, root};
    hd::quic_recovery recovery;
    httpserver::server::peer_policy policy;
    std::shared_ptr<hd::quic_amplification_budget> budget = std::make_shared<hd::quic_amplification_budget>(path());
    unsigned random = 30;
    bool random_ok = true, fixed_token = false;
    hd::quic_connection_identity identity{cid(1),
                                          cid(2),
                                          budget,
                                          recovery,
                                          policy,
                                          [this](std::span<std::byte> out) {
                                              if (!random_ok) return false;
                                              std::fill(out.begin(), out.end(), std::byte(fixed_token && out.size() == 16 ? 99 : ++random));
                                              return true;
                                          },
                                          storage.critical()};
    explicit fixture(hd::quic_recovery_config config = {}) : recovery(config, storage.data(), storage.critical()) { budget->mark_address_validated(); }
    hd::quic_identity_result receive(hd::quic_path_identity p, std::uint64_t pn, std::span<const hd::quic_frame> frames, hd::quic_recovery::time_point now = {}, bool fresh = true,
                                     bool authenticated = true, std::size_t credit = 1200) {
        return identity.receive({p, cid(1), pn, fresh, authenticated, credit}, frames, now);
    }
};
}  // namespace
LT_BEGIN_AUTO_TEST(identity_suite, register_before_advertise_bounded_history_retirement_and_teardown)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    auto delivery = std::make_shared<sink>();
    hd::quic_datagram_dispatch dispatch(8, 4);
    fixture f;
    auto initial = dispatch.register_cid(cid(1), owner.datagrams(), delivery);
    LT_ASSERT(initial);
    LT_ASSERT(f.identity.attach_routes(dispatch, owner.datagrams(), delivery, *initial));
    LT_ASSERT(f.identity.issue_cid());
    std::array<std::byte, 1200> out{};
    auto plan = f.recovery.prepare_packet(hd::quic_pn_space::application, out, {});
    LT_ASSERT(plan);
    hd::quic_frame_cursor cursor;
    auto frame = hd::next_quic_frame(std::span(out).first(plan.bytes), cursor);
    LT_ASSERT(frame.code == hd::quic_codec_code::ok);
    auto issued = std::get<hd::quic_new_connection_id_frame>(frame.value);
    auto packet = std::make_shared<hd::io_datagram>();
    packet->bytes.push_back(std::byte{0x40});
    packet->bytes.insert(packet->bytes.end(), issued.cid.begin(), issued.cid.end());
    packet->bytes.resize(16);
    LT_CHECK(dispatch.dispatch(packet).code == hd::datagram_dispatch_code::routed);
    ex.run_pending();
    LT_CHECK_EQ(delivery->packets, 1u);
    LT_ASSERT(f.recovery.abandon_packet(plan.token));
    const std::array<hd::quic_frame, 1> retire{hd::quic_retire_connection_id_frame{1}};
    LT_ASSERT(f.receive(path(), 1, retire));
    LT_CHECK(dispatch.dispatch(packet).code == hd::datagram_dispatch_code::retired);
    const std::array<hd::quic_frame, 1> illegal{hd::quic_retire_connection_id_frame{0}};
    LT_CHECK(f.receive(path(), 2, illegal).code == hd::quic_identity_code::protocol);
    f.identity.set_peer_active_limit(4);
    f.random_ok = false;
    LT_CHECK(f.identity.issue_cid().code == hd::quic_identity_code::random_failure);
LT_END_AUTO_TEST(register_before_advertise_bounded_history_retirement_and_teardown)
LT_BEGIN_AUTO_TEST(identity_suite, peer_duplicate_conflict_sparse_sequence_active_limit_and_retirement_floor)
    fixture f;
    auto one = cid(3);
    std::array<std::byte, 16> token{std::byte{9}};
    std::array<hd::quic_frame, 1> frames{hd::quic_new_connection_id_frame{hd::k_quic_max_integer, hd::k_quic_max_integer, std::span(one.bytes).first(one.size), token}};
    LT_ASSERT(f.receive(path(), 1, frames));
    LT_ASSERT(f.receive(path(), 2, frames));
    token[0] = std::byte{10};
    LT_CHECK(f.receive(path(), 3, frames).code == hd::quic_identity_code::protocol);
    token[0] = std::byte{9};
    frames[0] = hd::quic_new_connection_id_frame{1, 0, std::span(one.bytes).first(one.size), token};
    LT_ASSERT(f.receive(path(), 4, frames));
    LT_CHECK_EQ(f.identity.active_peer_cids(), 1u);
    LT_CHECK_EQ(f.identity.retirement_floor(), hd::k_quic_max_integer);
LT_END_AUTO_TEST(peer_duplicate_conflict_sparse_sequence_active_limit_and_retirement_floor)
LT_BEGIN_AUTO_TEST(identity_suite, unsent_wrong_and_cross_arrival_response_bind_sent_path_and_withhold_data)
    fixture f;
    const std::array<hd::quic_frame, 1> trigger{hd::quic_ping_frame{}};
    auto p = path(5000);
    auto received = f.receive(p, 10, trigger);
    LT_ASSERT(received);
    LT_CHECK(!received.allow_payload);
    auto unsent = f.identity.prepare_control({}, 29);
    LT_ASSERT(unsent);
    auto challenge = unsent->data;
    LT_ASSERT(f.identity.complete_control(unsent->token, hd::quic_identity_emission::unsent, {}));
    std::array<hd::quic_frame, 1> response{hd::quic_path_frame{true, challenge}};
    LT_ASSERT(f.receive(path(), 11, response));
    LT_CHECK(f.identity.application_path() == path());
    auto sent = f.identity.prepare_control({}, 29);
    LT_ASSERT(sent);
    challenge = sent->data;
    LT_ASSERT(f.identity.complete_control(sent->token, hd::quic_identity_emission::sent, {}));
    response[0] = hd::quic_path_frame{true, challenge};
    auto cross = f.receive(path(6000), 12, response);
    LT_ASSERT(cross);
    LT_CHECK(cross.activated);
    LT_CHECK(f.identity.application_path() == p);
    LT_CHECK(!cross.allow_payload);
LT_END_AUTO_TEST(unsent_wrong_and_cross_arrival_response_bind_sent_path_and_withhold_data)
LT_BEGIN_AUTO_TEST(identity_suite, live_policy_and_highest_nonprobing_intent_gate_activation)
    for (bool deny : {false, true}) {
        fixture f;
        auto p = path(5000, "127.0.0.2");
        const std::array<hd::quic_frame, 1> trigger{hd::quic_ping_frame{}};
        LT_ASSERT(f.receive(p, 10, trigger));
        auto sent = f.identity.prepare_control({}, 29);
        LT_ASSERT(sent);
        LT_ASSERT(f.identity.complete_control(sent->token, hd::quic_identity_emission::sent, {}));
        if (deny) {
            LT_ASSERT(f.policy.deny("127.0.0.2").ok());
        } else {
            LT_ASSERT(f.receive(path(), 11, trigger));
        }
        const std::array<hd::quic_frame, 1> response{hd::quic_path_frame{true, sent->data}};
        auto result = f.receive(p, 12, response);
        LT_CHECK(!result.activated);
        LT_CHECK(f.identity.application_path() == path());
    }
LT_END_AUTO_TEST(live_policy_and_highest_nonprobing_intent_gate_activation)
LT_BEGIN_AUTO_TEST(identity_suite, authentication_freshness_probing_and_candidate_capacity_cannot_select_output)
    fixture f;
    std::array<std::byte, 8> bytes{std::byte{9}};
    const std::array<hd::quic_frame, 1> challenge{hd::quic_path_frame{false, bytes}};
    LT_CHECK(!f.receive(path(5000), 1, challenge, {}, true, false).allow_payload);
    LT_CHECK(!f.receive(path(5000), 1, challenge, {}, false).allow_payload);
    LT_CHECK_EQ(f.identity.path_count(), 1u);
    for (unsigned port = 5000; port < 5020; ++port) f.receive(path(port), port, challenge);
    LT_CHECK(f.identity.path_count() <= 3u);
    LT_CHECK(f.identity.application_path() == path());
LT_END_AUTO_TEST(authentication_freshness_probing_and_candidate_capacity_cannot_select_output)
LT_BEGIN_AUTO_TEST(identity_suite, padded_probe_independent_credit_retry_and_one_shot_response)
    fixture f;
    std::array<std::byte, 8> bytes{std::byte{9}};
    const std::array<hd::quic_frame, 1> challenge{hd::quic_path_frame{false, bytes}};
    LT_ASSERT(f.receive(path(5000), 1, challenge, {}, true, true, 400));
    auto response = f.identity.prepare_control({}, 29);
    LT_ASSERT(response);
    LT_CHECK(response->response);
    LT_CHECK_EQ(response->wire_bytes, 1200u);
    LT_CHECK(response->budget != f.budget);
    LT_CHECK_EQ(response->budget->debited(), 1200u);
    LT_ASSERT(f.identity.complete_control(response->token, hd::quic_identity_emission::ambiguous, {}));
    LT_CHECK_EQ(response->budget->debited(), 1200u);
    auto next = f.identity.prepare_control({}, 29);
    LT_CHECK(!next);
    LT_CHECK_EQ(f.budget->debited(), 0u);
LT_END_AUTO_TEST(padded_probe_independent_credit_retry_and_one_shot_response)
LT_BEGIN_AUTO_TEST(identity_suite, acknowledged_retirements_release_bounded_slots_for_later_sparse_ids)
    fixture f;
    std::array<std::byte, 1200> out{};
    for (unsigned seq = 1; seq < 16; ++seq) {
        auto c = cid(seq + 40);
        std::array<std::byte, 16> token{std::byte(seq)};
        const std::array<hd::quic_frame, 1> frames{hd::quic_new_connection_id_frame{seq, seq, std::span(c.bytes).first(c.size), token}};
        LT_ASSERT(f.receive(path(), seq, frames));
        auto send = f.recovery.prepare_packet(hd::quic_pn_space::application, out, {});
        LT_ASSERT(send);
        LT_ASSERT(f.recovery.commit_sent(send.token, {}, 100, true, true));
        LT_ASSERT(f.recovery.receive_ack(hd::quic_pn_space::application, {send.packet_number, 0, 0, 0, {}, {}}, {}));
        while (auto done = f.recovery.take_completion()) f.identity.information_completed(*done);
    }
    LT_CHECK_EQ(f.identity.active_peer_cids(), 1u);
LT_END_AUTO_TEST(acknowledged_retirements_release_bounded_slots_for_later_sparse_ids)
LT_BEGIN_AUTO_TEST(identity_suite, pending_retirement_exhaustion_is_explicit_and_not_unbounded)
    fixture f;
    for (unsigned seq = 1; seq <= 8; ++seq) {
        auto c = cid(seq + 40);
        std::array<std::byte, 16> token{std::byte(seq)};
        const std::array<hd::quic_frame, 1> frames{hd::quic_new_connection_id_frame{seq, seq, std::span(c.bytes).first(c.size), token}};
        LT_ASSERT(f.receive(path(), seq, frames));
    }
    auto c = cid(99);
    std::array<std::byte, 16> token{std::byte{99}};
    const std::array<hd::quic_frame, 1> frames{hd::quic_new_connection_id_frame{99, 99, std::span(c.bytes).first(c.size), token}};
    LT_CHECK(f.receive(path(), 99, frames).code == hd::quic_identity_code::capacity);
LT_END_AUTO_TEST(pending_retirement_exhaustion_is_explicit_and_not_unbounded)
LT_BEGIN_AUTO_TEST(identity_suite, local_cumulative_history_stops_issuance_without_reusing_or_leaking_routes)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    auto delivery = std::make_shared<sink>();
    hd::quic_datagram_dispatch dispatch(8);
    fixture f;
    auto initial = dispatch.register_cid(cid(1), owner.datagrams(), delivery);
    LT_ASSERT(initial);
    LT_ASSERT(f.identity.attach_routes(dispatch, owner.datagrams(), delivery, *initial));
    LT_ASSERT(f.identity.issue_cid());
    for (unsigned seq = 1; seq < hd::quic_connection_identity::max_cid_history; ++seq) {
        const std::array<hd::quic_frame, 1> frame{hd::quic_retire_connection_id_frame{seq}};
        LT_ASSERT(f.receive(path(), seq, frame));
    }
    LT_CHECK(f.identity.issue_cid().code == hd::quic_identity_code::capacity);
LT_END_AUTO_TEST(local_cumulative_history_stops_issuance_without_reusing_or_leaking_routes)
LT_BEGIN_AUTO_TEST(identity_suite, path_timeouts_space_reclamation_and_retry_never_activate_abandoned_candidate)
    fixture f;
    const std::array<hd::quic_frame, 1> trigger{hd::quic_ping_frame{}};
    LT_ASSERT(f.receive(path(5000), 1, trigger));
    auto first = f.identity.prepare_control({}, 29);
    LT_ASSERT(first);
    LT_ASSERT(f.identity.complete_control(first->token, hd::quic_identity_emission::sent, {}));
    // The old validated path is probed too, but the candidate retry is PTO spaced.
    auto old = f.identity.prepare_control({}, 29);
    LT_ASSERT(old);
    LT_CHECK(old->path == path());
    LT_ASSERT(f.identity.complete_control(old->token, hd::quic_identity_emission::sent, {}));
    LT_CHECK(!f.identity.prepare_control(500ms + hd::quic_recovery::time_point {}, 29));
    auto retry = f.identity.prepare_control(1100ms + hd::quic_recovery::time_point {}, 29);
    LT_ASSERT(retry);
    LT_CHECK(retry->data != first->data);
    LT_ASSERT(f.identity.complete_control(retry->token, hd::quic_identity_emission::sent, 1100ms + hd::quic_recovery::time_point {}));
    f.identity.expire(4s + hd::quic_recovery::time_point {});
    LT_CHECK_EQ(f.identity.path_count(), 1u);
    LT_CHECK(f.identity.application_path() == path());
    const std::array<hd::quic_frame, 1> late{hd::quic_path_frame{true, retry->data}};
    auto result = f.receive(path(), 2, late, 4s + hd::quic_recovery::time_point {});
    LT_CHECK(!result.activated);
LT_END_AUTO_TEST(path_timeouts_space_reclamation_and_retry_never_activate_abandoned_candidate)
LT_BEGIN_AUTO_TEST(identity_suite, retire_prior_to_is_applied_across_entire_packet_independent_of_frame_order)
    for (bool reverse : {false, true}) {
        fixture f;
        std::array<std::byte, 16> token{};
        for (unsigned seq = 1; seq < 4; ++seq) {
            auto c = cid(seq + 40);
            token[0] = std::byte(seq);
            const std::array<hd::quic_frame, 1> frame{
            hd::quic_new_connection_id_frame{seq, 0, std::span(c.bytes).first(c.size), token}};
            LT_ASSERT(f.receive(path(), seq, frame));
        }
        auto a = cid(90), b = cid(91);
        std::array<std::byte, 16> at{std::byte{90}}, bt{std::byte{91}};
        std::array<hd::quic_frame, 2> frames{hd::quic_new_connection_id_frame{4, 0, std::span(a.bytes).first(a.size), at},
            hd::quic_new_connection_id_frame{5, 3, std::span(b.bytes).first(b.size), bt}};
        if (reverse) std::reverse(frames.begin(), frames.end());
        LT_ASSERT(f.receive(path(), 10, frames));
        LT_CHECK_EQ(f.identity.active_peer_cids(), 3u);
        LT_CHECK_EQ(f.identity.retirement_floor(), 3u);
    }
LT_END_AUTO_TEST(retire_prior_to_is_applied_across_entire_packet_independent_of_frame_order)
LT_BEGIN_AUTO_TEST(identity_suite, retired_reset_token_history_cannot_be_reused_for_another_issued_id)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    auto delivery = std::make_shared<sink>();
    hd::quic_datagram_dispatch dispatch(8);
    fixture f;
    f.fixed_token = true;
    auto initial = dispatch.register_cid(cid(1), owner.datagrams(), delivery);
    LT_ASSERT(initial);
    LT_ASSERT(f.identity.attach_routes(dispatch, owner.datagrams(), delivery, *initial));
    LT_ASSERT(f.identity.issue_cid());
    const std::array<hd::quic_frame, 1> retire{hd::quic_retire_connection_id_frame{1}};
    LT_ASSERT(f.receive(path(), 1, retire));
    LT_CHECK(f.identity.issue_cid().code == hd::quic_identity_code::capacity);
LT_END_AUTO_TEST(retired_reset_token_history_cannot_be_reused_for_another_issued_id)
LT_BEGIN_AUTO_TEST(identity_suite, path_packet_padding_uses_selected_destination_cid_width)
    fixture f;
    auto short_cid = cid(9);
    short_cid.size = 3;
    std::array<std::byte, 16> token{std::byte{9}};
    const std::array<hd::quic_frame, 2> frames{hd::quic_new_connection_id_frame{1, 0, std::span(short_cid.bytes).first(short_cid.size), token}, hd::quic_ping_frame{}};
    LT_ASSERT(f.receive(path(5000), 1, frames));
    auto plan = f.identity.prepare_control({}, 21);
    LT_ASSERT(plan);
    LT_CHECK_EQ(plan->destination.size, 3u);
    LT_CHECK_EQ(plan->packet.bytes + plan->destination.size + 21, plan->wire_bytes);
    LT_ASSERT(f.identity.complete_control(plan->token, hd::quic_identity_emission::unsent, {}));
LT_END_AUTO_TEST(path_packet_padding_uses_selected_destination_cid_width)
LT_BEGIN_AUTO_TEST(identity_suite, unpadded_reachability_waits_for_padded_mtu_probe_before_activation)
    fixture f;
    const std::array<hd::quic_frame, 1> trigger{hd::quic_ping_frame{}};
    LT_ASSERT(f.receive(path(5000), 1, trigger, {}, true, true, 13));
    auto small = f.identity.prepare_control({}, 21);
    LT_ASSERT(small);
    LT_CHECK(small->wire_bytes < 1200);
    LT_ASSERT(f.identity.complete_control(small->token, hd::quic_identity_emission::sent, {}));
    const std::array<hd::quic_frame, 1> answer{hd::quic_path_frame{true, small->data}};
    auto reached = f.receive(path(5000), 2, answer, {}, true, true, 400);
    LT_CHECK(!reached.activated);
    LT_CHECK(f.identity.application_path() == path());
    auto padded = f.identity.prepare_control({}, 21);
    LT_ASSERT(padded);
    LT_CHECK_EQ(padded->wire_bytes, 1200u);
    LT_ASSERT(f.identity.complete_control(padded->token, hd::quic_identity_emission::sent, {}));
    const std::array<hd::quic_frame, 1> verified{hd::quic_path_frame{true, padded->data}};
    LT_CHECK(f.receive(path(5000), 3, verified).activated);
LT_END_AUTO_TEST(unpadded_reachability_waits_for_padded_mtu_probe_before_activation)
LT_BEGIN_AUTO_TEST(identity_suite, backoff_pto_prevents_early_candidate_retry_even_with_available_credit)
    fixture f;
    hd::quic_recovery_environment env;
    env.handshake_confirmed = true;
    f.recovery.set_environment(env, {});
    auto ping = f.recovery.reserve_packet(hd::quic_pn_space::application);
    LT_ASSERT(ping);
    LT_ASSERT(f.recovery.commit_sent(ping.token, {}, 100, true, true));
    for (auto seconds : {2, 6, 14, 30}) f.recovery.expire(hd::quic_recovery::time_point {} + std::chrono::seconds(seconds));
    LT_CHECK(f.recovery.pto_count() >= 3);
    const auto now = hd::quic_recovery::time_point {} + 30s;
    const std::array<hd::quic_frame, 1> trigger{hd::quic_ping_frame{}};
    LT_ASSERT(f.receive(path(5000), 1, trigger, now));
    auto first = f.identity.prepare_control(now, 21);
    LT_ASSERT(first);
    LT_ASSERT(f.identity.complete_control(first->token, hd::quic_identity_emission::sent, now));
    auto old = f.identity.prepare_control(now, 21);
    LT_ASSERT(old);
    LT_ASSERT(f.identity.complete_control(old->token, hd::quic_identity_emission::sent, now));
    LT_CHECK(!f.identity.prepare_control(now + 5s, 21));
LT_END_AUTO_TEST(backoff_pto_prevents_early_candidate_retry_even_with_available_credit)
LT_BEGIN_AUTO_TEST(identity_suite, application_preparation_pins_identity_until_actual_completion)
    fixture f;
    auto pending = f.recovery.reserve_packet(hd::quic_pn_space::application);
    LT_ASSERT(pending);
    const std::array<hd::quic_frame, 1> trigger{hd::quic_ping_frame{}};
    LT_CHECK(f.receive(path(5000), 1, trigger).code == hd::quic_identity_code::busy);
    LT_CHECK_EQ(f.identity.path_count(), 1u);
    LT_ASSERT(f.recovery.abandon_packet(pending.token));
    LT_ASSERT(f.receive(path(5000), 1, trigger));
LT_END_AUTO_TEST(application_preparation_pins_identity_until_actual_completion)
LT_BEGIN_AUTO_TEST(identity_suite, previous_active_path_is_reclaimed_after_bounded_reprobe_window)
    fixture f;
    const std::array<hd::quic_frame, 1> trigger{hd::quic_ping_frame{}};
    LT_ASSERT(f.receive(path(5000), 1, trigger));
    auto sent = f.identity.prepare_control({}, 21); LT_ASSERT(sent);
    LT_ASSERT(f.identity.complete_control(sent->token, hd::quic_identity_emission::sent, {}));
    const std::array<hd::quic_frame, 1> response{hd::quic_path_frame{true, sent->data}};
    LT_CHECK(f.receive(path(5000), 2, response).activated);
    f.identity.expire(4s + hd::quic_recovery::time_point {});
    LT_CHECK_EQ(f.identity.path_count(), 1u);
    LT_CHECK(f.identity.application_path() == path(5000));
    LT_ASSERT(f.receive(path(6000), 3, trigger, 4s + hd::quic_recovery::time_point {}));
LT_END_AUTO_TEST(previous_active_path_is_reclaimed_after_bounded_reprobe_window)
LT_BEGIN_AUTO_TEST(identity_suite, retained_peer_history_rejects_reusing_retired_cid_or_token_under_new_sequence)
    for (bool reuse_token : {false, true}) {
        fixture f;
        auto a = cid(9), b = cid(10), c = cid(11);
        std::array<std::byte, 16> at{std::byte{9}}, bt{std::byte{10}}, ct{std::byte{11}};
        const std::array<hd::quic_frame, 1> first{hd::quic_new_connection_id_frame{1, 1, std::span(a.bytes).first(a.size), at}};
        const std::array<hd::quic_frame, 1> second{hd::quic_new_connection_id_frame{2, 2, std::span(b.bytes).first(b.size), bt}};
        LT_ASSERT(f.receive(path(), 1, first)); LT_ASSERT(f.receive(path(), 2, second));
        const auto next_cid = reuse_token ? c : a;
        const auto next_token = reuse_token ? at : ct;
        const std::array<hd::quic_frame, 1> reused{hd::quic_new_connection_id_frame{3, 2, std::span(next_cid.bytes).first(next_cid.size), next_token}};
        LT_CHECK(f.receive(path(), 3, reused).code == hd::quic_identity_code::protocol);
    }
LT_END_AUTO_TEST(retained_peer_history_rejects_reusing_retired_cid_or_token_under_new_sequence)
LT_BEGIN_AUTO_TEST(identity_suite, unpublished_routes_roll_back_on_retention_and_listener_exhaustion)
    for (bool full_listener : {false, true}) {
        httpserver::manual_executor ex; hd::io_connection_owner owner(ex);
        auto delivery = std::make_shared<sink>(); hd::quic_datagram_dispatch dispatch(8, full_listener ? 1 : 4);
        hd::quic_recovery_config config;
        if (!full_listener) config.max_information = 0;
        fixture f(config);
        auto initial = dispatch.register_cid(cid(1), owner.datagrams(), delivery); LT_ASSERT(initial);
        LT_ASSERT(f.identity.attach_routes(dispatch, owner.datagrams(), delivery, *initial));
        const auto start = f.random;
        LT_CHECK(f.identity.issue_cid().code == hd::quic_identity_code::capacity);
        LT_CHECK(f.random <= start + 8);
        auto packet = std::make_shared<hd::io_datagram>(); packet->bytes.assign(16, std::byte{}); packet->bytes[0] = std::byte{0x40};
        std::fill(packet->bytes.begin() + 1, packet->bytes.begin() + 9, std::byte{31});
        const auto code = f.identity.issue_cid().code;
        LT_CHECK(code == hd::quic_identity_code::capacity);
        LT_CHECK(dispatch.dispatch(packet).code != hd::datagram_dispatch_code::routed);
        const std::array<hd::quic_frame, 1> unissued{hd::quic_retire_connection_id_frame{hd::k_quic_max_integer}};
        LT_CHECK(f.receive(path(), 1, unissued).code == hd::quic_identity_code::protocol);
    }
LT_END_AUTO_TEST(unpublished_routes_roll_back_on_retention_and_listener_exhaustion)
LT_BEGIN_AUTO_TEST(identity_suite, canonical_socket_identity_requires_an_unused_peer_cid_and_separate_budget)
    fixture f;
    auto changed = path(5000); changed.socket_id = 2;
    const std::array<hd::quic_frame, 1> trigger{hd::quic_ping_frame{}};
    LT_CHECK(f.receive(changed, 1, trigger).code == hd::quic_identity_code::dropped); LT_CHECK_EQ(f.identity.path_count(), 1u);
    auto fresh = cid(9); std::array<std::byte, 16> token{std::byte{9}};
    const std::array<hd::quic_frame, 2> frames{hd::quic_new_connection_id_frame{1, 0, std::span(fresh.bytes).first(fresh.size), token}, hd::quic_ping_frame{}};
    LT_ASSERT(f.receive(changed, 2, frames));
    auto plan = f.identity.prepare_control({}, 21); LT_ASSERT(plan);
    LT_CHECK(plan->path == changed && plan->destination == fresh);
    LT_CHECK(plan->budget != f.budget);
    LT_ASSERT(f.identity.complete_control(plan->token, hd::quic_identity_emission::unsent, {}));
LT_END_AUTO_TEST(canonical_socket_identity_requires_an_unused_peer_cid_and_separate_budget)
LT_BEGIN_AUTO_TEST(identity_suite, peer_retirement_discards_candidate_using_retired_cid_without_reusing_active_cid)
    fixture f;
    auto changed = path(5000); changed.socket_id = 2;
    auto a = cid(9), b = cid(10); std::array<std::byte, 16> at{std::byte{9}}, bt{std::byte{10}};
    const std::array<hd::quic_frame, 2> discovered{hd::quic_new_connection_id_frame{1, 0, std::span(a.bytes).first(a.size), at}, hd::quic_ping_frame{}};
    LT_ASSERT(f.receive(changed, 1, discovered));
    auto probe = f.identity.prepare_control({}, 21); LT_ASSERT(probe);
    LT_ASSERT(f.identity.complete_control(probe->token, hd::quic_identity_emission::sent, {}));
    const std::array<hd::quic_frame, 1> retired{hd::quic_new_connection_id_frame{2, 2, std::span(b.bytes).first(b.size), bt}};
    LT_ASSERT(f.receive(path(), 2, retired));
    LT_CHECK_EQ(f.identity.path_count(), 1u); LT_CHECK(f.identity.application_cid() == b);
    const std::array<hd::quic_frame, 1> late{hd::quic_path_frame{true, probe->data}};
    LT_CHECK(!f.receive(path(), 3, late).activated);
LT_END_AUTO_TEST(peer_retirement_discards_candidate_using_retired_cid_without_reusing_active_cid)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
