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
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

// Constant-time equality for secret comparisons (CWE-208 mitigation).
// The length early-out is unavoidable (the lengths themselves are not
// secret here: the credential sizes are fixed at policy construction
// and the comparison length is the attacker's own input size). Past
// that gate every byte pair folds into one accumulator with no data
// dependent branch, so the number of executed instructions and memory
// accesses does not depend on where the first difference sits.

#if !defined(HTTPSERVER_COMPILATION)
#error "secure_compare.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_SECURE_COMPARE_HPP_
#define SRC_HTTPSERVER_DETAIL_SECURE_COMPARE_HPP_

#include <cstddef>
#include <string_view>

namespace httpserver {

namespace detail {

inline bool constant_time_equal(std::string_view left,
                                std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        diff |= static_cast<unsigned char>(left[i])
                ^ static_cast<unsigned char>(right[i]);
    }
    return diff == 0;
}

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_SECURE_COMPARE_HPP_
