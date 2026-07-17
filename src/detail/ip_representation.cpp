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

#include "httpserver/ip_representation.hpp"

// sockaddr family pulls in the platform's BSD-socket headers. The
// public ip_representation.hpp forward-declares `struct sockaddr` at
// file scope; the implementations below need the full definition plus
// inet_ntop / sockaddr_in[6] / NI_MAXHOST, which live in the headers
// included here.
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <winsock2.h>
#include <ws2tcpip.h>
#else  // WIN32 check
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif  // WIN32 check

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"

namespace httpserver {
namespace http {

namespace {

// Bit helpers for the 16-bit `mask`. Deliberately `constexpr`
// functions rather than function-like macros, taking the bit index as
// `unsigned int` and forcing the shift through `1u`: GCC's VRP keeps
// the `[0, 15]` bound across a function call (it loses it across a
// macro expansion site), so no file-scoped `-Warray-bounds`
// suppression is needed. `mask` is a `uint16_t`, so the
// bitwise-and-assign goes through an explicit `static_cast<uint16_t>`
// to keep the destination type visible.
constexpr bool check_bit(uint16_t var, unsigned int pos) {
    return (var & (1u << pos)) != 0;
}
constexpr void clear_bit(uint16_t& var, unsigned int pos) {
    var = static_cast<uint16_t>(var & ~(1u << pos));
}

}  // namespace

std::string get_ip_str(const struct sockaddr *sa) {
    if (!sa) throw std::invalid_argument("socket pointer is null");

    char to_ret[NI_MAXHOST];
    if (AF_INET6 == sa->sa_family) {
        inet_ntop(AF_INET6, &((reinterpret_cast<const sockaddr_in6*>(sa))->sin6_addr), to_ret, INET6_ADDRSTRLEN);
        return to_ret;
    } else if (AF_INET == sa->sa_family) {
        inet_ntop(AF_INET, &((reinterpret_cast<const sockaddr_in*>(sa))->sin_addr), to_ret, INET_ADDRSTRLEN);
        return to_ret;
    } else {
        throw std::invalid_argument("IP family must be either AF_INET or AF_INET6");
    }
}

uint16_t get_port(const struct sockaddr* sa) {
    if (!sa) throw std::invalid_argument("socket pointer is null");

    if (sa->sa_family == AF_INET) {
        return (reinterpret_cast<const struct sockaddr_in*>(sa))->sin_port;
    } else if (sa->sa_family == AF_INET6) {
        return (reinterpret_cast<const struct sockaddr_in6*>(sa))->sin6_port;
    } else {
        throw std::invalid_argument("IP family must be either AF_INET or AF_INET6");
    }
}

ip_representation::ip_representation(const struct sockaddr* ip) {
    std::fill(pieces, pieces + 16, 0);
    if (ip->sa_family == AF_INET) {
        ip_version = http_utils::IPV4;
        const in_addr* sin_addr_pt = &((reinterpret_cast<const struct sockaddr_in*>(ip))->sin_addr);
        for (int i = 0; i < 4; i++) {
            pieces[12 + i] = (reinterpret_cast<const u_char*>(sin_addr_pt))[i];
        }
    } else {
        ip_version = http_utils::IPV6;
        const in6_addr* sin_addr6_pt = &((reinterpret_cast<const struct sockaddr_in6*>(ip))->sin6_addr);
        for (int i = 0; i < 16; i++) {
            pieces[i] = (reinterpret_cast<const u_char*>(sin_addr6_pt))[i];
        }
    }
    mask = constants::DEFAULT_MASK_VALUE;
}

namespace {

// "::ffff:1.2.3.4" / "::1.2.3.4" prefix invariants: bytes 10-11 must
// be either 0x00 (pure ::-mapped) or 0xFF (v4-mapped). Lifted out of
// parse_nested_ipv4 so the surrounding function stays below the CCN
// bar.
bool ipv4_mapped_prefix_invalid(uint16_t a, uint16_t b) {
    return (a != 0 && a != 255) || (b != 0 && b != 255);
}

// The 12-byte prefix that precedes a nested IPv4 dotted-quad must be
// all-zero for bytes 0-9, with bytes 10-11 each 0x00 (pure ::-mapped)
// or 0xFF (v4-mapped). Throws invalid_argument on any other prefix.
// Lifted out of parse_nested_ipv4 so the surrounding function stays
// below the CCN bar.
void validate_ipv4_mapped_prefix(const uint16_t* pieces) {
    for (unsigned int k = 0; k < 10; k++) {
        if (pieces[k] != 0) {
            throw std::invalid_argument(
                "IP is badly formatted. Nested IPV4 can be preceded only by 0 "
                "(and, optionally, two 255 octects)");
        }
    }
    if (ipv4_mapped_prefix_invalid(pieces[10], pieces[11])) {
        throw std::invalid_argument(
            "IP is badly formatted. Nested IPV4 can be preceded only by 0 "
            "(and, optionally, two 255 octects)");
    }
}

}  // namespace

void ip_representation::parse_ipv4(const std::string& ip) {
    ip_version = http_utils::IPV4;
    auto parts = string_utilities::string_split(ip, '.');
    if (parts.size() != 4) {
        throw std::invalid_argument("IP is badly formatted. Max 4 parts in IPV4.");
    }
    for (unsigned int i = 0; i < parts.size(); i++) {
        if (parts[i] == "*") {
            clear_bit(mask, static_cast<unsigned int>(12 + i));
            continue;
        }
        const int64_t piece = strtol(parts[i].c_str(), nullptr, 10);
        if (piece < 0 || piece > 255) {
            throw std::invalid_argument("IP is badly formatted. 255 is max value for ip part.");
        }
        pieces[12+i] = static_cast<uint16_t>(piece);
    }
}

// Returns how many 16-bit groups the single empty placeholder token
// expands to when apply_ipv6_part later walks `parts`.
//
// `parts` comes from string_split(ip, ':', /*collapse=*/false), which
// emits one empty token per empty field but never a trailing empty
// token:
//   "a::b" -> {"a", "", "b"}   (one empty placeholder)
//   "::1"  -> {"", "", "1"}    (leading "::" gives TWO empties)
//   "::"   -> {"", ""}         (trailing empty dropped)
//
// `8 - (parts.size() - 1)` assumes exactly one token is the
// placeholder and every other token writes exactly one group. Two
// shapes break that assumption and are corrected below:
//   - a trailing nested-IPv4 dotted quad is one token but writes TWO
//     groups, so one fewer group is omitted (omitted -= 1);
//   - a leading "::" yields two empty tokens, so the size formula
//     counted the second empty as a group-writing token: one more
//     group is omitted (omitted += 1) and the duplicate is removed
//     via parts.erase(parts.begin()). NOTE this side effect mutates
//     the vector the caller (parse_ipv6) iterates, leaving exactly
//     one empty placeholder for apply_ipv6_part to expand.
unsigned int ip_representation::compute_ipv6_omitted_segments(std::vector<std::string>& parts) {
    unsigned int omitted = 8 - (parts.size() - 1);
    if (omitted == 0) return 0;

    int empty_count = 0;
    for (unsigned int i = 0; i < parts.size(); i++) {
        if (parts[i].size() == 0) empty_count++;
    }
    if (empty_count <= 1) return omitted;

    // > 1 empty tokens: the only legal shape is a leading "::" (two
    // consecutive empties). A trailing dotted quad writes two groups,
    // not one — see the contract comment above.
    if (parts.back().find('.') != std::string::npos) omitted -= 1;

    const bool leading_double_colon =
        empty_count == 2 && parts[0].empty() && parts[1].empty();
    if (!leading_double_colon) {
        throw std::invalid_argument(
            "IP is badly formatted. Cannot have more than one omitted segment in IPV6.");
    }
    omitted += 1;
    parts.erase(parts.begin());
    return omitted;
}

void ip_representation::parse_nested_ipv4(const std::vector<std::string>& parts,
                                          unsigned int i, int y) {
    if (y != 12) {
        throw std::invalid_argument("IP is badly formatted. Missing parts before nested IPV4.");
    }
    if (i != parts.size() - 1) {
        throw std::invalid_argument("IP is badly formatted. Nested IPV4 should be at the end");
    }
    auto subparts = string_utilities::string_split(parts[i], '.');
    if (subparts.size() != 4) {
        throw std::invalid_argument("IP is badly formatted. Nested IPV4 can have max 4 parts.");
    }
    // Bytes 0-9 must be zero; bytes 10-11 must be 0x00 or 0xFF.
    validate_ipv4_mapped_prefix(pieces);
    for (unsigned int ii = 0; ii < subparts.size(); ii++) {
        if (subparts[ii] == "*") {
            clear_bit(mask, static_cast<unsigned int>(y + ii));
            continue;
        }
        const int64_t subpart = strtol(subparts[ii].c_str(), nullptr, 10);
        if (subpart < 0 || subpart > 255) {
            throw std::invalid_argument("IP is badly formatted. 255 is max value for ip part.");
        }
        pieces[y+ii] = static_cast<uint16_t>(subpart);
    }
}

void ip_representation::apply_ipv6_part(std::vector<std::string>& parts, unsigned int i,
                                        int& y, unsigned int omitted) {
    auto& part = parts[i];
    if (part == "*") {
        clear_bit(mask, static_cast<unsigned int>(y));
        clear_bit(mask, static_cast<unsigned int>(y + 1));
        y += 2;
        return;
    }
    if (part.empty()) {
        // Placeholder for one or more omitted segments. Zero-fill the
        // implied slots; the bump is `omitted` segments x 2 bytes each.
        for (unsigned int o = 0; o < omitted; o++) {
            pieces[y]   = 0;
            pieces[y+1] = 0;
            y += 2;
        }
        return;
    }
    if (part.size() < 4) {
        // Pad short hex group to 4 chars (e.g. "f" -> "000f").
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(4) << part;
        part = ss.str();
    }
    if (part.size() == 4) {
        const std::string hi_str = part.substr(0, 2);
        const std::string lo_str = part.substr(2, 2);
        char* endp = nullptr;
        const int64_t hi = strtol(hi_str.c_str(), &endp, 16);
        if (*endp != '\0') {
            throw std::invalid_argument(
                "IP is badly formatted. IPV6 part contains a non-hex character.");
        }
        const int64_t lo = strtol(lo_str.c_str(), &endp, 16);
        if (*endp != '\0') {
            throw std::invalid_argument(
                "IP is badly formatted. IPV6 part contains a non-hex character.");
        }
        pieces[y]   = static_cast<uint16_t>(hi);
        pieces[y+1] = static_cast<uint16_t>(lo);
        y += 2;
        return;
    }
    if (part.find('.') == std::string::npos) {
        throw std::invalid_argument(
            "IP is badly formatted. IPV6 parts can have max 4 characters (or nest an IPV4)");
    }
    parse_nested_ipv4(parts, i, y);
}

void ip_representation::parse_ipv6(const std::string& ip) {
    ip_version = http_utils::IPV6;
    auto parts = string_utilities::string_split(ip, ':', false);
    if (parts.size() > 8) {
        throw std::invalid_argument("IP is badly formatted. Max 8 parts in IPV6.");
    }
    unsigned int omitted = compute_ipv6_omitted_segments(parts);
    int y = 0;
    for (unsigned int i = 0; i < parts.size(); i++) {
        apply_ipv6_part(parts, i, y, omitted);
    }
}

ip_representation::ip_representation(const std::string& ip) {
    mask = constants::DEFAULT_MASK_VALUE;
    std::fill(pieces, pieces + 16, 0);
    if (ip.find(':') != std::string::npos) {
        parse_ipv6(ip);
    } else {
        parse_ipv4(ip);
    }
}

namespace {

// Add (16 - i) * piece[i] for both ip representations when both have
// the i-th octet masked in. Pulled out of operator< so the surrounding
// function stays below the CCN bar.
void accumulate_octet_score(const ip_representation& a,
                            const ip_representation& b, int i,
                            int64_t& a_score, int64_t& b_score) {
    if (!(check_bit(a.mask, static_cast<unsigned int>(i))
          && check_bit(b.mask, static_cast<unsigned int>(i)))) return;
    // Cast the (16 - i) factor to int64_t before the multiply so the product
    // is computed in the destination type. Without the cast the multiplication
    // happens in int and only the result is widened, which CodeQL flags as a
    // potential overflow.
    a_score += static_cast<int64_t>(16 - i) * a.pieces[i];
    b_score += static_cast<int64_t>(16 - i) * b.pieces[i];
}

// True when both v4-mapped-prefix octets on a and b are 0x00 or 0xFF.
// Mirrors the "::ffff:" / "::"-prefix invariant from parse_nested_ipv4.
bool is_v4_mapped_prefix_octet_pair(uint16_t a, uint16_t b) {
    return (a == 0x00 || a == 0xFF) && (b == 0x00 || b == 0xFF);
}

}  // namespace

// Strict-weak ordering for std::set<ip_representation> (the allow and
// deny lists). NOT lexicographic: each operand gets a position-weighted
// score — the sum over octet index i of (16 - i) * pieces[i] — and the
// totals are compared. An octet contributes only when it is unmasked
// on BOTH operands (see accumulate_octet_score), so wildcard octets on
// either side drop out of the comparison entirely: a wildcard entry
// and any address it overlaps compare EQUIVALENT (neither is less).
//
// Bytes 10-11 (the v4-mapped prefix position) are excluded from the
// first accumulation pass so they can be handled specially. If the
// scores over all other octets are tied AND both operands' bytes 10
// and 11 each hold a v4-mapped-prefix value (0x00 or 0xFF, see
// is_v4_mapped_prefix_octet_pair), the mid-function `return false`
// means "not less". The condition is symmetric in *this and b, so the
// reversed comparison also returns false: keys such as
// `::ffff:x.y.z.w`, `::x.y.z.w` and plain IPv4 x.y.z.w are treated as
// EQUIVALENT rather than ordered by the 0xFF bytes. Only when that
// case does not apply are bytes 10-11 folded into the scores and the
// totals compared.
//
// insert_wildcard_aware in src/detail/webserver_lifecycle.cpp relies on
// this equivalence: std::set::find must locate a stored entry that
// merely OVERLAPS the new one (wildcard-subsumed, or v4-mapped vs
// plain form) so the more-permissive entry can win.
bool ip_representation::operator <(const ip_representation& b) const {
    int64_t this_score = 0;
    int64_t b_score = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 10 || i == 11) continue;
        accumulate_octet_score(*this, b, i, this_score, b_score);
    }

    if (this_score == b_score
            && is_v4_mapped_prefix_octet_pair(pieces[10], b.pieces[10])
            && is_v4_mapped_prefix_octet_pair(pieces[11], b.pieces[11])) {
        return false;
    }

    accumulate_octet_score(*this, b, 10, this_score, b_score);
    accumulate_octet_score(*this, b, 11, this_score, b_score);

    return this_score < b_score;
}

}  // namespace http
}  // namespace httpserver
