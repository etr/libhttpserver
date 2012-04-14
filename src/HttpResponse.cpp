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
#include <cstdio>
#include "HttpUtils.hpp"
#include "Webserver.hpp"
#include "HttpResponse.hpp"

using namespace std;

namespace httpserver
{
//RESPONSE
void HttpResponse::HttpResponseInit
(
	const string& content,
	int responseCode,
	const std::string& contentType,
	const HttpResponse::ResponseType_T& responseType
)
{
	if(responseType == HttpResponse::FILE_CONTENT)
	{
		FILE* f;
		if(!(f = fopen(content.c_str(), "r")))
		{
			this->content = NOT_FOUND_ERROR;
			this->responseCode = 404;
			this->setHeader(HttpUtils::http_header_content_type, contentType);
			this->fp = -1;
		}
		else
		{
			this->responseCode = responseCode;
			this->filename = content;
			this->fp = fileno(f);
		}
	}
	else
	{
		this->content = content;
		this->responseCode = responseCode;
		this->setHeader(HttpUtils::http_header_content_type, contentType);
		this->fp = -1;
	}
}

};
