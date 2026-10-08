/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "./http3_fixture.hpp"
#include <cstdlib>
#include <new>
#include <memory>
#include <limits>
#include <httpserver/detail/quic_flow_control.hpp>
#include "./littletest.hpp"
namespace {
int allocation_countdown = -1;
}
void* operator new(std::size_t size) {
    if (allocation_countdown == 0) {
        allocation_countdown = -1; throw std::bad_alloc();
    }
    if (allocation_countdown > 0) --allocation_countdown;
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
using namespace h3test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(http3_connection_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(http3_connection_suite)
LT_BEGIN_AUTO_TEST(http3_connection_suite, critical_roles_uniqueness_and_terminal_precedence)
    for (unsigned type : {0U, 2U, 3U}) {
        hd::quic_storage_pool pool(200000, 30000, budget());
        hd::http3_connection c(pool.data(), pool.critical());
        LT_CHECK(!c.attach_stream(2)); LT_CHECK(!feed(c, 2, bytes({type})).error);
        LT_CHECK_EQ(c.terminal(2, {hd::quic_terminal_kind::eof})->wire_code, 0x104U);
        hd::http3_connection reset(pool.data(), pool.critical()); reset.attach_stream(2);
        LT_CHECK(!feed(reset, 2, bytes({type})).error);
        LT_CHECK_EQ(reset.terminal(2, {hd::quic_terminal_kind::reset})->wire_code, 0x104U);
        hd::http3_connection d(pool.data(), pool.critical()); d.attach_stream(2); d.attach_stream(6);
        feed(d, 2, bytes({type})); LT_CHECK_EQ(feed(d, 6, bytes({type})).error->wire_code, 0x103U);
    }
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2);
    LT_CHECK_EQ(feed(c, 2, bytes({1})).error->wire_code, 0x103U);
    hd::http3_connection d(pool.data(), pool.critical()); LT_CHECK_EQ(d.attach_stream(1)->wire_code, 0x103U);
LT_END_AUTO_TEST(critical_roles_uniqueness_and_terminal_precedence)
LT_BEGIN_AUTO_TEST(http3_connection_suite, control_matrix_and_reserved_frames)
    for (auto wire : {bytes({0, 0, 0}), bytes({0, 33, 0})}) {
        hd::quic_storage_pool pool(200000, 30000, budget());
        hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2);
        LT_CHECK_EQ(feed(c, 2, wire).error->wire_code, 0x10aU);
    }
    for (unsigned type : {0U, 1U, 2U, 4U, 5U, 6U, 8U, 9U}) {
        hd::quic_storage_pool pool(200000, 30000, budget());
        hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2);
        LT_CHECK_EQ(feed(c, 2, bytes({0, 4, 0, type, 0})).error->wire_code, 0x105U);
    }
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2);
    LT_CHECK(!feed(c, 2, bytes({0, 4, 0, 33, 3, 1, 2, 3, 7, 1, 0})).error);
    LT_CHECK_EQ(feed(c, 2, bytes({7, 2, 0, 0})).error->wire_code, 0x106U);
LT_END_AUTO_TEST(control_matrix_and_reserved_frames)
LT_BEGIN_AUTO_TEST(http3_connection_suite, requests_defaults_sequence_and_qpack_mapping)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(0);
    LT_CHECK(!feed(c, 0, bytes({1, 3, 0, 0, 0xd1})).error);
    LT_ASSERT(c.event(0)); LT_CHECK_EQ(c.event(0)->fields[0].value, "GET");
    auto data_wire = bytes({0, 1, 42});
    c.release_event(0); LT_CHECK(!feed(c, 0, data_wire).error);
    LT_CHECK_EQ(c.event(0)->payload_begin, 7U); c.release_event(0);
    LT_CHECK(!feed(c, 0, bytes({1, 2, 0, 0})).error); c.release_event(0);
    LT_CHECK_EQ(feed(c, 0, bytes({0, 0})).error->wire_code, 0x105U);
    for (auto wire : {bytes({0, 0}), bytes({4, 0}), bytes({2, 0}), bytes({6, 0}), bytes({8, 0}), bytes({9, 0})}) {
        hd::http3_connection d(pool.data(), pool.critical()); d.attach_stream(0);
        LT_CHECK_EQ(feed(d, 0, wire).error->wire_code, 0x105U);
    }
    hd::http3_connection d(pool.data(), pool.critical()); d.attach_stream(0);
    LT_CHECK_EQ(feed(d, 0, bytes({1, 1, 0})).error->wire_code, 0x200U);
