/*
     This file is part of libhttpserver
     Copyright (C) 2014 Chris Love

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

#include <httpserver.hpp>
#include <iostream>
#include <unistd.h>
#include <cstdio>

using namespace httpserver;

class service_resource: public http_resource<service_resource> {
public:
	service_resource();

	~service_resource();
	
	void render_GET(const http_request &req, http_response** res);

	void render_PUT(const http_request &req, http_response** res);

	void render_POST(const http_request &req, http_response** res);	

	void render(const http_request &req, http_response** res);

	void render_HEAD(const http_request &req, http_response** res);

	void render_OPTIONS(const http_request &req, http_response** res);

	void render_CONNECT(const http_request &req, http_response** res);

	void render_DELETE(const http_request &req, http_response** res);

private:


};

service_resource::service_resource()
{}

service_resource::~service_resource()
{}

void
service_resource::render_GET(const http_request &req, http_response** res)
{
	std::cout << "service_resource::render_GET()" << std::endl;

	*res = new http_response(http_response_builder("GET response", 200).string_response());	
}


void
service_resource::render_PUT(const http_request &req, http_response** res)
{
	std::cout << "service_resource::render_PUT()" << std::endl;	

	*res = new http_response(http_response_builder("PUT response", 200).string_response());		
}


void
service_resource::render_POST(const http_request &req, http_response** res)
{
	std::cout << "service_resource::render_POST()" << std::endl;	

	*res = new http_response(http_response_builder("POST response", 200).string_response());		
}
void
service_resource::render(const http_request &req, http_response** res)
{
	std::cout << "service_resource::render()" << std::endl;	

	*res = new http_response(http_response_builder("generic response", 200).string_response());	
}


void
service_resource::render_HEAD(const http_request &req, http_response** res)
{
	std::cout << "service_resource::render_HEAD()" << std::endl;

	*res = new http_response(http_response_builder("HEAD response", 200).string_response());		
}

void
service_resource::render_OPTIONS(const http_request &req, http_response** res)
{
	std::cout << "service_resource::render_OPTIONS()" << std::endl;

	*res = new http_response(http_response_builder("OPTIONS response", 200).string_response());		
}

void
service_resource::render_CONNECT(const http_request &req, http_response** res)
{
	std::cout << "service_resource::render_CONNECT()" << std::endl;	

	*res = new http_response(http_response_builder("CONNECT response", 200).string_response());			
}

void
service_resource::render_DELETE(const http_request &req, http_response** res)
{
	std::cout << "service_resource::render_DELETE()" << std::endl;	

	*res = new http_response(http_response_builder("DELETE response", 200).string_response());			
}

int main(int argc, char **argv)
{
	uint16_t port=8080;
	int c;
    const char *key="key.pem";
    const char *cert="cert.pem";
    bool secure=false;

	while ((c = getopt(argc,argv,"p:k:c:s")) != EOF) {
		switch (c) {
		case 'p':
			port=strtoul(optarg,NULL,10);
			break;
        case 'k':
            key = optarg;
            break;
        case 'c':
            cert=optarg;
            break;
        case 's':
            secure=true;
            break;
		default:
			break;
		}
	}

	std::cout << "Using port " << port << std::endl;
    if (secure) {
            std::cout << "Key: " << key << " Certificate: " << cert
                      << std::endl;
    }

    //
    // Use builder to define webserver configuration options
    //
    create_webserver cw = create_webserver(port).max_threads(5);

    if (secure) {
        cw.use_ssl().https_mem_key(key).https_mem_cert(cert);
    }

    //
    // Create webserver using the configured options
    //
	webserver ws = cw;

    //
    // Create and register service resource available at /service
    //
	service_resource res;
	ws.register_resource("/service",&res,true);

    //
    // Start and block the webserver
    //
	ws.start(true);

	return 0;
}
