/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <httpserver.hpp>
#include <httpserver/server/tls_credentials.hpp>

namespace srv = httpserver::server;
namespace http = httpserver::http;

std::string read_pem(const char* path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    auto credentials = std::make_shared<srv::tls_credentials>();
    srv::tls_host_credentials host{"a.example", read_pem(argv[1]), read_pem(argv[2]), {"h2", "http/1.1"}};
    if (!credentials->replace({{host}, 0}).ok()) return 1;
    srv::server_options options;
    options.protocols().enable(http::protocol::http_2);
    options.tls().provider = srv::tls_provider::system_default;
    options.tls().profile = srv::tls_profile::certificates;
    options.add_listener({"127.0.0.1", 0, true, credentials});
    srv::native_server server(options);
    if (!server.route(http::method::known(http::method_id::get), "/hello", [](httpserver::exchange& x) -> httpserver::task<void> {
        x.start_response(http::status::from_code(200), {});
        const std::string text = "hello h2\n";
        co_await x.writer().write(std::as_bytes(std::span(text)));
        co_await x.writer().finish();
    }).ok() || !server.listen().ok()) return 1;
    std::cout << server.get_bound_port(0) << std::endl;
    std::string command;
    std::getline(std::cin, command);
    server.stop();
}
