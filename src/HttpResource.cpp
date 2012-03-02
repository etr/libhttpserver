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

void HttpResource::setAllowing(const string& method, bool allowing) 
{
	if(this->allowedMethods.count(method)) 
	{
		this->allowedMethods[method] = allowing;
	}
}

void HttpResource::allowAll() 
{
    map<string,bool>::iterator it;
    for ( it=this->allowedMethods.begin() ; it != this->allowedMethods.end(); it++ )
        this->allowedMethods[(*it).first] = true;
}

void HttpResource::disallowAll() 
{
	map<string,bool>::iterator it;
	for ( it=this->allowedMethods.begin() ; it != this->allowedMethods.end(); it++ )
		this->allowedMethods[(*it).first] = false;
}

bool HttpResource::isAllowed(const string& method) 
{
	if(this->allowedMethods.count(method))
	{
		return this->allowedMethods[method];
	}
	else
	{
		return false;
	}
}

};
