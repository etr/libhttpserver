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

#ifndef SRC_HTTPSERVER_DEFERRED_RESPONSE_HPP_
#define SRC_HTTPSERVER_DEFERRED_RESPONSE_HPP_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <memory>
#include <string>
#include "httpserver/http_utils.hpp"
#include "httpserver/string_response.hpp"

struct MHD_Response;

namespace httpserver {

namespace details {
    MHD_Response* get_raw_response_helper(void* cls, ssize_t (*cb)(void*, uint64_t, char*, size_t));
}

template <class T>
class deferred_response : public string_response {
 public:
     explicit deferred_response(
             ssize_t(*cycle_callback)(std::shared_ptr<T>, char*, size_t),
             std::shared_ptr<T> closure_data,
             const std::string& content = "",
             int response_code = http::http_utils::http_ok,
             const std::string& content_type = http::http_utils::text_plain):
         string_response(content, response_code, content_type),
         cycle_callback(cycle_callback),
         closure_data(closure_data) { }

     deferred_response(const deferred_response& other) = default;
     deferred_response(deferred_response&& other) noexcept = default;
     deferred_response& operator=(const deferred_response& b) = default;
     deferred_response& operator=(deferred_response&& b) = default;

     ~deferred_response() = default;

     MHD_Response* get_raw_response() {
         return details::get_raw_response_helper(reinterpret_cast<void*>(this), &cb);
     }

 private:
     ssize_t (*cycle_callback)(std::shared_ptr<T>, char*, size_t);
     std::shared_ptr<T> closure_data;

     static ssize_t cb(void* cls, uint64_t, char* buf, size_t max) {
         deferred_response<T>* dfr = static_cast<deferred_response<T>*>(cls);
         return dfr->cycle_callback(dfr->closure_data, buf, max);
     }
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DEFERRED_RESPONSE_HPP_
