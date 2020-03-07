/*
     This file is part of libhttpserver
     Copyright (C) 2014 Sebastiano Merlino

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

#include <unistd.h>

#include <cstdio>
#include <iostream>

#include <httpserver.hpp>

using namespace httpserver;

bool verbose=false;

class service_resource: public http_resource {
public:
	service_resource();

	~service_resource();

	const std::shared_ptr<http_response> render_GET(const http_request &req);
	const std::shared_ptr<http_response> render_PUT(const http_request &req);
	const std::shared_ptr<http_response> render_POST(const http_request &req);
	const std::shared_ptr<http_response> render(const http_request &req);
	const std::shared_ptr<http_response> render_HEAD(const http_request &req);
	const std::shared_ptr<http_response> render_OPTIONS(const http_request &req);
	const std::shared_ptr<http_response> render_CONNECT(const http_request &req);
	const std::shared_ptr<http_response> render_DELETE(const http_request &req);

private:


};

service_resource::service_resource()
{}

service_resource::~service_resource()
{}

const std::shared_ptr<http_response>
service_resource::render_GET(const http_request &req)
{
    std::cout << "service_resource::render_GET()" << std::endl;

    if (verbose) std::cout << req;
    string_response* res = new string_response("GET response", 200);

    if (verbose) std::cout << *res;

    return std::shared_ptr<http_response>(res);
}


const std::shared_ptr<http_response>
service_resource::render_PUT(const http_request &req)
{
    std::cout << "service_resource::render_PUT()" << std::endl;

    if (verbose) std::cout << req;

    string_response* res = new string_response("PUT response", 200);

    if (verbose) std::cout << *res;

    return std::shared_ptr<http_response>(res);
}


const std::shared_ptr<http_response>
service_resource::render_POST(const http_request &req)
{
    std::cout << "service_resource::render_POST()" << std::endl;

    if (verbose) std::cout << req;

    string_response* res = new string_response("POST response", 200);

    if (verbose) std::cout << *res;

    return std::shared_ptr<http_response>(res);
}

const std::shared_ptr<http_response>
service_resource::render(const http_request &req)
{
    std::cout << "service_resource::render()" << std::endl;

    if (verbose) std::cout << req;

    string_response* res = new string_response("generic response", 200);

    if (verbose) std::cout << *res;

    return std::shared_ptr<http_response>(res);
}


const std::shared_ptr<http_response>
service_resource::render_HEAD(const http_request &req)
{
    std::cout << "service_resource::render_HEAD()" << std::endl;

    if (verbose) std::cout << req;

    string_response* res = new string_response("HEAD response", 200);

    if (verbose) std::cout << *res;

    return std::shared_ptr<http_response>(res);
}

const std::shared_ptr<http_response>
service_resource::render_OPTIONS(const http_request &req)
{
    std::cout << "service_resource::render_OPTIONS()" << std::endl;

    if (verbose) std::cout << req;

    string_response* res = new string_response("OPTIONS response", 200);

    if (verbose) std::cout << *res;

    return std::shared_ptr<http_response>(res);
}

const std::shared_ptr<http_response>
service_resource::render_CONNECT(const http_request &req)
{
    std::cout << "service_resource::render_CONNECT()" << std::endl;

    if (verbose) std::cout << req;

    string_response* res = new string_response("CONNECT response", 200);

    if (verbose) std::cout << *res;

    return std::shared_ptr<http_response>(res);
}

const std::shared_ptr<http_response>
service_resource::render_DELETE(const http_request &req)
{
    std::cout << "service_resource::render_DELETE()" << std::endl;

    if (verbose) std::cout << req;

    string_response* res = new string_response("DELETE response", 200);

    if (verbose) std::cout << *res;

    return std::shared_ptr<http_response>(res);
}

void usage()
{
    std::cout << "Usage:" << std::endl
              << "service [-p <port>][-s [-k <keyFileName>][-c <certFileName>]][-v]" << std::endl;
}

int main(int argc, char **argv)
{
	uint16_t port=8080;
	int c;
    const char *key="key.pem";
    const char *cert="cert.pem";
    bool secure=false;

	while ((c = getopt(argc,argv,"p:k:c:sv?")) != EOF) {
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
        case 'v':
            verbose=true;
            break;
		default:
            usage();
            exit(1);
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
