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

#ifndef SRC_HTTPSERVER_STRING_UTILITIES_HPP_
#define SRC_HTTPSERVER_STRING_UTILITIES_HPP_

#include <string>
#include <vector>

namespace httpserver {

namespace string_utilities {

/**
 * Function used to convert a string to its uppercase version.
 * It generates a new string in output
 * @param str The string to turn uppercase
 * @return a string that is the uppercase version of the previous
**/
const std::string to_upper_copy(const std::string& str);
const std::string to_lower_copy(const std::string& str);
const std::vector<std::string> string_split(const std::string& s, char sep = ' ', bool collapse = true);

}  // namespace string_utilities

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_STRING_UTILITIES_HPP_
