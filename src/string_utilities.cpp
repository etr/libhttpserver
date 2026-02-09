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

#include "httpserver/string_utilities.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace httpserver {
namespace string_utilities {

const std::string to_upper_copy(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), (int(*)(int)) std::toupper);

    return result;
}

const std::string to_lower_copy(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), (int(*)(int)) std::tolower);

    return result;
}

const std::vector<std::string> string_split(const std::string& s, char sep, bool collapse) {
    std::vector<std::string> result;
    if (s.empty()) return result;

    std::string::size_type start = 0;
    std::string::size_type end;

    while ((end = s.find(sep, start)) != std::string::npos) {
        std::string token = s.substr(start, end - start);
        if (!collapse || !token.empty()) {
            result.push_back(std::move(token));
        }
        start = end + 1;
    }

    // Handle the last token (after the final separator)
    // Only add if there's content or if not collapsing
    // Note: match istringstream behavior which does not emit trailing empty token
    if (start < s.size()) {
        std::string token = s.substr(start);
        if (!collapse || !token.empty()) {
            result.push_back(std::move(token));
        }
    }

    return result;
}

bool is_valid_hex(const std::string& s) {
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

unsigned char hex_char_to_val(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
    return 0;
}

}  // namespace string_utilities
}  // namespace httpserver
