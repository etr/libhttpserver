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

#include "details/http_endpoint.hpp"
#include "http_utils.hpp"
#include "string_utilities.hpp"

using namespace std;

namespace httpserver
{

namespace details
{

using namespace http;

http_endpoint::~http_endpoint()
{
    if(reg_compiled)
    {
        regfree(&(this->re_url_normalized));
    }
}

http_endpoint::http_endpoint
(
    const string& url,
    bool family,
    bool registration,
    bool use_regex
):
    family_url(family),
    reg_compiled(false)
{
    this->url_normalized = use_regex ? "^/" : "/";
    vector<string> parts;

#ifdef CASE_INSENSITIVE
    string_utilities::to_lower_copy(url, url_complete);
#else
    url_complete = url;
#endif

    if (url_complete[url_complete.size() - 1] == '/')
    {
        url_complete = url_complete.substr(0, url_complete.size() - 1);
    }

    if (url_complete[0] != '/')
    {
        url_complete = "/" + url_complete;
    }

    parts = http_utils::tokenize_url(url);
    string buffered;
    bool first = true;

    for (unsigned int i = 0; i < parts.size(); i++)
    {
        if(!registration)
        {
            this->url_normalized += (first ? "" : "/") + parts[i];
            first = false;

            this->url_pieces.push_back(parts[i]);

            continue;
        }

        if((parts[i] != "") && (parts[i][0] != '{'))
        {
            if(first)
            {
                this->url_normalized = (parts[i][0] == '^' ? "" : this->url_normalized) + parts[i];
                first = false;
            }
            else
            {
                this->url_normalized += "/" + parts[i];
            }
            this->url_pieces.push_back(parts[i]);

            continue;
        }

        if((parts[i].size() < 3) || (parts[i][0] != '{') || (parts[i][parts[i].size() - 1] != '}'))
            throw std::invalid_argument("Bad URL format");

        std::string::size_type bar = parts[i].find_first_of('|');
        this->url_pars.push_back(parts[i].substr(1, bar != string::npos ? bar - 1 : parts[i].size() - 2));
        this->url_normalized += (first ? "" : "/") + (bar != string::npos ? parts[i].substr(bar + 1, parts[i].size() - bar - 2) : "([^\\/]+)");

        first = false;

        this->chunk_positions.push_back(i);

        this->url_pieces.push_back(parts[i]);
    }

    if(use_regex)
    {
        this->url_normalized += "$";
        regcomp(&(this->re_url_normalized), url_normalized.c_str(),
                REG_EXTENDED|REG_ICASE|REG_NOSUB
        );
        this->reg_compiled = true;
    }
}

http_endpoint::http_endpoint(const http_endpoint& h):
    url_complete(h.url_complete),
    url_normalized(h.url_normalized),
    url_pars(h.url_pars),
    url_pieces(h.url_pieces),
    chunk_positions(h.chunk_positions),
    family_url(h.family_url),
    reg_compiled(h.reg_compiled)
{
    if(this->reg_compiled)
        regcomp(&(this->re_url_normalized), url_normalized.c_str(),
                REG_EXTENDED|REG_ICASE|REG_NOSUB
        );
}

http_endpoint& http_endpoint::operator =(const http_endpoint& h)
{
    this->url_complete = h.url_complete;
    this->url_normalized = h.url_normalized;
    this->family_url = h.family_url;
    this->reg_compiled = h.reg_compiled;
    if(this->reg_compiled)
        regcomp(&(this->re_url_normalized), url_normalized.c_str(),
                REG_EXTENDED|REG_ICASE|REG_NOSUB
        );
    this->url_pars = h.url_pars;
    this->url_pieces = h.url_pieces;
    this->chunk_positions = h.chunk_positions;
    return *this;
}

bool http_endpoint::operator <(const http_endpoint& b) const
{
    COMPARATOR(this->url_normalized, b.url_normalized, std::toupper);
}

bool http_endpoint::match(const http_endpoint& url) const
{
    if (!this->reg_compiled) throw std::invalid_argument("Cannot run match. Regex suppressed.");

    if(!this->family_url || url.url_pieces.size() < this->url_pieces.size())
        return regexec(&(this->re_url_normalized), url.url_complete.c_str(), 0, NULL, 0) == 0;

    string nn = "/";
    bool first = true;
    for(unsigned int i = 0; i < this->url_pieces.size(); i++)
    {
        nn += (first ? "" : "/") + url.url_pieces[i];
        first = false;
    }
    return regexec(&(this->re_url_normalized), nn.c_str(), 0, NULL, 0) == 0;
}

};

};
