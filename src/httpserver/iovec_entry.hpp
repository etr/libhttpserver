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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_IOVEC_ENTRY_HPP_
#define SRC_HTTPSERVER_IOVEC_ENTRY_HPP_

#include <cstddef>

namespace httpserver {

// Library-defined POD describing a single scatter/gather buffer at the
// public API surface. Replaces `struct iovec` from <sys/uio.h>, keeping
// the public-header surface free of POSIX-only system headers.
//
// Layout is pinned to match POSIX `struct iovec` and libmicrohttpd's
// `MHD_IoVec` so the dispatch path can `reinterpret_cast` a contiguous
// array of iovec_entry into either C type at zero copy. The pinning
// asserts live next to the cast site (currently `iovec_response.cpp`,
// moving to `details/body.hpp` once TASK-009 lands).
//
// `base` is `const void*` because libhttpserver never writes through
// these buffers on the response path.
struct iovec_entry {
    const void* base;
    std::size_t len;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_IOVEC_ENTRY_HPP_
