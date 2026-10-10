/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef TEST_UNIT_PACKAGE_BACKEND_WITNESS_HPP_
#define TEST_UNIT_PACKAGE_BACKEND_WITNESS_HPP_

#include <stdexcept>
#include <string_view>
#include <httpserver/detail/io_managed_backend.hpp>
#if defined(_WIN32)
#include <httpserver/detail/io_iocp_backend.hpp>
#elif defined(__linux__)
#include <httpserver/detail/io_epoll_backend.hpp>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <httpserver/detail/io_kqueue_backend.hpp>
#endif

namespace package_test {
inline std::string_view native_backend(httpserver::detail::io_socket_backend& backend) {
    namespace hd = httpserver::detail;
#if defined(_WIN32)
    if (dynamic_cast<hd::io_iocp_backend*>(&backend)) return "iocp";
#elif defined(__linux__)
    if (dynamic_cast<hd::io_epoll_backend*>(&backend)) return "epoll";
#elif defined(__APPLE__) || defined(__FreeBSD__)
    if (dynamic_cast<hd::io_kqueue_backend*>(&backend)) return "kqueue";
#endif
    throw std::runtime_error("managed factory did not select the native backend");
}
}  // namespace package_test
#endif  // TEST_UNIT_PACKAGE_BACKEND_WITNESS_HPP_
