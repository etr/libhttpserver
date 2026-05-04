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

#ifndef SRC_HTTPSERVER_BODY_KIND_HPP_
#define SRC_HTTPSERVER_BODY_KIND_HPP_

#include <cstdint>

namespace httpserver {

// Tag identifying which subclass of detail::body a given http_response is
// currently holding. Consumers reach this through http_response::kind()
// (TASK-011) and should never have to name detail::body directly — the
// enum is the only consumer-visible part of the body hierarchy.
//
// `empty` is enumerator 0 so a value-initialised body_kind{} matches the
// "no body" state, which is what TASK-009's default-constructed
// http_response will report.
//
// Underlying type is pinned to std::uint8_t so that future additions
// stay within a single byte and do not silently grow http_response. The
// fixed underlying type also makes the enum forward-declarable, although
// http_response.hpp will still pull in this full header (consumers will
// name the enumerators).
enum class body_kind : std::uint8_t {
    empty,
    string,    // NOLINT(build/include_what_you_use) - enumerator, not std::string
    file,
    iovec,
    pipe,
    deferred,
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_BODY_KIND_HPP_
