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

// Guard deliberately suffixed _V3_: the pure path-derived token
// SRC_HTTPSERVER_HTTP_METHOD_HPP_ collides with the guard of the
// unrelated v2 public header httpserver/http_method.hpp, which would
// make this header a no-op in any TU that also includes the v2
// umbrella <httpserver.hpp>.
#ifndef SRC_HTTPSERVER_HTTP_METHOD_HPP_V3_  // NOLINT(build/header_guard)
#define SRC_HTTPSERVER_HTTP_METHOD_HPP_V3_  // NOLINT(build/header_guard)

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace httpserver {

namespace http {

// Identifier for an HTTP method known to the engine. The identifier
// `del` (rather than `delete`) avoids the C++ keyword; the wire token
// for it is "DELETE". `extension` covers any RFC 9110 token outside the
// known set; `unknown_` is the invalid sentinel and must remain the
// last enumerator. Library-owned (DR-V3-001): no backend numeric IDs.
enum class method_id : std::uint8_t {
    get,
    head,
    post,
    put,
    del,        // wire token "DELETE"
    connect,
    options,
    trace,
    patch,
    extension,  // any RFC 9110 token outside the known set
    unknown_,   // invalid sentinel; must remain last
};

namespace detail {

// Case-folded lookup table for the known methods: wire token per
// method_id slot, empty for extension/unknown_.
inline constexpr std::array<std::string_view, 11> method_tokens = {
    "GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT",
    "OPTIONS", "TRACE", "PATCH", "", "",
};

// Lookup table for the RFC 9110 tchar set: every byte allowed in a
// token (ALPHA / DIGIT / "!#$%&'*+-.^_`|~").
inline constexpr std::array<bool, 256> tchar_table = [] {
    std::array<bool, 256> t {};
    for (const char c : std::string_view("!#$%&'*+-.^_`|~0123456789")) {
        t[static_cast<unsigned char>(c)] = true;
    }
    for (char c = 'A'; c <= 'Z'; ++c) {
        t[static_cast<unsigned char>(c)] = true;
    }
    for (char c = 'a'; c <= 'z'; ++c) {
        t[static_cast<unsigned char>(c)] = true;
    }
    return t;
} ();

// True iff every byte of v is in the RFC 9110 token set (tchar).
constexpr bool is_token(std::string_view v) noexcept {
    if (v.empty()) return false;
    for (const char c : v) {
        if (!tchar_table[static_cast<unsigned char>(c)]) return false;
    }
    return true;
}

// ASCII-uppercase of an assumed-token string.
constexpr std::uint8_t ascii_upper(const char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') ? static_cast<std::uint8_t>(u - 'a' + 'A')
                                  : u;
}

}  // namespace detail

// Semantic HTTP method value. A known method stores only its
// method_id; an extension method additionally stores the uppercased
// wire token. Comparisons are case-insensitive by construction (both
// representations are normalized). The default-constructed value is
// the invalid unknown_ state.
class method {
 public:
    // Precondition: id is one of the nine known enumerators (below
    // extension). No validation is performed for constexpr friendliness.
    static constexpr method known(method_id id) noexcept {
        return method{id};
    }

    // Extension method from an RFC 9110 token; the name is uppercased.
    // Precondition: name is a valid token (checked by parse(); the
    // factory itself assumes a pre-validated token).
    static method extension(std::string name) {
        std::string upper;
        upper.reserve(name.size());
        for (const char c : name) {
            upper.push_back(static_cast<char>(detail::ascii_upper(c)));
        }
        return method{method_id::extension, std::move(upper)};
    }

    // Parse a wire token. Normalizes case, resolves known methods
    // without allocating, and returns an extension method for any other
    // valid RFC 9110 token; nullopt for anything else (empty input,
    // separators, CTLs, spaces).
    static constexpr std::optional<method> parse(std::string_view token) {
        if (!detail::is_token(token)) return std::nullopt;

        const std::size_t n = token.size();
        for (std::size_t id = 0;
             id < static_cast<std::size_t>(method_id::extension); ++id) {
            const std::string_view known_token = detail::method_tokens[id];
            if (known_token.size() != n) continue;
            bool match = true;
            for (std::size_t i = 0; i < n; ++i) {
                if (detail::ascii_upper(token[i])
                        != detail::ascii_upper(known_token[i])) {
                    match = false;
                    break;
                }
            }
            if (match) return method{static_cast<method_id>(id)};
        }

        std::string upper(token);
        for (char& c : upper) {
            c = static_cast<char>(detail::ascii_upper(c));
        }
        return method{method_id::extension, std::move(upper)};
    }

    constexpr method() noexcept = default;

    constexpr method_id id() const noexcept { return id_; }

    constexpr bool is_extension() const noexcept {
        return id_ == method_id::extension;
    }

    // False only for the default/unknown_ state.
    constexpr bool valid() const noexcept {
        return id_ != method_id::unknown_;
    }

    // Upper-case wire token; empty for the invalid state.
    constexpr std::string_view name() const noexcept {
        if (id_ == method_id::unknown_) return {};
        if (id_ == method_id::extension) return ext_name_;
        return detail::method_tokens[static_cast<std::size_t>(id_)];
    }

    friend constexpr bool operator==(const method& a, const method& b) noexcept {
        return a.id_ == b.id_ && (a.id_ != method_id::extension
                                  || a.ext_name_ == b.ext_name_);
    }

    friend constexpr bool operator!=(const method& a, const method& b) noexcept {
        return !(a == b);
    }

 private:
    constexpr explicit method(method_id id) noexcept : id_(id) { }

    explicit method(method_id id, std::string ext_name) noexcept
        : id_(id), ext_name_(std::move(ext_name)) { }

    method_id id_ = method_id::unknown_;
    std::string ext_name_;  // uppercased token; empty for known methods
};

// Wire token of the method; empty for the invalid state.
inline constexpr std::string_view to_string(const method& m) noexcept {
    return m.name();
}

// TASK-118: a set over the nine known method slots (the v2 route
// family vocabulary). Extension methods are not representable -- they
// register singly through route_registry::route -- and the unknown_
// state is never a member. A constexpr bitset: build via set(), probe
// via contains(), and render with to_string() in method_id order (the
// Allow wire form, comma-space separated).
class method_set {
 public:
    constexpr method_set() noexcept = default;

    // Sets one known-method slot. extension and unknown_ ids are
    // ignored (an extension method has no slot to occupy).
    constexpr void set(method_id id) noexcept {
        if (is_known(id)) mask_ |= bit(id);
    }

    // True iff @p m is a known method whose slot is set. Extension
    // methods and the unknown_ state are never members.
    constexpr bool contains(const method& m) const noexcept {
        return m.valid() && !m.is_extension() && (mask_ & bit(m.id())) != 0;
    }

    // False only for the empty set.
    constexpr bool any() const noexcept { return mask_ != 0; }

    // Union in place (the registry's Allow merge).
    constexpr void merge(const method_set& other) noexcept {
        mask_ |= other.mask_;
    }

    friend constexpr bool operator==(const method_set& a,
                                     const method_set& b) noexcept {
        return a.mask_ == b.mask_;
    }

    friend constexpr bool operator!=(const method_set& a,
                                     const method_set& b) noexcept {
        return !(a == b);
    }

 private:
    static constexpr std::size_t known_slots =
        static_cast<std::size_t>(method_id::extension);

    static constexpr bool is_known(method_id id) noexcept {
        return static_cast<std::size_t>(id) < known_slots;
    }

    static constexpr std::uint16_t bit(method_id id) noexcept {
        return static_cast<std::uint16_t>(
            1u << static_cast<unsigned>(id));
    }

    std::uint16_t mask_ = 0;
};

// The Allow wire form: every set slot's wire token in method_id order
// (GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH),
// comma-space separated; empty for the empty set.
inline std::string to_string(const method_set& set) {
    std::string out;
    for (std::size_t id = 0; id < static_cast<std::size_t>(method_id::extension);
            ++id) {
        if (!set.contains(method::known(static_cast<method_id>(id)))) {
            continue;
        }
        if (!out.empty()) out.append(", ");
        out.append(detail::method_tokens[id]);
    }
    return out;
}

}  // namespace http

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_HTTP_METHOD_HPP_V3_  // NOLINT(build/header_guard)
