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
#include "../unit/websocket_engine_helpers.hpp"
#include "../conformance/corpus.hpp"
#include "../parity/response_frame.hpp"
#ifndef HTTP1_CORPUS_DIR
#define HTTP1_CORPUS_DIR "../conformance/http1"
#endif
using namespace ws_engine_test;  // NOLINT(build/namespaces)
using protocol_corpus::require;
int main(int argc, char** argv) {
    try {
        auto cases = protocol_corpus::load(argc > 1 ? argv[1] : HTTP1_CORPUS_DIR);
        for (const auto& c : cases) {
            std::atomic<int> first{0}, sentinel{0};
            std::atomic<bool> failed{false};
            rig r;
            r.config.body = {4, 2, 32};
            r.routes.route(h::http::method::known(h::http::method_id::post), "/",
                [&](h::exchange& x) -> h::task<void> {
                    ++first;
                    if (!x.admit_body({}).ok()) co_return;
                    auto read = co_await x.body().collect(128);
                    if (!read.status.ok()) {
                        failed = true; co_return;
                    }
                    h::http::fields fields; fields.append("Content-Length", "0");
                    x.start_response(h::http::status::from_code(200), fields);
                    co_await x.writer().finish();
                });
            r.routes.route(h::http::method::known(h::http::method_id::get), "/sentinel",
                [&](h::exchange& x) -> h::task<void> {
                    ++sentinel;
                    h::http::fields fields; fields.append("Content-Length", "0");
                    x.start_response(h::http::status::from_code(200), fields);
                    co_await x.writer().finish();
                });
            require(r.start(), c, "engine start");
            std::string wire = c.wire;
            if (c.stage != "truncated") wire += "GET /sentinel HTTP/1.1\r\nHost: h\r\nConnection: close\r\n\r\n";
            if (c.stage == "truncated") {
                const auto end = wire.find("\r\n\r\n") + 4;
                io_loopback::write_all(r.pair.peer(), wire.data(), end);
                require(until([&] { return first.load() == 1; }), c, "admission before EOF");
                io_loopback::write_all(r.pair.peer(), wire.data() + end, wire.size() - end);
                ::shutdown(r.pair.peer(), SHUT_WR);
            } else {
                io_loopback::write_all(r.pair.peer(), wire.data(), wire.size());
            }
            require(until([&] { return r.stopped.load(); }), c, "terminal close");
            std::string response;
            std::byte scratch[1024];
            for (int i = 0; i < 128; ++i) {
                auto result = sys::read_some(r.pair.peer(), scratch, sizeof scratch);
                if (!result.transferred) break;
                response.append(reinterpret_cast<char*>(scratch), result.transferred);
            }
            parity::response_frame_parser parser;
            auto seen = parser.feed(response);
            if (c.stage == "valid") {
                require(first == 1 && sentinel == 1 && seen.size() == 2, c, "valid pipeline dispatch and responses");
                require(seen[0].status == 200 && seen[1].status == 200, c, "valid response status");
            } else {
                require(sentinel == 0, c, "smuggled sentinel never dispatches");
                if (c.stage == "body" || c.stage == "truncated")
                    require(first == 1 && failed.load(), c, "malformed body after admission");
                else require(first == 0, c, "head/framing rejection before handler");
            }
            require(r.scope.active() == 0, c, "drained engine");
            std::cout << "PASS native " << c.name << '\n';
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
