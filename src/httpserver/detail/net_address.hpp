/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "net_address.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_NET_ADDRESS_HPP_
#define SRC_HTTPSERVER_DETAIL_NET_ADDRESS_HPP_
#include <httpserver/net/address.hpp>
namespace httpserver::net::detail {
address address_from_bytes(address_family family, const std::byte* raw);
}  // namespace httpserver::net::detail
#endif  // SRC_HTTPSERVER_DETAIL_NET_ADDRESS_HPP_
