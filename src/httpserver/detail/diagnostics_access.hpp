/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "diagnostics_access.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_DIAGNOSTICS_ACCESS_HPP_
#define SRC_HTTPSERVER_DETAIL_DIAGNOSTICS_ACCESS_HPP_
#include <httpserver/server/diagnostics.hpp>
namespace httpserver::detail {
struct diagnostics_access {
    static void record(const server::diagnostics&, server::diagnostic_event) noexcept;
    // Install before publishing a root budget to concurrent producers.
    static void attach(server::resource_budget&, const server::diagnostics&) noexcept;

 private:
    static void account(void*, server::resource, std::size_t, int) noexcept;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_DIAGNOSTICS_ACCESS_HPP_
