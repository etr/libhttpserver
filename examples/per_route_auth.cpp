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

// Demonstrates DR-012 / PRD-HOOK-REQ-006: a `before_handler` hook
// registered via `http_resource::add_hook(...)` is per-route. The hook
// fires only when *this* resource is dispatched -- a sibling route
// registered on the same webserver is not touched.
//
// The example wires two resources:
//
//   /public  -- always 200 OK, no auth.
//   /private -- a per-route before_handler hook checks the Authorization
//               header (HTTP Basic). Missing or invalid credentials
//               short-circuit with 401; valid credentials let the
//               handler run and return 200 OK.
//
// SECURITY NOTE (CWE-798): the expected credentials are loaded from the
// environment (AUTH_USER / AUTH_PASS); do not hardcode them. Set both
// before running:
//
//   export AUTH_USER=alice AUTH_PASS=hunter2
//   ./per_route_auth
//
// TRANSPORT NOTE: HTTP Basic auth sends credentials base64-encoded but
// NOT encrypted. In production, serve only over TLS (use
// create_webserver().use_ssl(...)).
//
// CONSTANT-TIME NOTE: The credential comparison below uses
// std::string_view::operator==, which short-circuits on the first
// differing byte and can leak information via timing side-channels
// (CWE-208). In production, replace with a constant-time comparison
// (e.g., CRYPTO_memcmp or an equivalent fixed-length loop).
//
// Then:
//
//   curl -v http://localhost:8080/public                       # 200 OK
//   curl -v http://localhost:8080/private                      # 401
//   curl -v -u alice:hunter2 http://localhost:8080/private     # 200 OK
//
// Contrast with `centralized_authentication.cpp`, which installs the
// server-wide `auth_handler` setter (the v1 alias for a webserver-wide
// before_handler hook). Per-route `add_hook` is the right shape when
// only a subset of routes need protection without dragging the rest of
// the surface through an auth_skip_paths list.

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <httpserver.hpp>

namespace hs = httpserver;

namespace {

class hello_resource : public hs::http_resource {
 public:
    hs::http_response render_get(const hs::http_request&) override {
        return hs::http_response::string("Hello, World!");
    }
};

class private_resource : public hs::http_resource {
 public:
    hs::http_response render_get(const hs::http_request&) override {
        return hs::http_response::string("Welcome to the private area.");
    }
};

}  // namespace

int main() {
    const char* expected_user = std::getenv("AUTH_USER");
    const char* expected_pass = std::getenv("AUTH_PASS");
    if (expected_user == nullptr || expected_pass == nullptr) {
        std::cerr << "per_route_auth: set AUTH_USER and AUTH_PASS before "
                     "running.\n";
        return 1;
    }

    hs::webserver ws{hs::create_webserver(8080)};

    auto pub = std::make_shared<hello_resource>();
    auto priv = std::make_shared<private_resource>();

    // The hook is registered on `priv` ONLY. Dispatches against `pub`
    // never enter this callback -- a sibling-route observation that is
    // the load-bearing property of per-route hooks (DR-012).
    std::string user{expected_user};
    std::string pass{expected_pass};
    auto h = priv->add_hook(hs::hook_phase::before_handler,
        std::function<hs::hook_action(hs::before_handler_ctx&)>(
            [user, pass](hs::before_handler_ctx& ctx) {
                // Null should never occur at before_handler on a matched
                // route, but fail closed (deny) rather than pass on any
                // unexpected null to avoid a fail-open security hole
                // (CWE-636).
                if (ctx.request == nullptr) {
                    return hs::hook_action::respond_with(
                        hs::http_response::unauthorized(
                            "Basic", "private-realm", "Unauthorized"));
                }
                std::string_view u = ctx.request->get_user();
                std::string_view p = ctx.request->get_pass();
                if (u == user && p == pass) {
                    return hs::hook_action::pass();
                }
                return hs::hook_action::respond_with(
                    hs::http_response::unauthorized(
                        "Basic", "private-realm", "Unauthorized"));
            }));

    // Keep the handle in scope for the full server lifetime; its
    // destructor at scope exit removes the registration. Since main()
    // never returns (start(true) blocks), this is the simple shape.

    ws.register_path("/public", pub);
    ws.register_path("/private", priv);

    ws.start(true);   // blocking
    return 0;
}
