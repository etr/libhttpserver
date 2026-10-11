/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "resource_scope.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_RESOURCE_SCOPE_HPP_
#define SRC_HTTPSERVER_DETAIL_RESOURCE_SCOPE_HPP_
#include <httpserver/server/budgets.hpp>
namespace httpserver::detail {
// Public configuration supplies aggregate limits. Every private child inherits
// all kinds explicitly, including capacities smaller than the default limits.
inline server::resource_budget child_budget(const server::resource_budget& parent) {
    server::budget_limits limits;
    for (std::size_t i = 0; i < server::resource_count; ++i) {
        auto kind = static_cast<server::resource>(i);
        limits.set(kind, parent.capacity(kind));
    }
    server::resource_budget child;
    static_cast<void>(parent.child(limits, child));
    return child;
}
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_RESOURCE_SCOPE_HPP_
