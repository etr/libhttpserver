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
#include <string>
#include "./protocol_fuzz.hpp"
namespace {
std::string run(std::span<const std::byte> wire, std::size_t chunk) {
    httpserver::websocket::options limits;
    limits.max_message_bytes = 1024; limits.incoming_bytes = 2048; limits.incoming_messages = 3;
    httpserver::detail::websocket_codec codec(limits);
    std::size_t offset = 0;
    std::string events;
    httpserver::websocket::feed_result result;
    for (std::size_t turn = 0; turn <= wire.size() * 2 + 2; ++turn) {
        auto part = wire.subspan(offset, std::min(chunk, wire.size() - offset));
        result = codec.feed(part);
        fuzz_check(result.consumed <= part.size()); offset += result.consumed;
        fuzz_check(codec.incoming_bytes() <= limits.incoming_bytes && codec.incoming_messages() <= limits.incoming_messages);
        if (auto control = codec.take_control()) {
            events += "c" + std::to_string(control->opcode);
            events.append(reinterpret_cast<const char*>(control->payload.data()), control->payload.size());
        }
        while (auto message = codec.pop()) {
            events += "m" + std::to_string(static_cast<int>(message->kind));
            events.append(reinterpret_cast<const char*>(message->data.data()), message->data.size());
        }
        if (!result.status.ok() || offset == wire.size()) break;
        if (result.blocked && result.consumed == 0) break;
        fuzz_check(result.consumed > 0);
    }
    if (!result.status.ok()) {
        fuzz_check(codec.feed({}).status.code() == result.status.code());
        fuzz_check(codec.incoming_bytes() == 0 && codec.incoming_messages() == 0);
    }
    codec.clear();
    fuzz_check(codec.incoming_bytes() == 0 && codec.incoming_messages() == 0);
    return events + ":" + std::to_string(offset) + ":" + std::to_string(static_cast<int>(result.status.code()));
}
}  // namespace
void fuzz_websocket_codec(std::span<const std::byte> input) {
    input = input.first(std::min(input.size(), protocol_fuzz_max_input));
    auto whole = run(input, std::max<std::size_t>(1, input.size()));
    fuzz_check(whole == run(input, 1)); fuzz_check(whole == run(input, 13));
}
#ifdef PROTOCOL_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    fuzz_websocket_codec({reinterpret_cast<const std::byte*>(data), size}); return 0;
}
#endif
