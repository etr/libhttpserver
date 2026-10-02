/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#ifndef SRC_HTTPSERVER_SERVER_OPTIONS_HPP_
#define SRC_HTTPSERVER_SERVER_OPTIONS_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/server/hooks.hpp>

namespace httpserver {

namespace server {

// Selection of the sole optional TLS/crypto provider linked into the
// build (DR-V3-002). `none` is the TLS-off configuration; TLS, HTTP/2,
// and HTTP/3 are then unavailable. `system_default` selects the
// provider chosen at build time; its identity is never part of this
// API.
enum class tls_provider : std::uint8_t {
    none,
    system_default,
};

// Credential profile used with the selected provider. `certificates`
// authenticates this endpoint with a certificate chain; `mutual_tls`
// additionally requires a client chain; `external_psk` uses
// out-of-band shared keys and is limited to HTTP/1 in v3.
enum class tls_profile : std::uint8_t {
    none,
    certificates,
    mutual_tls,
    external_psk,
};

// Documented configuration bounds quoted by validation diagnostics.
inline constexpr std::size_t max_workers = 1024;
inline constexpr std::size_t max_listeners = 64;
// 24 hours; the ceiling for every timeout field.
inline constexpr std::chrono::milliseconds max_timeout{86400000};

// Closed set over http::protocol. Defaults to the TLS-off baseline
// {HTTP/1.0, HTTP/1.1}. HTTP/2 and HTTP/3 require a TLS provider, so
// they are opt-in via enable().
class protocol_set {
 public:
    constexpr protocol_set() noexcept : enabled_(kDefaultMask) { }

    constexpr bool contains(http::protocol version) const noexcept {
        return (enabled_ & mask(version)) != 0;
    }

    constexpr void enable(http::protocol version) noexcept {
        enabled_ = static_cast<std::uint8_t>(enabled_ | mask(version));
    }

    constexpr void disable(http::protocol version) noexcept {
        enabled_ = static_cast<std::uint8_t>(enabled_ & ~mask(version));
    }

 private:
    static constexpr std::uint8_t kDefaultMask = 0x03;  // HTTP/1.0 + HTTP/1.1

    static constexpr std::uint8_t mask(http::protocol version) noexcept {
        return static_cast<std::uint8_t>(
            1u << static_cast<unsigned>(version));
    }