LT_END_AUTO_TEST(requests_defaults_sequence_and_qpack_mapping)
LT_BEGIN_AUTO_TEST(http3_connection_suite, request_clean_truncation_reset_unknown_and_qpack_instructions)
    hd::quic_storage_pool pool(200000, 60000, budget());
    hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(0);
    feed(c, 0, bytes({1, 3, 0})); LT_CHECK_EQ(c.terminal(0, {hd::quic_terminal_kind::eof})->wire_code, 0x106U);
    hd::http3_connection d(pool.data(), pool.critical()); d.attach_stream(0);
    feed(d, 0, bytes({1, 3, 0})); LT_CHECK(!d.terminal(0, {hd::quic_terminal_kind::reset}));
    LT_CHECK_EQ(d.attach_stream(0)->wire_code, 0x103U);
    hd::http3_connection e(pool.data(), pool.critical()); e.attach_stream(2);
    LT_CHECK(!feed(e, 2, bytes({33, 1, 2, 3, 4})).error); LT_CHECK(!e.terminal(2, {hd::quic_terminal_kind::eof}));
    hd::http3_connection f(pool.data(), pool.critical()); f.attach_stream(2);
    LT_CHECK(!feed(f, 2, bytes({3, 0x7f, 0})).error);
    LT_CHECK_EQ(feed(f, 2, bytes({0x80})).error->wire_code, 0x202U);
    hd::http3_connection g(pool.data(), pool.critical()); g.attach_stream(2);
    LT_CHECK(!feed(g, 2, bytes({2, 0x20})).error);
    LT_CHECK_EQ(feed(g, 2, bytes({0x21})).error->wire_code, 0x201U);
LT_END_AUTO_TEST(request_clean_truncation_reset_unknown_and_qpack_instructions)
LT_BEGIN_AUTO_TEST(http3_connection_suite, stalled_stream_does_not_block_control_or_other_request)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_connection c(pool.data(), pool.critical());
    c.attach_stream(0); c.attach_stream(4); c.attach_stream(2);
    feed(c, 0, bytes({1, 2, 0, 0})); c.release_event(0);
    auto wire = bytes({0, 3, 1, 2, 3}); feed(c, 0, wire);
    LT_CHECK_EQ(feed(c, 0, bytes({0, 0})).consumed, 0U);
    LT_CHECK(!feed(c, 2, bytes({0, 4, 0})).error); LT_CHECK(c.peer_settings().received);
    LT_CHECK(!feed(c, 4, bytes({1, 3, 0, 0, 0xd1})).error); LT_ASSERT(c.event(4));
    LT_CHECK_EQ(std::to_integer<unsigned>(c.event(0)->payload[0]), 1U);
LT_END_AUTO_TEST(stalled_stream_does_not_block_control_or_other_request)
LT_BEGIN_AUTO_TEST(http3_connection_suite, reservations_descriptor_caps_and_critical_capacity)
    auto root = budget();
    {
        hd::quic_storage_pool pool(1, 30000, root);
        hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2); c.attach_stream(0);
        LT_CHECK(!feed(c, 2, bytes({0, 4, 0})).error);
        LT_CHECK_EQ(feed(c, 0, bytes({1, 2})).error->wire_code, 0x107U);
    }
    LT_CHECK_EQ(root.in_use(hs::resource::quic_reassembly_bytes), 0U);
    LT_CHECK_EQ(root.in_use(hs::resource::streams), 0U);
    hd::quic_storage_pool pool(200000, 30000, root);
    hd::http3_limits limits; limits.stream_records = 1;
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(0);
    LT_CHECK_EQ(c.attach_stream(4)->wire_code, 0x107U);
LT_END_AUTO_TEST(reservations_descriptor_caps_and_critical_capacity)
LT_BEGIN_AUTO_TEST(http3_connection_suite, allocation_refusal_is_internal_and_rolls_back)
    for (int stage = 0; stage < 2; ++stage) {
        hd::quic_storage_pool pool(200000, 30000, budget());
        hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(0);
        auto wire = bytes({1, 3, 0, 0, 0xd1});
        allocation_countdown = stage;
        auto r = feed(c, 0, wire); allocation_countdown = -1;
        LT_ASSERT(r.error); LT_CHECK_EQ(r.error->wire_code, 0x102U);
        LT_CHECK_EQ(pool.data().budget.in_use(hs::resource::quic_reassembly_bytes), 0U);
    }
