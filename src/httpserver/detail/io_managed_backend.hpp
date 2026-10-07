/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "io_managed_backend.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_MANAGED_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_MANAGED_BACKEND_HPP_
#include <memory>
#include <httpserver/detail/io_socket_backend.hpp>
#include <httpserver/server/options.hpp>
namespace httpserver {
namespace detail {
std::unique_ptr<io_socket_backend> make_socket_backend(server::loop_mode mode);
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_IO_MANAGED_BACKEND_HPP_
