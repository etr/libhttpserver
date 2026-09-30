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

#ifndef SRC_HTTPSERVER_SERVER_BUDGETS_HPP_
#define SRC_HTTPSERVER_SERVER_BUDGETS_HPP_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace server {

// Accounting keys for the hierarchical limits (architecture §3.4):
// server, listener, connection, and stream budgets over the resource
// kinds below. The byte-valued kinds count aggregate bytes in flight
// for their scope; the count-valued kinds cap concurrent objects. The
// ws_-prefixed kind budgets messages of the upgrade-based subprotocol
// (RFC 6455). The set is libhttpserver-owned (DR-V3-001); no backend
// or OS numeric identifiers appear here.
enum class resource : std::uint8_t {
    connections,
    streams,
    header_bytes,
    header_fields,
    body_buffer_bytes,
    response_queue_bytes,
    hpack_table_bytes,
    qpack_table_bytes,
    blocked_headers,
    quic_reassembly_bytes,
    ws_message_bytes,
    timers,
    routes,
};

// Number of resource kinds; covers every enumerator.
inline constexpr std::size_t resource_count = 13;

// Stable diagnostic names, indexed by resource.
inline constexpr std::array<std::string_view, resource_count> resource_names = {
    "connections",         "streams",            "header_bytes",
    "header_fields",       "body_buffer_bytes",  "response_queue_bytes",
    "hpack_table_bytes",   "qpack_table_bytes",  "blocked_headers",
    "quic_reassembly_bytes", "ws_message_bytes", "timers",
    "routes",
};

// Human-readable name of a resource kind; empty for out-of-range values
// (total over the whole uint8_t domain).
constexpr std::string_view to_string(resource kind) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < resource_count ? resource_names[index]
                                  : std::string_view{};
}

// Documented per-kind capacity ceiling enforced by configuration
// validation at server scope and by child-budget construction at every
// nested scope. Out-of-range kinds yield 0 (total over uint8_t).
constexpr std::size_t max_capacity(resource kind) noexcept {
    constexpr std::array<std::size_t, resource_count> ceilings = {
        1048576,    // connections
        1048576,    // streams
        268435456,  // header_bytes
        65536,      // header_fields
        1073741824,  // body_buffer_bytes
        1073741824,  // response_queue_bytes
        16777216,   // hpack_table_bytes
        16777216,   // qpack_table_bytes
        65536,      // blocked_headers
        268435456,  // quic_reassembly_bytes
        268435456,  // ws_message_bytes
        1048576,    // timers
        1048576,    // routes
    };
    const auto index = static_cast<std::size_t>(kind);
    return index < resource_count ? ceilings[index] : 0;
}

// Per-kind capacity installed by a default-constructed budget_limits.
// Conservative production defaults, all within the ceilings above.
constexpr std::size_t default_capacity(resource kind) noexcept {
    constexpr std::array<std::size_t, resource_count> defaults = {
        1024,       // connections
        4096,       // streams
        1048576,    // header_bytes
        256,        // header_fields
        8388608,    // body_buffer_bytes
        4194304,    // response_queue_bytes
        65536,      // hpack_table_bytes
        65536,      // qpack_table_bytes
        256,        // blocked_headers
        1048576,    // quic_reassembly_bytes
        1048576,    // ws_message_bytes
        1024,       // timers
        1024,       // routes
    };
    const auto index = static_cast<std::size_t>(kind);
    return index < resource_count ? defaults[index] : 0;
}

// Capacity per resource kind for one scope of the server > listener >
// connection > stream hierarchy. Default construction installs
// default_capacity for every kind; get/set read and overwrite one kind.
// Scope bounds (a child capacity may not exceed its parent's) are
// enforced when budgets are nested, not here.
class budget_limits {
 public:
    constexpr budget_limits() noexcept {
        for (std::size_t i = 0; i < resource_count; ++i) {
            capacities_[i] = default_capacity(static_cast<resource>(i));
        }
    }

    constexpr std::size_t get(resource kind) const noexcept {
        return capacities_[static_cast<std::size_t>(kind)];
    }

    constexpr void set(resource kind, std::size_t capacity) noexcept {
        capacities_[static_cast<std::size_t>(kind)] = capacity;
    }

 private:
    std::array<std::size_t, resource_count> capacities_{};
};

namespace detail {

// One accounting node in the server > listener > connection > stream
// hierarchy. Nodes are shared (shared_ptr) between the budget handles
// and reservations that refer to a scope, so accounting outlives any
// single handle. Counters are lock-free; admission never blocks.
class budget_node {
 public:
    budget_node(const budget_limits& limits,
                std::shared_ptr<budget_node> parent) noexcept
        : parent_(std::move(parent)), limits_(limits) { }

    std::size_t capacity(resource kind) const noexcept {
        return limits_.get(kind);
    }

    std::size_t in_use(resource kind) const noexcept {
        return counters_[static_cast<std::size_t>(kind)].load(
            std::memory_order_relaxed);
    }

