/*
    This file is part of libhttpserver
    Copyright (C) 2011-2019 Sebastiano Merlino

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
    USA
*/

#define HTTPSERVER_COMPILATION

#if defined(_WIN32) && !defined(__CYGWIN__)
#define _WINDOWS
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x600
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include "httpserver/create_webserver.hpp"

namespace httpserver {

create_webserver& create_webserver::bind_address(const std::string& ip) {
    _bind_address_storage = std::make_shared<struct sockaddr_storage>();
    std::memset(_bind_address_storage.get(), 0, sizeof(struct sockaddr_storage));

    // Try IPv4 first
    auto* addr4 = reinterpret_cast<struct sockaddr_in*>(_bind_address_storage.get());
    if (inet_pton(AF_INET, ip.c_str(), &(addr4->sin_addr)) == 1) {
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(_port);
        _bind_address = reinterpret_cast<const struct sockaddr*>(_bind_address_storage.get());
        return *this;
    }

    // Try IPv6
    auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(_bind_address_storage.get());
    if (inet_pton(AF_INET6, ip.c_str(), &(addr6->sin6_addr)) == 1) {
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(_port);
        _bind_address = reinterpret_cast<const struct sockaddr*>(_bind_address_storage.get());
        _use_ipv6 = true;
        return *this;
    }

    throw std::invalid_argument("Invalid IP address: " + ip);
}

}  // namespace httpserver
