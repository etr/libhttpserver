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
#include <memory>

#include <httpserver.hpp>

bool verbose = false;

static void log_if_verbose(const httpserver::http_request& req,
                            const httpserver::http_response& res) {
    if (verbose) {
        std::cout << req;
        std::cout << res;
    }
}

class service_resource: public httpserver::http_resource {
 public:
     httpserver::http_response render_get(const httpserver::http_request &req) override;
     httpserver::http_response render_put(const httpserver::http_request &req) override;
     httpserver::http_response render_post(const httpserver::http_request &req) override;
     httpserver::http_response render(const httpserver::http_request &req) override;
     httpserver::http_response render_head(const httpserver::http_request &req) override;
     httpserver::http_response render_options(const httpserver::http_request &req) override;
     httpserver::http_response render_connect(const httpserver::http_request &req) override;
     httpserver::http_response render_delete(const httpserver::http_request &req) override;
};

httpserver::http_response service_resource::render_get(const httpserver::http_request &req) {
    std::cout << "service_resource::render_get()" << std::endl;
    auto res = httpserver::http_response::string("GET response");
    log_if_verbose(req, res);
    return res;
}

httpserver::http_response service_resource::render_put(const httpserver::http_request &req) {
    std::cout << "service_resource::render_put()" << std::endl;
    auto res = httpserver::http_response::string("PUT response");
    log_if_verbose(req, res);
    return res;
}

httpserver::http_response service_resource::render_post(const httpserver::http_request &req) {
    std::cout << "service_resource::render_post()" << std::endl;
    auto res = httpserver::http_response::string("POST response");
    log_if_verbose(req, res);
    return res;
}

httpserver::http_response service_resource::render(const httpserver::http_request &req) {
    std::cout << "service_resource::render()" << std::endl;
    auto res = httpserver::http_response::string("generic response");
    log_if_verbose(req, res);
    return res;
}

httpserver::http_response service_resource::render_head(const httpserver::http_request &req) {
    std::cout << "service_resource::render_head()" << std::endl;
    auto res = httpserver::http_response::string("HEAD response");
    log_if_verbose(req, res);
    return res;
}

httpserver::http_response service_resource::render_options(const httpserver::http_request &req) {
    std::cout << "service_resource::render_options()" << std::endl;
    auto res = httpserver::http_response::string("OPTIONS response");
    log_if_verbose(req, res);
    return res;
}

httpserver::http_response service_resource::render_connect(const httpserver::http_request &req) {
    std::cout << "service_resource::render_connect()" << std::endl;
    auto res = httpserver::http_response::string("CONNECT response");
    log_if_verbose(req, res);
    return res;
}

httpserver::http_response service_resource::render_delete(const httpserver::http_request &req) {
    std::cout << "service_resource::render_delete()" << std::endl;
    auto res = httpserver::http_response::string("DELETE response");
    log_if_verbose(req, res);
    return res;
}

void usage() {
    std::cout << "Usage:" << std::endl
              << "service [-p <port>][-s [-k <keyFileName>][-c <certFileName>]][-v]" << std::endl;
}

int main(int argc, char **argv) {
    uint16_t port = 8080;
    int c;
    const char *key = "key.pem";
    const char *cert = "cert.pem";
    bool secure = false;

    while ((c = getopt(argc, argv, "p:k:c:sv?")) != EOF) {
        switch (c) {
        case 'p':
            port = strtoul(optarg, nullptr, 10);
            break;
        case 'k':
            key = optarg;
            break;
        case 'c':
            cert = optarg;
            break;
        case 's':
            secure = true;
            break;
        case 'v':
            verbose = true;
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
    httpserver::create_webserver cw = httpserver::create_webserver(port).max_threads(5);

    if (secure) {
        cw.use_ssl().https_mem_key(key).https_mem_cert(cert);
    }

    //
    // Create webserver using the configured options
    //
    httpserver::webserver ws{cw};

    //
    // Create and register service resource available at /service
    //
    auto res = std::make_shared<service_resource>();
    ws.register_prefix("/service", res);

    //
    // Start and block the webserver
    //
    ws.start(true);

    return 0;
}
