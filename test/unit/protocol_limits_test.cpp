/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <filesystem>  // NOLINT(build/c++17): native v3 requires C++20.
#include <limits>
#include <type_traits>
#include <httpserver/detail/io_poll_sys.hpp>
#include <httpserver/detail/protocol_limits.hpp>
#include <httpserver/server/configuration.hpp>
#include <httpserver/server/server.hpp>
#include "./littletest.hpp"
#include "config.h"

namespace {
namespace srv = httpserver::server;
namespace http = httpserver::http;
srv::server_options plain(std::uint16_t port = 0) {
    srv::server_options options;
    options.add_listener({"127.0.0.1", port, false});
    return options;
}
srv::server_options mixed(std::uint16_t port = 0) {
    auto options = plain(port);
    options.tls().provider = srv::tls_provider::system_default;
    options.tls().profile = srv::tls_profile::certificates;
    options.add_listener({"127.0.0.1", 0, true});
    options.add_listener({"127.0.0.1", 0, true, {}, srv::listener_transport::udp});
    options.protocols().enable(http::protocol::http_2);
    options.protocols().enable(http::protocol::http_3);
    return options;
}
}  // namespace
LT_BEGIN_SUITE(protocol_limits)
void set_up() {}
void tear_down() {}
LT_END_SUITE(protocol_limits)
LT_BEGIN_AUTO_TEST(protocol_limits, plain_report_and_immutable_inspection)
auto options = plain();
srv::effective_protocol_limits report;
LT_CHECK(options.resolve_limits(report).ok());
LT_CHECK(!report.http2 && !report.http3 && !report.quic);
srv::native_server server(options);
options.http2().emplace();
LT_CHECK(server.effective_limits(report).ok());
LT_CHECK(server.listen().ok());
LT_CHECK(server.effective_limits(report).ok());
srv::drain_ticket ticket;
LT_CHECK(server.begin_drain(std::chrono::milliseconds(100), ticket).ok());
LT_CHECK(server.effective_limits(report).ok());
server.stop();
LT_CHECK(server.effective_limits(report).ok());
LT_END_AUTO_TEST(plain_report_and_immutable_inspection)
LT_BEGIN_AUTO_TEST(protocol_limits, defaults_semantic_and_build_availability)
const auto options = mixed();
LT_CHECK(options.validate().ok());
srv::effective_protocol_limits report;
report.quic.emplace().max_stream_records = 777;
const auto result = options.resolve_limits(report);
#ifdef NATIVE_V3_TLS
LT_CHECK(result.ok());
LT_CHECK_EQ(report.http2->max_concurrent_requests, 128u);
LT_CHECK_EQ(report.http2->headers.max_compressed_bytes, 65536u);
LT_CHECK_EQ(report.http2->headers.max_expanded_bytes, 65536u);
LT_CHECK_EQ(report.http2->headers.max_fields, 256u);
LT_CHECK_EQ(report.http2->decoder_table_bytes, 4096u);
LT_CHECK_EQ(report.http2->pre_ack_decoder_table_bytes, 4096u);
LT_CHECK_EQ(report.http3->max_concurrent_requests, 16u);
LT_CHECK_EQ(report.http3->headers.max_compressed_bytes, 8192u);
LT_CHECK_EQ(report.http3->headers.max_expanded_bytes, 16384u);
LT_CHECK_EQ(report.http3->headers.max_fields, 128u);
LT_CHECK_EQ(report.http3->max_blocked_headers, 8u);
LT_CHECK_EQ(report.http3->max_retained_header_bytes, 65536u);
LT_CHECK_EQ(report.quic->max_stream_records, 64u);
LT_CHECK_EQ(report.quic->initial_max_bidirectional_streams, 16u);
LT_CHECK_EQ(report.quic->initial_max_unidirectional_streams, 8u);
LT_CHECK_EQ(report.quic->advertised_max_udp_payload_bytes, 1200u);
LT_CHECK_EQ(report.quic->native_send_datagram_bytes, 1200u);
LT_CHECK(!report.http2->initial_advertisement.max_header_list_bytes);
LT_CHECK(!report.http3->initial_advertisement.max_field_section_bytes);
#else
LT_CHECK(result.code() == http::outcome_code::not_supported);
LT_CHECK_EQ(report.quic->max_stream_records, 777u);
#endif
srv::native_server server(options);
srv::effective_protocol_limits inspected;
LT_CHECK(server.effective_limits(inspected).code() == result.code());
LT_CHECK(!server.listen().ok());  // No published credentials.
LT_CHECK(server.effective_limits(inspected).code() == result.code());
for (std::size_t i = 0; i != options.listener_count(); ++i)
    LT_CHECK_EQ(server.get_bound_port(i), 0u);
