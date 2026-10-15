/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "protocol_limits.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_PROTOCOL_LIMITS_HPP_
#define SRC_HTTPSERVER_DETAIL_PROTOCOL_LIMITS_HPP_
#include <httpserver/server/options.hpp>
#include <httpserver/detail/http2_request_engine.hpp>
#include <httpserver/detail/http3_request_engine.hpp>
namespace httpserver::detail {
struct resolved_protocol_plan {
    server::effective_protocol_limits report;
    http2_request_limits http2;
    http3_request_limits http3;
    std::size_t data_bytes = 0, critical_bytes = 0;
};
struct http3_storage_requirements { std::size_t data_bytes = 0, critical_bytes = 0; };
server::effective_http3_limits default_http3_limits(const server::budget_limits& budgets);
http3_request_limits native_http3_limits(const server::effective_http3_limits& limits, std::size_t records, const server::timeout_options& timeouts);
http3_request_limits native_http3_limits(const server::resource_budget& budget, std::size_t requests, std::size_t records, const server::timeout_options& timeouts);
std::size_t quic_transport_descriptor_storage(std::size_t records);
quic_recovery_config native_recovery_limits(std::size_t data_bytes, std::size_t critical_bytes);
http3_storage_requirements http3_required_storage(const http3_request_limits& limits, std::size_t records);
struct protocol_plan_access {
    static http::outcome resolve(const server::server_options& options, resolved_protocol_plan& out);
};
http::outcome resolve_protocol_policy(const server::server_options& options, resolved_protocol_plan& out);
http::outcome protocol_availability(const server::server_options& options);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_PROTOCOL_LIMITS_HPP_
