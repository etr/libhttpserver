/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "hook_bus.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HOOK_BUS_HPP_
#define SRC_HTTPSERVER_DETAIL_HOOK_BUS_HPP_
#include <memory>
#include <httpserver/server/hooks.hpp>
namespace httpserver::server::detail {
std::shared_ptr<hook_bus_impl> hook_bus_create();
}  // namespace httpserver::server::detail
#endif  // SRC_HTTPSERVER_DETAIL_HOOK_BUS_HPP_