    std::uint8_t enabled_;
};

// One listening endpoint. address is "" or "*" for any local address,
// or a numeric IPv4/IPv6 literal; names are resolved by the caller, not
// by validation. port 0 requests an ephemeral port. tls selects whether
// the endpoint speaks TLS (which requires a tls_provider).
struct listener_options {
    std::string address;
    std::uint16_t port = 0;
    bool tls = false;
};

// The §3.4 timeout inventory, in milliseconds. Every field must be
// positive and at most max_timeout.
struct timeout_options {
    std::chrono::milliseconds handshake{30000};
    std::chrono::milliseconds header{30000};
    std::chrono::milliseconds body_idle{60000};
    std::chrono::milliseconds suspension{300000};
    std::chrono::milliseconds write_idle{30000};
    std::chrono::milliseconds ws_close{10000};
    std::chrono::milliseconds drain{30000};
};

// Worker concurrency. workers == 0 lets the runtime pick the degree of
// parallelism (documented, valid).
struct concurrency_options {
    std::size_t workers = 0;
};

// Provider/profile pair. provider == none pairs only with
// profile == none.
struct tls_options {
    tls_provider provider = tls_provider::none;
    tls_profile profile = tls_profile::none;
};

namespace detail {

constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

// Decimal field of an IPv4 literal: one to three digits, no leading
// zero, value at most 255.
constexpr bool valid_ipv4_field(std::string_view field) noexcept {
    if (field.empty() || field.size() > 3) return false;
    if (field.size() > 1 && field[0] == '0') return false;
    unsigned value = 0;
    for (const char c : field) {
        if (!is_digit(c)) return false;
        value = value * 10u + static_cast<unsigned>(c - '0');
    }
    return value <= 255u;
}

// Dotted-quad IPv4 literal ("192.0.2.1"): exactly four fields, no
// extra separators.
constexpr bool valid_ipv4(std::string_view v) noexcept {
    std::size_t pos = 0;
    for (int field = 0; field < 4; ++field) {
        const std::size_t dot = v.find('.', pos);
        const bool last = field == 3;
        if (last != (dot == std::string_view::npos)) return false;
        const std::size_t width =
            dot == std::string_view::npos ? std::string_view::npos
                                          : dot - pos;
        if (!valid_ipv4_field(v.substr(pos, width))) return false;
        pos = dot == std::string_view::npos ? v.size() : dot + 1;
    }
    return pos == v.size();
}

constexpr int hex_value(char c) noexcept {
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Hextet of an IPv6 literal: one to four hex digits.
constexpr bool valid_ipv6_group(std::string_view group) noexcept {
    if (group.empty() || group.size() > 4) return false;
    for (const char c : group) {
        if (hex_value(c) < 0) return false;
    }
    return true;
}

// Weight of one ":"-run field: 1 for a hextet, 2 for a trailing
// embedded IPv4, -1 when malformed.
constexpr int ipv6_field_weight(std::string_view field,
                                bool allow_ipv4_tail) noexcept {
    const bool ipv4_tail = allow_ipv4_tail
        && field.find('.') != std::string_view::npos;
    if (ipv4_tail) return valid_ipv4(field) ? 2 : -1;
    return valid_ipv6_group(field) ? 1 : -1;
}

// Group count of a ":"-separated run of groups; -1 when malformed. An
// empty run (either side of "::") contributes 0. When allow_ipv4_tail
// is set, a trailing dotted-quad field counts as two groups.
constexpr int count_ipv6_groups(std::string_view run,
                                bool allow_ipv4_tail) noexcept {
    if (run.empty()) return 0;
    int groups = 0;
    std::size_t pos = 0;
    for (;;) {
        const std::size_t colon = run.find(':', pos);
        const std::size_t width =
            colon == std::string_view::npos ? std::string_view::npos
                                            : colon - pos;
        const std::string_view field = run.substr(pos, width);
        const bool tail = allow_ipv4_tail
            && colon == std::string_view::npos;
        const int weight = ipv6_field_weight(field, tail);
        if (weight < 0) return -1;
        groups += weight;
        if (colon == std::string_view::npos) break;
        pos = colon + 1;
        if (pos == run.size()) return -1;  // trailing ':' inside the run
    }
    return groups;
}

// Numeric IPv6 literal per RFC 4291 §2.2, without a zone index: at most
// one "::" compression, at most eight groups, optional trailing
// embedded IPv4.
constexpr bool valid_ipv6(std::string_view v) noexcept {
    if (v.empty()) return false;
    const std::size_t compressed = v.find("::");
    if (compressed != std::string_view::npos
            && v.find("::", compressed + 1) != std::string_view::npos) {
        return false;  // more than one "::"
    }
    if (compressed == std::string_view::npos) {
        return count_ipv6_groups(v, true) == 8;
    }
    const int head = count_ipv6_groups(v.substr(0, compressed), false);
    const int tail = count_ipv6_groups(v.substr(compressed + 2), true);
    if (head < 0 || tail < 0) return false;
    return head + tail <= 8;
}

// V2: a listener address is "" or "*" (any local address) or a numeric
// IPv4/IPv6 literal.
constexpr bool valid_listen_address(std::string_view address) noexcept {
    return address.empty() || address == "*"
        || valid_ipv4(address) || valid_ipv6(address);
}

// V4: workers == 0 means auto and is always valid.
inline http::outcome check_workers(std::size_t workers) {
    if (workers > max_workers) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "server_options: workers " + std::to_string(workers)
                + " exceeds the maximum of " + std::to_string(max_workers));
    }
    return http::outcome::okay();
}

// V5: one timeout field must be positive and within max_timeout.
inline http::outcome check_timeout(std::string_view name,
                                   std::chrono::milliseconds value) {
    if (value <= std::chrono::milliseconds::zero()) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "server_options: timeout '" + std::string(name)
                + "' must be positive");
    }
    if (value > max_timeout) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "server_options: timeout '" + std::string(name) + "' of "
                + std::to_string(value.count())
                + "ms exceeds the maximum of "
                + std::to_string(max_timeout.count()) + "ms");
    }
    return http::outcome::okay();
}

// V5: the whole timeout inventory.
inline http::outcome check_timeouts(const timeout_options& timeouts) {
    const std::pair<std::string_view, std::chrono::milliseconds> fields[] = {
        {"handshake", timeouts.handshake},
        {"header", timeouts.header},
        {"body_idle", timeouts.body_idle},
        {"suspension", timeouts.suspension},
        {"write_idle", timeouts.write_idle},
        {"ws_close", timeouts.ws_close},
        {"drain", timeouts.drain},
    };
    for (const auto& field : fields) {
        if (const http::outcome result =
                check_timeout(field.first, field.second);
            !result.ok()) {
            return result;
        }
    }
    return http::outcome::okay();
}

