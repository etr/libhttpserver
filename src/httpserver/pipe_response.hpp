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

#ifndef SRC_HTTPSERVER_PIPE_RESPONSE_HPP_
#define SRC_HTTPSERVER_PIPE_RESPONSE_HPP_

#include "httpserver/http_utils.hpp"
#include "httpserver/http_response.hpp"

struct MHD_Response;

namespace httpserver {

class pipe_response : public http_response {
 public:
     pipe_response() = default;

     explicit pipe_response(
             int pipe_fd,
             int response_code = http::http_utils::http_ok,
             const std::string& content_type = http::http_utils::application_octet_stream):
         http_response(response_code, content_type),
         pipe_fd(pipe_fd) { }

     pipe_response(const pipe_response& other) = delete;
     pipe_response(pipe_response&& other) noexcept = default;

     pipe_response& operator=(const pipe_response& b) = delete;
     pipe_response& operator=(pipe_response&& b) = default;

     ~pipe_response() = default;

     MHD_Response* get_raw_response();

 private:
     int pipe_fd = -1;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_PIPE_RESPONSE_HPP_
