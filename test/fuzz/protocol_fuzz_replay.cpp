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

#include <iostream>
#include <string>
#include "./protocol_fuzz.hpp"
#include "../conformance/corpus.hpp"
#ifndef HTTP1_CORPUS_DIR
#define HTTP1_CORPUS_DIR "../conformance/http1"
#define WEBSOCKET_CORPUS_DIR "../conformance/websocket"
#endif
int main() {
    try {
        for (const auto& c : protocol_corpus::load(HTTP1_CORPUS_DIR)) {
            auto wire = std::span(reinterpret_cast<const std::byte*>(c.wire.data()), c.wire.size());
            std::cout << "REPLAY " << c.name << std::endl;
            fuzz_http1_head(wire);
            auto end = c.wire.find("\r\n\r\n");
            fuzz_http1_body(end == std::string::npos ? wire : wire.subspan(end + 4));
        }
        for (const auto& c : protocol_corpus::load(WEBSOCKET_CORPUS_DIR)) {
            std::cout << "REPLAY " << c.name << std::endl;
            fuzz_websocket_codec({reinterpret_cast<const std::byte*>(c.wire.data()), c.wire.size()});
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
