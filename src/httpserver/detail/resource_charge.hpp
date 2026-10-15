/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "resource_charge.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_RESOURCE_CHARGE_HPP_
#define SRC_HTTPSERVER_DETAIL_RESOURCE_CHARGE_HPP_
#include <algorithm>
#include <memory>
#include <utility>
#include <httpserver/server/budgets.hpp>
namespace httpserver::detail {
// Variable-sized retained storage uses one counter handle, including when a
// peer supplies single-byte fragments. Empty scopes preserve standalone codecs.
class resource_charge final {
 public:
    resource_charge() = default;
    resource_charge(const server::resource_budget& budget, server::resource kind)
        : node_(budget.node_), kind_(kind) {}
    resource_charge(const resource_charge&) = delete;
    resource_charge& operator=(const resource_charge&) = delete;
    resource_charge(resource_charge&& other) noexcept;
    resource_charge& operator=(resource_charge&& other) noexcept;
    ~resource_charge() { shrink(units_); }
    bool grow(std::size_t units);
    void shrink(std::size_t units) noexcept;
    resource_charge take(std::size_t units);
    std::size_t units() const { return units_; }

 private:
    std::shared_ptr<server::detail::budget_node> node_;
    server::resource kind_ = server::resource::header_bytes;
    std::size_t units_ = 0;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_RESOURCE_CHARGE_HPP_