// V6: every capacity in the server-scope budget must be positive and
// within its documented ceiling.
inline http::outcome check_budget(const budget_limits& limits) {
    for (std::size_t i = 0; i < resource_count; ++i) {
        const auto kind = static_cast<resource>(i);
        const std::size_t capacity = limits.get(kind);
        if (capacity == 0 || capacity > max_capacity(kind)) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "server_options: budget '" + std::string(to_string(kind))
                    + "' capacity must be positive and at most "
                    + std::to_string(max_capacity(kind)));
        }
    }
    return http::outcome::okay();
}

constexpr bool same_listener(const listener_options& a,
                             const listener_options& b) noexcept {
    return a.address == b.address && a.port == b.port && a.tls == b.tls;
}

// V2 and V3: every listener address is well-formed and no two listeners
// are identical.
inline http::outcome check_listeners(
        const std::vector<listener_options>& listeners) {
    for (std::size_t i = 0; i < listeners.size(); ++i) {
        if (!valid_listen_address(listeners[i].address)) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "server_options: listener " + std::to_string(i)
                    + " address '" + listeners[i].address
                    + "' is not \"\", \"*\", or a numeric IP literal");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (same_listener(listeners[i], listeners[j])) {
                return http::outcome(
                    http::outcome_code::invalid_argument,
                    "server_options: listener " + std::to_string(i)
                        + " duplicates listener " + std::to_string(j));
            }
        }
    }
    return http::outcome::okay();
}

constexpr bool multi_version_enabled(const protocol_set& protocols) noexcept {
    return protocols.contains(http::protocol::http_2)
        || protocols.contains(http::protocol::http_3);
}

// V7 and V8: with no provider selected, neither HTTP/2 nor HTTP/3 can
// be enabled, no profile may be named, and no listener may request TLS.
inline http::outcome check_tls_off(
        const tls_options& tls, const protocol_set& protocols,
        const std::vector<listener_options>& listeners) {
    if (tls.provider != tls_provider::none) {
        return http::outcome::okay();
    }
    if (multi_version_enabled(protocols)) {
        return http::outcome(
            http::outcome_code::not_supported,
            "server_options: HTTP/2 and HTTP/3 require a TLS provider,"
            " which is not selected");
    }
    if (tls.profile != tls_profile::none) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "server_options: a TLS profile requires a TLS provider");
    }
    for (std::size_t i = 0; i < listeners.size(); ++i) {
        if (listeners[i].tls) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "server_options: listener " + std::to_string(i)
                    + " requests TLS but no TLS provider is selected");
        }
    }
    return http::outcome::okay();
}

// V9: selecting the system provider requires naming a profile.
inline http::outcome check_provider_profile_pair(const tls_options& tls) {
    if (tls.provider == tls_provider::system_default
            && tls.profile == tls_profile::none) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "server_options: the system_default TLS provider requires a"
            " TLS profile");
    }
    return http::outcome::okay();
}

// V10: the external PSK profile negotiates only over HTTP/1 in v3.
inline http::outcome check_psk_protocols(
        const tls_options& tls, const protocol_set& protocols) {
    if (tls.profile == tls_profile::external_psk
            && multi_version_enabled(protocols)) {
        return http::outcome(
            http::outcome_code::not_supported,
            "server_options: the external_psk TLS profile is limited to"
            " HTTP/1 and cannot serve HTTP/2 or HTTP/3");
    }
    return http::outcome::okay();
}

// V11: a listener that does not speak TLS itself is reachable only over
// HTTP/1.x, so the protocol set must leave some HTTP/1.x enabled.
inline http::outcome check_plaintext_listeners(
        const protocol_set& protocols,
        const std::vector<listener_options>& listeners) {
    const bool h1_available = protocols.contains(http::protocol::http_1_0)
        || protocols.contains(http::protocol::http_1_1);
    if (h1_available) {
        return http::outcome::okay();
    }
    for (std::size_t i = 0; i < listeners.size(); ++i) {
        if (!listeners[i].tls) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "server_options: listener " + std::to_string(i)
                    + " does not use TLS and no HTTP/1.x protocol is"
                    " enabled");
        }
    }
    return http::outcome::okay();
}

// V7-V11: provider, profile, protocol, and per-listener TLS flags must
// combine into a servable configuration.
inline http::outcome check_tls_protocol_combination(
        const tls_options& tls, const protocol_set& protocols,
        const std::vector<listener_options>& listeners) {
    if (const http::outcome result = check_tls_off(tls, protocols, listeners);
        !result.ok()) {
        return result;
    }
    if (const http::outcome result = check_provider_profile_pair(tls);
        !result.ok()) {
        return result;
    }
    if (const http::outcome result = check_psk_protocols(tls, protocols);
        !result.ok()) {
        return result;
    }
    if (const http::outcome result =
            check_plaintext_listeners(protocols, listeners);
        !result.ok()) {
        return result;
    }
    return http::outcome::okay();
}

}  // namespace detail

