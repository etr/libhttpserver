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

#ifndef _DEFERRED_RESPONSE_HPP_
#define _DEFERRED_RESPONSE_HPP_

#include "httpserver/string_response.hpp"

namespace httpserver
{

namespace details
{
    ssize_t cb(void*, uint64_t, char*, size_t);
};

typedef ssize_t(*cycle_callback_ptr)(char*, size_t);

class deferred_response : public string_response
{
    public:
        explicit deferred_response(
                cycle_callback_ptr cycle_callback,
                const std::string& content = "",
                int response_code = http::http_utils::http_ok,
                const std::string& content_type = http::http_utils::text_plain
        ):
            string_response(content, response_code, content_type),
            cycle_callback(cycle_callback),
            completed(false)
        {
        }

        deferred_response(const deferred_response& other):
            string_response(other),
            cycle_callback(other.cycle_callback),
            completed(other.completed)
        {
        }

        deferred_response(deferred_response&& other) noexcept:
            string_response(std::move(other)),
            cycle_callback(std::move(other.cycle_callback)),
            completed(other.completed)
        {
        }

        deferred_response& operator=(const deferred_response& b)
        {
            if (this == &b) return *this;

            (string_response&) (*this) = b;
            this->cycle_callback = b.cycle_callback;
            this->completed = b.completed;

            return *this;
        }

        deferred_response& operator=(deferred_response&& b)
        {
            if (this == &b) return *this;

            (string_response&) (*this) = std::move(b);
            this->cycle_callback = std::move(b.cycle_callback);
            this->completed = b.completed;

            return *this;
        }

        ~deferred_response()
        {
        }

        MHD_Response* get_raw_response();
        void decorate_response(MHD_Response* response);
    private:
        cycle_callback_ptr cycle_callback;
        bool completed;

        friend ssize_t details::cb(void* cls, uint64_t pos, char* buf, size_t max);
};

}
#endif // _DEFERRED_RESPONSE_HPP_
