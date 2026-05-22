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

// Local bit ops mirror the ones in http_utils.cpp; both TUs need them
// and a shared header would be overkill for two one-line macros.
#pragma GCC diagnostic ignored "-Warray-bounds"
#define CHECK_BIT(var, pos) ((var) & (1 << (pos)))
#define CLEAR_BIT(var, pos) ((var) &= ~(1 << (pos)))

namespace httpserver {
namespace http {

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

}  // namespace

void ip_representation::parse_ipv4(const std::string& ip) {
    ip_version = http_utils::IPV4;
    auto parts = string_utilities::string_split(ip, '.');
    if (parts.size() != 4) {
        throw std::invalid_argument("IP is badly formatted. Max 4 parts in IPV4.");
    }
    for (unsigned int i = 0; i < parts.size(); i++) {
        if (parts[i] == "*") {
            CLEAR_BIT(mask, 12+i);
            continue;
        }
        pieces[12+i] = strtol(parts[i].c_str(), nullptr, 10);
        if (pieces[12+i] > 255) {
            throw std::invalid_argument("IP is badly formatted. 255 is max value for ip part.");
        }
    }
}

unsigned int ip_representation::compute_ipv6_omitted_segments(std::vector<std::string>& parts) {
    unsigned int omitted = 8 - (parts.size() - 1);
    if (omitted == 0) return 0;

    int empty_count = 0;
    for (unsigned int i = 0; i < parts.size(); i++) {
        if (parts[i].size() == 0) empty_count++;
    }
    if (empty_count <= 1) return omitted;

    // > 1 empty segments: the only legal shape is a leading "::" (which
    // string_split produces as two consecutive empties) on an IPv6 that
    // also has a nested IPv4 trailing dotted-quad — the "::" produces
    // one extra empty segment beyond the canonical single placeholder.
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
    for (unsigned int ii = 0; ii < subparts.size(); ii++) {
        if (subparts[ii] == "*") {
            CLEAR_BIT(mask, y+ii);
            continue;
        }
        pieces[y+ii] = strtol(subparts[ii].c_str(), nullptr, 10);
        if (pieces[y+ii] > 255) {
            throw std::invalid_argument("IP is badly formatted. 255 is max value for ip part.");
        }
    }
}

void ip_representation::apply_ipv6_part(std::vector<std::string>& parts, unsigned int i,
                                        int& y, unsigned int omitted) {
    auto& part = parts[i];
    if (part == "*") {
        CLEAR_BIT(mask, y);
        CLEAR_BIT(mask, y+1);
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
        pieces[y]   = strtol(part.substr(0, 2).c_str(), nullptr, 16);
        pieces[y+1] = strtol(part.substr(2, 2).c_str(), nullptr, 16);
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
    if (!(CHECK_BIT(a.mask, i) && CHECK_BIT(b.mask, i))) return;
    a_score += (16 - i) * a.pieces[i];
    b_score += (16 - i) * b.pieces[i];
}

// True when both v4-mapped-prefix octets on a and b are 0x00 or 0xFF.
// Mirrors the "::ffff:" / "::"-prefix invariant from parse_nested_ipv4.
bool is_v4_mapped_prefix_octet_pair(uint16_t a, uint16_t b) {
    return (a == 0x00 || a == 0xFF) && (b == 0x00 || b == 0xFF);
}

}  // namespace

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
