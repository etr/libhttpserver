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

// Readers retain one immutable generation while scanning. Writers serialize
// read-modify-publish operations; no reader-held lock spans a pattern scan.

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
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
            std::lock_guard<std::mutex> lock(writers_);
            auto next = std::make_shared<generation>(*current_.load(std::memory_order_acquire));
            insert_aware(denying ? next->deny : next->allow, *parsed);
            publish(std::move(next));
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
            std::lock_guard<std::mutex> lock(writers_);
            auto next = std::make_shared<generation>(*current_.load(std::memory_order_acquire));
            std::vector<net::address_pattern>& list =
                denying ? next->deny : next->allow;
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (list[i] == *parsed) {
                    list.erase(list.begin()
                               + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
            publish(std::move(next));
        }
        return http::outcome::okay();
    }

    void set_mode(peer_policy_mode mode) noexcept {
        std::lock_guard<std::mutex> lock(writers_);
        auto next = std::make_shared<generation>(*current_.load(std::memory_order_acquire));
        next->mode = mode;
        publish(std::move(next));
    }

    peer_policy_mode mode() const noexcept {
        return current_.load(std::memory_order_acquire)->mode;
    }

    void set_enabled(bool enabled) noexcept {
        std::lock_guard<std::mutex> lock(writers_);
        auto next = std::make_shared<generation>(*current_.load(std::memory_order_acquire));
        next->enabled = enabled;
        publish(std::move(next));
    }

    bool enabled() const noexcept {
        return current_.load(std::memory_order_acquire)->enabled;
    }

    bool armed() const noexcept {
        return armed_.load(std::memory_order_acquire);
    }

    peer_verdict classify(const net::peer_address& peer) {
        if (!armed()) return peer_verdict{};
        const auto snapshot = current_.load(std::memory_order_acquire);
        // A concurrent disarm may have published after the gate was observed.
        if (!snapshot->armed) return peer_verdict{};
        const bool denied = matches(snapshot->deny, peer.address);
        const bool allowed = matches(snapshot->allow, peer.address);
        return decide(snapshot->mode, denied, allowed);
    }

 private:
    static bool matches(const std::vector<net::address_pattern>& list,
                        const net::address& candidate) noexcept {
        for (const net::address_pattern& entry : list) {
            if (entry.matches(candidate)) return true;
        }
        return false;
    }

    struct generation {
        std::vector<net::address_pattern> deny;
        std::vector<net::address_pattern> allow;
        peer_policy_mode mode = peer_policy_mode::accept_all;
        bool enabled = true;
        bool armed = false;
    };

    void publish(std::shared_ptr<generation> next) noexcept {
        next->armed = next->enabled
            && (next->mode == peer_policy_mode::reject_all || !next->deny.empty()
                || !next->allow.empty());
        const bool armed = next->armed;
        // Publish ownership before releasing the scalar gate. Readers arriving
        // during publication may use an older complete generation; readers
        // synchronized after a mutation see its gate and generation together.
        current_.store(std::move(next), std::memory_order_release);
        armed_.store(armed, std::memory_order_release);
    }

    std::mutex writers_;
    std::atomic<std::shared_ptr<const generation>> current_{
        std::make_shared<const generation>()};
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
