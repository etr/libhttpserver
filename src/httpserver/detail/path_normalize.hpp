/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// Internal detail header.  Strict gate: reachable only from libhttpserver
// translation units.
#if !defined(HTTPSERVER_COMPILATION)
#error "path_normalize.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_PATH_NORMALIZE_HPP_
#define SRC_HTTPSERVER_DETAIL_PATH_NORMALIZE_HPP_

#include <string>
#include <vector>

namespace httpserver {
namespace detail {

// Pre-normalize an auth_skip_paths list so the per-request comparison
// in webserver_impl::should_skip_auth runs against already-canonical
// entries.  Each entry is fed through normalize_path — the helper that
// normalizes the *request* path inside should_skip_auth.  Note that
// normalize_path is declared in no header: it is a file-local
// (unnamed-namespace) function in src/detail/webserver_request.cpp.
// Entries ending in "/*" keep their trailing "/*" wildcard suffix;
// the prefix before the wildcard is normalized.
//
// Pure function: no shared state, callable from the webserver
// constructor body.  The definition lives in
// src/detail/webserver_request.cpp alongside normalize_path so the
// helper and its callers share a single canonicalisation rule.
std::vector<std::string> normalize_auth_skip_paths(
        const std::vector<std::string>& raw);

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_PATH_NORMALIZE_HPP_
