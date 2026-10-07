/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>
#include <httpserver/detail/http2_request_engine.hpp>
#include <httpserver/exchange.hpp>
#include "fuzz/http2_engine_fuzz.hpp"
namespace {
namespace h = httpserver;
namespace hd = h::detail;
void require(bool okay) { if (!okay) std::abort(); }
struct observation {
    std::size_t consumed = 0;
    std::optional<hd::http2_error> error;
    std::vector<std::uint8_t> output;
};
void accounting(const h::server::resource_budget& budget, bool empty) {
    for (std::size_t i = 0; i < h::server::resource_count; ++i) {
        auto kind = static_cast<h::server::resource>(i);
        require(budget.in_use(kind) <= budget.capacity(kind));
        if (empty) require(!budget.in_use(kind));
    }
}
observation replay(std::span<const std::uint8_t> input, std::size_t fragment) {
    observation seen;
    auto budget = h::server::resource_budget::root({});
    // One logical clock shared by both fragmentation runs. Owner methods also
    // service wall time; long deadlines prevent it affecting this bounded replay.
    static const auto now = hd::http2_connection::time_point() + std::chrono::hours(1000000);
    {
        h::server::route_registry routes;
        require(h::server::route_registry::create(budget, routes).ok());
        routes.route(h::http::method::known(h::http::method_id::get), "/", [](h::exchange& x) -> h::task<void> {
            x.respond(h::http::status::from_code(204), {}); co_return;
        });
        h::manual_executor executor;
        hd::http2_request_limits limits;
        limits.max_streams = 4; limits.body_buffer_bytes = 64; limits.response_buffer_bytes = 64;
        limits.headers = {2048, 2048, 16};
        limits.connection.control_frames = 8; limits.connection.control_bytes = 512;
        limits.connection.settings_timeout = std::chrono::hours(24);
        hd::http2_request_engine engine(budget, routes, executor, limits);
        auto pump = [&] {
            for (unsigned turn = 0; turn < 1024; ++turn) {
                executor.run_pending(); auto bytes = engine.output(now);
                if (bytes.empty()) return;
                const auto n = std::min<std::size_t>(bytes.size(), 7);
                require(seen.output.size() + n <= 131072);
                seen.output.insert(seen.output.end(), bytes.begin(), bytes.begin() + n);
                require(engine.advance_output(n)); accounting(budget, false);
            }
            require(false);
        };
        pump(); engine.begin_turn();
        for (std::size_t turns = 0; seen.consumed < input.size() && !engine.failure(); ++turns) {
            require(turns <= 131072);
            auto result = engine.feed(input.subspan(seen.consumed, std::min(fragment, input.size() - seen.consumed)), now);
            require(result.consumed <= input.size() - seen.consumed);
            seen.consumed += result.consumed;
            if (result.progress == hd::http2_progress::yield) engine.begin_turn();
            else require(result.consumed > 0);
            if (result.progress == hd::http2_progress::frame_ready || result.error) pump();
            accounting(budget, false);
        }
        engine.eof(); pump(); seen.error = engine.failure();
        if (seen.error) {
            auto next = engine.feed(input, now);
            require(!next.consumed && next.error && next.error->wire_code == seen.error->wire_code);
            require(engine.output(now).empty());
        }
    }
    accounting(budget, true); return seen;
}
}  // namespace
void http2_engine_fuzz_input(std::span<const std::uint8_t> bytes) {
    bytes = bytes.first(std::min<std::size_t>(65536, bytes.size()));
    auto whole = replay(bytes, 65536), fragmented = replay(bytes, 17);
    require(whole.consumed == fragmented.consumed && whole.output == fragmented.output);
    require(whole.error.has_value() == fragmented.error.has_value());
    if (whole.error) require(whole.error->wire_code == fragmented.error->wire_code && whole.error->scope == fragmented.error->scope);
}
#ifdef HTTP2_ENGINE_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* bytes, std::size_t size) {
    http2_engine_fuzz_input({bytes, size}); return 0;
}
#endif
