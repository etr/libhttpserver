/*
     This file is part of libhttpserver
     Copyright (C) 2011 Sebastiano Merlino

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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/
#ifndef _STRING_UTILITIES_H_
#define _STRING_UTILITIES_H_

#include <string>
#include <vector>

namespace httpserver
{
namespace string_utilities
{

std::string to_upper_copy(std::string str);
std::string to_lower_copy(std::string str);
std::vector<std::string> string_split(std::string s, char sep);
std::string regex_replace(std::string str, std::string pattern, std::string replace_str);

};
};

#endif
