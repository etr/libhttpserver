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
     License along with this library; if not, see the file LICENSE in
     the distribution; if not, write to the Free Software Foundation,
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/

// The ASCII case helpers the authentication translation units share
// (TASK-115; the duplication gate requires one copy, not one per auth
// policy). Scheme tokens, algorithm tokens, and qop tokens are all
// ASCII case-insensitive per RFC 7235 section 2.1, and the response
// hex arrives client-cased while the server computes canonical
// lowercase. Locale-independent by construction: only A-Z fold.

#if !defined(HTTPSERVER_COMPILATION)
#error "auth_text.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_AUTH_TEXT_HPP_
#define SRC_HTTPSERVER_DETAIL_AUTH_TEXT_HPP_

#include <string_view>

namespace httpserver {

namespace detail {

namespace auth_text {

inline char lowered(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a')
                                  : c;
}

inline bool ascii_iequal(std::string_view left,
                         std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (lowered(left[i]) != lowered(right[i])) return false;
    }
    return true;
}

}  // namespace auth_text

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_AUTH_TEXT_HPP_
