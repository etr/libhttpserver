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

#include <httpserver/detail/http1_body_decoder.hpp>
#include <httpserver/detail/http1_host.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include "../conformance/corpus.hpp"
#ifndef HTTP1_CORPUS_DIR
#define HTTP1_CORPUS_DIR "../conformance/http1"
#endif
namespace d = httpserver::detail;
using protocol_corpus::require;
void replay(const protocol_corpus::entry& c, std::size_t split, bool bytewise) {
    d::http1_head_parser head({});
    const auto boundary = c.wire.find("\r\n\r\n");
    const auto end = boundary == std::string::npos ? c.wire.size() : boundary + 4;
    auto prefix = std::string_view(c.wire).substr(0, end);
    if (bytewise) {
        for (const char& ch : prefix) head.feed(std::string_view(&ch, 1));
    } else {
        head.feed(prefix.substr(0, std::min(split, end)));
        head.feed(prefix.substr(std::min(split, end)));
    }
    if (c.stage == "head") {
        require(head.state() == d::http1_head_state::failed, c, "head must reject");
        const auto failure = head.failure();
        head.feed("GET /sentinel HTTP/1.1\r\nHost: h\r\n\r\n");
        require(head.failure().code() == failure.code() && head.close_policy() != d::http1_close_policy::none,
                c, "sticky rejection and close");
        return;
    }
    require(head.state() == d::http1_head_state::complete, c, "complete head");
    auto parsed = head.take();
    if (c.stage == "host") {
        require(!d::valid_http1_host(parsed), c, "invalid Host semantics");
        return;
    }
    auto mode = d::http1_body_mode::compute(parsed);
    if (c.stage == "framing") {
        require(mode.kind == d::http1_body_kind::rejected && !mode.failure.ok()
                && mode.close_policy != d::http1_close_policy::none, c, "ambiguous framing must reject");
        return;
    }
    require(d::valid_http1_host(parsed), c, "valid Host semantics");
    d::http1_body_decoder body(mode, {4, 2, 32});
    const auto wire = std::string_view(c.wire).substr(end);
    std::size_t consumed = 0;
    std::string payload;
    std::array<std::byte, 3> sink;
    for (std::size_t turns = 0; turns <= wire.size() * 2 + 2; ++turns) {
        const auto count = bytewise ? std::min<std::size_t>(1, wire.size() - consumed)
            : std::min(wire.size() - consumed, consumed < split ? split - consumed : wire.size());
        const auto result = body.decode(wire.substr(consumed, count));
        require(result.consumed <= count, c, "exact consumption");
        consumed += result.consumed;
        require(body.staged_bytes() <= 4, c, "staging bound");
        while (true) {
            auto pulled = body.pull(sink);
            if (pulled.kind != d::body_pull::data) break;
            payload.append(reinterpret_cast<const char*>(sink.data()), pulled.copied);
        }
        if (!body.failure().ok() || body.message_complete() || consumed == wire.size()) break;
        require(count != 0 || result.consumed != 0, c, "bounded progress");
    }
    if (c.stage == "body") {
        const auto expected = c.name.starts_with("trailer-") ? httpserver::http::outcome_code::limit_exceeded
            : httpserver::http::outcome_code::protocol_error;
        require(body.failure().code() == expected && body.close_policy() != d::http1_close_policy::none,
                c, "typed malformed body rejection and close");
        require(body.decode("sentinel").consumed == 0, c, "sticky body failure");
    } else if (c.stage == "truncated") {
        require(!body.message_complete() && body.failure().ok(), c, "truncation awaits EOF in engine");
    } else {
        require(body.failure().ok() && body.message_complete(), c, "body completion");
        require(payload == c.payload, c, "concrete decoded payload");
        require(consumed == wire.size(), c, "body boundary");
        if (c.name == "chunk-trailers") require(body.trailers().first("X-T") == "v", c, "trailer value");
    }
}
int main(int argc, char** argv) {
    try {
        const auto cases = protocol_corpus::load(argc > 1 ? argv[1] : HTTP1_CORPUS_DIR);
        for (const auto& c : cases) {
            replay(c, c.wire.size(), false);
            replay(c, 0, true);
            for (std::size_t split = 0; split <= c.wire.size(); ++split) replay(c, split, false);
            std::cout << "PASS " << c.name << " RFC " << c.rfc << '\n';
        }
        d::http1_head_parser exact({27, 1}), over({26, 1});
        const std::string request = "GET / HTTP/1.1\r\nHost: h\r\n\r\n";
        exact.feed(request); over.feed(request);
        if (exact.state() != d::http1_head_state::complete || over.failure().ok())
            throw std::runtime_error("exact/over head byte budget");
        d::http1_head_parser pipeline({});
        pipeline.feed(request + request);
        if (pipeline.residue_size() != request.size()) throw std::runtime_error("pipeline residue");
        pipeline.take();
        if (pipeline.state() != d::http1_head_state::complete) throw std::runtime_error("pipeline suffix");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
