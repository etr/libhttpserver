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

#include <cstdio>
#include <string>

#include "httpserver/hook_context.hpp"

namespace httpserver {

std::string peer_address::to_string() const {
    // No <netinet/in.h> / inet_ntop here so we keep this TU free of
    // backend-platform headers. The format is canonical-enough for
    // log lines without dragging in the full POSIX socket surface.
    // 46 is POSIX INET6_ADDRSTRLEN; we round up for snprintf's NUL.
    char buf[64];
    switch (fam) {
    case family::ipv4:
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                      static_cast<unsigned>(bytes[0]),
                      static_cast<unsigned>(bytes[1]),
                      static_cast<unsigned>(bytes[2]),
                      static_cast<unsigned>(bytes[3]));
        return std::string{buf};
    case family::ipv6: {
        // Group as eight uint16_t big-endian words, colon-separated.
        // Skip zero-compression for simplicity at TASK-045; TASK-046
        // can refine when telemetry/log requirements firm up.
        // Cast each byte to unsigned before shifting to avoid signed-int
        // promotion UB on exotic 16-bit-int platforms and to match the
        // unsigned int expected by '%x' (CWE-704 / finding #1 & #25).
        std::snprintf(buf, sizeof(buf),
                      "%x:%x:%x:%x:%x:%x:%x:%x",
                      (static_cast<unsigned>(bytes[0])  << 8) | static_cast<unsigned>(bytes[1]),
                      (static_cast<unsigned>(bytes[2])  << 8) | static_cast<unsigned>(bytes[3]),
                      (static_cast<unsigned>(bytes[4])  << 8) | static_cast<unsigned>(bytes[5]),
                      (static_cast<unsigned>(bytes[6])  << 8) | static_cast<unsigned>(bytes[7]),
                      (static_cast<unsigned>(bytes[8])  << 8) | static_cast<unsigned>(bytes[9]),
                      (static_cast<unsigned>(bytes[10]) << 8) | static_cast<unsigned>(bytes[11]),
                      (static_cast<unsigned>(bytes[12]) << 8) | static_cast<unsigned>(bytes[13]),
                      (static_cast<unsigned>(bytes[14]) << 8) | static_cast<unsigned>(bytes[15]));
        return std::string{buf};
    }
    case family::unspec:
    default:
        return std::string{};
    }
}

}  // namespace httpserver
