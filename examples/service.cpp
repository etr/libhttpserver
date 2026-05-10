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

class service_resource: public httpserver::http_resource {
 public:
     service_resource();

     ~service_resource();

     std::shared_ptr<httpserver::http_response> render_get(const httpserver::http_request &req);
     std::shared_ptr<httpserver::http_response> render_put(const httpserver::http_request &req);
     std::shared_ptr<httpserver::http_response> render_post(const httpserver::http_request &req);
     std::shared_ptr<httpserver::http_response> render(const httpserver::http_request &req);
     std::shared_ptr<httpserver::http_response> render_head(const httpserver::http_request &req);
     std::shared_ptr<httpserver::http_response> render_options(const httpserver::http_request &req);
     std::shared_ptr<httpserver::http_response> render_connect(const httpserver::http_request &req);
     std::shared_ptr<httpserver::http_response> render_delete(const httpserver::http_request &req);
};

service_resource::service_resource() { }

service_resource::~service_resource() { }

std::shared_ptr<httpserver::http_response> service_resource::render_get(const httpserver::http_request &req) {
    std::cout << "service_resource::render_get()" << std::endl;

    if (verbose) std::cout << req;
    auto res = std::make_shared<httpserver::http_response>(httpserver::http_response::string("GET response"));

    if (verbose) std::cout << *res;

    return res;
}


std::shared_ptr<httpserver::http_response> service_resource::render_put(const httpserver::http_request &req) {
    std::cout << "service_resource::render_put()" << std::endl;

    if (verbose) std::cout << req;

    auto res = std::make_shared<httpserver::http_response>(httpserver::http_response::string("PUT response"));

    if (verbose) std::cout << *res;

    return res;
}

std::shared_ptr<httpserver::http_response> service_resource::render_post(const httpserver::http_request &req) {
    std::cout << "service_resource::render_post()" << std::endl;

    if (verbose) std::cout << req;

    auto res = std::make_shared<httpserver::http_response>(httpserver::http_response::string("POST response"));

    if (verbose) std::cout << *res;

    return res;
}

std::shared_ptr<httpserver::http_response> service_resource::render(const httpserver::http_request &req) {
    std::cout << "service_resource::render()" << std::endl;

    if (verbose) std::cout << req;

    auto res = std::make_shared<httpserver::http_response>(httpserver::http_response::string("generic response"));

    if (verbose) std::cout << *res;

    return res;
}

std::shared_ptr<httpserver::http_response> service_resource::render_head(const httpserver::http_request &req) {
    std::cout << "service_resource::render_head()" << std::endl;

    if (verbose) std::cout << req;

    auto res = std::make_shared<httpserver::http_response>(httpserver::http_response::string("HEAD response"));

    if (verbose) std::cout << *res;

    return res;
}

std::shared_ptr<httpserver::http_response> service_resource::render_options(const httpserver::http_request &req) {
    std::cout << "service_resource::render_options()" << std::endl;

    if (verbose) std::cout << req;

    auto res = std::make_shared<httpserver::http_response>(httpserver::http_response::string("OPTIONS response"));

    if (verbose) std::cout << *res;

    return res;
}

std::shared_ptr<httpserver::http_response> service_resource::render_connect(const httpserver::http_request &req) {
    std::cout << "service_resource::render_connect()" << std::endl;

    if (verbose) std::cout << req;

    auto res = std::make_shared<httpserver::http_response>(httpserver::http_response::string("CONNECT response"));

    if (verbose) std::cout << *res;

    return res;
}

std::shared_ptr<httpserver::http_response> service_resource::render_delete(const httpserver::http_request &req) {
    std::cout << "service_resource::render_delete()" << std::endl;

    if (verbose) std::cout << req;

    auto res = std::make_shared<httpserver::http_response>(httpserver::http_response::string("DELETE response"));

    if (verbose) std::cout << *res;

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
    httpserver::webserver ws = cw;

    //
    // Create and register service resource available at /service
    //
    auto res = std::make_shared<service_resource>();
    ws.register_resource("/service", res, true);

    //
    // Start and block the webserver
    //
    ws.start(true);

    return 0;
}