LT_END_AUTO_TEST(defaults_semantic_and_build_availability)
LT_BEGIN_AUTO_TEST(protocol_limits, explicit_inapplicable_profiles_preserve_output)
for (unsigned profile = 0; profile != 3; ++profile) {
    auto options = plain();
    if (profile == 0)
        options.http2().emplace();
    if (profile == 1)
        options.http3().emplace();
    if (profile == 2)
        options.quic().emplace();
    srv::effective_protocol_limits out;
    out.quic.emplace().max_stream_records = 777;
    LT_CHECK(options.validate().code() == http::outcome_code::invalid_argument);
    LT_CHECK(options.resolve_limits(out).code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(out.quic->max_stream_records, 777u);
}
LT_END_AUTO_TEST(explicit_inapplicable_profiles_preserve_output)
LT_BEGIN_AUTO_TEST(protocol_limits, explicit_boundaries_are_semantic)
for (unsigned field = 0; field != 13; ++field) {
    auto options = mixed();
    auto& h2 = options.http2().emplace();
    auto& h3 = options.http3().emplace();
    auto& quic = options.quic().emplace();
    switch (field) {
        case 0:
            h2.max_concurrent_requests = 0;
            break;
        case 1:
            h2.max_concurrent_requests = srv::max_capacity(srv::resource::streams) + 1;
            break;
        case 2:
            h2.headers.max_fields = 257;
            break;
        case 3:
            h2.headers.max_expanded_bytes = std::numeric_limits<std::size_t>::max();
            break;
        case 4:
            h2.headers.max_compressed_bytes = std::numeric_limits<std::size_t>::max();
            break;
        case 5:
            h2.decoder_table_bytes = 65536;
            h2.encoder_table_bytes = 1;
            break;
        case 6:
            h3.decoder_table_bytes = 65536;
            h3.encoder_table_bytes = 1;
            break;
        case 7:
            h3.headers.max_fields = 256;
            break;  // Semantic extra field seat.
        case 8:
            h3.max_retained_header_bytes = 8191;
            break;
        case 9:
            h3.max_blocked_headers = 257;
            break;
        case 10:
            quic.max_stream_records = 6;
            break;
        case 11:
            quic.max_stream_records = 65537;
            break;
        case 12:
            h3.max_concurrent_requests = 59;
            break;  // 64 includes six critical roles.
    }
    LT_CHECK(options.validate().code() == http::outcome_code::invalid_argument);
    srv::effective_protocol_limits out;
    out.http2.emplace().max_concurrent_requests = 777;
    LT_CHECK(options.resolve_limits(out).code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(out.http2->max_concurrent_requests, 777u);
    srv::native_server server(options);
    LT_CHECK(server.listen().code() == http::outcome_code::invalid_argument);
    for (std::size_t i = 0; i != options.listener_count(); ++i)
        LT_CHECK_EQ(server.get_bound_port(i), 0u);
}
LT_END_AUTO_TEST(explicit_boundaries_are_semantic)
LT_BEGIN_AUTO_TEST(protocol_limits, independent_growth_zero_tables_and_copy)
auto options = mixed();
options.budgets().set(srv::resource::header_fields, 4096);
options.budgets().set(srv::resource::quic_reassembly_bytes, 16 * 1024 * 1024);
auto& h2 = options.http2().emplace();
h2.max_concurrent_requests = 192;
h2.headers = {70000, 70000, 512};
h2.decoder_table_bytes = 8192;
h2.encoder_table_bytes = 16384;
auto& h3 = options.http3().emplace();
h3.max_concurrent_requests = 24;
h3.headers = {12000, 20000, 256};
h3.decoder_table_bytes = 0;
h3.encoder_table_bytes = 8192;
h3.max_blocked_headers = 32;  // Independent ceiling, greater than active requests.
h3.max_retained_header_bytes = 96000;
options.quic().emplace().max_stream_records = 96;
auto copied = options;
LT_CHECK(copied.validate().ok());
const auto& const_copy = copied;
LT_CHECK_EQ(*const_copy.http2()->max_concurrent_requests, 192u);
LT_CHECK_EQ(*const_copy.http3()->max_concurrent_requests, 24u);
LT_CHECK_EQ(*const_copy.quic()->max_stream_records, 96u);
srv::effective_protocol_limits out;
const auto result = copied.resolve_limits(out);
#ifdef NATIVE_V3_TLS
LT_CHECK(result.ok());
LT_CHECK_EQ(out.http2->max_concurrent_requests, 192u);
LT_CHECK_EQ(out.http2->headers.max_compressed_bytes, 70000u);
LT_CHECK_EQ(out.http2->headers.max_fields, 512u);
LT_CHECK_EQ(out.http2->decoder_table_bytes, 8192u);
LT_CHECK_EQ(out.http2->encoder_table_bytes, 16384u);
LT_CHECK_EQ(out.http3->max_concurrent_requests, 24u);
LT_CHECK_EQ(out.http3->headers.max_compressed_bytes, 12000u);
LT_CHECK_EQ(out.http3->headers.max_fields, 256u);
LT_CHECK_EQ(out.http3->encoder_table_bytes, 8192u);
LT_CHECK_EQ(out.http3->max_blocked_headers, 32u);
LT_CHECK_EQ(out.quic->max_stream_records, 96u);
srv::native_server server(copied);
copied.http2()->max_concurrent_requests = 1;
LT_CHECK(server.effective_limits(out).ok());
LT_CHECK_EQ(out.http2->max_concurrent_requests, 192u);
#else
LT_CHECK(result.code() == http::outcome_code::not_supported);
#endif
LT_END_AUTO_TEST(independent_growth_zero_tables_and_copy)

LT_BEGIN_AUTO_TEST(protocol_limits, resolved_private_owner_configuration_has_no_default_clamps)
auto options = mixed();
options.budgets().set(srv::resource::header_fields, 4096);
options.budgets().set(srv::resource::quic_reassembly_bytes, 16 * 1024 * 1024);
options.http2().emplace().max_concurrent_requests = 192;
options.http2()->headers = {70000, 70000, 512};
options.http2()->decoder_table_bytes = 8192;
options.http2()->encoder_table_bytes = 16384;
options.http3().emplace().max_concurrent_requests = 24;
options.http3()->headers = {12000, 20000, 256};
options.http3()->max_retained_header_bytes = 96000;
options.quic().emplace().max_stream_records = 96;
httpserver::detail::resolved_protocol_plan plan;
LT_ASSERT(httpserver::detail::resolve_protocol_policy(options, plan).ok());
LT_CHECK_EQ(plan.http2.max_streams, 192u);
LT_CHECK_EQ(plan.http2.headers.max_compressed_bytes, 70000u);
LT_CHECK_EQ(plan.http3.max_active_requests, 24u);
LT_CHECK_EQ(plan.http3.framing.headers.max_compressed_bytes, 12000u);
LT_CHECK_EQ(plan.http3.framing.stream_records, 96u);
LT_CHECK_EQ(plan.http3.framing.qpack_retained_bytes, 96000u);
LT_CHECK(plan.data_bytes > 393216u);
LT_END_AUTO_TEST(resolved_private_owner_configuration_has_no_default_clamps)
LT_BEGIN_AUTO_TEST(protocol_limits, reduced_hpack_decoder_still_needs_initial_allowance)
auto options = mixed();
options.protocols().disable(http::protocol::http_3);
options = plain();
options.tls().provider = srv::tls_provider::system_default;
options.tls().profile = srv::tls_profile::certificates;
options.add_listener({"127.0.0.1", 0, true});
options.protocols().enable(http::protocol::http_2);
options.budgets().set(srv::resource::hpack_table_bytes, 4096);
options.http2().emplace().decoder_table_bytes = 0;
options.http2()->encoder_table_bytes = 0;
LT_CHECK(options.validate().ok());
options.http2()->encoder_table_bytes = 1;
LT_CHECK(options.validate().code() == http::outcome_code::invalid_argument);
LT_END_AUTO_TEST(reduced_hpack_decoder_still_needs_initial_allowance)
LT_BEGIN_AUTO_TEST(protocol_limits, h3_exact_prepaid_envelope_and_checked_arithmetic)
auto options = mixed();
options.budgets().set(srv::resource::quic_reassembly_bytes, 16 * 1024 * 1024);
options.http3().emplace();
options.quic().emplace().max_stream_records = 96;
httpserver::detail::resolved_protocol_plan plan;
LT_ASSERT(httpserver::detail::resolve_protocol_policy(options, plan).ok());
const auto need = httpserver::detail::http3_required_storage(plan.http3, 96);
const auto exact = need.data_bytes + need.critical_bytes + 131072;
options.budgets().set(srv::resource::quic_reassembly_bytes, exact);
LT_CHECK(options.validate().ok());
options.budgets().set(srv::resource::quic_reassembly_bytes, exact - 1);
LT_CHECK(options.validate().code() == http::outcome_code::invalid_argument);
auto invalid = plan.http3;
invalid.framing.headers.max_fields = std::numeric_limits<std::size_t>::max();
bool overflow = false;
try {
    static_cast<void>(httpserver::detail::http3_required_storage(invalid, 96));
} catch (const std::invalid_argument&) {
    overflow = true;
}
LT_CHECK(overflow);
options.budgets().set(srv::resource::quic_reassembly_bytes, srv::max_capacity(srv::resource::quic_reassembly_bytes));
options.quic()->max_stream_records = 65536;
LT_CHECK(options.validate().code() == http::outcome_code::invalid_argument);  // Actual persistent record bytes exceed the root.
options.quic()->max_stream_records = 65537;
LT_CHECK(options.validate().code() == http::outcome_code::invalid_argument);
LT_END_AUTO_TEST(h3_exact_prepaid_envelope_and_checked_arithmetic)
LT_BEGIN_AUTO_TEST(protocol_limits, explicit_policy_checks_resolved_defaults_and_critical_seats)
auto options = mixed();
options.budgets().set(srv::resource::quic_reassembly_bytes, 16 * 1024 * 1024);
options.http3().emplace();
options.budgets().set(srv::resource::header_fields, 128);
LT_CHECK(options.validate().code() == http::outcome_code::invalid_argument);
options.budgets().set(srv::resource::header_fields, 256);
options.budgets().set(srv::resource::streams, 3);
options.http3()->max_concurrent_requests = 1;
LT_CHECK(options.validate().code() == http::outcome_code::invalid_argument);
options.budgets().set(srv::resource::streams, 4);
options.http3()->decoder_table_bytes = 0;
options.http3()->encoder_table_bytes = 0;
options.http3()->max_blocked_headers = 32;
LT_CHECK(options.validate().ok());
LT_END_AUTO_TEST(explicit_policy_checks_resolved_defaults_and_critical_seats)
LT_BEGIN_AUTO_TEST(protocol_limits, invalid_mixed_policy_precedes_all_fixed_port_binds)
namespace sys = httpserver::detail::pollsys;
for (auto mode : {srv::loop_mode::managed, srv::loop_mode::external}) {
    std::uint16_t port = 0;
    auto probe = sys::open_listener("127.0.0.1", 0, port);
    LT_ASSERT(probe != sys::k_invalid_socket);
    LT_ASSERT(sys::close_socket(probe));
    auto options = mixed(port);
    options.loop() = mode;
    options.add_listener({"127.0.0.1", port, true, {}, srv::listener_transport::udp});
    options.http3().emplace().max_concurrent_requests = 0;
    srv::native_server server(options);
    LT_CHECK(server.listen().code() == http::outcome_code::invalid_argument);
    std::uint16_t bound = 0;
    auto rebound = sys::open_listener("127.0.0.1", port, bound);
    LT_CHECK(rebound != sys::k_invalid_socket && bound == port);
    sys::close_socket(rebound);
    sockaddr_storage address{};
    LT_ASSERT(sys::fill_listener_address("127.0.0.1", port, address));
    auto datagram = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    LT_ASSERT(datagram != sys::k_invalid_socket);
    LT_CHECK(::bind(datagram, reinterpret_cast<const sockaddr*>(&address), sizeof(sockaddr_in)) == 0);
    sys::close_socket(datagram);
    LT_CHECK_EQ(server.get_bound_port(0), 0u);
    LT_CHECK_EQ(server.get_bound_port(1), 0u);
    LT_CHECK_EQ(server.get_bound_port(2), 0u);
}
LT_END_AUTO_TEST(invalid_mixed_policy_precedes_all_fixed_port_binds)
LT_BEGIN_AUTO_TEST(protocol_limits, repeated_resolution_has_no_descriptor_activity)
#ifdef __linux__
auto count = [] {
    std::size_t result = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        static_cast<void>(entry);
        ++result;
    }
    return result;
};
const auto options = plain();
const auto before = count();
srv::effective_protocol_limits out;
for (unsigned i = 0; i < 100; ++i)
    LT_ASSERT(options.resolve_limits(out).ok());
LT_CHECK_EQ(count(), before);
#else
LT_SKIP("Descriptor observation uses Linux /proc/self/fd; nonlocal lifecycle checks are CI-owned");
#endif
LT_END_AUTO_TEST(repeated_resolution_has_no_descriptor_activity)
LT_BEGIN_AUTO_TEST(protocol_limits, h2_compressed_storage_leaves_native_frame_and_stream_workspace)
auto options = plain();
options.tls().provider = srv::tls_provider::system_default;
options.tls().profile = srv::tls_profile::certificates;
options.add_listener({"127.0.0.1", 0, true});
options.protocols().enable(http::protocol::http_2);
options.budgets().set(srv::resource::body_buffer_bytes, 65536);
options.http2().emplace().headers.max_compressed_bytes = 65536;
LT_CHECK(options.validate().code() == http::outcome_code::invalid_argument);
LT_END_AUTO_TEST(h2_compressed_storage_leaves_native_frame_and_stream_workspace)
LT_BEGIN_AUTO_TEST(protocol_limits, cached_failure_preserves_every_output_section)
for (unsigned family = 0; family < 3; ++family) {
    auto options = plain();
    if (family == 0) options.timeouts().header = std::chrono::milliseconds::zero();
    if (family == 1) options.http3().emplace();
    if (family == 2) options.budgets().set(srv::resource::header_fields, 0);
    srv::effective_protocol_limits out;
    out.http2.emplace().max_concurrent_requests = 777;
    out.http3.emplace().decoder_table_bytes = 888;
    out.quic.emplace().max_stream_records = 999;
    out.budgets.set(srv::resource::header_fields, 123);
    LT_CHECK(options.resolve_limits(out).code() == http::outcome_code::invalid_argument);
    srv::native_server server(options);
    LT_CHECK(server.effective_limits(out).code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(out.http2->max_concurrent_requests, 777u);
    LT_CHECK_EQ(out.http3->decoder_table_bytes, 888u);
    LT_CHECK_EQ(out.quic->max_stream_records, 999u);
    LT_CHECK_EQ(out.budgets.get(srv::resource::header_fields), 123u);
}
LT_END_AUTO_TEST(cached_failure_preserves_every_output_section)
LT_BEGIN_AUTO_TEST(protocol_limits, unused_provider_selection_keeps_plaintext_resolution_available)
auto options = plain();
options.tls().provider = srv::tls_provider::system_default;
options.tls().profile = srv::tls_profile::certificates;
srv::effective_protocol_limits report;
LT_CHECK(options.resolve_limits(report).ok());
LT_CHECK(!report.http2 && !report.http3 && !report.quic);
srv::native_server server(options);
LT_CHECK(server.effective_limits(report).ok());
LT_END_AUTO_TEST(unused_provider_selection_keeps_plaintext_resolution_available)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
