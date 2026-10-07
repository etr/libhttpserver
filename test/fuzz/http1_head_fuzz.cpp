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

#include <httpserver/detail/http1_body_mode.hpp>
#include <httpserver/detail/http1_host.hpp>
#include <algorithm>
#include <string>
#include "./protocol_fuzz.hpp"
namespace d = httpserver::detail;
namespace {
std::string run(std::string_view wire, std::size_t chunk, std::size_t cap) {
    d::http1_head_parser parser({cap, 16});
    for (std::size_t offset = 0; offset < wire.size(); offset += chunk) {
        parser.feed(wire.substr(offset, chunk));
        if (parser.state() == d::http1_head_state::failed) break;
    }
    if (parser.state() == d::http1_head_state::failed) {
        auto failure = parser.failure().code();
        parser.feed("suffix");
        fuzz_check(parser.failure().code() == failure);
        return "error:" + std::to_string(static_cast<int>(failure));
    }
    std::string result;
    for (unsigned heads = 0; heads < 64 && parser.state() == d::http1_head_state::complete; ++heads) {
        auto residue = parser.residue_size();
        fuzz_check(residue <= wire.size());
        auto head = parser.take();
        auto mode = d::http1_body_mode::compute(head);
        result += head.raw_target + head.route_path + std::to_string(static_cast<int>(mode.kind))
            + std::to_string(d::valid_http1_host(head)) + std::to_string(residue);
    }
    return result + ":" + std::to_string(static_cast<int>(parser.state()));
}
}  // namespace
void fuzz_http1_head(std::span<const std::byte> input) {
    input = input.first(std::min(input.size(), protocol_fuzz_max_input));
    auto wire = std::string_view(reinterpret_cast<const char*>(input.data()), input.size());
    // Wide admission excludes prospective whole-feed budget differences;
    // a separate tight run covers that intentional parser contract.
    auto whole = run(wire, std::max<std::size_t>(1, input.size()), input.size() + 32);
    fuzz_check(whole == run(wire, 1, input.size() + 32));
    run(wire, 7, 128);
}
#ifdef PROTOCOL_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    fuzz_http1_head({reinterpret_cast<const std::byte*>(data), size}); return 0;
}
#endif
