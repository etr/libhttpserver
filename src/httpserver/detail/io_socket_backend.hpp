/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "io_socket_backend.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_SOCKET_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_SOCKET_BACKEND_HPP_

#include <cstddef>
#include <cstdint>
#include <httpserver/detail/io_operation.hpp>
#include <httpserver/detail/io_poll_sys.hpp>
#include <httpserver/server/readiness.hpp>

namespace httpserver {
namespace detail {

// Private transport ownership shared by managed drivers and the external adapter.
class io_socket_backend : public io_backend, public server::readiness_driver {
 public:
    virtual http::outcome ready() const = 0;
    virtual void activate_external() = 0;
    virtual void adopt_connection(std::uint64_t id, pollsys::native_socket_t socket) = 0;
    virtual void adopt_listener(std::uint64_t id, pollsys::native_socket_t socket) = 0;
    virtual pollsys::native_socket_t native_handle(std::uint64_t id) const = 0;
    virtual void release_connection(std::uint64_t id) = 0;
    virtual std::size_t wake() = 0;
    virtual std::size_t close() = 0;
};

}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_IO_SOCKET_BACKEND_HPP_
