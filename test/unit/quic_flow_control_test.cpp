/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <cstdlib>
#include <new>
#include <httpserver/detail/quic_flow_control.hpp>
#include "../littletest.hpp"
namespace {
int allocation_countdown = -1;
}
void* operator new(std::size_t size) {
    if (allocation_countdown == 0) {
        allocation_countdown = -1;
        throw std::bad_alloc();
    }
    if (allocation_countdown > 0) --allocation_countdown;
    if (auto* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
namespace hd = httpserver::detail;
namespace {
auto budget() { return httpserver::server::resource_budget::root({}); }
hd::quic_transport_parameters parameters(std::uint64_t data = 12, std::uint64_t stream = 8) {
    hd::quic_transport_parameters p;
    p.initial_max_data = data;
    p.initial_max_stream_data_bidi_local = stream;
    p.initial_max_stream_data_bidi_remote = stream;
    p.initial_max_stream_data_uni = stream;
    p.initial_max_streams_bidi = p.initial_max_streams_uni = 3;
    return p;
}
constexpr std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
}  // namespace
LT_BEGIN_SUITE(flow_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(flow_suite)
LT_BEGIN_AUTO_TEST(flow_suite, count_limits_are_cumulative_and_rejections_do_not_open_ids)
    for (auto role : {hd::quic_endpoint_role::server, hd::quic_endpoint_role::client}) {
        hd::quic_flow_control f(role, parameters(), parameters(), 12, budget());
        const auto local = role == hd::quic_endpoint_role::server ? 1U : 0U;
        const auto peer = local ^ 1;
        for (bool uni : {false, true}) {
            auto id = peer + (uni ? 2 : 0);
            LT_CHECK(f.observe_peer(id + 12).code == hd::quic_flow_code::stream_limit_error);
            LT_CHECK(!f.ids().opened(id));
            LT_ASSERT(f.observe_peer(id + 8));
            LT_CHECK(f.ids().opened_count(id) == 3);
            LT_CHECK(f.ids().opened(id));
            for (unsigned i = 0; i < 3; ++i) {
                auto opened = f.open_local(uni);
                LT_ASSERT(opened);
                LT_CHECK(opened.id == local + (uni ? 2 : 0) + 4 * i);
            }
            LT_CHECK(f.open_local(uni).code == hd::quic_flow_code::blocked);
        }
    }
LT_END_AUTO_TEST(count_limits_are_cumulative_and_rejections_do_not_open_ids)
LT_BEGIN_AUTO_TEST(flow_suite, receive_highest_offsets_share_connection_credit_transactionally)
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 10, budget());
    LT_ASSERT(f.observe_peer(4));
    hd::quic_stream_state a(0, hd::quic_endpoint_role::server, f.ids(), {}, budget());
    hd::quic_stream_state b(4, hd::quic_endpoint_role::server, f.ids(), {}, budget());
    LT_ASSERT(f.receive(a, hd::quic_stream_frame{0, 4, std::span(data).first(4)}));
    LT_CHECK(f.received() == 8);
    LT_ASSERT(f.receive(a, hd::quic_stream_frame{0, 4, std::span(data).first(4)}));
    LT_ASSERT(f.receive(a, hd::quic_stream_frame{0, 0, std::span(data).first(4)}));
    LT_CHECK(f.received() == 8);
    LT_CHECK(f.receive(b, hd::quic_stream_frame{4, 0, data}).code == hd::quic_flow_code::flow_control_error);
    LT_CHECK(b.buffered_bytes() == 0 && f.received() == 8);
    LT_ASSERT(f.receive(b, hd::quic_stream_frame{4, 0, std::span(data).first(4)}));
    LT_CHECK(f.received() == 12);
    LT_CHECK(f.receive(a, hd::quic_stream_frame{0, 8, std::span(data).first(1)}).code == hd::quic_flow_code::flow_control_error);
LT_END_AUTO_TEST(receive_highest_offsets_share_connection_credit_transactionally)
LT_BEGIN_AUTO_TEST(flow_suite, extraction_is_not_consumption_and_independent_body_releases_partial_credit)
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(16), parameters(), 10, budget());
    LT_ASSERT(f.observe_peer(4));
    hd::quic_stream_state a(0, hd::quic_endpoint_role::server, f.ids(), {}, budget());
    hd::quic_stream_state b(4, hd::quic_endpoint_role::server, f.ids(), {}, budget());
    LT_ASSERT(f.receive(a, hd::quic_stream_frame{0, 0, data}));
    LT_ASSERT(f.receive(b, hd::quic_stream_frame{4, 0, data}));
    std::array<std::byte, 8> parser{};
    LT_CHECK(b.read(parser).bytes == 8);
    LT_CHECK(!f.pending_credit());
    LT_CHECK(f.consume_body(a, 1).code == hd::quic_flow_code::invalid_consumption);
    LT_ASSERT(f.consume_protocol(b, 2));
    LT_ASSERT(f.consume_body(b, 5));
    LT_ASSERT(f.consume_body(b, 5));
    auto max = f.pending_credit();
    LT_ASSERT(max);
    LT_CHECK(max->kind == hd::quic_flow_kind::max_data && max->limit == 21);
    f.credit_emitted(*max);
    LT_CHECK(f.pending_credit()->limit == 21);
    f.credit_acknowledged(*max);
    max = f.pending_credit();
    LT_ASSERT(max);
    LT_CHECK(max->kind == hd::quic_flow_kind::max_stream_data && max->stream == 4 && max->limit == 13);
    f.credit_acknowledged(*max);
    LT_CHECK(!f.pending_credit());
    LT_ASSERT(f.receive(b, hd::quic_stream_frame{4, 8, std::span(data).first(5)}));
    LT_CHECK(a.buffered_bytes() == 8 && b.buffered_bytes() == 5);
    LT_CHECK(f.consume_protocol(b, 4).code == hd::quic_flow_code::invalid_consumption);
LT_END_AUTO_TEST(extraction_is_not_consumption_and_independent_body_releases_partial_credit)
LT_BEGIN_AUTO_TEST(flow_suite, send_limits_grow_monotonically_and_retransmission_is_free)
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(6, 4), 10, budget());
    LT_ASSERT(f.open_local(false));
    LT_ASSERT(f.open_local(false));
    LT_CHECK(f.send_allowance(1, 0, 8).bytes == 4);
    LT_ASSERT(f.record_stream_sent(1, 0, 4, false));
    LT_CHECK(f.send_allowance(5, 0, 8).bytes == 2);
    LT_ASSERT(f.record_stream_sent(5, 0, 2, false));
    LT_CHECK(f.sent() == 6);
    LT_CHECK(f.send_allowance(1, 4, 1).code == hd::quic_flow_code::blocked);
    LT_ASSERT(f.record_stream_sent(1, 0, 4, false));
    LT_CHECK(f.sent() == 6);
    LT_ASSERT(f.apply({hd::quic_flow_kind::max_data, 12}));
    LT_ASSERT(f.apply({hd::quic_flow_kind::max_data, 2}));
    LT_ASSERT(f.apply({hd::quic_flow_kind::max_stream_data, 8, 1}));
    LT_ASSERT(f.apply({hd::quic_flow_kind::max_stream_data, 1, 1}));
    LT_CHECK(f.send_allowance(1, 4, 8).bytes == 4);
    LT_CHECK(f.apply({hd::quic_flow_kind::max_stream_data, 8, 9}).code == hd::quic_flow_code::stream_state_error);
    LT_CHECK(f.apply({hd::quic_flow_kind::max_stream_data, 8, 2}).code == hd::quic_flow_code::stream_state_error);
    LT_ASSERT(f.record_stream_sent(1, 4, 4, true));
    LT_CHECK(f.record_reset_sent(1, 7).code == hd::quic_flow_code::final_size_error);
    LT_ASSERT(f.record_reset_sent(1, 8));
