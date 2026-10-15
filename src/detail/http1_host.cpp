/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/http1_host.hpp>

namespace httpserver::detail::http1_host {

bool name_character(char c) { return alphanumeric(c) || std::string_view("-._~!$&'()*+,;=").find(c) != std::string_view::npos; }

bool reg_name(std::string_view name) {
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '%') {
            if (name.size() - i < 3 || hex_digit_value(name[i + 1]) < 0 || hex_digit_value(name[i + 2]) < 0) return false;
            i += 2;
        } else if (!name_character(name[i])) {
            return false;
        }
    }
    return true;
}

bool future_literal(std::string_view literal) {
    auto dot = literal.find('.');
    if (dot == std::string_view::npos || dot <= 1 || dot + 1 == literal.size()) return false;
    for (char c : literal.substr(1, dot - 1))
        if (hex_digit_value(c) < 0) return false;
    for (char c : literal.substr(dot + 1))
        if (!name_character(c) && c != ':') return false;
    return true;
}

bool ip_literal(std::string_view literal) {
    if (literal.empty()) return false;
    if (literal.front() == 'v' || literal.front() == 'V') return future_literal(literal);
    return literal.find(':') != std::string_view::npos && net::parse_address(literal).has_value();
}

bool port(std::string_view value) {
    for (char c : value)
        if (c < '0' || c > '9') return false;
    return true;
}

}  // namespace httpserver::detail::http1_host

namespace httpserver::detail {

bool valid_http1_host(std::string_view host) {
    if (host.starts_with("[")) {
        auto end = host.find(']');
        if (end == std::string_view::npos || !http1_host::ip_literal(host.substr(1, end - 1))) return false;
        auto suffix = host.substr(end + 1);
        return suffix.empty() || (suffix.front() == ':' && http1_host::port(suffix.substr(1)));
    }
    auto colon = host.find(':');
    if (colon == std::string_view::npos) return http1_host::reg_name(host);
    return http1_host::reg_name(host.substr(0, colon)) && http1_host::port(host.substr(colon + 1));
}

bool valid_http1_host(const http::request_head& head) {
    auto hosts = head.head_fields.all("host");
    if (hosts.size() > 1 || (head.request_protocol == http::protocol::http_1_1 && hosts.empty())) return false;
    return hosts.empty() || valid_http1_host(hosts.front());
}

}  // namespace httpserver::detail
