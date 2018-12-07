/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino

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

#include <algorithm>
#include <string>
#include <istream>
#include <sstream>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <regex.h>
#include "string_utilities.hpp"

namespace httpserver
{
namespace string_utilities
{

std::string to_upper_copy(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(),
        result.end(),
        result.begin(),
        (int(*)(int)) std::toupper
    );

    return result;
}

void to_upper(std::string& str)
{
    std::transform(str.begin(),
        str.end(),
        str.begin(),
        (int(*)(int)) std::toupper
    );
}

std::string to_lower_copy(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(),
        result.end(),
        result.begin(),
        (int(*)(int)) std::tolower
    );

    return result;
}

std::vector<std::string> string_split(
        const std::string& s,
        char sep,
        bool collapse
)
{
    std::vector<std::string> result;

    std::istringstream buf(s);
    for(std::string token; getline(buf, token, sep); )
    {
        if((collapse && token != "") || !collapse)
            result.push_back(token);
    }
    return result;
}

std::string regex_replace(const std::string& str,
        const std::string& pattern,
        const std::string& replace_str
)
{
    regex_t preg;
    regmatch_t substmatch[1];
    regcomp(&preg, pattern.c_str(), REG_EXTENDED|REG_ICASE);
    std::string result;
    if ( regexec(&preg, str.c_str(), 1, substmatch, 0) == 0 )
    {
        char ns[substmatch[0].rm_so + 1 +
            replace_str.size() + (str.size() - substmatch[0].rm_eo) + 2
        ];

        memcpy(ns, str.c_str(), substmatch[0].rm_so+1);

        memcpy(&ns[substmatch[0].rm_so],
                replace_str.c_str(),
                replace_str.size()
        );

        memcpy(&ns[substmatch[0].rm_so+replace_str.size()],
                &str[substmatch[0].rm_eo], strlen(&str[substmatch[0].rm_eo])
        );

        ns[substmatch[0].rm_so +
            replace_str.size() +
            strlen(&str[substmatch[0].rm_eo])
        ] = 0;

        result = std::string((char*)ns);
    }
    regfree(&preg);

    return result;
}

};
};
