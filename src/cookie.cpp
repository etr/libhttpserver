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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// TASK-064: implementation of the structured cookie value type.
// See src/httpserver/cookie.hpp for the public contract.

#include "httpserver/cookie.hpp"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace httpserver {

namespace {

// Forbidden control characters shared by every setter (CWE-113).
constexpr std::string_view kForbiddenCtrlChars("\r\n\0", 3);

// Additional reject set for cookie names: per RFC 6265 §4.1.1 a
// cookie-name is a `token` (RFC 7230 §3.2.6), which forbids separators
// and whitespace. We at minimum reject `;`, `=`, and ASCII whitespace,
// plus the control set above.
bool is_invalid_name_byte(unsigned char c) noexcept {
    // Control + whitespace + the two attribute-delimiter characters
    // that would terminate the cookie-name in a `Set-Cookie` header.
    return c <= 0x20  // includes CR, LF, NUL, SP, HT, ...
        || c == 0x7f
        || c == ';'
        || c == '=';
}

void validate_name(std::string_view v) {
    // RFC 6265 §4.1.1 + RFC 7230 §3.2.6 token rule (relaxed: we reject
    // only the bytes that would cause syntactic ambiguity in a
    // `Set-Cookie` header).
    for (unsigned char c : v) {
        if (is_invalid_name_byte(c)) {
            throw std::invalid_argument(
                "cookie::with_name: name contains forbidden character "
                "(CR, LF, NUL, whitespace, ';', '=', or other control)");
        }
    }
}

void validate_value(std::string_view v) {
    // Reject CR/LF/NUL plus `;` (attribute injection guard). The rest
    // of the RFC 6265 cookie-value ABNF (DQUOTE handling, etc.) is left
    // to the renderer; the value field is byte-transparent otherwise.
    for (unsigned char c : v) {
        if (c == '\r' || c == '\n' || c == '\0' || c == ';') {
            throw std::invalid_argument(
                "cookie::with_value: value contains forbidden character "
                "(CR, LF, NUL, or ';')");
        }
    }
}

void validate_attr_param(std::string_view setter, std::string_view v) {
    if (v.find_first_of(kForbiddenCtrlChars) != std::string_view::npos) {
        throw std::invalid_argument(
            std::string(setter) +
            ": attribute value contains forbidden control character "
            "(CR, LF, or NUL)");
    }
    // CWE-113: a semicolon in a Domain or Path attribute value would
    // be emitted verbatim by the renderer, injecting synthetic
    // attributes into the Set-Cookie header (e.g. with_path("/; Secure")
    // produces 'name=val; Path=/; Secure' even though the caller only
    // meant to set a path).
    if (v.find(';') != std::string_view::npos) {
        throw std::invalid_argument(
            std::string(setter) +
            ": attribute value contains forbidden character (';')");
    }
}

// IMF-fixdate formatter (RFC 7231 §7.1.1.1):
//     "Sun, 06 Nov 1994 08:49:37 GMT"
// Locale-independent: hand-rolls the day/month spellings rather than
// going through strftime, whose abbreviations are locale-sensitive.
std::string format_imf_fixdate(std::int64_t epoch_seconds) {
    static constexpr const char* kDayNames[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static constexpr const char* kMonthNames[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    const std::time_t t = static_cast<std::time_t>(epoch_seconds);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    // tm_wday: 0..6 (Sunday=0); tm_mon: 0..11; tm_year: years since 1900
    const int wday  = (tm.tm_wday >= 0 && tm.tm_wday < 7) ? tm.tm_wday : 0;
    const int mon   = (tm.tm_mon  >= 0 && tm.tm_mon  < 12) ? tm.tm_mon : 0;
    const int year  = tm.tm_year + 1900;
    const int mday  = tm.tm_mday;
    const int hour  = tm.tm_hour;
    const int minute = tm.tm_min;
    const int sec   = tm.tm_sec;

    char buf[40];
    std::snprintf(buf, sizeof(buf),
                  "%s, %02d %s %04d %02d:%02d:%02d GMT",
                  kDayNames[wday],
                  mday,
                  kMonthNames[mon],
                  year,
                  hour, minute, sec);
    return std::string(buf);
}

// Trim ASCII whitespace from both ends.
std::string_view trim_ws(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) {
        sv.remove_suffix(1);
    }
    return sv;
}

// Map a same_site_mode value to its serialized attribute text.
// Returns an empty view for `unset` so the renderer emits no
// SameSite attribute at all.
std::string_view same_site_attribute_text(same_site_mode m) noexcept {
    switch (m) {
        case same_site_mode::strict: return "; SameSite=Strict";
        case same_site_mode::lax:    return "; SameSite=Lax";
        case same_site_mode::none:   return "; SameSite=None";
        case same_site_mode::unset:  break;
    }
    return {};
}

// Split a cookie-pair token into trimmed name + (DQUOTE-stripped)
// value. Returns {has_value, name_view, value_view}.
//   - has_value=false: token had no `=` or had no name; caller skips.
//   - otherwise: name and value are byte-views into `tok`.
struct cookie_token_split {
    bool ok;
    std::string_view name;
    std::string_view value;
};

cookie_token_split split_cookie_token(std::string_view tok) noexcept {
    const std::size_t eq = tok.find('=');
    if (eq == std::string_view::npos || eq == 0) {
        return {false, {}, {}};
    }
    std::string_view name_sv = trim_ws(tok.substr(0, eq));
    std::string_view value_sv = tok.substr(eq + 1);

    // Strip a single outer DQUOTE pair from the value (RFC 6265
    // §5.2 step 2 / common browser practice).
    if (value_sv.size() >= 2
            && value_sv.front() == '"'
            && value_sv.back() == '"') {
        value_sv = value_sv.substr(1, value_sv.size() - 2);
    }
    if (name_sv.empty()) {
        return {false, {}, {}};
    }
    return {true, name_sv, value_sv};
}

}  // namespace

// ----------------------------------------------------------------------
// Private validating mutators.
// ----------------------------------------------------------------------
void cookie::do_set_name(std::string v) {
    validate_name(v);
    name_ = std::move(v);
}

void cookie::do_set_value(std::string v) {
    validate_value(v);
    value_ = std::move(v);
}

void cookie::do_set_domain(std::string v) {
    validate_attr_param("cookie::with_domain", v);
    domain_ = std::move(v);
}

void cookie::do_set_path(std::string v) {
    validate_attr_param("cookie::with_path", v);
    path_ = std::move(v);
}

// ----------------------------------------------------------------------
// Fluent setter pairs. & / && only differ in the return statement.
// ----------------------------------------------------------------------
cookie& cookie::with_name(std::string v) & {
    do_set_name(std::move(v));
    return *this;
}
cookie&& cookie::with_name(std::string v) && {
    do_set_name(std::move(v));
    return std::move(*this);
}

cookie& cookie::with_value(std::string v) & {
    do_set_value(std::move(v));
    return *this;
}
cookie&& cookie::with_value(std::string v) && {
    do_set_value(std::move(v));
    return std::move(*this);
}

cookie& cookie::with_domain(std::string v) & {
    do_set_domain(std::move(v));
    return *this;
}
cookie&& cookie::with_domain(std::string v) && {
    do_set_domain(std::move(v));
    return std::move(*this);
}

cookie& cookie::with_path(std::string v) & {
    do_set_path(std::move(v));
    return *this;
}
cookie&& cookie::with_path(std::string v) && {
    do_set_path(std::move(v));
    return std::move(*this);
}

cookie& cookie::with_expires(std::int64_t epoch_seconds) & noexcept {
    expires_ = epoch_seconds;
    return *this;
}
cookie&& cookie::with_expires(std::int64_t epoch_seconds) && noexcept {
    expires_ = epoch_seconds;
    return std::move(*this);
}

cookie& cookie::with_max_age(std::int64_t seconds) & noexcept {
    max_age_ = seconds;
    return *this;
}
cookie&& cookie::with_max_age(std::int64_t seconds) && noexcept {
    max_age_ = seconds;
    return std::move(*this);
}

cookie& cookie::with_secure(bool v) & noexcept {
    secure_ = v;
    return *this;
}
cookie&& cookie::with_secure(bool v) && noexcept {
    secure_ = v;
    return std::move(*this);
}

cookie& cookie::with_http_only(bool v) & noexcept {
    http_only_ = v;
    return *this;
}
cookie&& cookie::with_http_only(bool v) && noexcept {
    http_only_ = v;
    return std::move(*this);
}

cookie& cookie::with_same_site(same_site_mode v) & noexcept {
    same_site_ = v;
    return *this;
}
cookie&& cookie::with_same_site(same_site_mode v) && noexcept {
    same_site_ = v;
    return std::move(*this);
}

// ----------------------------------------------------------------------
// to_set_cookie_header() -- RFC 6265 §4.1 rendering.
//
// Attribute order is fixed (regardless of setter call order):
//   name=value; Expires=...; Max-Age=...; Domain=...; Path=...;
//   Secure; HttpOnly; SameSite=...
//
// SameSite=None auto-coerces Secure to true on emit (browsers reject
// SameSite=None without Secure).
// ----------------------------------------------------------------------
std::string cookie::to_set_cookie_header() const {
    if (name_.empty()) {
        throw std::invalid_argument(
            "cookie::to_set_cookie_header: name is empty -- a cookie "
            "with no name is not a valid Set-Cookie");
    }

    // Reserve enough for a typical cookie. The exact growth is
    // irrelevant; one reserve avoids most reallocations.
    std::string out;
    out.reserve(name_.size() + value_.size() + 96);

    out.append(name_);
    out.push_back('=');
    out.append(value_);

    append_time_attributes(out);
    append_target_attributes(out);
    append_flag_attributes(out);
    out.append(same_site_attribute_text(same_site_));
    return out;
}

void cookie::append_time_attributes(std::string& out) const {
    if (expires_.has_value()) {
        out.append("; Expires=", 10);
        out.append(format_imf_fixdate(*expires_));
    }
    if (max_age_.has_value()) {
        out.append("; Max-Age=", 10);
        out.append(std::to_string(*max_age_));
    }
}

void cookie::append_target_attributes(std::string& out) const {
    if (!domain_.empty()) {
        out.append("; Domain=", 9);
        out.append(domain_);
    }
    if (!path_.empty()) {
        out.append("; Path=", 7);
        out.append(path_);
    }
}

void cookie::append_flag_attributes(std::string& out) const {
    // SameSite=None auto-coerces Secure=true (browser requirement).
    const bool effective_secure =
        secure_ || (same_site_ == same_site_mode::none);
    if (effective_secure) {
        out.append("; Secure", 8);
    }
    if (http_only_) {
        out.append("; HttpOnly", 10);
    }
}

// ----------------------------------------------------------------------
// parse_cookie_header() -- RFC 6265 §5.4 parsing.
//
// Single-pass walker over the `; `-separated entries. Skips entries
// without an `=`. Strips outer DQUOTE pairs around values. Trims ASCII
// whitespace around each token. Byte-transparent otherwise.
// ----------------------------------------------------------------------
std::vector<cookie> cookie::parse_cookie_header(std::string_view header_value) {
    std::vector<cookie> out;
    if (header_value.empty()) {
        return out;
    }

    std::size_t pos = 0;
    while (pos <= header_value.size()) {
        const std::size_t end =
            header_value.find(';', pos);
        const std::size_t tok_end =
            (end == std::string_view::npos) ? header_value.size() : end;

        std::string_view tok = header_value.substr(pos, tok_end - pos);
        tok = trim_ws(tok);

        if (!tok.empty()) {
            const std::size_t eq = tok.find('=');
            if (eq != std::string_view::npos && eq > 0) {
                std::string_view name_sv = tok.substr(0, eq);
                std::string_view value_sv = tok.substr(eq + 1);

                // Strip a single pair of outer DQUOTEs from the value
                // (RFC 6265 §5.2 step 2 implementation detail and
                // common browser practice).
                if (value_sv.size() >= 2
                        && value_sv.front() == '"'
                        && value_sv.back() == '"') {
                    value_sv = value_sv.substr(1, value_sv.size() - 2);
                }

                // Trim trailing whitespace from the name (rare, but
                // defends against `a =b`).
                name_sv = trim_ws(name_sv);

                if (!name_sv.empty()) {
                    cookie c;
                    // Bypass validation: parsed input may technically
                    // contain bytes the validator would reject (e.g. a
                    // SP inside a quoted value). We accept what the
                    // wire delivers; the renderer is the strict side.
                    c.name_ = std::string(name_sv);
                    c.value_ = std::string(value_sv);
                    out.push_back(std::move(c));
                }
            }
        }

        if (end == std::string_view::npos) {
            break;
        }
        pos = end + 1;
    }
    return out;
}

}  // namespace httpserver
