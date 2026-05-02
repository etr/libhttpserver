/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#ifndef SRC_HTTPSERVER_HTTP_METHOD_HPP_
#define SRC_HTTPSERVER_HTTP_METHOD_HPP_

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace httpserver {

// Strongly-typed HTTP method primitive consumed by http_resource, the
// route table, and lambda registration. The identifier `del` (rather
// than `delete`) avoids the C++ keyword; the wire-protocol token
// returned by to_string() is "DELETE".
//
// `count_` is a sentinel and must remain the last enumerator. Any new
// method goes immediately before it; to_string()'s switch must also be
// updated. The 32-bit underlying storage of method_set leaves 23 bits
// of growth headroom past the 9 standard methods (PRD-REQ-REQ-003,
// DR-006).
enum class http_method : std::uint8_t {
    get,
    head,
    post,
    put,
    del,        // wire token "DELETE"
    connect,
    options,
    trace,
    patch,
    count_      // sentinel; must remain last
};

namespace detail {

// Bit position for an http_method enumerator. Defined here so member
// functions and free operators can share one definition. Out-of-range
// inputs (>= 32) are masked out by the caller; this helper is total.
constexpr std::uint32_t method_bit(http_method m) noexcept {
    return std::uint32_t{1} << static_cast<std::uint8_t>(m);
}

// All-valid-methods mask: bits 0 .. count_-1 set, the rest cleared.
constexpr std::uint32_t valid_method_mask() noexcept {
    return (std::uint32_t{1}
            << static_cast<std::uint8_t>(http_method::count_)) - 1u;
}

}  // namespace detail

// Fixed-size set of allowed HTTP methods (one bit per http_method
// enumerator). Aggregate so it stays standard layout / trivially
// copyable; brace-init with {bits} is fine, and default-init gives an
// empty set. Comparison is defaulted (constexpr noexcept).
struct method_set {
    std::uint32_t bits = 0;

    constexpr bool contains(http_method m) const noexcept {
        return (bits & detail::method_bit(m)) != 0u;
    }

    constexpr method_set& set(http_method m) noexcept {
        bits |= detail::method_bit(m);
        return *this;
    }

    constexpr method_set& clear(http_method m) noexcept {
        bits &= ~detail::method_bit(m);
        return *this;
    }

    // set_all() and clear_all() operate over the valid-method window
    // (bits 0 .. count_-1); bits beyond count_ stay zero so complement
    // round-trips cleanly.
    constexpr method_set& set_all() noexcept {
        bits = detail::valid_method_mask();
        return *this;
    }

    constexpr method_set& clear_all() noexcept {
        bits = 0u;
        return *this;
    }

    friend constexpr bool operator==(method_set, method_set) noexcept = default;
};

// to_string returns the uppercase RFC 9110 wire token for use in logs
// and the 405 Allow: header. Total over the 9 declared enumerators;
// any other underlying value (only producible via static_cast) returns
// an empty view rather than crashing — keeps logging robust against
// stale enum values.
constexpr std::string_view to_string(http_method m) noexcept {
    switch (m) {
        case http_method::get:     return std::string_view{"GET"};
        case http_method::head:    return std::string_view{"HEAD"};
        case http_method::post:    return std::string_view{"POST"};
        case http_method::put:     return std::string_view{"PUT"};
        case http_method::del:     return std::string_view{"DELETE"};
        case http_method::connect: return std::string_view{"CONNECT"};
        case http_method::options: return std::string_view{"OPTIONS"};
        case http_method::trace:   return std::string_view{"TRACE"};
        case http_method::patch:   return std::string_view{"PATCH"};
        case http_method::count_:  return std::string_view{};
    }
    return std::string_view{};
}

// Bitwise composition. Operators on http_method yield a method_set so
// `get | post` is a two-method set ready to feed into route_entry.
// All operators are constexpr noexcept — usable in compile-time
// context (the "consteval-friendly" requirement) AND at runtime, which
// the route-table writer path needs.

constexpr method_set operator|(http_method a, http_method b) noexcept {
    return method_set{detail::method_bit(a) | detail::method_bit(b)};
}

constexpr method_set operator&(http_method a, http_method b) noexcept {
    return method_set{detail::method_bit(a) & detail::method_bit(b)};
}

constexpr method_set operator^(http_method a, http_method b) noexcept {
    return method_set{detail::method_bit(a) ^ detail::method_bit(b)};
}

// ~http_method == "every valid method except this one" (bounded to the
// count_ window).
constexpr method_set operator~(http_method m) noexcept {
    return method_set{detail::valid_method_mask() & ~detail::method_bit(m)};
}

constexpr method_set operator|(method_set a, method_set b) noexcept {
    return method_set{a.bits | b.bits};
}

constexpr method_set operator&(method_set a, method_set b) noexcept {
    return method_set{a.bits & b.bits};
}

constexpr method_set operator^(method_set a, method_set b) noexcept {
    return method_set{a.bits ^ b.bits};
}

// ~method_set is also bounded to the valid-method window so
// `~method_set{}.set_all() == method_set{}` holds — i.e. complement is
// an involution within the 9-bit window. Without the masking, unused
// upper bits would leak in and break round-tripping.
constexpr method_set operator~(method_set s) noexcept {
    return method_set{detail::valid_method_mask() & ~s.bits};
}

// Mixed (method_set, http_method) overloads — convenience for the
// common "set | method" composition.
constexpr method_set operator|(method_set s, http_method m) noexcept {
    return method_set{s.bits | detail::method_bit(m)};
}

constexpr method_set operator|(http_method m, method_set s) noexcept {
    return s | m;
}

constexpr method_set operator&(method_set s, http_method m) noexcept {
    return method_set{s.bits & detail::method_bit(m)};
}

constexpr method_set operator&(http_method m, method_set s) noexcept {
    return s & m;
}

constexpr method_set operator^(method_set s, http_method m) noexcept {
    return method_set{s.bits ^ detail::method_bit(m)};
}

constexpr method_set operator^(http_method m, method_set s) noexcept {
    return s ^ m;
}

// Compound assignment on method_set (free functions to match the
// non-member binary operators above).
constexpr method_set& operator|=(method_set& s, method_set rhs) noexcept {
    s.bits |= rhs.bits;
    return s;
}

constexpr method_set& operator&=(method_set& s, method_set rhs) noexcept {
    s.bits &= rhs.bits;
    return s;
}

constexpr method_set& operator^=(method_set& s, method_set rhs) noexcept {
    s.bits ^= rhs.bits;
    return s;
}

constexpr method_set& operator|=(method_set& s, http_method m) noexcept {
    s.bits |= detail::method_bit(m);
    return s;
}

constexpr method_set& operator&=(method_set& s, http_method m) noexcept {
    s.bits &= detail::method_bit(m);
    return s;
}

constexpr method_set& operator^=(method_set& s, http_method m) noexcept {
    s.bits ^= detail::method_bit(m);
    return s;
}

// Layout / width invariants — pinned once at namespace scope so every
// TU including this header gets the protection. Placed AFTER the
// method_set definition so is_standard_layout_v / sizeof are well-formed.
static_assert(static_cast<std::uint8_t>(http_method::count_) <= 32,
              "http_method::count_ must fit in method_set's 32-bit bitmask");
static_assert(std::is_standard_layout_v<method_set>,
              "method_set must be standard layout");
static_assert(std::is_trivially_copyable_v<method_set>,
              "method_set must be trivially copyable");
static_assert(sizeof(method_set) == sizeof(std::uint32_t),
              "method_set must be exactly the size of its underlying uint32_t");

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_METHOD_HPP_
