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

// Internal detail header. Strict gate: reachable only from libhttpserver
// translation units.
#if !defined(HTTPSERVER_COMPILATION)
#error "route_tier.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_ROUTE_TIER_HPP_
#define SRC_HTTPSERVER_DETAIL_ROUTE_TIER_HPP_

#include <optional>
// Disabling lint error on regex (the only reason it errors is because the Chromium team prefers google/re2)
#include <regex>  // NOLINT [build/c++11]
#include <stdexcept>
#include <string>
#include <utility>

#include "httpserver/detail/http_endpoint.hpp"

namespace httpserver {
namespace detail {

// classify_route_tier: single source-of-truth for v2 tier placement.
//
// Given a non-prefix http_endpoint, returns which of three storage tiers
// owns the route and, for the regex tier, the already-compiled std::regex
// so callers need not compile it a second time.
//
// Tier rules (in priority order):
//   radix  — parameterized path (url_pars non-empty); no regex needed.
//   regex  — is_regex_compiled() true AND the literal url_complete does NOT
//             match the compiled pattern (true metacharacters present).
//   exact  — everything else: plain paths with regex_checking enabled
//             (literal matches its own pattern, so the fast hash tier
//             is equivalent), or regex_checking disabled entirely.
//
// Prefix routes (family == true) are NOT classified here; callers handle
// them before invoking this helper.
//
// Inline so both webserver_register.cpp and webserver_routes.cpp can call
// it without picking up a TU boundary; previously a static helper in an
// anonymous namespace inside webserver.cpp, lifted here when the file was
// decomposed.
// 'pattern' names the regex tier — avoids the trailing underscore that was
// needed to escape the reserved token 'regex'. The label is self-describing:
// routes in this tier match by compiled regex pattern. (finding #4)
enum class route_tier_kind { exact, radix, pattern };

struct route_tier_result {
    route_tier_kind kind = route_tier_kind::exact;
    std::optional<std::regex> re;  // populated iff kind == pattern
};

inline route_tier_result classify_route_tier(const detail::http_endpoint& idx) {
    route_tier_result res;

    if (!idx.get_url_pars().empty()) {
        res.kind = route_tier_kind::radix;
        return res;
    }

    if (idx.is_regex_compiled()) {
        // Compile the normalized pattern once and run the self-match
        // check. If the literal url_complete matches its own regex, the
        // pattern is trivially ^/literal$ and the exact hash tier is
        // faster and correct. Otherwise the path has meaningful regex
        // metacharacters and belongs in the regex tier.
        // Guard std::regex construction (CWE-248): a malformed pattern
        // throws std::regex_error. Convert to std::invalid_argument so
        // callers get a catchable typed exception at registration time.
        std::regex re;
        try {
            re = std::regex(idx.get_url_normalized(),
                            std::regex::extended | std::regex::icase
                            | std::regex::nosubs);
        } catch (const std::regex_error& e) {
            throw std::invalid_argument(
                std::string("invalid regex route pattern '")
                + idx.get_url_normalized() + "': " + e.what());
        }
        if (std::regex_match(idx.get_url_complete(), re)) {
            res.kind = route_tier_kind::exact;
        } else {
            res.kind = route_tier_kind::pattern;
            res.re   = std::move(re);
        }
        return res;
    }

    res.kind = route_tier_kind::exact;
    return res;
}

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_ROUTE_TIER_HPP_
