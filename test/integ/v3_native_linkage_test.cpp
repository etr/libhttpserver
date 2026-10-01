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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-108 step 10 (PRD-V3N-REQ-001/002): the link-surface audit.
// This program links libhttpserver_v3core.la -- the v3 native engine
// objects and NOTHING else: no microhttpd, no gnutls, no curl. It
// constructs a native_server, registers a lambda route, and asserts
// validate-shaped behavior; it NEVER listens. LINKING IS THE AUDIT:
// if any part of the native TLS-off surface grew a third-party
// dependency, this translation unit stops compiling or linking. The
// companion scripts/audit-v3-native-linkage.sh inspects the built
// binary's dynamic dependencies for the banned libraries and scans
// the sources for banned includes.

#include <chrono>
#include <cstdint>
#include <utility>

#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/server.hpp>

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;

using httpserver::exchange;
using httpserver::task;

int audit_link_surface() {
    srv::server_options options;
    srv::listener_options listener;
    listener.address = "127.0.0.1";
    listener.port = 0;
    options.add_listener(listener);
    options.timeouts().header = std::chrono::milliseconds(30000);

    srv::native_server server(std::move(options));
    const http::outcome routed = server.route(
        http::method::known(http::method_id::get), "/health",
        [](exchange&) -> task<void> { co_return; });
    if (!routed.ok()) return 1;

    // The REQ-016 gate: a configuration with a listener validates; the
    // same server is never asked to listen (this audit owns no ports).
    // A second listener index stays unbound: get_bound_port reports 0.
    const std::uint16_t unbound = server.get_bound_port(1);
    const bool quiet = !server.is_running();
    server.request_stop();
    server.stop();

    return quiet && unbound == 0 ? 0 : 1;
}

}  // namespace

int main() { return audit_link_surface(); }
