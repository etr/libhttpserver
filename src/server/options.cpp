/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/protocol_limits.hpp>
#include <httpserver/server/options.hpp>

namespace httpserver::server::detail {

http::outcome check_workers(std::size_t workers) {
    if (workers > max_workers) {
        return http::outcome(http::outcome_code::invalid_argument, "server_options: workers " + std::to_string(workers) + " exceeds the maximum of " + std::to_string(max_workers));
    }
    return http::outcome::okay();
}

http::outcome check_timeout(std::string_view name, std::chrono::milliseconds value) {
    if (value <= std::chrono::milliseconds::zero()) {
        return http::outcome(http::outcome_code::invalid_argument, "server_options: timeout '" + std::string(name) + "' must be positive");
    }
    if (value > max_timeout) {
        return http::outcome(http::outcome_code::invalid_argument,
                             "server_options: timeout '" + std::string(name) + "' of " + std::to_string(value.count()) + "ms exceeds the maximum of " + std::to_string(max_timeout.count()) + "ms");
    }
    return http::outcome::okay();
}

http::outcome check_timeouts(const timeout_options& timeouts) {
    const std::pair<std::string_view, std::chrono::milliseconds> fields[] = {
        {"handshake", timeouts.handshake},   {"header", timeouts.header},     {"body_idle", timeouts.body_idle}, {"suspension", timeouts.suspension},
        {"write_idle", timeouts.write_idle}, {"ws_close", timeouts.ws_close}, {"drain", timeouts.drain},
    };
    for (const auto& field : fields) {
        if (const http::outcome result = check_timeout(field.first, field.second); !result.ok()) {
            return result;
        }
    }
    return http::outcome::okay();
}

http::outcome check_budget(const budget_limits& limits) {
    for (std::size_t i = 0; i < resource_count; ++i) {
        const auto kind = static_cast<resource>(i);
        const std::size_t capacity = limits.get(kind);
        if (capacity == 0 || capacity > max_capacity(kind)) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "server_options: budget '" + std::string(to_string(kind)) + "' capacity must be positive and at most " + std::to_string(max_capacity(kind)));
        }
    }
    return http::outcome::okay();
}

http::outcome check_listeners(const std::vector<listener_options>& listeners) {
    for (std::size_t i = 0; i < listeners.size(); ++i) {
        if (listeners[i].transport != listener_transport::tcp && listeners[i].transport != listener_transport::udp) {
            return {http::outcome_code::invalid_argument, "server_options: invalid listener transport"};
        }
        if (!valid_listen_address(listeners[i].address)) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "server_options: listener " + std::to_string(i) + " address '" + listeners[i].address + "' is not \"\", \"*\", or a numeric IP literal");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (same_listener(listeners[i], listeners[j])) {
                return http::outcome(http::outcome_code::invalid_argument, "server_options: listener " + std::to_string(i) + " duplicates listener " + std::to_string(j));
            }
        }
    }
    return http::outcome::okay();
}

http::outcome check_tls_off(const tls_options& tls, const protocol_set& protocols, const std::vector<listener_options>& listeners) {
    if (tls.provider != tls_provider::none) {
        return http::outcome::okay();
    }
    if (multi_version_enabled(protocols)) {
        return http::outcome(http::outcome_code::not_supported,
                             "server_options: HTTP/2 and HTTP/3 require a TLS provider,"
                             " which is not selected");
    }
    if (tls.profile != tls_profile::none) {
        return http::outcome(http::outcome_code::invalid_argument, "server_options: a TLS profile requires a TLS provider");
    }
    for (std::size_t i = 0; i < listeners.size(); ++i) {
        if (listeners[i].tls) {
            return http::outcome(http::outcome_code::invalid_argument, "server_options: listener " + std::to_string(i) + " requests TLS but no TLS provider is selected");
        }
    }
    return http::outcome::okay();
}

http::outcome check_provider_profile_pair(const tls_options& tls) {
    if (tls.provider == tls_provider::system_default && tls.profile == tls_profile::none) {
        return http::outcome(http::outcome_code::invalid_argument,
                             "server_options: the system_default TLS provider requires a"
                             " TLS profile");
    }
    return http::outcome::okay();
}

http::outcome check_psk_protocols(const tls_options& tls, const protocol_set& protocols) {
    if (tls.profile == tls_profile::external_psk && multi_version_enabled(protocols)) {
        return http::outcome(http::outcome_code::not_supported,
                             "server_options: the external_psk TLS profile is limited to"
                             " HTTP/1 and cannot serve HTTP/2 or HTTP/3");
    }
    return http::outcome::okay();
}

http::outcome check_plaintext_listeners(const protocol_set& protocols, const std::vector<listener_options>& listeners) {
    const bool h1_available = protocols.contains(http::protocol::http_1_0) || protocols.contains(http::protocol::http_1_1);
    if (h1_available) {
        return http::outcome::okay();
    }
    for (std::size_t i = 0; i < listeners.size(); ++i) {
        if (!listeners[i].tls) {
            return http::outcome(http::outcome_code::invalid_argument, "server_options: listener " + std::to_string(i) +
                                                                           " does not use TLS and no HTTP/1.x protocol is"
                                                                           " enabled");
        }
    }
    return http::outcome::okay();
}

http::outcome check_peer_policy(const std::vector<std::string>& entries, const char* list) {
    for (const std::string& entry : entries) {
        if (!valid_peer_pattern(entry)) {
            return http::outcome(http::outcome_code::invalid_argument, "server_options: peer policy " + std::string(list) + " entry '" + entry +
                                                                           "' is not an address,"
                                                                           " trailing-wildcard, or CIDR spelling");
        }
    }
    return http::outcome::okay();
}