LT_END_AUTO_TEST(send_limits_grow_monotonically_and_retransmission_is_free)
LT_BEGIN_AUTO_TEST(flow_suite, asymmetric_parameters_choose_limits_from_each_advertisers_perspective)
    auto local = parameters(100), peer = parameters(100);
    local.initial_max_stream_data_bidi_local = 3;
    local.initial_max_stream_data_bidi_remote = 7;
    local.initial_max_stream_data_uni = 2;
    peer.initial_max_stream_data_bidi_local = 5;
    peer.initial_max_stream_data_bidi_remote = 1;
    peer.initial_max_stream_data_uni = 4;
    for (auto role : {hd::quic_endpoint_role::client, hd::quic_endpoint_role::server}) {
        hd::quic_flow_control f(role, local, peer, 8, budget());
        auto own = f.open_local(false), uni = f.open_local(true);
        LT_ASSERT(own && uni);
        const auto other = own.id ^ 1;
        LT_ASSERT(f.observe_peer(other));
        LT_ASSERT(f.observe_peer(other + 2));
        LT_CHECK(f.send_allowance(own.id, 0, 8).bytes == 1);
        LT_CHECK(f.send_allowance(other, 0, 8).bytes == 5);
        LT_CHECK(f.send_allowance(uni.id, 0, 8).bytes == 4);
        hd::quic_stream_state a(own.id, role, f.ids(), {}, budget()), b(other, role, f.ids(), {}, budget()), c(other + 2, role, f.ids(), {}, budget());
        LT_CHECK(f.receive(a, hd::quic_stream_frame{own.id, 0, std::span(data).first(4)}).code == hd::quic_flow_code::flow_control_error);
        LT_ASSERT(f.receive(a, hd::quic_stream_frame{own.id, 0, std::span(data).first(3)}));
        LT_ASSERT(f.receive(b, hd::quic_stream_frame{other, 0, std::span(data).first(7)}));
        LT_ASSERT(f.receive(c, hd::quic_stream_frame{other + 2, 0, std::span(data).first(2)}));
    }
    auto zero = parameters(0, 0);
    zero.initial_max_streams_bidi = zero.initial_max_streams_uni = 0;
    hd::quic_flow_control f(hd::quic_endpoint_role::server, zero, zero, 4, budget());
    LT_CHECK(f.open_local(false).code == hd::quic_flow_code::blocked);
    LT_CHECK(f.observe_peer(0).code == hd::quic_flow_code::stream_limit_error);
    LT_ASSERT(f.apply({hd::quic_flow_kind::max_streams_bidi, 1}));
    LT_ASSERT(f.open_local(false));
    LT_CHECK(f.send_allowance(1, 0, 1).code == hd::quic_flow_code::blocked);
    LT_ASSERT(f.record_stream_sent(1, 0, 0, true));
