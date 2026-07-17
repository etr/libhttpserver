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

// http_utils_helpers.hpp -- free-function helpers in namespace
// httpserver::http (sockaddr -> string/port, header/arg map dumps, URL
// unescape, file loading).
//
// Carved out of httpserver/http_utils.hpp to keep that header
// under the project per-file line-count ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh). Included from the bottom of http_utils.hpp
// after the types these declarations reference (header_view_map /
// header_map / arg_view_map / unescaper_ptr) are defined; not intended to
// be included on its own.
#ifndef SRC_HTTPSERVER_HTTP_UTILS_HELPERS_HPP_
#define SRC_HTTPSERVER_HTTP_UTILS_HELPERS_HPP_

#include <stddef.h>
#include <stdint.h>
#include <iosfwd>
#include <string>

namespace httpserver {
namespace http {

/**
 * Method used to get an ip in form of string from a sockaddr structure.
 *
 * The buffer length is computed internally from the address family; the
 * v1 `maxlen` parameter was removed in v2.0.
 *
 * @param sa The sockaddr object to find the ip address from.
 * @return string containing the ip address.
**/
std::string get_ip_str(const struct sockaddr *sa);

/**
 * Method used to get a port from a sockaddr
 * @param sa The sockaddr object to find the port from
 * @return short representing the port
**/
uint16_t get_port(const struct sockaddr* sa);

/**
 * Method to output the contents of a headers map to a std::ostream
 * @param os The ostream
 * @param prefix Prefix to identify the map
 * @param map
**/
void dump_header_map(std::ostream& os, const std::string& prefix, const http::header_view_map& map);

/**
 * Overload that accepts the owning header_map directly, avoiding an O(n)
 * copy into a temporary header_view_map. Preferred for diagnostic output
 * (operator<<) where the source map is immediately available.
 * @param os The ostream
 * @param prefix Prefix to identify the map
 * @param map
**/
void dump_header_map(std::ostream& os, const std::string& prefix, const http::header_map& map);

/**
 * Method to output the contents of an arguments map to a std::ostream
 * @param os The ostream
 * @param prefix Prefix to identify the map
 * @param map
**/
void dump_arg_map(std::ostream& os, const std::string& prefix, const http::arg_view_map& map);

/**
 * Process escape sequences ('+'=space, %HH) Updates val in place; the
 * result should be UTF-8 encoded and cannot be larger than the input.
 * The result must also still be 0-terminated.
 *
 * @param val the string to unescape
 * @return length of the resulting val (strlen(val) maybe
 *  shorter afterwards due to elimination of escape sequences)
 */
size_t http_unescape(std::string* val);

const std::string load_file(const std::string& filename);

size_t base_unescaper(std::string*, unescaper_ptr unescaper);

}  // namespace http
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_HTTP_UTILS_HELPERS_HPP_
