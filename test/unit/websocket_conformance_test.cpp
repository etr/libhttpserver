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

#include <httpserver/detail/websocket_codec.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include "./websocket_test_helpers.hpp"
#include "../conformance/corpus.hpp"
#ifndef WEBSOCKET_CORPUS_DIR
#define WEBSOCKET_CORPUS_DIR "../conformance/websocket"
#endif
using protocol_corpus::require;
void replay(const protocol_corpus::entry& c, std::size_t split, bool bytewise) {
    auto limits = ws_test::small();
    httpserver::detail::websocket_codec codec(limits);
    auto wire = ws_test::bytes(c.wire);
    std::size_t offset = 0;
    httpserver::websocket::feed_result result;
    std::string payload, controls;
    for (std::size_t turns = 0; turns <= wire.size() * 2 + 2; ++turns) {
        auto count = bytewise ? std::min<std::size_t>(1, wire.size() - offset)
            : std::min(wire.size() - offset, offset < split ? split - offset : wire.size());
        result = codec.feed(std::span(wire).subspan(offset, count));
        require(result.consumed <= count, c, "consumed bound");
        offset += result.consumed;
        require(codec.incoming_bytes() <= limits.incoming_bytes
                && codec.incoming_messages() <= limits.incoming_messages, c, "queue bounds");
        if (auto control = codec.take_control()) {
            controls.append(reinterpret_cast<const char*>(control->payload.data()), control->payload.size());
        }
        while (auto message = codec.pop()) {
            payload.append(reinterpret_cast<const char*>(message->data.data()), message->data.size());
        }
        if (!result.status.ok() || offset == wire.size() || result.blocked) break;
        require(result.consumed != 0, c, "feed progress");
    }
    if (c.verdict == "accept") {
        require(result.status.ok() && offset == wire.size(), c, "accepted wire");
        require((c.stage == "control" ? controls : payload) == c.payload, c, "concrete payload");
    } else {
        const auto expected = c.verdict == "limit" ? httpserver::http::outcome_code::limit_exceeded
            : httpserver::http::outcome_code::protocol_error;
        require(result.status.code() == expected, c, "typed rejection");
        require(codec.feed({}).status.code() == expected, c, "sticky terminal failure");
        require(codec.incoming_bytes() == 0 && codec.incoming_messages() == 0, c, "terminal release");
    }
    codec.clear();
    require(codec.incoming_bytes() == 0 && codec.incoming_messages() == 0, c, "reset releases queue");
}
int main(int argc, char** argv) {
    try {
        auto cases = protocol_corpus::load(argc > 1 ? argv[1] : WEBSOCKET_CORPUS_DIR);
        for (const auto& c : cases) {
            replay(c, c.wire.size(), false); replay(c, 0, true);
            for (std::size_t split = 0; split <= c.wire.size(); ++split) replay(c, split, false);
            std::cout << "PASS " << c.name << " RFC6455 " << c.rfc << '\n';
        }
        auto limits = ws_test::small();
        httpserver::detail::websocket_codec codec(limits);
        auto frame = ws_test::frame(2, ws_test::bytes("12345678"));
        for (int n = 0; n < 2; ++n) {
            if (!codec.feed(frame).status.ok()) throw std::runtime_error("queue fill");
        }
        auto blocked = codec.feed(frame);
        if (!blocked.blocked || blocked.consumed != 6 || codec.incoming_bytes() != 16)
            throw std::runtime_error("admission before payload");
        if (!codec.pop() || codec.feed(std::span(frame).subspan(blocked.consumed)).consumed != 8)
            throw std::runtime_error("resume after dequeue");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
