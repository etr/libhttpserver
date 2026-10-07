/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <memory>
#include <httpserver/detail/io_managed_backend.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#if defined(__linux__)
#include <httpserver/detail/io_epoll_backend.hpp>
#endif
namespace httpserver {
namespace detail {
std::unique_ptr<io_socket_backend> make_socket_backend(server::loop_mode mode) {
#if defined(__linux__)
    if (mode == server::loop_mode::managed) return std::make_unique<io_epoll_backend>();
#endif
    return std::make_unique<io_poll_backend>(mode);
}
}  // namespace detail
}  // namespace httpserver