LT_END_AUTO_TEST(allocation_refusal_is_internal_and_rolls_back)
LT_BEGIN_AUTO_TEST(http3_connection_suite, turn_budget_stream_types_and_local_ids)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_limits limits; limits.frames_per_turn = 1;
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(2);
    auto wire = bytes({0x40, 0, 4, 0, 33, 0});
    auto r = feed(c, 2, wire); LT_CHECK_EQ(r.consumed, 4U); LT_CHECK(r.progress == hd::http3_progress::yield);
    LT_CHECK_EQ(feed(c, 2, std::span(wire).subspan(4)).consumed, 0U);
    c.begin_turn(); LT_CHECK_EQ(feed(c, 2, std::span(wire).subspan(4)).consumed, 2U);
    LT_CHECK(!c.attach_local_stream(hd::http3_role::control, 3));
    LT_CHECK(!c.attach_local_stream(hd::http3_role::control, 3));
    LT_CHECK(!c.attach_local_stream(hd::http3_role::qpack_encoder, 7));
    LT_CHECK_EQ(c.attach_local_stream(hd::http3_role::qpack_decoder, 7)->wire_code, 0x103U);
LT_END_AUTO_TEST(turn_budget_stream_types_and_local_ids)
LT_BEGIN_AUTO_TEST(http3_connection_suite, semantic_credit_is_delayed_and_reset_settles_abandonment)
    auto root = budget(); hd::quic_storage_pool pool(200000, 60000, root);
    hd::quic_transport_parameters parameters;
    parameters.initial_max_data = 100;
    parameters.initial_max_stream_data_bidi_remote = 20;
    parameters.initial_max_streams_bidi = 2;
    hd::quic_flow_control flow(hd::quic_endpoint_role::server, parameters, parameters, 8, pool.critical());
    LT_ASSERT(flow.observe_peer(0));
    hd::quic_stream_state stream(0, hd::quic_endpoint_role::server, flow.ids(), {32, 4, 100}, pool.data());
    auto wire = bytes({1, 2, 0, 0, 0, 3, 1, 2, 3, 33, 0});
    LT_ASSERT(flow.receive(stream, hd::quic_stream_frame{0, 0, wire, false}));
    std::array<std::byte, 32> extracted{}; auto read = stream.read(extracted);
    LT_CHECK_EQ(read.bytes, wire.size());
    hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(0);
    auto r = feed(c, 0, std::span(extracted).first(read.bytes)); LT_CHECK_EQ(r.consumed, 4U);
    LT_ASSERT(flow.consume_protocol(stream, c.event(0)->payload_end)); c.release_event(0);
    r = feed(c, 0, std::span(extracted).subspan(4, read.bytes - 4));
    LT_ASSERT(c.event(0)); LT_CHECK_EQ(c.event(0)->payload_begin, 6U);
    LT_ASSERT(flow.consume_protocol(stream, c.event(0)->payload_begin));
    auto before = flow.pending_credit(hd::quic_flow_kind::max_stream_data, 0);
    LT_ASSERT(before); LT_CHECK_EQ(before->limit, 26U);
    c.release_event(0);
    LT_CHECK_EQ(flow.pending_credit(hd::quic_flow_kind::max_stream_data, 0)->limit, 26U);
    LT_ASSERT(flow.consume_body(stream, 7));
    LT_CHECK_EQ(flow.pending_credit(hd::quic_flow_kind::max_stream_data, 0)->limit, 27U);
    LT_ASSERT(flow.consume_body(stream, 9));
    LT_CHECK_EQ(flow.pending_credit(hd::quic_flow_kind::max_stream_data, 0)->limit, 29U);
    r = feed(c, 0, std::span(extracted).subspan(9, 2)); LT_CHECK_EQ(r.consumed, 2U);
    LT_ASSERT(flow.consume_protocol(stream, c.offset(0)));
    LT_CHECK_EQ(flow.pending_credit(hd::quic_flow_kind::max_stream_data, 0)->limit, 31U);
    LT_ASSERT(flow.receive(stream, hd::quic_reset_stream_frame{0, 42, 15}));
    LT_CHECK(!c.terminal(0, {hd::quic_terminal_kind::reset, 42})); LT_ASSERT(flow.settle_reset(stream));
    LT_CHECK_EQ(flow.pending_credit(hd::quic_flow_kind::max_data)->limit, 115U);
