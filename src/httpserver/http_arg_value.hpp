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

#ifndef SRC_HTTPSERVER_HTTP_ARG_VALUE_HPP_
#define SRC_HTTPSERVER_HTTP_ARG_VALUE_HPP_

#include <string>
#include <string_view>
#include <vector>

namespace httpserver {

class http_arg_value {
 public:
    std::string_view get_flat_value() const {
        return values.empty() ? "" : values[0];
    }

    std::vector<std::string_view> get_all_values() const {
        return values;
    }

    operator std::string() const {
        return std::string(get_flat_value());
    }

    operator std::string_view() const {
        return get_flat_value();
    }

    operator std::vector<std::string>() const {
        std::vector<std::string> result;
        for (auto const & value : values) {
            result.push_back(std::string(value));
        }
        return result;
    }

    std::vector<std::string_view> values;
};

}  // end namespace httpserver

#endif  // SRC_HTTPSERVER_HTTP_ARG_VALUE_HPP_
