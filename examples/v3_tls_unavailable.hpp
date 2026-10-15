/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef EXAMPLES_V3_TLS_UNAVAILABLE_HPP_
#define EXAMPLES_V3_TLS_UNAVAILABLE_HPP_
#include <iostream>
#include <memory>

#include <httpserver/features.hpp>
#include <httpserver/server/server.hpp>
#include <httpserver/server/tls_credentials.hpp>
namespace v3_example {
inline int tls_unavailable(httpserver::http::protocol protocol = httpserver::http::protocol::http_1_1) {
    namespace srv = httpserver::server;
    namespace http = httpserver::http;
    srv::tls_credentials store;
    std::shared_ptr<srv::tls_psk_runtime> runtime;
    const auto replaced = store.replace({});
    const auto created = srv::tls_psk_runtime::create({1, 1}, runtime);
    const auto published = store.publish_acme({});
    const auto removed = store.remove_acme("a.example");
    srv::server_options options;
    options.tls().provider = srv::tls_provider::system_default;
    options.tls().profile = srv::tls_profile::certificates;
    options.protocols().enable(protocol);
    options.add_listener(
        {"127.0.0.1",
         0,
         true,
         {},
         protocol == http::protocol::http_3 ? srv::listener_transport::udp : srv::listener_transport::tcp});
    srv::native_server server(options);
    const auto listened = server.listen();
    const auto unsupported = [](const auto& result) { return result.code() == http::outcome_code::not_supported; };
    if (!unsupported(replaced) || !unsupported(created) || !unsupported(published) || !unsupported(removed) ||
        !unsupported(listened) || server.get_bound_port(0) != 0) {
        return 1;
    }
    std::cout << "replace=not_supported runtime=not_supported publish=not_supported remove=not_supported "
                 "listen=not_supported port=0\n";
    return 0;
}
}  // namespace v3_example
#endif  // EXAMPLES_V3_TLS_UNAVAILABLE_HPP_
