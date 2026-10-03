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

// TASK-119 (plan D5): the v3 peer policy store -- the v3 equivalent
// of v2's deny_ip/allow_ip sets with default_policy (PRD-V3N-REQ-014,
// DR-V3-008). One server-wide policy: two pattern lists (deny, allow)
// under one lock, a mode (the v2 default_policy), and an enabled
// flag. Runtime-mutable from any thread including inside handlers:
// every member is synchronized, and classify() takes a consistent
// snapshot of both lists.
//
// Pattern vocabulary: net::parse_pattern (exact literals, trailing-*
// IPv4 wildcards, CIDR over both families). Pattern spellings that
// fail to parse are rejected with a typed invalid_argument outcome
// (the v2 API threw std::invalid_argument; REQ-037 maps it to the
// typed surface). Removing a pattern that is not present is a
// documented no-op (ok); removing one by a different spelling of the
// same parsed pattern works (identity is the parsed value).
//
// Overlap rule (the v2 insert_wildcard_aware semantics over the
// prefix-bits form): when the inserted entry is already covered by a
// wider-or-equal entry of the same list, the insert is a no-op; every
// narrower entry the new one covers is replaced by it. One list never
// holds two entries where one covers the other.
//
// Decision truth table (v2's classify_decision, verbatim; the reason
// values keep the v2 names):
//   accept_all, denied,   !allowed -> refuse, denied
//   reject_all, denied              -> refuse, denied
//   reject_all, !denied,  !allowed  -> refuse, not_on_allow_list
//   anything else                   -> accept
// An allow entry overrides a deny entry under accept_all; under
// reject_all a denied peer is refused either way. An unspec peer
// never matches an entry, so reject_all refuses it
// (not_on_allow_list) and accept_all admits it.
//
// Zero-cost-when-unused: armed() is one atomic load, false when the
// policy is disabled, the mode is accept_all, and both lists are
// empty; an engine that observes !armed() may skip classify()
// entirely (the observable verdict is the same).
//
// NOT part of the v2 umbrella <httpserver.hpp>: like the rest of the
// v3 server area this is an additive surface.

#ifndef SRC_HTTPSERVER_SERVER_PEER_POLICY_HPP_
#define SRC_HTTPSERVER_SERVER_PEER_POLICY_HPP_

#include <cstdint>
#include <memory>
#include <string_view>

#include <httpserver/http/outcome.hpp>
#include <httpserver/net/address.hpp>

namespace httpserver {

namespace server {

// The default stance of the policy (v2's default_policy).
enum class peer_policy_mode : std::uint8_t {
    accept_all,  // admit unless denied (and not allowed)
    reject_all,  // admit only when allowed (or denied-and-allowed is
                 // still refused: deny wins the reason under reject)
};

// Why a peer was refused. The values keep the v2 accept_decision
// reason names; `none` accompanies an accepted verdict.
enum class peer_refusal : std::uint8_t {
    none,
    denied,
    not_on_allow_list,
};

// One policy decision: the verdict and, on refusal, the typed reason.
struct peer_verdict {
    bool accepted = true;
    peer_refusal reason = peer_refusal::none;

    bool operator==(const peer_verdict&) const = default;
    bool operator!=(const peer_verdict&) const = default;
};

namespace detail {

class peer_policy_impl;

// TU-defined bridges over the incomplete pimpl: the header stays
// vocabulary-only, the storage lives in the library.
std::shared_ptr<peer_policy_impl> peer_policy_create();
http::outcome peer_policy_denied_add(
    const std::shared_ptr<peer_policy_impl>& owner, std::string_view text);
http::outcome peer_policy_allowed_add(
    const std::shared_ptr<peer_policy_impl>& owner, std::string_view text);
http::outcome peer_policy_denied_drop(
    const std::shared_ptr<peer_policy_impl>& owner, std::string_view text);
http::outcome peer_policy_allowed_drop(
    const std::shared_ptr<peer_policy_impl>& owner, std::string_view text);
void peer_policy_set_mode(const std::shared_ptr<peer_policy_impl>& owner,
                          peer_policy_mode mode) noexcept;
peer_policy_mode peer_policy_get_mode(
    const std::shared_ptr<peer_policy_impl>& owner) noexcept;
void peer_policy_set_enabled(
    const std::shared_ptr<peer_policy_impl>& owner, bool enabled) noexcept;
bool peer_policy_get_enabled(
    const std::shared_ptr<peer_policy_impl>& owner) noexcept;
bool peer_policy_armed(
    const std::shared_ptr<peer_policy_impl>& owner) noexcept;
peer_verdict peer_policy_classify(
    const std::shared_ptr<peer_policy_impl>& owner,
    const net::peer_address& peer);

}  // namespace detail

// The server-wide peer policy store (thread-safe, runtime-mutable).
// Non-copyable and non-movable: listener engines hold references into
// the object for their whole live window.
class peer_policy {
 public:
    peer_policy()
        : impl_(detail::peer_policy_create()) { }

    peer_policy(const peer_policy&) = delete;
    peer_policy& operator=(const peer_policy&) = delete;

    // Adds @p pattern to the deny (allow) list. Typed failures:
    // invalid_argument when the spelling does not parse. Idempotent
    // per the overlap rule.
    http::outcome deny(std::string_view pattern) {
        return detail::peer_policy_denied_add(impl_, pattern);
    }

    http::outcome allow(std::string_view pattern) {
        return detail::peer_policy_allowed_add(impl_, pattern);
    }

    // Removes @p pattern from the deny (allow) list. Removing a
    // pattern that is not present is a documented no-op (ok);
    // identity is the parsed pattern, so any spelling of it works.
    // A non-parsing spelling fails invalid_argument.
    http::outcome remove_denied(std::string_view pattern) {
        return detail::peer_policy_denied_drop(impl_, pattern);
    }

    http::outcome remove_allowed(std::string_view pattern) {
        return detail::peer_policy_allowed_drop(impl_, pattern);
    }

    void set_mode(peer_policy_mode mode) noexcept {
        detail::peer_policy_set_mode(impl_, mode);
    }

    peer_policy_mode mode() const noexcept {
        return detail::peer_policy_get_mode(impl_);
    }

    // A disabled policy admits every peer (the mode and lists are
    // kept, so re-enabling restores the previous stance).
    void set_enabled(bool enabled) noexcept {
        detail::peer_policy_set_enabled(impl_, enabled);
    }

    bool enabled() const noexcept {
        return detail::peer_policy_get_enabled(impl_);
    }

    // The zero-cost-when-unused gate (see the file comment).
    bool armed() const noexcept {
        return detail::peer_policy_armed(impl_);
    }

    // The policy decision for @p peer under the current snapshot:
    // the truth table above. Never blocks on user code.
    peer_verdict classify(const net::peer_address& peer) const {
        return detail::peer_policy_classify(impl_, peer);
    }

 private:
    std::shared_ptr<detail::peer_policy_impl> impl_;
};

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_PEER_POLICY_HPP_