LT_END_AUTO_TEST(semantic_credit_is_delayed_and_reset_settles_abandonment)
LT_BEGIN_AUTO_TEST(http3_connection_suite, oversized_configuration_cannot_wrap_decoded_field_charge)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_limits limits; limits.headers.max_fields = std::numeric_limits<std::size_t>::max();
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(0);
    auto r = feed(c, 0, bytes({1, 3, 0, 0, 0xd1})); LT_ASSERT(r.error); LT_CHECK_EQ(r.error->wire_code, 0x107U);
LT_END_AUTO_TEST(oversized_configuration_cannot_wrap_decoded_field_charge)
LT_BEGIN_AUTO_TEST(http3_connection_suite, local_critical_terminals_are_connection_errors)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_connection c(pool.data(), pool.critical());
    LT_CHECK(!c.attach_local_stream(hd::http3_role::control, 3));
    LT_CHECK_EQ(c.terminal(3, {hd::quic_terminal_kind::reset})->wire_code, 0x104U);
LT_END_AUTO_TEST(local_critical_terminals_are_connection_errors)
LT_BEGIN_AUTO_TEST(http3_connection_suite, allocation_failure_during_budget_refusal_stays_typed)
    hd::quic_storage_pool pool(200000, 900, budget());
    hd::http3_connection c(pool.data(), pool.critical());
    allocation_countdown = 0;
    bool escaped = false;
    std::optional<hd::http3_error> error;
    try {
        error = c.attach_stream(0);
    } catch (const std::bad_alloc&) {
        escaped = true;
    }
    allocation_countdown = -1;
    LT_CHECK(!escaped); LT_ASSERT(error); LT_CHECK_EQ(error->wire_code, 0x102U);
LT_END_AUTO_TEST(allocation_failure_during_budget_refusal_stays_typed)
LT_BEGIN_AUTO_TEST(http3_connection_suite, protected_control_progress_with_data_pool_fully_reserved)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2);
    hs::reservation saturation;
    LT_ASSERT(pool.data().budget.reserve(hs::resource::quic_reassembly_bytes, 200000, saturation).ok());
    auto wire = bytes({0, 4, 4, 1, 0, 7, 0, 33, 2, 1, 2});
    for (const auto& byte : wire) {
        auto r = feed(c, 2, {&byte, 1}); LT_CHECK(!r.error); LT_CHECK_EQ(r.consumed, 1U);
    }
    LT_CHECK(c.peer_settings().received); LT_CHECK_EQ(c.peer_settings().qpack_capacity, 0U);
    LT_CHECK_EQ(pool.data().budget.in_use(hs::resource::quic_reassembly_bytes), 200000U);
LT_END_AUTO_TEST(protected_control_progress_with_data_pool_fully_reserved)
LT_BEGIN_AUTO_TEST(http3_connection_suite, client_goaway_payload_is_push_id)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2);
    LT_CHECK(!feed(c, 2, bytes({0, 4, 0, 7, 1, 3})).error);
LT_END_AUTO_TEST(client_goaway_payload_is_push_id)
LT_BEGIN_AUTO_TEST(http3_connection_suite, descriptor_allocation_rolls_back_and_lease_outlives_pool_handle)
    auto root = budget();
    for (int stage : {0, 1}) {
        hd::quic_storage_pool pool(200000, 30000, root);
        hd::http3_connection c(pool.data(), pool.critical());
        const auto before = pool.critical().budget.in_use(hs::resource::quic_reassembly_bytes);
        allocation_countdown = stage;
        auto error = c.attach_stream(0); allocation_countdown = -1;
        LT_ASSERT(error); LT_CHECK_EQ(error->wire_code, 0x102U);
        LT_CHECK_EQ(pool.critical().budget.in_use(hs::resource::quic_reassembly_bytes), before);
        LT_CHECK_EQ(pool.critical().budget.in_use(hs::resource::streams), 0U);
    }
    std::unique_ptr<hd::http3_connection> owner;
    {
        hd::quic_storage_pool pool(200000, 30000, root);
        owner = std::make_unique<hd::http3_connection>(pool.data(), pool.critical()); owner->attach_stream(0);
    }
    LT_CHECK_EQ(root.in_use(hs::resource::quic_reassembly_bytes), 230000U);
    LT_CHECK(!feed(*owner, 0, bytes({1, 2, 0, 0})).error);
    owner.reset(); LT_CHECK_EQ(root.in_use(hs::resource::quic_reassembly_bytes), 0U);
