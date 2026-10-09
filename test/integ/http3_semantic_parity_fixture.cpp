/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "../support/http3_semantic_routes.hpp"
#include "../support/http3_semantic_h1.hpp"
#include "../unit/http2_request_fixture.hpp"
#include "../unit/http3_request_fixture.hpp"
#include "../unit/http3_route_parity_fixture.hpp"
namespace {
void require(bool condition) {
    if (!condition) throw std::runtime_error("semantic parity failed");
}
std::string quote(std::string_view value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        if (c < 32 || c > 126) throw std::runtime_error("unexpected observation field byte");
        out += c;
    }
    return out + '"';
}
std::string hex(std::string_view bytes) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (unsigned char c : bytes) {
        result += digits[c >> 4];
        result += digits[c & 15];
    }
    return result;
}
using field = std::pair<std::string, std::string>;
void fields(const std::vector<field>& values) {
    std::cout << '[';
    bool first = true;
    for (const auto& [name, value] : values) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '[' << quote(name) << ',' << quote(value) << ']';
    }
    std::cout << ']';
}
void emit(unsigned version, const std::string& body, const std::vector<field>& headers, const std::vector<field>& trailers, unsigned missing, const std::string& raw_wire,
          const std::string& raw_status, const std::string& framing, const std::vector<field>& raw_fields) {
    const std::string upload("first\0second\xffthird\0", 19);
    require(body == upload && trailers == std::vector<field>{{"x-reply", "done"}} && missing == 404);
    std::cout << "{\"protocol\":\"h" << version << "\",\"status\":200,\"body_hex\":" << quote(hex(body)) << ",\"headers\":";
    fields(headers);
    std::cout << ",\"trailers\":";
    fields(trailers);
    std::cout << ",\"raw_fields\":";
    fields(raw_fields);
    std::cout << ",\"wire_hex\":" << quote(hex(raw_wire)) << ",\"raw_status\":" << quote(raw_status) << ",\"framing\":" << quote(framing) << ",\"missing_status\":" << missing << '}' << std::endl;
}
void run(unsigned version) {
    h3test::request_fixture f;
    h3semantic::install(f.routes, [](const char*, httpserver::exchange&, std::size_t) {});
    const std::string upload("first\0second\xffthird\0", 19);
    std::vector<field> headers, trailers;
    std::string body;
    unsigned missing = 0;
    std::string raw_wire, raw_status, framing;
    std::vector<field> raw_fields;
    if (version == 1) {
        h3parity::http1_fixture h1(f.root, f.routes);
        h1.send(
            "POST /items/../semantic?x=1 HTTP/1.1\r\nHost: localhost\r\nx-repeat: "
            "a\r\nx-repeat: b\r\nTransfer-Encoding: chunked\r\n\r\n");
        h1.send("6\r\n" + upload.substr(0, 6) + "\r\nd\r\n" + upload.substr(6) + "\r\n0\r\n\r\n");
        auto response = h1.response(&raw_wire);
        require(response.status == 200);
        body = response.body;
        raw_status = response.raw_status_line;
        framing = response.framing;
        for (const auto& field : response.headers) raw_fields.emplace_back(field.name, field.value);
        auto observed = h3semantic::observe_h1_fields(response, raw_wire);
        headers = std::move(observed.headers);
        trailers = std::move(observed.trailers);
        h1.send("GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n");
        missing = h1.response().status;
    } else {
        h3test::hd::http2_request_engine h2(f.root, f.routes, f.executor);
        h3test::hd::hpack_encoder encoder(f.root);
        auto wire = h2test::preface();
        h2test::append(
            wire,
            h2test::frame(
                1, 4, 1, h2test::encode(encoder, {{":method", "POST"}, {":scheme", "https"}, {":authority", "localhost"}, {":path", "/items/../semantic?x=1"}, {"x-repeat", "a"}, {"x-repeat", "b"}})));
        require(h2test::feed(h2, wire));
        f.executor.run_pending();
        require(h2test::feed(h2, h2test::frame(0, 0, 1, {upload.begin(), upload.begin() + 6})));
        f.executor.run_pending();
        require(h2test::feed(h2, h2test::frame(0, 1, 1, {upload.begin() + 6, upload.end()})));
        f.executor.run_pending();
        auto output = h2test::output(h2);
        raw_wire.assign(output.begin(), output.end());
        framing = "HTTP/2 frames";
        const auto responses = h2test::responses(output, true);
        for (const auto& response : responses)
            for (const auto& field : response.fields) raw_fields.emplace_back(field.name, field.value);
        require(responses.size() == 2 && responses.front().fields.front().value == "200");
        for (const auto& field : responses.front().fields)
            if (field.name.starts_with("x-")) headers.emplace_back(field.name, field.value);
        for (const auto& field : responses.back().fields) trailers.emplace_back(field.name, field.value);
        for (const auto& frame : h2test::frames(output))
            if (frame.type == 0) body.append(frame.payload.begin(), frame.payload.end());
        require(h2test::feed(h2, h2test::frame(1, 5, 3, h2test::encode(encoder, {{":method", "GET"}, {":scheme", "https"}, {":authority", "localhost"}, {":path", "/missing"}}))));
        f.executor.run_pending();
        auto missing_responses = h2test::responses(h2test::output(h2));
        require(missing_responses.size() == 1);
        missing = std::stoul(missing_responses.front().fields.front().value);
    }
    emit(version, body, headers, trailers, missing, raw_wire, raw_status, framing, raw_fields);
}
}  // namespace
int main() {
    try {
        run(1);
        run(2);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
