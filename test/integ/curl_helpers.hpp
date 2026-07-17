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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// A header-only place for boilerplate that
// is otherwise re-declared in every integ test that uses libcurl
// (writefunc, the obvious starting point). Extracted incrementally;
// existing per-TU duplicates in older test files can migrate as they
// are touched for unrelated reasons.

#ifndef TEST_INTEG_CURL_HELPERS_HPP_
#define TEST_INTEG_CURL_HELPERS_HPP_

#include <cstddef>
#include <string>

namespace httpserver_test {

// libcurl CURLOPT_WRITEFUNCTION-shaped sink that appends bytes to a
// std::string pointed to by `s`. Used in tandem with
//   curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, &writefunc);
//   curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
// Returns the byte count exactly as libcurl requires.
inline std::size_t writefunc(void* ptr, std::size_t size, std::size_t nmemb,
                             std::string* s) {
    const std::size_t bytes = size * nmemb;
    s->append(reinterpret_cast<char*>(ptr), bytes);
    return bytes;
}

}  // namespace httpserver_test

#endif  // TEST_INTEG_CURL_HELPERS_HPP_
