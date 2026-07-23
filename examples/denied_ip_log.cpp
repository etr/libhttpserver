/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// Demonstrates the solution to issue #332: log every denied-IP rejection.
//
// Configure the daemon with an ACCEPT default policy, deny the IPs you
// want refused via ws.deny_ip(), and register a connection-level
// `accept_decision` hook that logs every rejection to stderr.
//
// The hook is observation-only: throwing from it does not flip the
// decision (the daemon still rejects the connection per the policy
// callback's return), so it is safe to do I/O here.
//
// Run, then attempt to connect from one of the denied IPs to see
// stderr show a "[DENIED] ..." line per attempt.

#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include <httpserver.hpp>

namespace hs = httpserver;

class hello_resource : public hs::http_resource {
 public:
    hs::http_response render_get(const hs::http_request&) override {
        return hs::http_response::string("Hello, World!");
    }
};

int main() {
    hs::webserver ws{hs::create_webserver(8080)
                         .default_policy(hs::http::http_utils::ACCEPT)};

    // Deny whatever client IP you want refused. The shipping example
    // uses an RFC-1918 placeholder; swap with your client's address.
    ws.deny_ip("10.0.0.1");

    auto h = ws.add_hook(hs::hook_phase::accept_decision,
        std::function<void(const hs::accept_ctx&)>(
            [](const hs::accept_ctx& ctx) {
                if (!ctx.accepted) {
                    std::cerr << "[DENIED] peer=" << ctx.peer.to_string()
                              << " reason="
                              << (ctx.reason.has_value()
                                      ? std::string(*ctx.reason)
                                      : std::string("unspecified"))
                              << std::endl;
                }
            }));

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/hello", resource);

    ws.start(true);   // blocking
    return 0;
}
