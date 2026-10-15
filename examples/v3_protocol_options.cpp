/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <httpserver/features.hpp>
#include <httpserver/server/server.hpp>
#include <httpserver/server/tls_credentials.hpp>

namespace {
const char* outcome_name(const httpserver::http::outcome& result) {
    using httpserver::http::outcome_code;
    switch (result.code()) {
        case outcome_code::ok: return "ok";
        case outcome_code::not_supported: return "not_supported";
        default: return "unexpected_failure";
    }
}

bool demonstrate(const char* label, httpserver::http::protocol protocol, bool tls,
                 const std::string& chain, const std::string& key) {
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
    std::shared_ptr<srv::tls_credentials> credentials;
    const bool available = httpserver::query_features().tls_provider;
    if (tls && available) {
        credentials = std::make_shared<srv::tls_credentials>();
        const std::string alpn = protocol == http::protocol::http_3 ? "h3" : protocol == http::protocol::http_2 ? "h2" : "http/1.1";
        if (!credentials->replace({{{"a.example", chain, key, {alpn}}}, 0}).ok()) return false;
    }
    options.add_listener({"127.0.0.1", 0, tls, credentials,
        protocol == http::protocol::http_3 ? srv::listener_transport::udp : srv::listener_transport::tcp});
    options.budgets().set(srv::resource::header_fields, 4096);
    options.budgets().set(srv::resource::quic_reassembly_bytes, 16 * 1024 * 1024);
    if (protocol == http::protocol::http_2) {
        options.http2().emplace().max_concurrent_requests = 192;
        options.http2()->decoder_table_bytes = 8192;
        options.http2()->encoder_table_bytes = 16384;
    }
    if (protocol == http::protocol::http_3) {
        options.http3().emplace().max_concurrent_requests = 24;
        options.http3()->max_blocked_headers = 32;
        options.quic().emplace().max_stream_records = 96;
    }
    srv::effective_protocol_limits limits;
    const auto resolved = options.resolve_limits(limits);
    const auto validated = options.validate();
    srv::native_server server(options);
    const auto inspected = server.effective_limits(limits);
    if (inspected.code() != resolved.code()) return false;
    const auto listened = server.listen();
    const auto port = server.get_bound_port(0);
    std::cout << label << " validate=" << outcome_name(validated)
              << " listen=" << outcome_name(listened) << " port=" << port << '\n';
    server.stop();
    if (server.effective_limits(limits).code() != inspected.code()) return false;
    if (!tls || available) return validated.ok() && listened.ok() && port != 0;
    return listened.code() == http::outcome_code::not_supported && port == 0;
}
}  // namespace

int main(int argc, char** argv) {
    namespace http = httpserver::http;
    if (httpserver::query_features().tls_provider && argc != 3) {
        std::cerr << "usage: v3_protocol_options CHAIN_PEM KEY_PEM\n"; return 2;
    }
    const auto read = [](const char* path) { std::ifstream file(path); return std::string(std::istreambuf_iterator<char>(file), {}); };
    const std::string chain = argc == 3 ? read(argv[1]) : "", key = argc == 3 ? read(argv[2]) : "";
    const bool h1 = demonstrate("HTTP/1.1", http::protocol::http_1_1, false, chain, key);
    const bool tls = demonstrate("TLS", http::protocol::http_1_1, true, chain, key);
    const bool h2 = demonstrate("HTTP/2", http::protocol::http_2, true, chain, key);
    const bool h3 = demonstrate("HTTP/3", http::protocol::http_3, true, chain, key);
    return h1 && tls && h2 && h3 ? 0 : 1;
}
