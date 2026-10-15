/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "drain_ticket_state.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_DRAIN_TICKET_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_DRAIN_TICKET_STATE_HPP_
#include <httpserver/detail/drain_scope.hpp>
#include <memory>
#include <utility>
namespace httpserver::server {
class drain_ticket::impl {
 public:
    explicit impl(std::shared_ptr<httpserver::detail::drain_scope> scope)
        : scope_(std::move(scope)) { }

    std::shared_ptr<httpserver::detail::drain_scope> scope_;
};

}  // namespace httpserver::server
#endif  // SRC_HTTPSERVER_DETAIL_DRAIN_TICKET_STATE_HPP_
