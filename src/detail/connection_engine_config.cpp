/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/connection_engine.hpp>

namespace httpserver::detail {

auto connection_engine_config::from_budget_limits(const server::budget_limits& limits) noexcept -> connection_engine_config {
    connection_engine_config config;
    config.head = http1_head_budget::from_budget_limits(limits);
    config.body = http1_body_budget::from_budget_limits(limits);
    config.outbox = http1_outbox_budget::from_budget_limits(limits);
    config.websocket_limits = websocket::options::from_budgets(limits);
    return config;
}

}  // namespace httpserver::detail
