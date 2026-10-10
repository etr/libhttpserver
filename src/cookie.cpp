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

#include <httpserver/http/cookie.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace httpserver {
namespace http {
namespace {

std::string_view trim_token(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t");
    if (begin == std::string_view::npos) return {};
    return value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
}

bool forbidden_value(std::string_view value) {
    return value.find_first_of(std::string_view("\r\n\0;", 4)) != std::string_view::npos;
}

bool forbidden_name(std::string_view name) {
    return name.empty() || std::any_of(name.begin(), name.end(), [](unsigned char byte) {
        return byte <= 0x20 || byte == 0x7f || byte == ';' || byte == '=';
    });
}

bool forbidden_attribute(std::string_view value) {
    return forbidden_value(value) || value.find(',') != std::string_view::npos;
}

std::string format_date(std::int64_t epoch) {
    namespace chrono = std::chrono;
    const chrono::sys_seconds instant{chrono::seconds{epoch}};
    const auto date = chrono::floor<chrono::days>(instant);
    const chrono::year_month_day civil{date};
    const chrono::hh_mm_ss time{instant - date};
    constexpr const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%s, %02u %s %04d %02d:%02d:%02d GMT",
        weekdays[chrono::weekday{date}.c_encoding()], static_cast<unsigned>(civil.day()), months[static_cast<unsigned>(civil.month()) - 1],
        static_cast<int>(civil.year()), static_cast<int>(time.hours().count()),
        static_cast<int>(time.minutes().count()), static_cast<int>(time.seconds().count()));
    return buffer;
}

}  // namespace

std::vector<cookie> parse_cookie_header(std::string_view header) {
    std::vector<cookie> values;
    while (!header.empty()) {
        const auto separator = header.find(';');
        const auto token = trim_token(header.substr(0, separator));
        const auto equals = token.find('=');
        if (equals != std::string_view::npos) {
            const auto name = trim_token(token.substr(0, equals));
            auto value = token.substr(equals + 1);
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value.remove_prefix(1);
                value.remove_suffix(1);
            }
            if (!name.empty()) values.push_back(cookie{std::string(name), std::string(value)});
        }
        if (separator == std::string_view::npos) break;
        header.remove_prefix(separator + 1);
    }
    return values;
}

outcome render_set_cookie(const cookie& value, std::string& output) {
    if (forbidden_name(value.name) || forbidden_value(value.value)
            || forbidden_attribute(value.domain) || forbidden_attribute(value.path)) {
        return {outcome_code::invalid_argument, "cookie contains forbidden name, value or attribute bytes"};
    }
    if (value.expires && (*value.expires < -62167219200LL || *value.expires > 253402300799LL)) {
        return {outcome_code::invalid_argument, "cookie Expires is outside years 0000..9999"};
    }
    std::string_view same_site;
    switch (value.same_site) {
        case same_site_mode::unset: break;
        case same_site_mode::strict: same_site = "; SameSite=Strict"; break;
        case same_site_mode::lax: same_site = "; SameSite=Lax"; break;
        case same_site_mode::none: same_site = "; SameSite=None"; break;
        default: return {outcome_code::invalid_argument, "invalid cookie SameSite mode"};
    }
    std::string rendered = value.name + "=" + value.value;
    if (value.expires) rendered += "; Expires=" + format_date(*value.expires);
    if (value.max_age) rendered += "; Max-Age=" + std::to_string(*value.max_age);
    if (!value.domain.empty()) rendered += "; Domain=" + value.domain;
    if (!value.path.empty()) rendered += "; Path=" + value.path;
    if (value.secure || value.same_site == same_site_mode::none) rendered += "; Secure";
    if (value.http_only) rendered += "; HttpOnly";
    rendered += same_site;
    output = std::move(rendered);
    return outcome::okay();
}
}  // namespace http
}  // namespace httpserver
