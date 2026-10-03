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

// TASK-119 step 2: the peer policy storage (server/peer_policy.hpp).
// Two pattern lists under ONE shared mutex (a classify() snapshot is
// consistent across both; v2 used one mutex per list and could observe
// a deny from before a mutation and an allow from after it). The
// prefix-order insert keeps each list free of covering pairs, so
// membership is a plain scan. armed() is an atomic recomputed under
// the same lock at every mutation, keeping the zero-cost gate
// consistent with the store it summarizes.

#include <atomic>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

#include <httpserver/server/peer_policy.hpp>

namespace httpserver {

namespace server {

namespace detail {

namespace {

// v2's classify_decision truth table over the v3 vocabulary (the
// reason values keep the v2 names).
peer_verdict decide(peer_policy_mode mode, bool denied, bool allowed) {
    if (mode == peer_policy_mode::accept_all && denied && !allowed) {
        return peer_verdict{false, peer_refusal::denied};
    }
    if (mode == peer_policy_mode::reject_all) {
        if (denied) return peer_verdict{false, peer_refusal::denied};
        if (!allowed) {
            return peer_verdict{false, peer_refusal::not_on_allow_list};
        }
    }
    return peer_verdict{true, peer_refusal::none};
}

// True when @p wider covers @p narrower: same family, a prefix
// length at most as long, and the narrower base inside the wider
// prefix.
bool covers(const net::address_pattern& wider,
            const net::address_pattern& narrower) noexcept {
    return wider.prefix_bits <= narrower.prefix_bits
        && wider.matches(narrower.base);
}

// The overlap rule of one list: a no-op when an existing entry
// already covers @p entry; otherwise every entry @p entry covers is
// dropped and @p entry is appended.
void insert_aware(std::vector<net::address_pattern>& list,
                  const net::address_pattern& entry) {
    for (const net::address_pattern& existing : list) {
        if (covers(existing, entry)) return;
    }
    std::vector<net::address_pattern> kept;
    kept.reserve(list.size() + 1);
    for (const net::address_pattern& existing : list) {
        if (!covers(entry, existing)) kept.push_back(existing);
    }
    kept.push_back(entry);
    list.swap(kept);
}

}  // namespace

class peer_policy_impl {
 public:
    http::outcome add(std::string_view text, bool denying) {
        const std::optional<net::address_pattern> parsed =
            net::parse_pattern(text);
        if (!parsed.has_value()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "peer_policy: pattern '" + std::string(text)
                    + "' is not an address, trailing-wildcard, or CIDR"
                      " spelling");
        }
        {
            std::unique_lock<std::shared_mutex> lock(mu_);
            insert_aware(denying ? deny_ : allow_, *parsed);
            recompute_armed_locked();
        }
        return http::outcome::okay();
    }

    http::outcome drop(std::string_view text, bool denying) {
        const std::optional<net::address_pattern> parsed =
            net::parse_pattern(text);
        if (!parsed.has_value()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "peer_policy: pattern '" + std::string(text)
                    + "' is not an address, trailing-wildcard, or CIDR"
                      " spelling");
        }
        {
            std::unique_lock<std::shared_mutex> lock(mu_);
            std::vector<net::address_pattern>& list =
                denying ? deny_ : allow_;
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (list[i] == *parsed) {
                    list.erase(list.begin()
                               + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
            recompute_armed_locked();
        }
        return http::outcome::okay();
    }

    void set_mode(peer_policy_mode mode) noexcept {
        std::unique_lock<std::shared_mutex> lock(mu_);
        mode_ = mode;
        recompute_armed_locked();
    }

    peer_policy_mode mode() const noexcept {
        std::shared_lock<std::shared_mutex> lock(mu_);
        return mode_;
    }

    void set_enabled(bool enabled) noexcept {
        std::unique_lock<std::shared_mutex> lock(mu_);
        enabled_ = enabled;
        recompute_armed_locked();
    }

    bool enabled() const noexcept {
        std::shared_lock<std::shared_mutex> lock(mu_);
        return enabled_;
    }

    bool armed() const noexcept {
        return armed_.load(std::memory_order_acquire);
    }

    // The consistent snapshot decision: both lists under one shared
    // lock, then the v2 truth table.
    peer_verdict classify(const net::peer_address& peer) {
        if (!armed()) return peer_verdict{};
        std::shared_lock<std::shared_mutex> lock(mu_);
        const bool denied = matches_locked(deny_, peer.address);
        const bool allowed = matches_locked(allow_, peer.address);
        return decide(mode_, denied, allowed);
    }

 private:
    static bool matches_locked(const std::vector<net::address_pattern>& list,
                               const net::address& candidate) noexcept {
        for (const net::address_pattern& entry : list) {
            if (entry.matches(candidate)) return true;
        }
        return false;
    }

    void recompute_armed_locked() noexcept {
        const bool armed = enabled_
            && (mode_ == peer_policy_mode::reject_all || !deny_.empty()
                || !allow_.empty());
        armed_.store(armed, std::memory_order_release);
    }

    mutable std::shared_mutex mu_;
    std::vector<net::address_pattern> deny_;
    std::vector<net::address_pattern> allow_;
    peer_policy_mode mode_ = peer_policy_mode::accept_all;
    bool enabled_ = true;
    std::atomic<bool> armed_{false};
};

std::shared_ptr<peer_policy_impl> peer_policy_create() {
    return std::make_shared<peer_policy_impl>();
}

http::outcome peer_policy_denied_add(
        const std::shared_ptr<peer_policy_impl>& owner,
        std::string_view text) {
    return owner->add(text, true);
}

http::outcome peer_policy_allowed_add(
        const std::shared_ptr<peer_policy_impl>& owner,
        std::string_view text) {
    return owner->add(text, false);
}

http::outcome peer_policy_denied_drop(
        const std::shared_ptr<peer_policy_impl>& owner,
        std::string_view text) {
    return owner->drop(text, true);
}

http::outcome peer_policy_allowed_drop(
        const std::shared_ptr<peer_policy_impl>& owner,
        std::string_view text) {
    return owner->drop(text, false);
}

void peer_policy_set_mode(const std::shared_ptr<peer_policy_impl>& owner,
                          peer_policy_mode mode) noexcept {
    owner->set_mode(mode);
}

peer_policy_mode peer_policy_get_mode(
        const std::shared_ptr<peer_policy_impl>& owner) noexcept {
    return owner->mode();
}

void peer_policy_set_enabled(
        const std::shared_ptr<peer_policy_impl>& owner,
        bool enabled) noexcept {
    owner->set_enabled(enabled);
}

bool peer_policy_get_enabled(
        const std::shared_ptr<peer_policy_impl>& owner) noexcept {
    return owner->enabled();
}

bool peer_policy_armed(
        const std::shared_ptr<peer_policy_impl>& owner) noexcept {
    return owner->armed();
}

peer_verdict peer_policy_classify(
        const std::shared_ptr<peer_policy_impl>& owner,
        const net::peer_address& peer) {
    return owner->classify(peer);
}

}  // namespace detail

}  // namespace server

}  // namespace httpserver
