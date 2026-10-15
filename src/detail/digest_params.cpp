/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/digest_params.hpp>
#include <string>
#include <utility>

namespace httpserver::detail::digest::params_detail {

bool is_tchar(char c) noexcept {
    if (c >= '0' && c <= '9') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    return k_tchar_specials.find(c) != std::string_view::npos;
}

bool is_ctl(char c) noexcept {
    const unsigned value = static_cast<unsigned char>(c);
    return value < 0x20 || value == 0x7f;
}

void skip_ows(std::string_view v, std::size_t& pos) noexcept {
    while (pos < v.size() && is_ows(v[pos])) ++pos;
}

bool is_eight_hex(const std::string& value) noexcept {
    if (value.size() != 8) return false;
    for (const char c : value) {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        const bool upper = c >= 'A' && c <= 'F';
        if (!digit && !lower && !upper) return false;
    }
    return true;
}

bool match_scheme(std::string_view v, std::size_t& pos) noexcept {
    std::size_t i = 0;
    while (i < v.size() && is_tchar(v[i])) ++i;
    if (!ascii_iequal(v.substr(0, i), "Digest")) return false;
    std::size_t separator = i;
    while (separator < v.size() && is_ows(v[separator])) ++separator;
    if (separator == i || separator == v.size()) return false;
    pos = separator;
    return true;
}

bool parse_name(std::string_view v, std::size_t& pos, std::string& name) {
    const std::size_t start = pos;
    while (pos < v.size() && is_tchar(v[pos])) ++pos;
    const std::size_t end = pos;
    if (end == start || end - start > k_max_name) return false;
    skip_ows(v, pos);
    if (pos == v.size() || v[pos] != '=') return false;
    name.assign(v.substr(start, end - start));
    ++pos;
    return true;
}

bool parse_quoted(std::string_view v, std::size_t& pos, std::string& out) {
    ++pos;
    for (;;) {
        if (pos == v.size()) return false;  // unterminated quote
        const char c = v[pos];
        if (is_ctl(c)) return false;
        if (c == '"') {
            ++pos;
            return true;
        }
        if (c == '\\') {
            if (pos + 1 >= v.size()) return false;
            if (is_ctl(v[pos + 1])) return false;
            out.push_back(v[pos + 1]);
            pos += 2;
        } else {
            out.push_back(c);
            ++pos;
        }
        if (out.size() > k_max_value) return false;
    }
}

bool parse_value(std::string_view v, std::size_t& pos, std::string& out) {
    skip_ows(v, pos);
    if (pos == v.size()) return false;
    if (v[pos] == '"') return parse_quoted(v, pos, out);
    const std::size_t start = pos;
    while (pos < v.size() && is_tchar(v[pos])) ++pos;
    if (pos == start || pos - start > k_max_value) return false;
    out.assign(v.substr(start, pos - start));
    return true;
}

bool parse_one(std::string_view v, std::size_t& pos, std::string& name, std::string& value) {
    if (v[pos] == ',') return false;
    if (!parse_name(v, pos, name)) return false;
    if (!parse_value(v, pos, value)) return false;
    skip_ows(v, pos);
    if (pos < v.size()) {
        if (v[pos] != ',') return false;
        ++pos;
        skip_ows(v, pos);
        if (pos == v.size()) return false;
    }
    return true;
}

bool assign_param(digest_params& parsed, unsigned& seen, const std::string& name, std::string&& value) {
    for (std::size_t i = 0; i < std::size(k_param_rows); ++i) {
        if (!ascii_iequal(name, k_param_rows[i].name)) continue;
        const unsigned bit = 1u << i;
        if ((seen & bit) != 0) return false;
        if (k_param_rows[i].name == "nc" && !is_eight_hex(value)) {
            return false;
        }
        seen |= bit;
        parsed.*(k_param_rows[i].field) = std::move(value);
        return true;
    }
    return true;
}

bool mandatories_present(const digest_params& parsed) noexcept { return !parsed.username.empty() && !parsed.realm.empty() && !parsed.nonce.empty() && !parsed.uri.empty() && !parsed.response.empty(); }

}  // namespace httpserver::detail::digest::params_detail

namespace httpserver::detail::digest {

params_status parse_digest_credentials(std::string_view value, digest_params& out) {
    using params_detail::assign_param;
    using params_detail::k_max_params;
    using params_detail::k_max_whole;
    using params_detail::mandatories_present;
    using params_detail::match_scheme;
    using params_detail::parse_one;
    if (value.size() > k_max_whole) return params_status::malformed;
    std::size_t pos = 0;
    if (!match_scheme(value, pos)) return params_status::malformed;

    digest_params parsed;
    unsigned seen = 0;
    std::size_t count = 0;
    for (;;) {
        params_detail::skip_ows(value, pos);
        if (pos == value.size()) break;
        if (++count > k_max_params) return params_status::malformed;
        std::string name;
        std::string param_value;
        if (!parse_one(value, pos, name, param_value)) {
            return params_status::malformed;
        }
        if (!assign_param(parsed, seen, name, std::move(param_value))) {
            return params_status::malformed;
        }
    }
    if (!mandatories_present(parsed)) return params_status::malformed;
    out = std::move(parsed);
    return params_status::ok;
}

}  // namespace httpserver::detail::digest
