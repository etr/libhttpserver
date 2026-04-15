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

#ifndef SRC_HTTPSERVER_IOVEC_RESPONSE_HPP_
#define SRC_HTTPSERVER_IOVEC_RESPONSE_HPP_

#include <string>
#include <utility>
#include <vector>
#include "httpserver/http_utils.hpp"
#include "httpserver/http_response.hpp"

struct MHD_Response;

namespace httpserver {

class iovec_response : public http_response {
 public:
     iovec_response() = default;

     explicit iovec_response(
             std::vector<std::string> buffers,
             int response_code = http::http_utils::http_ok,
             const std::string& content_type = http::http_utils::text_plain):
         http_response(response_code, content_type),
         buffers(std::move(buffers)) { }

     iovec_response(const iovec_response& other) = default;
     iovec_response(iovec_response&& other) noexcept = default;

     iovec_response& operator=(const iovec_response& b) = default;
     iovec_response& operator=(iovec_response&& b) = default;

     ~iovec_response() = default;

     MHD_Response* get_raw_response();

 private:
     std::vector<std::string> buffers;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_IOVEC_RESPONSE_HPP_
