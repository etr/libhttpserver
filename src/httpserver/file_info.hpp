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

#ifndef SRC_HTTPSERVER_FILE_INFO_HPP_
#define SRC_HTTPSERVER_FILE_INFO_HPP_

#include <string>

namespace httpserver {
class webserver;

namespace http {

class file_info {
 public:
     size_t get_file_size() const;
     const std::string get_file_system_file_name() const;
     const std::string get_content_type() const;
     const std::string get_transfer_encoding() const;

     file_info() = default;

 private:
     size_t _file_size;
     std::string _file_system_file_name;
     std::string _content_type;
     std::string _transfer_encoding;

     void set_file_system_file_name(const std::string& file_system_file_name);
     void set_content_type(const std::string& content_type);
     void set_transfer_encoding(const std::string& transfer_encoding);
     void grow_file_size(size_t additional_file_size);

     friend class httpserver::webserver;
};

}  // namespace http
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_FILE_INFO_HPP_

