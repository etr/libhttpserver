/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_storage.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_STORAGE_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_STORAGE_HPP_
#include <memory>
#include <httpserver/server/budgets.hpp>
namespace httpserver::detail {
struct quic_storage_state;
// Budget and ancestor-envelope lifetime travel together to each storage owner.
struct quic_storage_lease {
    std::shared_ptr<quic_storage_state> owner;
    server::resource_budget budget;
};
class quic_storage_pool final {
 public:
    quic_storage_pool(std::size_t data_bytes, std::size_t critical_bytes, server::resource_budget ancestors);
    quic_storage_lease data() const;
    quic_storage_lease critical() const;

 private:
    std::shared_ptr<quic_storage_state> state_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_STORAGE_HPP_
