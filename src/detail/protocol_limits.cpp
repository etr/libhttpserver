/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>
#include <utility>
#include <httpserver/detail/capacity_math.hpp>
#include <httpserver/detail/http2_tcp_owner.hpp>
#include <httpserver/detail/protocol_limits.hpp>
#include "config.h"

namespace httpserver::detail {
namespace {
http::outcome invalid(const char* field, std::size_t maximum) {
    return {http::outcome_code::invalid_argument, std::string("server_options: ") + field + " exceeds its storage/budget bound " + std::to_string(maximum)};
}
http::outcome scalar(const char* field, const std::optional<std::size_t>& configured, std::size_t maximum, std::size_t& value, bool zero = false) {
    if (!configured)
        return {};
    if (!zero && !*configured) return {http::outcome_code::invalid_argument, std::string("server_options: ") + field + " must be positive"};
    if (*configured > maximum) return invalid(field, maximum);
    value = *configured;
    return {};
}
http::outcome headers(const server::header_capacity_options& profile, server::effective_header_limits& out, const server::budget_limits& budgets, bool h3) {
    const auto expanded = budgets.get(server::resource::header_bytes) / (h3 ? 5 : 3);
    const auto compressed = budgets.get(h3 ? server::resource::quic_reassembly_bytes : server::resource::body_buffer_bytes);
    const auto fields = budgets.get(server::resource::header_fields) - (h3 ? 1 : 0);
    if (auto result = scalar(h3 ? "http3.headers.max_compressed_bytes" : "http2.headers.max_compressed_bytes", profile.max_compressed_bytes, compressed, out.max_compressed_bytes); !result.ok()) {
        return result;
    }
    if (auto result = scalar(h3 ? "http3.headers.max_expanded_bytes" : "http2.headers.max_expanded_bytes", profile.max_expanded_bytes, expanded, out.max_expanded_bytes); !result.ok()) {
        return result;
    }
    if (auto result = scalar(h3 ? "http3.headers.max_fields" : "http2.headers.max_fields", profile.max_fields, fields, out.max_fields); !result.ok()) {
        return result;
    }
    if (!out.max_compressed_bytes || out.max_compressed_bytes > compressed || !out.max_expanded_bytes || out.max_expanded_bytes > expanded || !out.max_fields || out.max_fields > fields)
        return invalid(h3 ? "http3.headers resolved defaults" : "http2.headers resolved defaults", fields);
    return {};
}
http::outcome compression(const char* profile, const std::optional<std::size_t>& decoder, const std::optional<std::size_t>& encoder, std::size_t& decoded, std::size_t& encoded, std::size_t budget,
                          std::size_t initial) {
    if (auto result = scalar(profile, decoder, budget, decoded, true); !result.ok()) {
        return result;
    }
    if (auto result = scalar(profile, encoder, budget, encoded, true); !result.ok()) {
        return result;
    }
    const auto inbound = std::max(initial, decoded);
    if (inbound > budget || encoded > budget - inbound)
        return invalid(profile, budget);
    return {};
}
}  // namespace
http::outcome protocol_plan_access::resolve(const server::server_options& options, resolved_protocol_plan& out) {
    resolved_protocol_plan plan;
    if (const auto result = options.resolve_configuration(plan); !result.ok()) {
        return result;
    }
    if (const auto result = protocol_availability(options); !result.ok()) {
        return result;
    }
    out = std::move(plan);
    return {};
}
http::outcome protocol_availability(const server::server_options& options) {
#ifndef NATIVE_V3_TLS
    bool tls_endpoint = false;
    for (std::size_t i = 0; i < options.listener_count(); ++i) tls_endpoint |= options.listener(i).tls;
    if (tls_endpoint || options.protocols().contains(http::protocol::http_2) || options.protocols().contains(http::protocol::http_3))
        return {http::outcome_code::not_supported, "server_options: requested TLS/protocol transport is unavailable"};
#endif
    return {};
}
server::effective_http3_limits default_http3_limits(const server::budget_limits& budgets) {
    server::effective_http3_limits limits;
    limits.max_concurrent_requests = std::min<std::size_t>(16, budgets.get(server::resource::streams));
    limits.headers = {8192, 16384, 128};
    limits.decoder_table_bytes = limits.encoder_table_bytes = std::min<std::size_t>(4096, budgets.get(server::resource::qpack_table_bytes));
    limits.max_blocked_headers = std::min<std::size_t>(8, budgets.get(server::resource::blocked_headers));
    limits.max_retained_header_bytes = 65536;
    return limits;
}
http3_request_limits native_http3_limits(const server::effective_http3_limits& limits, std::size_t records, const server::timeout_options& timeouts) {
    http3_request_limits native;
    native.max_active_requests = limits.max_concurrent_requests;
    native.framing.headers = {limits.headers.max_compressed_bytes, limits.headers.max_expanded_bytes, limits.headers.max_fields};
    native.framing.qpack_capacity = limits.decoder_table_bytes;
    native.framing.qpack_encoder_capacity = limits.encoder_table_bytes;
    native.framing.qpack_blocked_streams = limits.max_blocked_headers;
    native.framing.qpack_retained_bytes = limits.max_retained_header_bytes;
    native.framing.stream_records = records;
    native.framing.data_chunk = 4096;
    native.max_receipt_records = 16;
    native.max_pending_output_records = 32;
    native.timeouts = timeouts;
    return native;
}
http3_request_limits native_http3_limits(const server::resource_budget& budget, std::size_t requests, std::size_t records, const server::timeout_options& timeouts) {
    server::budget_limits budgets;
    for (std::size_t i = 0; i < server::resource_count; ++i) {
        const auto resource = static_cast<server::resource>(i);
        budgets.set(resource, budget.capacity(resource));
    }
    auto limits = default_http3_limits(budgets);
    limits.max_concurrent_requests = requests;
    return native_http3_limits(limits, records, timeouts);
}
http::outcome resolve_protocol_policy(const server::server_options& options, resolved_protocol_plan& out) {
    resolved_protocol_plan plan;
    auto& report = plan.report;
    report.budgets = options.budgets();
    const auto capacity = [&](server::resource kind) { return options.budgets().get(kind); };
    bool h2 = false, h3 = false;
    for (std::size_t i = 0; i < options.listener_count(); ++i) {
        const auto& endpoint = options.listener(i);
        h2 |= endpoint.tls && endpoint.transport == server::listener_transport::tcp && options.protocols().contains(http::protocol::http_2);
        h3 |= endpoint.tls && endpoint.transport == server::listener_transport::udp && options.protocols().contains(http::protocol::http_3);
    }
    if ((options.http2() && !h2) || (options.http3() && !h3) || (options.quic() && !h3))
        return {http::outcome_code::invalid_argument, "server_options: explicit http2/http3/quic profile requires an applicable enabled transport"};
    if (h2) {
        auto& limits = report.http2.emplace();
        limits.max_concurrent_requests = std::min<std::size_t>(128, capacity(server::resource::streams));
        limits.headers = {std::min<std::size_t>(65536, capacity(server::resource::body_buffer_bytes) / 16), std::min<std::size_t>(65536, capacity(server::resource::header_bytes) / 3),
                          std::min<std::size_t>(256, capacity(server::resource::header_fields))};
        limits.decoder_table_bytes = limits.encoder_table_bytes = 4096;
        if (options.http2()) {
            const auto& profile = *options.http2();
            if (auto result = scalar("http2.max_concurrent_requests", profile.max_concurrent_requests, capacity(server::resource::streams), limits.max_concurrent_requests); !result.ok()) {
                return result;
            }
            if (auto result = headers(profile.headers, limits.headers, report.budgets, false); !result.ok()) {
                return result;
            }
            const auto body = capacity(server::resource::body_buffer_bytes);
            const auto workspace = capacity_add(http2_tcp_owner::storage_capacity(), capacity_add(16384 + 128, http2_request_engine::stream_storage()));
            if (body < workspace || limits.headers.max_compressed_bytes > body - workspace)
                return invalid("http2.headers.max_compressed_bytes plus native/frame/stream workspace", body);
            if (auto result = compression("http2 decoder_table_bytes + encoder_table_bytes (including pre-ACK 4096)", profile.decoder_table_bytes, profile.encoder_table_bytes,
                                          limits.decoder_table_bytes, limits.encoder_table_bytes, capacity(server::resource::hpack_table_bytes), 4096);
                !result.ok()) {
                return result;
            }
        }
        plan.http2.headers = {limits.headers.max_compressed_bytes, limits.headers.max_expanded_bytes, limits.headers.max_fields};
        plan.http2.max_streams = limits.max_concurrent_requests;
        plan.http2.decoder_table_bytes = limits.decoder_table_bytes;
        if (options.http2()) plan.http2.connection.encoder_table_bytes = limits.encoder_table_bytes;
        plan.http2.timeouts = options.timeouts();
        limits.initial_advertisement = {static_cast<std::uint32_t>(limits.max_concurrent_requests), static_cast<std::uint32_t>(limits.decoder_table_bytes), {}};
    }
    if (h3) {
        auto& limits = report.http3.emplace(default_http3_limits(options.budgets()));
        if (options.http3()) {
            const auto& profile = *options.http3();
            if (auto result = scalar("http3.max_concurrent_requests", profile.max_concurrent_requests, capacity(server::resource::streams), limits.max_concurrent_requests); !result.ok()) {
                return result;
            }
            if (auto result = scalar("http3.max_blocked_headers", profile.max_blocked_headers, capacity(server::resource::blocked_headers), limits.max_blocked_headers, true); !result.ok()) {
                return result;
            }
            if (auto result = scalar("http3.max_retained_header_bytes", profile.max_retained_header_bytes, capacity(server::resource::quic_reassembly_bytes), limits.max_retained_header_bytes);
                !result.ok()) {
                return result;
            }
        }
        auto& quic = report.quic.emplace();
        quic.max_stream_records = 64;
        if (options.quic()) {
            if (auto result = scalar("quic.max_stream_records", options.quic()->max_stream_records, 65536, quic.max_stream_records); !result.ok()) {
                return result;
            }
        }
        if (options.http3() || options.quic()) {
            const server::http3_capacity_options empty;
            const auto& profile = options.http3().value_or(empty);
            if (auto result = headers(profile.headers, limits.headers, report.budgets, true); !result.ok()) {
                return result;
            }
            if (auto result = compression("http3 decoder_table_bytes + encoder_table_bytes", profile.decoder_table_bytes, profile.encoder_table_bytes, limits.decoder_table_bytes,
                                          limits.encoder_table_bytes, capacity(server::resource::qpack_table_bytes), 0);
                !result.ok()) {
                return result;
            }
            if (capacity(server::resource::streams) < 4)
                return invalid("http3 critical peer streams plus one request require at least four stream seats", capacity(server::resource::streams));
            if (quic.max_stream_records < 7 || limits.max_concurrent_requests > quic.max_stream_records - 6)
                return invalid("quic.max_stream_records must hold local and peer control/QPACK records plus http3 requests", quic.max_stream_records);
            if (limits.headers.max_compressed_bytes > limits.max_retained_header_bytes)
                return invalid("http3.headers.max_compressed_bytes exceeds max_retained_header_bytes", limits.max_retained_header_bytes);
        }
        limits.initial_advertisement = {limits.decoder_table_bytes, limits.max_blocked_headers, {}};
        quic.initial_max_bidirectional_streams = limits.max_concurrent_requests;
        const auto bytes = capacity(server::resource::quic_reassembly_bytes);
        plan.data_bytes = plan.critical_bytes = std::min<std::size_t>(393216, (bytes / 8) * 3 + (bytes % 8) * 3 / 8);
    }
    if (h3) {
        const auto& limits = *report.http3;
        plan.http3 = native_http3_limits(limits, report.quic->max_stream_records, options.timeouts());
        if (options.http3() || options.quic()) {
            try {
                const auto need = http3_required_storage(plan.http3, report.quic->max_stream_records);
                plan.data_bytes = std::max(plan.data_bytes, need.data_bytes);
                plan.critical_bytes = std::max(plan.critical_bytes, need.critical_bytes);
                const auto budget = capacity(server::resource::quic_reassembly_bytes);
                if (budget < 131072 || plan.data_bytes > budget - 131072 || plan.critical_bytes > budget - 131072 - plan.data_bytes)
                    return invalid("http3/quic prepaid data + critical storage + UDP listener", budget);
            } catch (const std::invalid_argument& error) {
                return {http::outcome_code::invalid_argument, error.what()};
            }
        }
    }
    out = std::move(plan);
    return {};
}
}  // namespace httpserver::detail
