/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <utility>

#include <httpserver/detail/resource_charge.hpp>

namespace httpserver::detail {

resource_charge::resource_charge(resource_charge&& other) noexcept : node_(std::move(other.node_)), kind_(other.kind_), units_(std::exchange(other.units_, 0)) {}

auto resource_charge::operator=(resource_charge&& other) noexcept -> resource_charge& {
    if (this != &other) {
        shrink(units_);
        node_ = std::move(other.node_);
        kind_ = other.kind_;
        units_ = std::exchange(other.units_, 0);
    }
    return *this;
}

auto resource_charge::grow(std::size_t units) -> bool {
    if (units && node_ && !server::detail::charge_chain(*node_, kind_, units)) return false;
    units_ += units;
    return true;
}

auto resource_charge::shrink(std::size_t units) noexcept -> void {
    units = std::min(units, units_);
    if (units && node_) server::detail::release_chain(*node_, kind_, units);
    units_ -= units;
}

auto resource_charge::take(std::size_t units) -> resource_charge {
    resource_charge result;
    result.node_ = node_;
    result.kind_ = kind_;
    result.units_ = std::min(units, units_);
    units_ -= result.units_;
    return result;
}

}  // namespace httpserver::detail
