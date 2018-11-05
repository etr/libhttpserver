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

#include "http_endpoint.hpp"
#include "http_utils.hpp"
#include "string_utilities.hpp"

using namespace std;

namespace httpserver
{

using namespace http;

webserver::http_endpoint::~http_endpoint()
{
    if(reg_compiled)
    {
        regfree(&(this->re_url_modded));
    }
}

webserver::http_endpoint::http_endpoint
(
    const string& url,
    bool family,
    bool registration,
    bool use_regex
):
    family_url(family),
    reg_compiled(false)
{
    this->url_modded = use_regex ? "^/" : "/";
    vector<string> parts;

#ifdef CASE_INSENSITIVE
    string_utilities::to_lower_copy(url, url_complete);
#else
    url_complete = url;
#endif

    if(url_complete[0] != '/')
        url_complete = "/" + url_complete;

    http_utils::tokenize_url(url, parts);
    string buffered;
    bool first = true;

    for (unsigned int i = 0; i < parts.size(); i++)
    {
        if(!registration)
        {
            this->url_modded += (first ? "" : "/") + parts[i];
            first = false;

            this->url_pieces.push_back(parts[i]);

            continue;
        }

        if((parts[i] != "") && (parts[i][0] != '{'))
        {
            if(first)
            {
                this->url_modded = (parts[i][0] == '^' ? "" : this->url_modded) + parts[i];
                first = false;
            }
            else
            {
                this->url_modded += "/" + parts[i];
            }
            this->url_pieces.push_back(parts[i]);

            continue;
        }

        if((parts[i].size() < 3) || (parts[i][0] != '{') || (parts[i][parts[i].size() - 1] != '}'))
            throw webserver::http_endpoint::bad_http_endpoint();

        std::string::size_type bar = parts[i].find_first_of('|');
        this->url_pars.push_back(parts[i].substr(1, bar != string::npos ? bar - 1 : parts[i].size() - 2));
        this->url_modded += (first ? "" : "/") + (bar != string::npos ? parts[i].substr(bar + 1, parts[i].size() - bar - 2) : "([^\\/]+)");

        first = false;

        this->chunk_positions.push_back(i);

        this->url_pieces.push_back(parts[i]);
    }

    if(use_regex)
    {
        this->url_modded += "$";
        regcomp(&(this->re_url_modded), url_modded.c_str(),
                REG_EXTENDED|REG_ICASE|REG_NOSUB
        );
        this->reg_compiled = true;
    }
}

webserver::http_endpoint::http_endpoint(const webserver::http_endpoint& h):
    url_complete(h.url_complete),
    url_modded(h.url_modded),
    url_pars(h.url_pars),
    url_pieces(h.url_pieces),
    chunk_positions(h.chunk_positions),
    family_url(h.family_url),
    reg_compiled(h.reg_compiled)
{
    if(this->reg_compiled)
        regcomp(&(this->re_url_modded), url_modded.c_str(),
                REG_EXTENDED|REG_ICASE|REG_NOSUB
        );
}

webserver::http_endpoint& webserver::http_endpoint::operator =(const webserver::http_endpoint& h)
{
    this->url_complete = h.url_complete;
    this->url_modded = h.url_modded;
    this->family_url = h.family_url;
    this->reg_compiled = h.reg_compiled;
    if(this->reg_compiled)
        regcomp(&(this->re_url_modded), url_modded.c_str(),
                REG_EXTENDED|REG_ICASE|REG_NOSUB
        );
    this->url_pars = h.url_pars;
    this->url_pieces = h.url_pieces;
    this->chunk_positions = h.chunk_positions;
    return *this;
}

bool webserver::http_endpoint::operator <(const webserver::http_endpoint& b) const
{
    COMPARATOR(this->url_modded, b.url_modded, std::toupper);
}

bool webserver::http_endpoint::match(const webserver::http_endpoint& url) const
{

    if(!this->family_url || url.url_pieces.size() < this->url_pieces.size())  
        return regexec(&(this->re_url_modded), url.url_complete.c_str(), 0, NULL, 0) == 0;

    string nn = "/";
    bool first = true;
    for(unsigned int i = 0; i < this->url_pieces.size(); i++)
    {
        nn += (first ? "" : "/") + url.url_pieces[i];
        first = false;
    }
    return regexec(&(this->re_url_modded), nn.c_str(), 0, NULL, 0) == 0;
}

};

