/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <iostream>
#include <httpserver/server/server.hpp>

namespace {
const char* outcome_name(const httpserver::http::outcome& result) {
    using httpserver::http::outcome_code;
    switch (result.code()) {
        case outcome_code::ok: return "ok";
        case outcome_code::not_supported: return "not_supported";
        default: return "unexpected_failure";
    }
}

bool demonstrate(const char* label, httpserver::http::protocol protocol, bool tls) {
    namespace srv = httpserver::server;
    namespace http = httpserver::http;
    srv::server_options options;
    options.concurrency().workers = 1;
    options.protocols().disable(http::protocol::http_1_0);
    options.protocols().disable(http::protocol::http_1_1);
    options.protocols().enable(protocol);
    if (tls) {
        options.tls().provider = srv::tls_provider::system_default;
        options.tls().profile = srv::tls_profile::certificates;
    }
    options.add_listener({"127.0.0.1", 0, tls});
    const auto validated = options.validate();
    srv::native_server server(options);
    const auto listened = server.listen();
    const auto port = server.get_bound_port(0);
    std::cout << label << " validate=" << outcome_name(validated)
              << " listen=" << outcome_name(listened) << " port=" << port << '\n';
    server.stop();
    if (!tls) return validated.ok() && listened.ok() && port != 0;
    // A provider permits semantic validation; public encrypted listeners
    // still return not_supported before any bind, even in TLS-on builds.
    return validated.ok() && listened.code() == http::outcome_code::not_supported && port == 0;
}
}  // namespace

int main() {
    namespace http = httpserver::http;
    const bool h1 = demonstrate("HTTP/1.1", http::protocol::http_1_1, false);
    const bool tls = demonstrate("TLS", http::protocol::http_1_1, true);
    const bool h2 = demonstrate("HTTP/2", http::protocol::http_2, true);
    const bool h3 = demonstrate("HTTP/3", http::protocol::http_3, true);
    return h1 && tls && h2 && h3 ? 0 : 1;
}
