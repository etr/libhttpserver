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

#ifndef SRC_HTTPSERVER_IP_REPRESENTATION_HPP_
#define SRC_HTTPSERVER_IP_REPRESENTATION_HPP_

#include <stdint.h>

#include <algorithm>
#include <bit>
#include <string>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/http_utils.hpp"

// Forward-declared at file scope; the parsing path in src/http_utils.cpp
// pulls in <sys/socket.h> directly so the sockaddr ctor can read the
// address family. Same rationale as http_utils.hpp: backend headers must
// not leak through the umbrella to downstream consumers.
struct sockaddr;

namespace httpserver {
namespace http {

struct ip_representation {
    http_utils::IP_version_T ip_version;
    // The address as 16 octets, one per element. An IPv6 address fills all
    // 16 (each 4-hex-digit group is two octets: high byte then low byte);
    // an IPv4 (or IPv4-mapped) address fills octets[12..15] and leaves the
    // rest as the mapped prefix. See the parsers in
    // src/detail/ip_representation.cpp.
    uint8_t octets[16];
    // Bitmask over the 16 octets: bit i set means octet i is significant;
    // a cleared bit marks a '*' wildcard octet that matches anything.
    uint16_t mask;

    explicit ip_representation(http_utils::IP_version_T ip_version) :
        ip_version(ip_version) {
            mask = constants::DEFAULT_MASK_VALUE;
            std::fill(octets, octets + 16, 0);
    }

    explicit ip_representation(const std::string& ip);
    explicit ip_representation(const struct sockaddr* ip);

    bool operator<(const ip_representation& b) const;

    // Number of significant (non-wildcard) octets: the popcount of `mask`,
    // which carries one bit per octet.
    int weight() const {
        return std::popcount(mask);
    }

 private:
    // Helpers carved out of the string-ctor to keep each function under
    // the project cyclomatic-complexity bar. parse_ipv6 / parse_ipv4
    // do the top-level dispatch; the rest serve parse_ipv6.
    void parse_ipv4(const std::string& ip);
    void parse_ipv6(const std::string& ip);
    static unsigned int compute_ipv6_omitted_segments(std::vector<std::string>& parts);
    void apply_ipv6_part(std::vector<std::string>& parts, unsigned int i,
                         int& y, unsigned int omitted);
    void parse_nested_ipv4(const std::vector<std::string>& parts,
                           unsigned int i, int y);
};

}  // namespace http
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_IP_REPRESENTATION_HPP_
