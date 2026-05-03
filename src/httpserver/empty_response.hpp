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

#ifndef SRC_HTTPSERVER_EMPTY_RESPONSE_HPP_
#define SRC_HTTPSERVER_EMPTY_RESPONSE_HPP_

#include <microhttpd.h>
#include "httpserver/http_utils.hpp"
#include "httpserver/http_response.hpp"

struct MHD_Response;

namespace httpserver {

class empty_response : public http_response {
 public:
     enum response_flags {
         NONE = MHD_RF_NONE,
         HTTP_1_0_COMPATIBLE_STRICT = MHD_RF_HTTP_1_0_COMPATIBLE_STRICT,
         HTTP_1_0_SERVER = MHD_RF_HTTP_1_0_SERVER,
         SEND_KEEP_ALIVE_HEADER = MHD_RF_SEND_KEEP_ALIVE_HEADER,
         HEAD_ONLY = MHD_RF_HEAD_ONLY_RESPONSE
     };

     empty_response() = default;

     explicit empty_response(
             int response_code = http::http_utils::http_no_content,
             int flags = NONE):
         http_response(response_code, ""),
         flags(flags) { }

     // Move-only: base http_response is now move-only (TASK-009 / DR-005).
     empty_response(const empty_response&) = delete;
     empty_response(empty_response&& other) noexcept = default;

     empty_response& operator=(const empty_response&) = delete;
     empty_response& operator=(empty_response&& b) = default;

     ~empty_response() = default;

     MHD_Response* get_raw_response();

 private:
     int flags = NONE;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_EMPTY_RESPONSE_HPP_