LT_END_AUTO_TEST(asymmetric_parameters_choose_limits_from_each_advertisers_perspective)
LT_BEGIN_AUTO_TEST(flow_suite, storage_final_size_overlap_and_allocation_refusals_preserve_ledgers)
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(100, 100), parameters(), 8, budget());
    LT_ASSERT(f.observe_peer(0));
    auto b = budget();
    hd::quic_stream_state s(0, hd::quic_endpoint_role::server, f.ids(), {4, 4, 100}, b);
    LT_ASSERT(f.receive(s, hd::quic_stream_frame{0, 0, std::span(data).first(4)}));
    const auto charge = b.in_use(httpserver::server::resource::quic_reassembly_bytes);
    LT_CHECK(f.receive(s, hd::quic_stream_frame{0, 4, std::span(data).first(4), true}).code == hd::quic_flow_code::capacity);
    LT_CHECK(f.received() == 4 && !s.final_size());
    LT_CHECK(f.receive(s, hd::quic_stream_frame{0, 1, std::span(data).first(4)}).code == hd::quic_flow_code::protocol_violation);
    LT_CHECK(f.receive(s, hd::quic_reset_stream_frame{0, 1, 3}).code == hd::quic_flow_code::final_size_error);
    LT_CHECK(f.received() == 4 && b.in_use(httpserver::server::resource::quic_reassembly_bytes) == charge);
    LT_ASSERT(f.receive(s, hd::quic_reset_stream_frame{0, 1, 8}));
    LT_CHECK(f.received() == 8 && s.buffered_bytes() == 0 && b.in_use(httpserver::server::resource::quic_reassembly_bytes) == 0);
    LT_ASSERT(f.settle_reset(s));
    LT_ASSERT(f.settle_reset(s));
    auto credit = f.pending_credit();
    LT_ASSERT(credit);
    LT_CHECK(credit->kind == hd::quic_flow_kind::max_data && credit->limit == 108);
    f.credit_acknowledged(*credit);
    LT_CHECK(!f.pending_credit());
    LT_ASSERT(f.receive(s, hd::quic_stream_frame{0, 0, data, true}));
    LT_CHECK(f.received() == 8 && s.buffered_bytes() == 0);
    LT_ASSERT(f.observe_peer(4));
    hd::quic_stream_state other(4, hd::quic_endpoint_role::server, f.ids(), {}, b);
    allocation_countdown = 0;
    LT_CHECK(f.receive(other, hd::quic_stream_frame{4, 0, data}).code == hd::quic_flow_code::no_memory);
    LT_CHECK(f.received() == 8 && other.highest_received() == 0 && b.in_use(httpserver::server::resource::quic_reassembly_bytes) == 0);
    LT_ASSERT(f.receive(other, hd::quic_stream_frame{4, 0, data}));
    auto refused = budget();
    allocation_countdown = 1;
    hd::quic_flow_control no_descriptors(hd::quic_endpoint_role::server, parameters(), parameters(), 8, refused);
    LT_CHECK(no_descriptors.open_local(false).code == hd::quic_flow_code::no_memory);
    LT_CHECK(!no_descriptors.ids().opened(1) && refused.in_use(httpserver::server::resource::quic_reassembly_bytes) == 0);
