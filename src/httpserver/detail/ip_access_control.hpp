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

// IP allow/deny access-control collaborator. Internal header; only
// reachable when compiling libhttpserver translation units. NOT part of
// the installed surface; consumers cannot reach it through the public
// umbrella.
#if !defined(HTTPSERVER_COMPILATION)
#error "ip_access_control.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_IP_ACCESS_CONTROL_HPP_
#define SRC_HTTPSERVER_DETAIL_IP_ACCESS_CONTROL_HPP_

#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "httpserver/ip_representation.hpp"

namespace httpserver {
namespace detail {

// Owns the deny/allow IP sets and their locks — the whole of the
// webserver's IP access-control state. Extracted from webserver_impl so
// that responsibility lives behind one boundary: policy_callback consults
// it via classify(); webserver::{deny,allow,remove_denied,remove_allowed}_ip
// mutate it. No other state cluster on webserver_impl shares these mutexes.
class ip_access_control {
 public:
    // Add @p ip (an IP literal or wildcard pattern) to the deny / allow
    // list, preserving the wildcard-precedence invariant (see
    // insert_wildcard_aware). remove_* erase the exact entry previously
    // added. All four take the relevant list's mutex internally.
    void deny(std::string_view ip);
    void remove_denied(std::string_view ip);
    void allow(std::string_view ip);
    void remove_allowed(std::string_view ip);

    struct membership {
        bool denied = false;
        bool allowed = false;
    };

    // Consistent deny/allow snapshot for @p peer. Both lists are read with
    // their shared locks held together, matching the pre-extraction
    // policy_callback semantics (no mutation can interleave between the two
    // reads).
    [[nodiscard]] membership classify(
        const http::ip_representation& peer) const;

 private:
    // Insert @p t_ip into @p list, preserving the invariant that a
    // less-specific (lower weight()) wildcard entry takes precedence over a
    // more-specific one. When the incoming entry is less specific than a
    // stored match, the stored entry is erased first so the wildcard wins;
    // when it is equal or more specific, std::set::insert is a no-op and the
    // existing entry is kept. The unconditional insert covers all three
    // cases: (1) no existing entry — insert; (2) equal/higher weight — no-op
    // insert; (3) lower weight — erase first, then insert. This works only
    // because ip_representation::operator< treats OVERLAPPING entries as
    // equivalent: octets masked out on either side are excluded from the
    // comparison, so a wildcard and any address it matches compare equal
    // under std::set's ordering. find(t_ip) may therefore return a DIFFERENT
    // stored entry — one the new entry subsumes or collides with — not just
    // an exact duplicate; that comparator invariant is what the
    // erase-then-insert relies on. Caller holds the list's mutex.
    static void insert_wildcard_aware(std::set<http::ip_representation>& list,
                                      const http::ip_representation& t_ip);

    mutable std::shared_mutex deny_list_mutex_;
    std::set<http::ip_representation> deny_list_;

    mutable std::shared_mutex allow_list_mutex_;
    std::set<http::ip_representation> allow_list_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_IP_ACCESS_CONTROL_HPP_
