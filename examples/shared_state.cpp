/*
     This file is part of libhttpserver
     Copyright (C) 2011-2025 Sebastiano Merlino

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

// shared_state.cpp - the canonical example of when the class form is the
// right shape.
//
// The lambda form (see hello_world.cpp) is the recommended idiom for
// stateless endpoints. When two HTTP methods on the same path share
// mutable state, however, two independent lambdas cannot safely capture
// the same data: each handler is invoked concurrently from libmicrohttpd
// worker threads, and there is no natural object lifetime that both
// lambdas can attach to.
//
// An http_resource subclass solves this cleanly. The resource owns the
// shared data, every render_* override is a member function with access
// to it, and a single std::mutex guards every mutation and read.

#include <memory>
#include <mutex>
#include <string>

#include <httpserver.hpp>

class counter : public httpserver::http_resource {
 public:
    httpserver::http_response render_get(const httpserver::http_request&) override {
        std::lock_guard<std::mutex> lk(m_);
        return httpserver::http_response::string(std::to_string(n_));
    }

    httpserver::http_response render_post(const httpserver::http_request&) override {
        std::lock_guard<std::mutex> lk(m_);
        ++n_;
        return httpserver::http_response::string(std::to_string(n_)).with_status(201);
    }

 private:
    std::mutex m_;
    int n_ = 0;
};

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};
    ws.register_path("/counter", std::make_unique<counter>());
    ws.start(true);
}
