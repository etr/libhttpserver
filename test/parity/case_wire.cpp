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

// TASK-117 step 5: the promoted corpus case-wire rebuild (see
// case_wire.hpp). The head lines carry their CRLF; the empty line
// ends the head; every byte after it is the raw body.

#include <cstddef>
#include <string>
#include <vector>

#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <parity/case_wire.hpp>

namespace parity {

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t nl = text.find("\r\n", at);
        const std::size_t end = nl == std::string::npos ? text.size() : nl;
        lines.push_back(text.substr(at, end - at));
        at = nl == std::string::npos ? text.size() : nl + 2;
    }
    return lines;
}

corpus_request parse_case(const tcase& c) {
    std::string wire;
    for (const send_segment& segment : c.sends) {
        wire += segment.bytes;
    }
    const std::size_t split = wire.find("\r\n\r\n");
    corpus_request out;
    out.body = split == std::string::npos
        ? std::string()
        : wire.substr(split + 4);
    const std::vector<std::string> lines =
        split_lines(wire.substr(0, split == std::string::npos
                                       ? std::string::npos
                                       : split));
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (i == 0) {
            const std::size_t method_end = line.find(' ');
            const std::size_t target_end =
                line.find(' ', method_end + 1);
            out.head.raw_target =
                line.substr(method_end + 1,
                            target_end - method_end - 1);
            out.head.route_path = out.head.raw_target;
            out.head.request_method = httpserver::http::method::parse(
                line.substr(0, method_end)).value_or(
                    httpserver::http::method::known(
                        httpserver::http::method_id::post));
            out.head.request_protocol =
                httpserver::http::parse(
                    line.substr(target_end + 1))
                    .value_or(httpserver::http::protocol::http_1_1);
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(0, 1);
        }
        out.head.head_fields.append(line.substr(0, colon), value);
    }
    return out;
}

}  // namespace parity
