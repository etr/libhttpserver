/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <httpserver/detail/http2_connection.hpp>
#include "../conformance/corpus.hpp"
#include "./http2_fixture.hpp"
#include "./littletest.hpp"
namespace {
namespace hd = httpserver::detail;
struct verdict {
    std::string scope;
    unsigned code, offset;
    bool hold_output;
    std::vector<std::uint8_t> output;
};
verdict decode(const protocol_corpus::entry& c) {
    verdict v;
    std::istringstream fields(c.verdict);
    std::string held;
    if (!(fields >> v.scope >> v.code >> v.offset >> held)) throw std::runtime_error("bad HTTP/2 verdict");
    v.hold_output = held == "hold";
    std::istringstream bytes(c.payload);
    std::string byte;
    while (bytes >> byte) v.output.push_back(static_cast<std::uint8_t>(std::stoul(byte, nullptr, 16)));
    return v;
}
struct observation {
    std::vector<std::uint8_t> output;
    std::optional<hd::http2_error> error;
    std::size_t consumed = 0;
    hd::http2_settings peer;
};
void drain(hd::http2_connection& conn, observation& seen) {
    while (!conn.output().empty()) {
        auto bytes = conn.output();
        seen.output.insert(seen.output.end(), bytes.begin(), bytes.end());
        if (!conn.advance_output(bytes.size())) throw std::runtime_error("output advancement refused");
    }
}
observation replay(const protocol_corpus::entry& c, const verdict& expected, std::size_t split, bool bytewise) {
    hd::http2_limits limits;
    limits.control_frames = 2;
    hd::http2_connection conn(h2test::budget(), limits);
    observation seen;
    drain(conn, seen);
    while (seen.consumed < c.wire.size() && !conn.failure()) {
        auto end = bytewise ? seen.consumed + 1 : c.wire.size();
        if (seen.consumed < split) end = std::min(end, split);
        const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(c.wire.data()), end).subspan(seen.consumed);
        auto result = conn.feed(bytes);
        if (result.progress == hd::http2_progress::yield) {
            conn.begin_turn();
            continue;
        }
        seen.consumed += result.consumed;
        if (result.error) seen.error = result.error;
        if (result.progress == hd::http2_progress::frame_ready || (result.error && result.error->scope == hd::http2_error_scope::stream)) conn.release_frame();
        if (!expected.hold_output) drain(conn, seen);
        if (result.consumed == 0) throw std::runtime_error("unexpected parser park");
    }
    if (!conn.failure()) {
        auto final = conn.eof();
        if (final.error) seen.error = final.error;
    }
    drain(conn, seen);
    seen.peer = conn.peer_settings();
    if (conn.failure()) {
        auto after = conn.feed(h2test::preface());
        protocol_corpus::require(after.consumed == 0, c, "terminal suffix consumed");
        protocol_corpus::require(conn.output().empty(), c, "duplicate terminal output");
    }
    return seen;
}
void verify(const protocol_corpus::entry& c, const verdict& v, const observation& seen) {
    protocol_corpus::require(seen.consumed == v.offset, c, "consumed offset");
    protocol_corpus::require(seen.output == v.output, c, "exact output bytes");
    if (v.scope == "ok") {
        protocol_corpus::require(!seen.error, c, "unexpected error");
    } else {
        protocol_corpus::require(seen.error.has_value(), c, "missing error");
        const auto scope = v.scope == "stream" ? hd::http2_error_scope::stream : hd::http2_error_scope::connection;
        protocol_corpus::require(seen.error->scope == scope && static_cast<unsigned>(seen.error->wire_code) == v.code, c, "typed error");
    }
    if (c.name == "settings-order") protocol_corpus::require(seen.peer.header_table_size == 8192 && seen.peer.initial_window_size == 0, c, "settings effects");
}
}  // namespace
LT_BEGIN_SUITE(http2_conformance_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_conformance_suite)
LT_BEGIN_AUTO_TEST(http2_conformance_suite, rfc_transcripts_coalesced_bytewise_and_every_split)
    for (const auto& c : protocol_corpus::load(std::string(HTTP2_CORPUS_DIR))) {
        const auto expected = decode(c);
        const auto coalesced = replay(c, expected, 0, false);
        verify(c, expected, coalesced);
        verify(c, expected, replay(c, expected, 0, true));
        for (std::size_t split = 0; split <= c.wire.size(); ++split) verify(c, expected, replay(c, expected, split, false));
        LT_CHECK(coalesced.output == expected.output);
    }
LT_END_AUTO_TEST(rfc_transcripts_coalesced_bytewise_and_every_split)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
