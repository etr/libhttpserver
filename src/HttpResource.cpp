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
#include "HttpResource.hpp"
#include "HttpUtils.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Webserver.hpp"
#include "string_utilities.hpp"

using namespace std;

namespace httpserver 
{
//RESOURCE
HttpResource::HttpResource() 
{
	this->allowedMethods[MHD_HTTP_METHOD_GET] = true;
	this->allowedMethods[MHD_HTTP_METHOD_POST] = true;
	this->allowedMethods[MHD_HTTP_METHOD_PUT] = true;
	this->allowedMethods[MHD_HTTP_METHOD_HEAD] = true;
	this->allowedMethods[MHD_HTTP_METHOD_DELETE] = true;
	this->allowedMethods[MHD_HTTP_METHOD_TRACE] = true;
	this->allowedMethods[MHD_HTTP_METHOD_CONNECT] = true;
	this->allowedMethods[MHD_HTTP_METHOD_OPTIONS] = true;
}

HttpResource::~HttpResource() 
{
}

HttpResponse HttpResource::render(const HttpRequest& r) 
{
	if(this->isAllowed(r.getMethod()))
	{
		return this->render_404();
	} 
	else 
	{
		return this->render_405();
	}
}

HttpResponse HttpResource::render_404() 
{
	return HttpResponse(NOT_FOUND_ERROR, 404);
}

HttpResponse HttpResource::render_405() 
{
	return HttpResponse(METHOD_ERROR, 405);
}

HttpResponse HttpResource::render_500() 
{
	return HttpResponse(GENERIC_ERROR, 500);
}

HttpResponse HttpResource::render_GET(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_POST(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_PUT(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_DELETE(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_HEAD(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_TRACE(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_OPTIONS(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_CONNECT(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::routeRequest(const HttpRequest& r) 
{
	string method = string_utilities::to_upper_copy(r.getMethod());

	HttpResponse res;

	if(method == MHD_HTTP_METHOD_GET) 
	{
		res = this->render_GET(r);
	} 
	else if (method == MHD_HTTP_METHOD_POST) 
	{
		res = this->render_POST(r);
	} 
	else if (method == MHD_HTTP_METHOD_PUT) 
	{
		res = this->render_PUT(r);
	} 
	else if (method == MHD_HTTP_METHOD_DELETE) 
	{
		res = this->render_DELETE(r);
	} 
	else if (method == MHD_HTTP_METHOD_HEAD) 
	{
		res = this->render_HEAD(r);
	} 
	else if (method == MHD_HTTP_METHOD_TRACE) 
	{
		res = this->render_TRACE(r);
	} 
	else if (method == MHD_HTTP_METHOD_OPTIONS) 
	{
		res = this->render_OPTIONS(r);
	} 
	else if (method == MHD_HTTP_METHOD_CONNECT) 
	{
		res = this->render_CONNECT(r);
	} 
	else 
	{
		res = this->render(r);
	}
	return res;
}

};
