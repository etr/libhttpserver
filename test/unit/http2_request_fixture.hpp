/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_HTTP2_REQUEST_FIXTURE_HPP_
#define TEST_UNIT_HTTP2_REQUEST_FIXTURE_HPP_
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/http2_request_engine.hpp>
#include <httpserver/detail/hpack_encoder.hpp>
#include "./http2_fixture.hpp"
namespace h2test {
namespace http = httpserver::http;
inline std::vector<hd::hpack_field> get(std::string path = "/items/../hello?x=1") {
    return {{":method", "GET"}, {":scheme", "https"}, {":path", path}, {":authority", "example.test"}};
}
inline std::vector<std::uint8_t> encode(hd::hpack_encoder& encoder, const std::vector<hd::hpack_field>& fields) {
    auto result = encoder.encode_section(fields, {262144, 262144, 256});
    if (!result.status.ok()) throw std::runtime_error("fixture HPACK encode failed");
    return {result.value.begin(), result.value.end()};
}
inline bool feed(hd::http2_request_engine& engine, std::span<const std::uint8_t> bytes) {
    engine.begin_turn();
    while (!bytes.empty()) {
        auto result = engine.feed(bytes);
        if (result.error && result.error->scope == hd::http2_error_scope::connection) return false;
        if (result.progress == hd::http2_progress::yield) {
            engine.begin_turn();
            continue;
        }
        if (!result.consumed) return false;
        bytes = bytes.subspan(result.consumed);
    }
    return true;
}
inline std::vector<std::uint8_t> output(hd::http2_request_engine& engine, std::size_t step = 65536) {
    std::vector<std::uint8_t> out;
    for (unsigned i = 0; i < 100000; ++i) {
        auto bytes = engine.output();
        if (bytes.empty()) break;
        auto n = std::min(step, bytes.size());
        out.insert(out.end(), bytes.begin(), bytes.begin() + n);
        if (!engine.advance_output(n)) throw std::runtime_error("fixture output advance failed");
    }
    return out;
}
struct response {
    std::uint32_t stream;
    std::vector<hd::hpack_field> fields;
};
inline std::vector<response> responses(const std::vector<std::uint8_t>& wire) {
    hd::hpack_decoder decoder(budget());
    std::vector<response> out;
    std::vector<std::uint8_t> block;
    std::uint32_t stream = 0;
    for (std::size_t at = 0; at + 9 <= wire.size();) {
        auto n = (wire[at] << 16) | (wire[at + 1] << 8) | wire[at + 2];
        if (at + 9 + n > wire.size()) throw std::runtime_error("truncated output frame");
        if (wire[at + 3] == 1 || wire[at + 3] == 9) {
            auto id = (wire[at + 5] << 24) | (wire[at + 6] << 16) | (wire[at + 7] << 8) | wire[at + 8];
            if (wire[at + 3] == 1) {
                stream = id;
                if (!(wire[at + 4] & 1)) throw std::runtime_error("missing END_STREAM");
            } else if (stream != static_cast<std::uint32_t>(id)) {
                throw std::runtime_error("interleaved output block");
            }
            block.insert(block.end(), wire.begin() + at + 9, wire.begin() + at + 9 + n);
            if (wire[at + 4] & 4) {
                auto decoded = decoder.decode(block, {262144, 262144, 256});
                if (!decoded.status.ok()) throw std::runtime_error("bad response HPACK");
                out.push_back({stream, std::move(decoded.fields)});
                block.clear(); stream = 0;
            }
        } else if (stream) {
            throw std::runtime_error("control inside output block");
        }
        at += 9 + n;
    }
    return out;
}
inline unsigned count_type(const std::vector<std::uint8_t>& wire, unsigned type) {
    unsigned count = 0;
    for (std::size_t at = 0; at + 9 <= wire.size();) {
        auto n = (wire[at] << 16) | (wire[at + 1] << 8) | wire[at + 2];
        if (wire[at + 3] == type) ++count;
        at += 9 + n;
    }
    return count;
}
}  // namespace h2test
#endif  // TEST_UNIT_HTTP2_REQUEST_FIXTURE_HPP_
