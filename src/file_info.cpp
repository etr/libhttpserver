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

#include <string.h>
#include "httpserver/file_info.hpp"

namespace httpserver {
namespace http {

void file_info::set_file_system_file_name(const std::string& file_system_file_name) {
    _file_system_file_name = file_system_file_name;
}

void file_info::set_content_type(const std::string& content_type) {
    _content_type = content_type;
}

void file_info::set_transfer_encoding(const std::string& transfer_encoding) {
    _transfer_encoding = transfer_encoding;
}

void file_info::grow_file_size(size_t additional_file_size) {
    _file_size += additional_file_size;
}
size_t file_info::get_file_size() const {
    return _file_size;
}
const std::string file_info::get_file_system_file_name() const {
    return _file_system_file_name;
}
const std::string file_info::get_content_type() const {
    return _content_type;
}
const std::string file_info::get_transfer_encoding() const {
    return _transfer_encoding;
}

}  // namespace http
}  // namespace httpserver