LT_END_AUTO_TEST(descriptor_allocation_rolls_back_and_lease_outlives_pool_handle)
LT_BEGIN_AUTO_TEST(http3_connection_suite, incomplete_uni_type_terminals_allow_other_stream_progress)
    for (auto prefix : {bytes({}), bytes({0x40}), bytes({0x80, 0, 0}), bytes({0xc0, 0, 0, 0, 0, 0, 0})}) {
        for (auto kind : {hd::quic_terminal_kind::eof, hd::quic_terminal_kind::reset}) {
            hd::quic_storage_pool pool(200000, 30000, budget());
            hd::http3_connection c(pool.data(), pool.critical());
            LT_ASSERT(!c.attach_stream(2));
            auto r = feed(c, 2, prefix); LT_CHECK(!r.error); LT_CHECK_EQ(r.consumed, prefix.size());

            LT_ASSERT(!c.terminal(2, {kind}));
            LT_CHECK(!c.terminal(2, {kind})); LT_CHECK(!c.event(2));
            LT_CHECK_EQ(pool.critical().budget.in_use(hs::resource::streams), 1U);
            LT_ASSERT(!c.attach_stream(6)); LT_CHECK(!feed(c, 6, bytes({0, 4, 0})).error);
            LT_CHECK(c.peer_settings().received);
            LT_ASSERT(!c.attach_stream(0)); LT_CHECK(!feed(c, 0, bytes({1, 3, 0, 0, 0xd1})).error);
            LT_ASSERT(c.event(0)); LT_CHECK_EQ(c.event(0)->fields[0].value, "GET");
            c.release_event(0); LT_CHECK(!c.terminal(0, {hd::quic_terminal_kind::eof}));
            LT_CHECK_EQ(c.attach_stream(2)->wire_code, 0x103U);
        }
    }
LT_END_AUTO_TEST(incomplete_uni_type_terminals_allow_other_stream_progress)
LT_BEGIN_AUTO_TEST(http3_connection_suite, valid_qpack_exact_expanded_and_field_limits_publish_ordered_fields)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_limits limits; limits.headers.max_expanded_bytes = 84; limits.headers.max_fields = 2;
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(0);

    auto r = feed(c, 0, bytes({1, 4, 0, 0, 0xd1, 0xd9}));
    LT_CHECK(!r.error); LT_CHECK_EQ(r.consumed, 6U); LT_ASSERT(c.event(0));
    LT_ASSERT_EQ(c.event(0)->fields.size(), 2U);
    LT_CHECK_EQ(c.event(0)->fields[0].name, ":method"); LT_CHECK_EQ(c.event(0)->fields[0].value, "GET");
    LT_CHECK_EQ(c.event(0)->fields[1].name, ":status"); LT_CHECK_EQ(c.event(0)->fields[1].value, "200");
    c.release_event(0); LT_CHECK_EQ(pool.data().budget.in_use(hs::resource::quic_reassembly_bytes), 0U);
LT_END_AUTO_TEST(valid_qpack_exact_expanded_and_field_limits_publish_ordered_fields)
LT_BEGIN_AUTO_TEST(http3_connection_suite, valid_qpack_one_over_decoded_limits_releases_all_data_storage)
    for (auto bounds : {std::pair{83U, 2U}, std::pair{84U, 1U}}) {
        hd::quic_storage_pool pool(200000, 30000, budget());
        hd::http3_limits limits; limits.headers.max_expanded_bytes = bounds.first; limits.headers.max_fields = bounds.second;
        hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(0);
        const auto before = pool.data().budget.in_use(hs::resource::quic_reassembly_bytes);

        auto r = feed(c, 0, bytes({1, 4, 0, 0, 0xd1, 0xd9}));
        LT_ASSERT(r.error); LT_CHECK_EQ(r.error->wire_code, 0x107U);
        LT_CHECK(r.error->outcome == httpserver::http::outcome_code::limit_exceeded);
        LT_CHECK(!c.event(0)); LT_CHECK_EQ(r.consumed, 6U);
        LT_CHECK_EQ(pool.data().budget.in_use(hs::resource::quic_reassembly_bytes), before);
    }
LT_END_AUTO_TEST(valid_qpack_one_over_decoded_limits_releases_all_data_storage)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
