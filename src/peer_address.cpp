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

// peer_address::to_string out-of-line body. Carved out of hook_handle.cpp
// in TASK-051 to keep both TUs under FILE_LOC_MAX (the per-route hook
// additions to hook_handle.cpp pushed it past the 500-line ceiling).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "httpserver/hook_context.hpp"

namespace httpserver {

namespace {

// Eight 16-bit groups in network byte order.
using ipv6_groups = std::array<std::uint16_t, 8>;

// (start, length) of the zero-run RFC 5952 §4.2.2 collapses with "::".
// length == 0 means "no collapse" (RFC 5952 §4.3: a single zero MUST
// NOT be shortened).
struct zero_run {
    int start = -1;
    int len = 0;
};

// Assemble eight 16-bit groups in network byte order from the 16 raw
// bytes. Cast each byte to unsigned before shifting to avoid signed-int
// promotion UB and to match the unsigned int expected by '%x'
// (CWE-704 / TASK-045 finding #1 & #25).
ipv6_groups assemble_groups(const std::array<std::uint8_t, 16>& bytes) {
    ipv6_groups g{};
    for (std::size_t i = 0; i < 8; ++i) {
        g[i] = static_cast<std::uint16_t>(
            (static_cast<unsigned>(bytes[2 * i]) << 8) |
             static_cast<unsigned>(bytes[2 * i + 1]));
    }
    return g;
}

// RFC 5952 §5 IPv4-mapped (::ffff:0:0/96) detection. The plain
// IPv4-compatible (::a.b.c.d) form is NOT covered by §5 and stays in
// hex (e.g. "::1" rather than "::0.0.0.1").
bool is_ipv4_mapped(const ipv6_groups& g) {
    for (int i = 0; i < 5; ++i) {
        if (g[i] != 0) return false;
    }
    return g[5] == 0xffff;
}

// Format the IPv4-mapped form per RFC 5952 §5.
std::string format_ipv4_mapped(const std::array<std::uint8_t, 16>& bytes) {
    char buf[24];  // "::ffff:255.255.255.255" + NUL = 23
    std::snprintf(buf, sizeof(buf), "::ffff:%u.%u.%u.%u",
                  static_cast<unsigned>(bytes[12]),
                  static_cast<unsigned>(bytes[13]),
                  static_cast<unsigned>(bytes[14]),
                  static_cast<unsigned>(bytes[15]));
    return std::string{buf};
}

// Find the longest run of consecutive zero groups of length >= 2.
// Strict > update preserves first-occurrence tie-break (RFC 5952
// §4.2.3); runs of length 1 are dropped per RFC 5952 §4.3.
zero_run find_longest_zero_run(const ipv6_groups& g) {
    zero_run best;
    int cur_start = -1;
    int cur_len = 0;
    for (int i = 0; i < 8; ++i) {
        if (g[i] != 0) {
            cur_len = 0;
            continue;
        }
        if (cur_len == 0) cur_start = i;
        ++cur_len;
        if (cur_len > best.len) {
            best.len = cur_len;
            best.start = cur_start;
        }
    }
    if (best.len < 2) return zero_run{};
    return best;
}

// Append "%x" for a single group to `out`.
void append_group_hex(std::string& out, std::uint16_t group) {
    char scratch[5];  // up to 4 hex chars + NUL per group
    std::snprintf(scratch, sizeof(scratch), "%x",
                  static_cast<unsigned>(group));
    out += scratch;
}

// Emit the canonical text form, given the assembled groups and the
// collapse window. Edge cases ("::1", "1::", "::") fall out naturally
// because the "::" marker brings both colons with it.
std::string emit_canonical(const ipv6_groups& g, const zero_run& collapse) {
    std::string out;
    out.reserve(40);  // bounded by INET6_ADDRSTRLEN
    for (int i = 0; i < 8;) {
        if (i == collapse.start) {
            out += "::";
            i += collapse.len;
            continue;
        }
        append_group_hex(out, g[i]);
        ++i;
        // Emit a ':' separator if more groups remain AND the next slot
        // is not the collapse window (which brings its own leading ':').
        if (i < 8 && i != collapse.start) out += ':';
    }
    return out;
}

// RFC 5952 §4 canonicalizer for the 16-byte big-endian IPv6 address in
// `bytes`. The output is bounded by INET6_ADDRSTRLEN (45 + NUL = 46), so
// std::string's SBO covers it on every reasonable libc++/libstdc++ build.
//
// We deliberately implement the canonical form in pure C++ over the
// 16-byte buffer rather than delegating to `inet_ntop`:
//
//   (a) `peer_address.cpp` stays free of <netinet/in.h> / <sys/socket.h>
//       (see file-header comment above), matching the original TASK-051
//       split's "no backend-platform headers in this TU" rule.
//   (b) Post-processing produces deterministic, identical output across
//       glibc / musl / macOS / Windows builds. Platform `inet_ntop`
//       behaviour for the IPv4-mapped dotted-quad form (RFC 5952 §5) is
//       not uniformly canonical across libc versions.
std::string ipv6_canonical(const std::array<std::uint8_t, 16>& bytes) {
    const auto groups = assemble_groups(bytes);
    if (is_ipv4_mapped(groups)) return format_ipv4_mapped(bytes);
    return emit_canonical(groups, find_longest_zero_run(groups));
}

}  // namespace

std::string peer_address::to_string() const {
    // No <netinet/in.h> / inet_ntop here so we keep this TU free of
    // backend-platform headers. IPv6 is rendered in RFC 5952 §4 canonical
    // form by an in-TU post-processor (see ipv6_canonical above) for
    // deterministic output across libc versions.
    switch (fam) {
    case family::ipv4: {
        char buf[16];  // "255.255.255.255" + NUL = 16
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                      static_cast<unsigned>(bytes[0]),
                      static_cast<unsigned>(bytes[1]),
                      static_cast<unsigned>(bytes[2]),
                      static_cast<unsigned>(bytes[3]));
        return std::string{buf};
    }
    case family::ipv6:
        return ipv6_canonical(bytes);
    case family::unspec:
    default:
        return std::string{};
    }
}

}  // namespace httpserver
