/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/server/budgets.hpp>
#include <memory>
#include <string>
#include <utility>

namespace httpserver::server::detail {

budget_node::budget_node(const budget_limits& limits, std::shared_ptr<budget_node> parent) noexcept : parent_(std::move(parent)), limits_(limits) {
    if (parent_) {
        diagnostic_owner = parent_->diagnostic_owner;
        diagnostic_account = parent_->diagnostic_account;
    }
}

bool charge_chain(budget_node& leaf, resource kind, std::size_t units) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    std::size_t committed = 0;
    bool refused = false;
    for (budget_node* node = &leaf; node != nullptr; node = node->parent_.get()) {
        const std::size_t before = node->counters_[index].fetch_add(units, std::memory_order_relaxed);
        const std::size_t capacity = node->limits_.get(kind);
        if (before > capacity || units > capacity - before) {
            refused = true;
            break;
        }
        ++committed;
        if (node->prepaid_quic_storage && kind == resource::quic_reassembly_bytes) break;
    }
    if (!refused) {
        if (leaf.diagnostic_account) leaf.diagnostic_account(leaf.diagnostic_owner.get(), kind, units, 1);
        return true;
    }
    // The refused level holds a transient add; subtract it along with
    // the committed levels below it.
    budget_node* node = &leaf;
    for (std::size_t level = 0; level <= committed; ++level, node = node->parent_.get()) {
        node->counters_[index].fetch_sub(units, std::memory_order_relaxed);
    }
    if (leaf.diagnostic_account) leaf.diagnostic_account(leaf.diagnostic_owner.get(), kind, units, -1);
    return false;
}

void release_chain(budget_node& leaf, resource kind, std::size_t units) noexcept {
    if (leaf.diagnostic_account) leaf.diagnostic_account(leaf.diagnostic_owner.get(), kind, units, 0);
    const std::size_t index = static_cast<std::size_t>(kind);
    for (budget_node* node = &leaf; node != nullptr; node = node->parent_.get()) {
        node->counters_[index].fetch_sub(units, std::memory_order_relaxed);
        if (node->prepaid_quic_storage && kind == resource::quic_reassembly_bytes) break;
    }
}

}  // namespace httpserver::server::detail

namespace httpserver::server {

reservation::reservation(reservation&& other) noexcept : node_(std::move(other.node_)), kind_(other.kind_), units_(other.units_) {
    other.node_ = nullptr;
    other.units_ = 0;
}

auto reservation::operator=(reservation&& other) noexcept -> reservation& {
    if (this != &other) {
        release();
        node_ = std::move(other.node_);
        kind_ = other.kind_;
        units_ = other.units_;
        other.units_ = 0;
    }
    return *this;
}

auto reservation::release() noexcept -> void {
    if (node_ != nullptr) {
        detail::release_chain(*node_, kind_, units_);
        node_ = nullptr;
        units_ = 0;
    }
}

auto resource_budget::child(const budget_limits& limits, resource_budget& out) const -> http::outcome {
    if (node_ == nullptr) {
        return http::outcome(http::outcome_code::invalid_argument,
                             "resource_budget: cannot derive a child budget from an"
                             " empty budget");
    }
    for (std::size_t i = 0; i < resource_count; ++i) {
        const auto kind = static_cast<resource>(i);
        if (limits.get(kind) > node_->limits_.get(kind)) {
            return http::outcome(http::outcome_code::invalid_argument, "resource_budget: child '" + std::string(to_string(kind)) + "' capacity " + std::to_string(limits.get(kind)) +
                                                                           " exceeds the parent's " + std::to_string(node_->limits_.get(kind)));
        }
    }
    out = resource_budget(std::make_shared<detail::budget_node>(limits, node_));
    return http::outcome::okay();
}

auto resource_budget::reserve(resource kind, std::size_t units, reservation& out) const -> http::outcome {
    if (units == 0 || static_cast<std::size_t>(kind) >= resource_count) {
        return http::outcome(http::outcome_code::invalid_argument,
                             "resource_budget: reserve requires a valid resource kind"
                             " and at least one unit");
    }
    if (node_ == nullptr || !detail::charge_chain(*node_, kind, units)) {
        return http::outcome(http::outcome_code::limit_exceeded, "resource_budget: '" + std::string(to_string(kind)) + "' capacity exhausted in this scope or an ancestor");
    }
    out.release();
    out.node_ = node_;
    out.kind_ = kind;
    out.units_ = units;
    return http::outcome::okay();
}

}  // namespace httpserver::server
