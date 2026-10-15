/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string>
#include <limits>
#include <vector>
#include <httpserver/detail/http2_connection.hpp>
#include <httpserver/detail/protocol_limits.hpp>
#include "./http3_fixture.hpp"
#include "./littletest.hpp"

namespace {
namespace hd = httpserver::detail;
namespace srv = httpserver::server;
std::vector<std::uint8_t> settings(std::uint32_t capacity) {
    return {0,
            0,
            6,
            4,
            0,
            0,
            0,
            0,
            0,
            0,
            1,
            static_cast<std::uint8_t>(capacity >> 24),
            static_cast<std::uint8_t>(capacity >> 16),
            static_cast<std::uint8_t>(capacity >> 8),
            static_cast<std::uint8_t>(capacity)};
}
void receive(hd::http2_connection& connection, std::span<const std::uint8_t> input) {
    while (!input.empty()) {
        auto result = connection.feed(input);
        if (result.error || !result.consumed)
            throw std::runtime_error("HTTP/2 feed failed");
        input = input.subspan(result.consumed);
        if (result.progress == hd::http2_progress::frame_ready)
            connection.release_frame();
    }
}
}  // namespace
LT_BEGIN_SUITE(protocol_compression)
void set_up() {}
void tear_down() {}
LT_END_SUITE(protocol_compression)
LT_BEGIN_AUTO_TEST(protocol_compression, hpack_local_ceiling_grows_only_after_peer_permission)
hd::http2_limits limits;
limits.encoder_table_bytes = 16384;
hd::http2_connection connection(srv::resource_budget::root({}), limits);
const char magic[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
receive(connection, {reinterpret_cast<const std::uint8_t*>(magic), sizeof(magic) - 1});
receive(connection, settings(8192));
LT_CHECK_EQ(connection.compression().encoder().table().capacity(), 8192u);
receive(connection, settings(32768));
LT_CHECK_EQ(connection.compression().encoder().table().capacity(), 16384u);
receive(connection, settings(64));
LT_CHECK_EQ(connection.compression().encoder().table().capacity(), 64u);
receive(connection, settings(12000));
LT_CHECK_EQ(connection.compression().encoder().table().capacity(), 12000u);
auto encoded = connection.compression().encoder().encode({}, {65536, 65536, 256});
LT_ASSERT(encoded.status.ok());
hd::hpack_decoder decoder(srv::resource_budget::root({}));
LT_ASSERT(decoder.acknowledge_maximum(16384).ok());
LT_CHECK(decoder.decode(hd::hpack_octets(encoded.value), {65536, 65536, 256}).status.ok());
LT_CHECK_EQ(decoder.table().capacity(), 12000u);
LT_END_AUTO_TEST(hpack_local_ceiling_grows_only_after_peer_permission)
LT_BEGIN_AUTO_TEST(protocol_compression, hpack_zero_encoder_emits_conforming_update)
hd::http2_limits limits;
limits.encoder_table_bytes = 0;
hd::http2_connection connection(srv::resource_budget::root({}), limits);
auto encoded = connection.compression().encoder().encode({}, {100, 100, 1});
LT_ASSERT(encoded.status.ok());
LT_CHECK_EQ(encoded.value, std::string(1, '\x20'));
LT_END_AUTO_TEST(hpack_zero_encoder_emits_conforming_update)
LT_BEGIN_AUTO_TEST(protocol_compression, qpack_zero_decoder_positive_encoder_uses_charged_workspace)
auto root = srv::resource_budget::root({});
hd::quic_storage_pool pool(200000, 100000, root);
hd::http3_limits limits;
limits.qpack_capacity = 0;
limits.qpack_encoder_capacity = 8192;
hd::http3_connection connection(pool.data(), pool.critical(), limits);
LT_ASSERT(!connection.attach_stream(2));
const auto wire = h3test::bytes({0, 4, 5, 1, 0x60, 0x00, 7, 1});  // Peer maximum8192, one blocked stream.
LT_ASSERT(!h3test::feed(connection, 2, wire).error);
LT_CHECK_EQ(root.in_use(srv::resource::qpack_table_bytes), 8192u);
LT_ASSERT(!connection.attach_stream(0));
const hd::qpack_field_view fields[] = {{":status", "200"}, {"x-private-capacity", "yes"}};
auto section = connection.encode_response_section(0, fields);
LT_ASSERT(section.status.ok());
LT_CHECK(pool.critical().budget.in_use(srv::resource::quic_reassembly_bytes) >= 2 * limits.qpack_output_bytes);
LT_ASSERT(!connection.attach_local_stream(hd::http3_role::qpack_encoder, 7));
auto prefix = connection.local_prefix(hd::http3_role::qpack_encoder);
LT_ASSERT(!prefix.empty());
LT_ASSERT(connection.advance_local_prefix(hd::http3_role::qpack_encoder, prefix.size()));
LT_CHECK(!connection.local_prefix(hd::http3_role::qpack_encoder).empty());
LT_END_AUTO_TEST(qpack_zero_decoder_positive_encoder_uses_charged_workspace)
LT_BEGIN_AUTO_TEST(protocol_compression, qpack_both_zero_encodes_static_without_dynamic_instructions)
auto root = srv::resource_budget::root({});
hd::quic_storage_pool pool(200000, 100000, root);
hd::http3_limits limits;
limits.qpack_encoder_capacity = 0;
hd::http3_connection connection(pool.data(), pool.critical(), limits);
LT_ASSERT(!connection.attach_stream(2));
LT_ASSERT(!h3test::feed(connection, 2, h3test::bytes({0, 4, 5, 1, 0x60, 0x00, 7, 1})).error);
LT_ASSERT(!connection.attach_stream(0));
LT_ASSERT(!h3test::feed(connection, 0, h3test::bytes({1, 3, 0, 0, 0xd1})).error);
LT_ASSERT(connection.event(0));
connection.release_event(0);
const hd::qpack_field_view fields[] = {{":status", "200"}, {"x-private-capacity", "yes"}};
auto section = connection.encode_response_section(0, fields);
LT_CHECK(section.status.ok());
LT_CHECK_EQ(root.in_use(srv::resource::qpack_table_bytes), 0u);
LT_ASSERT(!connection.attach_local_stream(hd::http3_role::qpack_encoder, 7));
LT_ASSERT(connection.advance_local_prefix(hd::http3_role::qpack_encoder, 1));
LT_CHECK(connection.local_prefix(hd::http3_role::qpack_encoder).empty());
LT_END_AUTO_TEST(qpack_both_zero_encodes_static_without_dynamic_instructions)
LT_BEGIN_AUTO_TEST(protocol_compression, hpack_reduced_decoder_enforcement_waits_for_settings_ack)
hd::http2_settings initial;
initial.header_table_size = 0;
hd::http2_connection connection(srv::resource_budget::root({}), {}, initial);
const char magic[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
receive(connection, {reinterpret_cast<const std::uint8_t*>(magic), sizeof(magic) - 1});
receive(connection, settings(4096));
auto& decoder = connection.compression().decoder();
LT_CHECK_EQ(decoder.acknowledged_maximum(), 4096u);
const std::uint8_t literal[] = {0x40, 1, 'x', 1, 'y'};
LT_ASSERT(decoder.decode(literal, {100, 100, 4}).status.ok());
for (;;) {
    auto output = connection.output();
    if (output.empty())
        break;
    LT_ASSERT(connection.advance_output(output.size()));
}
const std::uint8_t ack[] = {0, 0, 0, 4, 1, 0, 0, 0, 0};
receive(connection, ack);
LT_CHECK_EQ(decoder.acknowledged_maximum(), 0u);
const std::uint8_t missing_update[] = {0x82};
LT_CHECK(!decoder.decode(missing_update, {100, 100, 4}).status.ok());
LT_END_AUTO_TEST(hpack_reduced_decoder_enforcement_waits_for_settings_ack)
LT_BEGIN_AUTO_TEST(protocol_compression, qpack_multiple_connections_compete_without_changing_resolved_policy)
srv::server_options options;
options.add_listener({"127.0.0.1", 0, true, {}, srv::listener_transport::udp});
options.tls().provider = srv::tls_provider::system_default;
options.tls().profile = srv::tls_profile::certificates;
options.protocols().enable(httpserver::http::protocol::http_3);
options.budgets().set(srv::resource::quic_reassembly_bytes, 16 * 1024 * 1024);
options.budgets().set(srv::resource::qpack_table_bytes, 8192);
options.http3().emplace();
hd::resolved_protocol_plan plan;
LT_ASSERT(hd::resolve_protocol_policy(options, plan).ok());
const auto root = srv::resource_budget::root(options.budgets());
hd::quic_storage_pool first(plan.data_bytes, plan.critical_bytes, root), second(plan.data_bytes, plan.critical_bytes, root);
hd::http3_connection connection(first.data(), first.critical(), plan.http3.framing);
LT_ASSERT(!connection.attach_stream(2));
LT_ASSERT(!h3test::feed(connection, 2, h3test::bytes({0, 4, 5, 1, 0x50, 0, 7, 1})).error);
LT_CHECK_EQ(root.in_use(srv::resource::qpack_table_bytes), 8192u);
hd::http3_connection refused(second.data(), second.critical(), plan.http3.framing);
const auto failure = refused.attach_stream(0);
LT_ASSERT(failure);
LT_CHECK(failure->outcome == httpserver::http::outcome_code::limit_exceeded);
hd::resolved_protocol_plan repeated;
LT_ASSERT(hd::resolve_protocol_policy(options, repeated).ok());
LT_CHECK_EQ(repeated.report.http3->decoder_table_bytes, plan.report.http3->decoder_table_bytes);
LT_CHECK_EQ(repeated.report.http3->encoder_table_bytes, plan.report.http3->encoder_table_bytes);
LT_END_AUTO_TEST(qpack_multiple_connections_compete_without_changing_resolved_policy)
LT_BEGIN_AUTO_TEST(protocol_compression, private_h2_policy_rejects_unrepresentable_wire_values)
for (unsigned field = 0; field < 3; ++field) {
    hd::http2_request_limits limits;
    if (field == 0) limits.max_streams = std::numeric_limits<std::size_t>::max();
    if (field == 1) limits.decoder_table_bytes = std::numeric_limits<std::size_t>::max();
    if (field == 2) limits.connection.encoder_table_bytes = std::numeric_limits<std::size_t>::max();
    auto root = srv::resource_budget::root({});
    srv::route_registry routes;
    LT_ASSERT(srv::route_registry::create(root, routes).ok());
    httpserver::manual_executor owner;
    hd::http2_request_engine engine(root, routes, owner, limits);
    LT_ASSERT(engine.failure());
    LT_CHECK(engine.failure()->outcome == httpserver::http::outcome_code::invalid_argument);
}
LT_END_AUTO_TEST(private_h2_policy_rejects_unrepresentable_wire_values)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