    std::shared_ptr<budget_node> parent_;
    budget_limits limits_;
    std::array<std::atomic<std::size_t>, resource_count> counters_{};
};

// Charges `units` for `kind` on the chain from `leaf` up to the root.
// Refusal at any level rolls back every partial charge, so a refused
// admission leaves zero residue at every ancestor. Returns true when
// the whole chain was charged.
inline bool charge_chain(budget_node& leaf, resource kind,
                         std::size_t units) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    std::size_t committed = 0;
    bool refused = false;
    for (budget_node* node = &leaf; node != nullptr;
         node = node->parent_.get()) {
        const std::size_t before =
            node->counters_[index].fetch_add(units,
                                             std::memory_order_relaxed);
        const std::size_t capacity = node->limits_.get(kind);
        if (before > capacity || units > capacity - before) {
            refused = true;
            break;
        }
        ++committed;
    }
    if (!refused) return true;
    // The refused level holds a transient add; subtract it along with
    // the committed levels below it.
    budget_node* node = &leaf;
    for (std::size_t level = 0; level <= committed; ++level,
         node = node->parent_.get()) {
        node->counters_[index].fetch_sub(units, std::memory_order_relaxed);
    }
    return false;
}

// Releases `units` for `kind` on the chain from `leaf` up to the root.
// Must be called exactly once per committed charge chain.
inline void release_chain(budget_node& leaf, resource kind,
                          std::size_t units) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    for (budget_node* node = &leaf; node != nullptr;
         node = node->parent_.get()) {
        node->counters_[index].fetch_sub(units, std::memory_order_relaxed);
    }
}

}  // namespace detail

// Move-only RAII handle for units admitted against a resource_budget.
// Destruction releases the charge; release() is the explicit terminal
// path and is idempotent; a moved-from reservation owns nothing and
// releases nothing. Release propagates to every ancestor of the scope
// that admitted the units. Never throws.
class reservation {
 public:
    reservation() noexcept = default;

    reservation(reservation&& other) noexcept
        : node_(std::move(other.node_)),
          kind_(other.kind_),
          units_(other.units_) {
        other.node_ = nullptr;
        other.units_ = 0;
    }

    reservation& operator=(reservation&& other) noexcept {
        if (this != &other) {
            release();
            node_ = std::move(other.node_);
            kind_ = other.kind_;
            units_ = other.units_;
            other.units_ = 0;
        }
        return *this;
    }

    reservation(const reservation&) = delete;
    reservation& operator=(const reservation&) = delete;

    ~reservation() {
        release();
    }

    // Explicit terminal path; safe to call twice.
    void release() noexcept {
        if (node_ != nullptr) {
            detail::release_chain(*node_, kind_, units_);
            node_ = nullptr;
            units_ = 0;
        }
    }

    bool owns() const noexcept { return node_ != nullptr; }

    resource kind() const noexcept { return kind_; }

    std::size_t units() const noexcept { return units_; }

 private:
    friend class resource_budget;

    std::shared_ptr<detail::budget_node> node_;
    resource kind_ = resource::connections;  // meaningful only while owning
    std::size_t units_ = 0;
};

// A scope of the hierarchical budget. Copies share one accounting node;
// child() derives a nested scope whose capacities may not exceed the
// parent's; reserve() admits units against this scope and every
// ancestor, refusing before capacity is committed anywhere on the
// chain. An empty budget (default construction) has no capacity and
// refuses all admission.
class resource_budget {
 public:
    resource_budget() noexcept = default;

    // Root scope with the given capacities.
    static resource_budget root(const budget_limits& limits) {
        return resource_budget(
            std::make_shared<detail::budget_node>(limits, nullptr));
    }

    bool valid() const noexcept { return node_ != nullptr; }

    // Derives a child scope. invalid_argument when this budget is
    // empty or any child capacity exceeds this scope's; `out` is
    // untouched on failure.
    http::outcome child(const budget_limits& limits,
                        resource_budget& out) const {
        if (node_ == nullptr) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "resource_budget: cannot derive a child budget from an"
                " empty budget");
        }
        for (std::size_t i = 0; i < resource_count; ++i) {
            const auto kind = static_cast<resource>(i);
            if (limits.get(kind) > node_->limits_.get(kind)) {
                return http::outcome(
                    http::outcome_code::invalid_argument,
                    "resource_budget: child '" + std::string(to_string(kind))
                        + "' capacity " + std::to_string(limits.get(kind))
                        + " exceeds the parent's "
                        + std::to_string(node_->limits_.get(kind)));
        }
        }
        out = resource_budget(
            std::make_shared<detail::budget_node>(limits, node_));
        return http::outcome::okay();
    }

    // Admits `units` of `kind`, charging this scope and every
    // ancestor. limit_exceeded when the chain lacks room; the charge is
    // rolled back and `out` is untouched on failure. invalid_argument
    // for zero units or an out-of-range kind.
    http::outcome reserve(resource kind, std::size_t units,
                          reservation& out) const {
        if (units == 0
                || static_cast<std::size_t>(kind) >= resource_count) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "resource_budget: reserve requires a valid resource kind"
                " and at least one unit");
        }
        if (node_ == nullptr
                || !detail::charge_chain(*node_, kind, units)) {
            return http::outcome(
                http::outcome_code::limit_exceeded,
                "resource_budget: '" + std::string(to_string(kind))
                    + "' capacity exhausted in this scope or an ancestor");
        }
        out.release();
        out.node_ = node_;
        out.kind_ = kind;
        out.units_ = units;
        return http::outcome::okay();
    }

    std::size_t capacity(resource kind) const noexcept {
        return node_ == nullptr ? 0 : node_->capacity(kind);
    }

    std::size_t in_use(resource kind) const noexcept {
        return node_ == nullptr ? 0 : node_->in_use(kind);
    }

 private:
    explicit resource_budget(
        std::shared_ptr<detail::budget_node> node) noexcept
        : node_(std::move(node)) { }

    std::shared_ptr<detail::budget_node> node_;
};

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_BUDGETS_HPP_
