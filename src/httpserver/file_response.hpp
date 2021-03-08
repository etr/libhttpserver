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

#ifndef SRC_HTTPSERVER_FILE_RESPONSE_HPP_
#define SRC_HTTPSERVER_FILE_RESPONSE_HPP_

#include <string>
#include "http_utils.hpp"
#include "httpserver/http_response.hpp"

struct MHD_Response;

namespace httpserver {

class file_response : public http_response {
 public:
     file_response() = default;

     explicit file_response(
             const std::string& filename,
             int response_code = http::http_utils::http_ok,
             const std::string& content_type = http::http_utils::text_plain):
         http_response(response_code, content_type),
         filename(filename) { }

     file_response(const file_response& other) = default;
     file_response(file_response&& other) noexcept = default;

     file_response& operator=(const file_response& b) = default;
     file_response& operator=(file_response&& b) = default;

     ~file_response() = default;

     MHD_Response* get_raw_response();

 private:
     std::string filename = "";
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_FILE_RESPONSE_HPP_
