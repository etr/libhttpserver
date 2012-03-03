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
#include <regex.h>
#include "HttpEndpoint.hpp"
#include "HttpUtils.hpp"
#include "string_utilities.hpp"

using namespace std;

namespace httpserver
{
using namespace http;
//ENDPOINT
HttpEndpoint::HttpEndpoint(bool family):
	url_complete("/"),
	url_modded("/"),
	family_url(family),
    reg_compiled(false)
{
}

HttpEndpoint::HttpEndpoint(const string& url, bool family, bool registration):
	url_complete(string_utilities::to_lower_copy(url)),
	url_modded("/"),
	family_url(family),
    reg_compiled(false)
{
	vector<string> parts = HttpUtils::tokenizeUrl(url);
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
				if(( parts[i].size() >= 3) && (parts[i][0] == '{') && (parts[i][parts[i].size() - 1] == '}') ) 
				{
					int bar = parts[i].find_first_of('|');
					if(bar != (int)string::npos)
					{
						this->url_pars.push_back(parts[i].substr(1, bar - 1));
						if(first)
						{
							this->url_modded += parts[i].substr(bar + 1, parts[i].size() - bar - 2);
							first = false;
						}
						else
						{
							this->url_modded += "/"+parts[i].substr(bar + 1, parts[i].size() - bar - 2);
						}
					}
					else
					{
						this->url_pars.push_back(parts[i].substr(1,parts[i].size() - 2));
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
					// RITORNARE ECCEZIONE
				}
			}
            this->url_pieces.push_back(parts[i]);
        }
        regcomp(&(this->re_url_modded), url_modded.c_str(), REG_EXTENDED|REG_ICASE);
        reg_compiled = true;
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
//	this->re_url_modded = boost::xpressive::sregex::compile( url_modded, boost::xpressive::regex_constants::icase );
}

HttpEndpoint::HttpEndpoint(const HttpEndpoint& h)
{
    this->url_complete = h.url_complete;
    this->url_modded = h.url_modded;
    this->family_url = h.family_url;
    this->reg_compiled = h.reg_compiled;
    if(this->reg_compiled)
        regcomp(&(this->re_url_modded), url_modded.c_str(), REG_EXTENDED|REG_ICASE);
    this->url_pars = h.url_pars;
    this->url_pieces = h.url_pieces;
    this->chunk_positions = h.chunk_positions;
}

HttpEndpoint::~HttpEndpoint()
{
    
    if(reg_compiled)
    {
        regfree(&(this->re_url_modded));
    }
    
}

HttpEndpoint& HttpEndpoint::operator =(const HttpEndpoint& h)
{
    this->url_complete = h.url_complete;
    this->url_modded = h.url_modded;
    this->family_url = h.family_url;
    this->reg_compiled = h.reg_compiled;
    if(this->reg_compiled)
        regcomp(&(this->re_url_modded), url_modded.c_str(), REG_EXTENDED|REG_ICASE);
    this->url_pars = h.url_pars;
    this->url_pieces = h.url_pieces;
    this->chunk_positions = h.chunk_positions;
    return *this;
}

bool HttpEndpoint::operator <(const HttpEndpoint& b) const 
{
	return string_utilities::to_lower_copy(this->url_modded) < string_utilities::to_lower_copy(b.url_modded);
}

bool HttpEndpoint::match(const HttpEndpoint& url) const 
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
//		return boost::xpressive::regex_match(nn, this->re_url_modded);
	}
	else
	{
        return regexec(&(this->re_url_modded), url.url_modded.c_str(), 0, NULL, 0) == 0;
//		return boost::xpressive::regex_match(url.url_modded, this->re_url_modded);
	}
}

const std::vector<std::string> HttpEndpoint::get_url_pars() const 
{
	return this->url_pars;
}

const std::vector<std::string> HttpEndpoint::get_url_pieces() const 
{
	return this->url_pieces;
}

const std::vector<int> HttpEndpoint::get_chunk_positions() const 
{
	return this->chunk_positions;
}

};