LT_END_AUTO_TEST(storage_final_size_overlap_and_allocation_refusals_preserve_ledgers)
LT_BEGIN_AUTO_TEST(flow_suite, retirement_replenishes_only_terminal_halves_with_room_for_retained_facts)
    auto p = parameters();
    p.initial_max_streams_uni = 1;
    hd::quic_flow_control f(hd::quic_endpoint_role::server, p, p, 2, budget());
    LT_ASSERT(f.observe_peer(2));
    hd::quic_stream_state s(2, hd::quic_endpoint_role::server, f.ids(), {}, budget());
    LT_CHECK(f.retire(s).code == hd::quic_flow_code::stream_state_error);
    LT_ASSERT(f.receive(s, hd::quic_reset_stream_frame{2, 1, 8}));
    LT_ASSERT(f.settle_reset(s));
    LT_ASSERT(f.retire(s));
    LT_ASSERT(f.retire(s));
    LT_CHECK(f.observe_peer(2).code == hd::quic_flow_code::stream_state_error);
    LT_ASSERT(f.observe_peer(6));
    hd::quic_stream_state second(6, hd::quic_endpoint_role::server, f.ids(), {}, budget());
    LT_ASSERT(f.receive(second, hd::quic_reset_stream_frame{6, 1, 0}));
    LT_ASSERT(f.settle_reset(second));
    LT_ASSERT(f.retire(second));
    LT_CHECK(f.observe_peer(10).code == hd::quic_flow_code::stream_limit_error);
    LT_CHECK(f.observe_peer(0).code == hd::quic_flow_code::capacity);
LT_END_AUTO_TEST(retirement_replenishes_only_terminal_halves_with_room_for_retained_facts)
LT_BEGIN_AUTO_TEST(flow_suite, retirement_rejects_recreated_objects_and_fin_bounds_send_eligibility)
    hd::quic_flow_control f(hd::quic_endpoint_role::server, parameters(), parameters(), 8, budget());
    LT_ASSERT(f.observe_peer(2));
    hd::quic_stream_state s(2, hd::quic_endpoint_role::server, f.ids(), {}, budget());
    LT_ASSERT(f.receive(s, hd::quic_reset_stream_frame{2, 1, 0}));
    LT_ASSERT(f.settle_reset(s));
    hd::quic_stream_state recreated(2, hd::quic_endpoint_role::server, f.ids(), {}, budget());
    LT_CHECK(f.retire(recreated).code == hd::quic_flow_code::stream_state_error);
    LT_ASSERT(f.retire(s));
    LT_ASSERT(f.open_local(false));
    LT_ASSERT(f.record_stream_sent(1, 0, 4, true));
    LT_CHECK(f.send_allowance(1, 4, 1).code == hd::quic_flow_code::blocked);
    LT_CHECK(f.send_allowance(1, 0, 8).bytes == 4);
LT_END_AUTO_TEST(retirement_rejects_recreated_objects_and_fin_bounds_send_eligibility)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