// The single backend-neutral configuration surface for a v3 server
// (PRD-V3N-REQ-014): listeners, concurrency, timeouts, budgets, TLS
// provider/profile, and enabled protocol versions. No backend flags
// appear here (DR-V3-001); the optional provider is chosen at build
// time and selected through tls_provider.
//
// validate() is the pre-listen gate (PRD-V3N-REQ-016): every option
// combination is judged, purely, before a listener accepts any
// connection. Validation performs no I/O, opens no descriptor, reads
// no environment, and is idempotent.
class server_options {
 public:
    server_options() = default;

    // Appends a listener. No validation happens here; add_listener
    // defers every judgment to validate().
    void add_listener(listener_options listener) {
        listeners_.push_back(std::move(listener));
    }

    std::size_t listener_count() const noexcept { return listeners_.size(); }

    // Precondition: i < listener_count().
    const listener_options& listener(std::size_t i) const noexcept {
        return listeners_[i];
    }

    concurrency_options& concurrency() noexcept { return concurrency_; }
    const concurrency_options& concurrency() const noexcept {
        return concurrency_;
    }

    timeout_options& timeouts() noexcept { return timeouts_; }
    const timeout_options& timeouts() const noexcept { return timeouts_; }

    tls_options& tls() noexcept { return tls_; }
    const tls_options& tls() const noexcept { return tls_; }

    protocol_set& protocols() noexcept { return protocols_; }
    const protocol_set& protocols() const noexcept { return protocols_; }

    budget_limits& budgets() noexcept { return budgets_; }
    const budget_limits& budgets() const noexcept { return budgets_; }

    // TASK-118 (plan D3): construction-time custom error-page
    // factories, the v3 equivalents of the v2 not_found_handler and
    // method_not_allowed_handler aliases. Set before the server is
    // constructed (factories, not runtime bus seats: the aliases are
    // gone in v3). An unset factory means the v2 default page. The 405
    // Allow header is appended by the engine on every method-mismatch
    // response, custom or default. Copies of server_options share the
    // installed factories.
    using response_factory =
        concurrency::unique_function<hook_response(const http::request_head&)>;

    void not_found_response(response_factory factory) {
        not_found_response_ =
            std::make_shared<const response_factory>(std::move(factory));
    }

    // Null when unset (the v2 default page serves).
    const response_factory* not_found_response() const noexcept {
        return not_found_response_.get();
    }

    void method_not_allowed_response(response_factory factory) {
        method_not_allowed_response_ =
            std::make_shared<const response_factory>(std::move(factory));
    }

    // Null when unset (the v2 default page serves).
    const response_factory* method_not_allowed_response() const noexcept {
        return method_not_allowed_response_.get();
    }

    // Judges the whole configuration. Returns an ok outcome, or exactly
    // one typed failure whose diagnostic names the violated rule or
    // bound. Pure, idempotent, and callable on a const object.
    http::outcome validate() const {
        if (listeners_.empty()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "server_options: at least one listener is required before"
                " listening");
        }
        if (listeners_.size() > max_listeners) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "server_options: listener count "
                    + std::to_string(listeners_.size())
                    + " exceeds the maximum of "
                    + std::to_string(max_listeners));
        }
        if (const http::outcome result = detail::check_listeners(listeners_);
            !result.ok()) {
            return result;
        }
        if (const http::outcome result =
                detail::check_workers(concurrency_.workers);
            !result.ok()) {
            return result;
        }
        if (const http::outcome result = detail::check_timeouts(timeouts_);
            !result.ok()) {
            return result;
        }
        if (const http::outcome result = detail::check_budget(budgets_);
            !result.ok()) {
            return result;
        }
        if (const http::outcome result = detail::check_tls_protocol_combination(
                tls_, protocols_, listeners_);
            !result.ok()) {
            return result;
        }
        return http::outcome::okay();
    }

 private:
    std::vector<listener_options> listeners_;
    concurrency_options concurrency_;
    timeout_options timeouts_;
    tls_options tls_;
    protocol_set protocols_;
    budget_limits budgets_;
    std::shared_ptr<const response_factory> not_found_response_;
    std::shared_ptr<const response_factory> method_not_allowed_response_;
};

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_OPTIONS_HPP_