http::outcome check_loop_mode(loop_mode loop, const tls_options&, const protocol_set&, const std::vector<listener_options>&) {
    if (loop != loop_mode::managed && loop != loop_mode::external) {
        return {http::outcome_code::invalid_argument, "server_options: invalid loop mode"};
    }
    return http::outcome::okay();
}

tls_client_certificate_mode resolved_client_certificate_mode(tls_profile profile, const tls_client_auth_options& auth) noexcept {
    return auth.mode.value_or(profile == tls_profile::mutual_tls ? tls_client_certificate_mode::require : tls_client_certificate_mode::none);
}

http::outcome check_client_auth(tls_profile profile, const tls_client_auth_options& auth) {
    const auto mode = resolved_client_certificate_mode(profile, auth);
    if (!valid_client_certificate_mode(mode)) {
        return {http::outcome_code::invalid_argument, "TLS client certificate mode invalid"};
    }
    if (auth.timing != tls_client_auth_timing::initial_handshake && auth.timing != tls_client_auth_timing::post_handshake) {
        return {http::outcome_code::invalid_argument, "TLS client authentication timing invalid"};
    }
    if ((profile == tls_profile::mutual_tls && mode == tls_client_certificate_mode::none) ||
        ((profile == tls_profile::none || profile == tls_profile::external_psk) && mode != tls_client_certificate_mode::none)) {
        return {http::outcome_code::invalid_argument, "TLS client authentication profile conflict"};
    }
    if (auth.timing == tls_client_auth_timing::post_handshake) {
        return {http::outcome_code::not_supported, "TLS post-handshake authentication unavailable"};
    }
    return {};
}

http::outcome check_transports(const protocol_set& protocols, const std::vector<listener_options>& listeners) {
    bool udp = false;
    const bool h3 = protocols.contains(http::protocol::http_3);
    const bool tcp_available = protocols.contains(http::protocol::http_1_0) || protocols.contains(http::protocol::http_1_1) || protocols.contains(http::protocol::http_2);
    for (const auto& endpoint : listeners) {
        if (endpoint.transport == listener_transport::udp) {
            udp = true;
            if (!endpoint.tls || !h3) return {http::outcome_code::invalid_argument, "server_options: UDP requires TLS and HTTP/3"};
        } else if (!tcp_available) {
            return {http::outcome_code::invalid_argument, "server_options: TCP needs an enabled HTTP/1 or HTTP/2 protocol"};
        }
    }
    if (h3 && !udp) return {http::outcome_code::invalid_argument, "server_options: HTTP/3 requires an explicit UDP listener"};
    return {};
}

http::outcome check_tls_protocol_combination(const tls_options& tls, const protocol_set& protocols, const std::vector<listener_options>& listeners, loop_mode loop) {
    if (const auto result = check_client_auth(tls.profile, tls.client_auth); !result.ok()) {
        return result;
    }
    if (const auto result = check_loop_mode(loop, tls, protocols, listeners); !result.ok()) {
        return result;
    }
    if (const http::outcome result = check_tls_off(tls, protocols, listeners); !result.ok()) {
        return result;
    }
    if (const http::outcome result = check_provider_profile_pair(tls); !result.ok()) {
        return result;
    }
    if (const http::outcome result = check_psk_protocols(tls, protocols); !result.ok()) {
        return result;
    }
    if (const http::outcome result = check_plaintext_listeners(protocols, listeners); !result.ok()) {
        return result;
    }
    return check_transports(protocols, listeners);
}

}  // namespace httpserver::server::detail

namespace httpserver::server {

auto server_options::resolve_configuration(httpserver::detail::resolved_protocol_plan& plan) const -> http::outcome {
    if (listeners_.empty()) {
        return http::outcome(http::outcome_code::invalid_argument,
                             "server_options: at least one listener is required before"
                             " listening");
    }
    if (listeners_.size() > max_listeners) {
        return http::outcome(http::outcome_code::invalid_argument, "server_options: listener count " + std::to_string(listeners_.size()) + " exceeds the maximum of " + std::to_string(max_listeners));
    }
    if (const http::outcome result = detail::check_listeners(listeners_); !result.ok()) {
        return result;
    }
    if (const http::outcome result = detail::check_workers(concurrency_.workers); !result.ok()) {
        return result;
    }
    if (const http::outcome result = detail::check_timeouts(timeouts_); !result.ok()) {
        return result;
    }
    if (const http::outcome result = detail::check_budget(budgets_); !result.ok()) {
        return result;
    }
    if (const http::outcome result = detail::check_tls_protocol_combination(tls_, protocols_, listeners_, loop_); !result.ok()) {
        return result;
    }
    if (const http::outcome result = detail::check_peer_policy(peers_.deny, "deny"); !result.ok()) {
        return result;
    }
    if (const http::outcome result = detail::check_peer_policy(peers_.allow, "allow"); !result.ok()) {
        return result;
    }
    return httpserver::detail::resolve_protocol_policy(*this, plan);
}

http::outcome server_options::validate() const {
    httpserver::detail::resolved_protocol_plan plan;
    return resolve_configuration(plan);
}
http::outcome server_options::resolve_limits(effective_protocol_limits& out) const {
    httpserver::detail::resolved_protocol_plan plan;
    if (const auto result = httpserver::detail::protocol_plan_access::resolve(*this, plan); !result.ok()) {
        return result;
    }
    out = std::move(plan.report);
    return {};
}

}  // namespace httpserver::server
