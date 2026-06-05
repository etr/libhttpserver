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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_COOKIE_HPP_
#define SRC_HTTPSERVER_COOKIE_HPP_

// TASK-064: structured cookie value type. Replaces the v2 string-blob
// `http_response::with_cookie(name, value)` surface (now deprecated) with
// a typed value carrying name, value, domain, path, expires, max-age,
// secure, http-only, and same-site attributes. Renders to a single
// well-formed RFC 6265 §4.1 `Set-Cookie` header on the wire; pairs with
// `cookie::parse_cookie_header` to round-trip request `Cookie:` headers
// per RFC 6265 §5.4.
//
// Encoding policy: BYTE-TRANSPARENT. RFC 6265 is silent on
// percent-encoding for cookie values and browsers disagree; the library
// does NOT encode or decode for the caller. Callers that need
// percent-encoding (e.g. for cookie values containing spaces in older
// UAs) must apply it themselves before constructing the cookie.
//
// Migration / deprecation:
//   v2.0 (this release): legacy `with_cookie(name, value)` and
//                        `get_cookies()` still compile but emit a
//                        [[deprecated]] warning. New code should use
//                        `with_cookie(cookie{}.with_name(...).with_value(...))`
//                        and `get_cookies_parsed()`.
//   v2.1 (next release): legacy string-blob path is removed.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace httpserver {

/// RFC 6265bis SameSite attribute values (`Strict`, `Lax`, `None`),
/// plus the `unset` sentinel meaning "do not emit a SameSite attribute
/// at all". `none` rendering MUST coexist with `Secure`; the renderer
/// auto-coerces `Secure=true` when SameSite=None is set, per browser
/// requirements.
enum class same_site_mode : std::uint8_t {
    unset = 0,
    strict,
    lax,
    none
};

/// Structured cookie value type. Default-constructs empty. Fluent
/// setters mirror the `http_response` style: both `&` and `&&`
/// overloads, so chains compose cheaply on lvalues and rvalues alike.
///
/// Setter validation (CWE-113): all setters reject CR, LF, and NUL in
/// any field. Name additionally rejects `;`, `=`, and ASCII whitespace
/// (RFC 6265 §4.1.1 cookie-name token rule); value additionally rejects
/// `;` (attribute injection guard). Violations throw
/// `std::invalid_argument`.
///
/// Copyable and movable -- required so `std::vector<cookie>` and the
/// public `http_response::with_cookie(cookie)` by-value overload both
/// work cheaply.
class cookie final {
 public:
    cookie() noexcept = default;
    cookie(const cookie&) = default;
    cookie(cookie&&) noexcept = default;
    cookie& operator=(const cookie&) = default;
    cookie& operator=(cookie&&) noexcept = default;
    ~cookie() = default;

    // ------------------------------------------------------------------
    // Fluent setters (& / && ref-qualified, mirroring http_response).
    // Each delegates to a private do_set_* helper that validates and
    // mutates the member. The two overloads only differ in the return
    // statement; this keeps the validation in exactly one place.
    // ------------------------------------------------------------------
    cookie& with_name(std::string v) &;
    cookie&& with_name(std::string v) &&;

    cookie& with_value(std::string v) &;
    cookie&& with_value(std::string v) &&;

    cookie& with_domain(std::string v) &;
    cookie&& with_domain(std::string v) &&;

    cookie& with_path(std::string v) &;
    cookie&& with_path(std::string v) &&;

    cookie& with_expires(std::int64_t epoch_seconds) & noexcept;
    cookie&& with_expires(std::int64_t epoch_seconds) && noexcept;

    cookie& with_max_age(std::int64_t seconds) & noexcept;
    cookie&& with_max_age(std::int64_t seconds) && noexcept;

    cookie& with_secure(bool v) & noexcept;
    cookie&& with_secure(bool v) && noexcept;

    cookie& with_http_only(bool v) & noexcept;
    cookie&& with_http_only(bool v) && noexcept;

    cookie& with_same_site(same_site_mode v) & noexcept;
    cookie&& with_same_site(same_site_mode v) && noexcept;

    // ------------------------------------------------------------------
    // Const accessors. Strings are returned as `const std::string&`
    // (no allocation, stable lifetime tied to *this). Optionals are
    // returned by value: they are small POD-ish wrappers.
    // ------------------------------------------------------------------
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    [[nodiscard]] const std::string& domain() const noexcept { return domain_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::optional<std::int64_t> expires() const noexcept {
        return expires_;
    }
    [[nodiscard]] std::optional<std::int64_t> max_age() const noexcept {
        return max_age_;
    }
    [[nodiscard]] bool is_secure() const noexcept { return secure_; }
    [[nodiscard]] bool is_http_only() const noexcept { return http_only_; }
    [[nodiscard]] same_site_mode same_site() const noexcept {
        return same_site_;
    }

    /// Render this cookie to an RFC 6265 §4.1 `Set-Cookie` header value
    /// (the part after `Set-Cookie: ` on the wire). Attribute ordering
    /// is fixed regardless of setter call order: name=value, Expires,
    /// Max-Age, Domain, Path, Secure, HttpOnly, SameSite. SameSite=None
    /// auto-coerces Secure to true (browsers reject SameSite=None
    /// without Secure).
    ///
    /// Throws std::invalid_argument if name is empty -- a cookie with
    /// no name is not a valid Set-Cookie.
    [[nodiscard]] std::string to_set_cookie_header() const;

    /// Parse an RFC 6265 §5.4 `Cookie:` request-header value into a
    /// flat list of (name, value) cookies. Request cookies carry no
    /// attributes per the spec, so domain/path/etc. on the returned
    /// entries are left default-constructed. Outer double-quote pairs
    /// around a value are stripped (§5.2 step 2). Entries without an
    /// `=` are skipped. Byte-transparent: no percent-decoding.
    [[nodiscard]] static std::vector<cookie> parse_cookie_header(
        std::string_view header_value);

 private:
    std::string name_;
    std::string value_;
    std::string domain_;
    std::string path_;
    std::optional<std::int64_t> expires_;
    std::optional<std::int64_t> max_age_;
    bool secure_ = false;
    bool http_only_ = false;
    same_site_mode same_site_ = same_site_mode::unset;

    void do_set_name(std::string v);
    void do_set_value(std::string v);
    void do_set_domain(std::string v);
    void do_set_path(std::string v);

    // Render helpers split out of to_set_cookie_header() to keep
    // each function's branch count under the project CCN ceiling.
    void append_time_attributes(std::string& out) const;
    void append_target_attributes(std::string& out) const;
    void append_flag_attributes(std::string& out) const;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_COOKIE_HPP_
