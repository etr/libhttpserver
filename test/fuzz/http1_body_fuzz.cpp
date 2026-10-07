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
#include <algorithm>
#include <array>
#include <string>
#include "./protocol_fuzz.hpp"
namespace d = httpserver::detail;
namespace {
std::string run(std::string_view wire, std::size_t chunk, bool length) {
    d::http1_body_mode mode;
    mode.kind = length ? d::http1_body_kind::length : d::http1_body_kind::chunked;
    mode.content_length = wire.size() / 2;
    d::http1_body_decoder decoder(mode, {17, 3, 64});
    std::array<std::byte, 7> sink;
    std::size_t offset = 0;
    std::string payload;
    for (std::size_t turn = 0; turn <= wire.size() * 3 + 4; ++turn) {
        const auto part = wire.substr(offset, chunk);
        auto result = decoder.decode(part);
        fuzz_check(result.consumed <= part.size());
        offset += result.consumed;
        fuzz_check(decoder.staged_bytes() <= 17);
        while (true) {
            auto pulled = decoder.pull(sink);
            if (pulled.kind != d::body_pull::data) break;
            payload.append(reinterpret_cast<const char*>(sink.data()), pulled.copied);
        }
        if (!decoder.failure().ok() || decoder.message_complete() || offset == wire.size()) break;
        fuzz_check(result.consumed > 0);
    }
    if (!decoder.failure().ok()) {
        auto result = decoder.decode("suffix");
        fuzz_check(result.kind == d::http1_body_decode::failed && result.consumed == 0);
        // Failed pulls release queued data rather than exposing a partial message.
        payload.clear();
    }
    if (decoder.message_complete()) fuzz_check(decoder.decode("suffix").consumed == 0);
    return payload + ":" + std::to_string(offset) + ":" + std::to_string(static_cast<int>(decoder.failure().code()))
        + ":" + std::to_string(decoder.message_complete()) + ":" + std::to_string(decoder.trailers().size());
}
}  // namespace
void fuzz_http1_body(std::span<const std::byte> input) {
    input = input.first(std::min(input.size(), protocol_fuzz_max_input));
    auto wire = std::string_view(reinterpret_cast<const char*>(input.data()), input.size());
    for (bool length : {false, true}) {
        auto whole = run(wire, std::max<std::size_t>(1, input.size()), length);
        fuzz_check(whole == run(wire, 1, length));
        fuzz_check(whole == run(wire, 11, length));
    }
}
#ifdef PROTOCOL_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    fuzz_http1_body({reinterpret_cast<const std::byte*>(data), size}); return 0;
}
#endif
