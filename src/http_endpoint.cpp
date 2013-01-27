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

namespace details
{

http_endpoint::~http_endpoint()
{
    if(reg_compiled)
    {
        regfree(&(this->re_url_modded));
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
    if(use_regex)
        this->url_modded = "^/";
    else
        this->url_modded = "/";
    vector<string> parts;
    string_utilities::to_lower_copy(url, url_complete);

    if(url_complete[0] != '/')
        url_complete = "/" + url_complete;

    http_utils::tokenize_url(url, parts);
    string buffered;
    bool first = true;
    if(registration)
    {
        for(unsigned int i = 0; i< parts.size(); i++)
        {
            if((parts[i] != "") && (parts[i][0] != '{')) 
            {
                if(first)
                {
                    if(parts[i][0] == '^')
                    {
                        this->url_modded = parts[i];
                    }
                    else
                    {
                        this->url_modded += parts[i];
                    }
                    first = false;
                }
                else
                {
                    this->url_modded += "/" + parts[i];
                }
            } 
            else 
            {
                if(
                    (parts[i].size() >= 3) && 
                    (parts[i][0] == '{') && 
                    (parts[i][parts[i].size() - 1] == '}') 
                ) 
                {
                    int bar = parts[i].find_first_of('|');
                    if(bar != (int)string::npos)
                    {
                        this->url_pars.push_back(parts[i].substr(1, bar - 1));
                        if(first)
                        {
                            this->url_modded += parts[i].substr(
                                    bar + 1, parts[i].size() - bar - 2
                            );
                            first = false;
                        }
                        else
                        {
                            this->url_modded += "/"+parts[i].substr(
                                    bar + 1, parts[i].size() - bar - 2
                            );
                        }
                    }
                    else
                    {
                        this->url_pars.push_back(
                                parts[i].substr(1,parts[i].size() - 2)
                        );
                        if(first)
                        {
                            this->url_modded += "([^\\/]+)";
                            first = false;
                        }
                        else
                        {
                            this->url_modded += "/([^\\/]+)";
                        }
                    }
                    this->chunk_positions.push_back(i);
                } 
                else 
                {
                    throw bad_http_endpoint();
                }
            }
            this->url_pieces.push_back(parts[i]);
        }
    }
    else
    {
        for(unsigned int i = 0; i< parts.size(); i++)
        {
            if(first)
            {
                this->url_modded += parts[i];
                first = false;
            }
            else
            {
                this->url_modded += "/" + parts[i];
            }
            this->url_pieces.push_back(parts[i]);
        }
    }
    if(use_regex)
    {
        this->url_modded += "$";
        regcomp(&(this->re_url_modded), url_modded.c_str(), 
                REG_EXTENDED|REG_ICASE|REG_NOSUB
        );
        reg_compiled = true;
    }
}

http_endpoint::http_endpoint(const http_endpoint& h):
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

http_endpoint& http_endpoint::operator =(const http_endpoint& h)
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

bool http_endpoint::operator <(const http_endpoint& b) const 
{
    COMPARATOR(this->url_modded, b.url_modded, toupper);
}

bool http_endpoint::match(const http_endpoint& url) const 
{
    if(this->family_url && (url.url_pieces.size() >= this->url_pieces.size()))
    {
        string nn = "/";
        bool first = true;
        for(unsigned int i = 0; i < this->url_pieces.size(); i++)
        {
            if(first)
            {
                nn += url.url_pieces[i];
                first = false;
            }
            else
            {
                nn += "/" + url.url_pieces[i];
            }
        }
        return regexec(&(this->re_url_modded), nn.c_str(), 0, NULL, 0) == 0;
    }
    else
        return regexec(&(this->re_url_modded), 
                url.url_modded.c_str(), 0, NULL, 0) == 0;
}

};

};
