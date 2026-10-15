/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <httpserver/response_definition.hpp>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include "./example_host.hpp"
#include "./example_reply.hpp"
namespace http = httpserver::http;
namespace srv = httpserver::server;
using definition = httpserver::response_definition;
std::vector<std::byte> bytes(std::string_view text) {
    const auto view = std::as_bytes(std::span(text));
    return {view.begin(), view.end()};
}
// Tiny prefilled pipe with a closed writer: no application producer can block.
std::FILE* pipe_body() {
    int handles[2];
#if defined(_WIN32)
    if (_pipe(handles, 4096, _O_BINARY) != 0) {
        return nullptr;
    }
    auto* reader = _fdopen(handles[0], "rb");
    const int written = _write(handles[1], "pipe-eof\n", 9);
    _close(handles[1]);
    if (!reader) {
        _close(handles[0]);
    }
#else
    if (pipe(handles) != 0) {
        return nullptr;
    }
    auto* reader = fdopen(handles[0], "rb");
    const auto written = write(handles[1], "pipe-eof\n", 9);
    close(handles[1]);
    if (!reader) {
        close(handles[0]);
    }
#endif
    if (written != 9 && reader) {
        std::fclose(reader);
        return nullptr;
    }
    return reader;
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3 || !std::filesystem::is_regular_file(argv[1])) {
        std::cerr << "usage: response_sources BODY_FILE [seconds]\n";
        return 2;
    }
    const auto status = http::status::from_code(200);
    const auto shoutcast = status.with_shoutcast();
    definition owned, borrowed, reopen, factory, file, pipe, missing, invalid, icy;
    auto storage = std::make_shared<const std::string>("leased\n");
    const auto span = std::as_bytes(std::span(*storage));
    if (definition::borrowed(status, {}, span, {}, invalid).code() != http::outcome_code::invalid_argument) {
        return 1;
    }
    if (!definition::owned_bytes(shoutcast, {}, bytes("icy\n"), icy).ok() ||
        !definition::owned_bytes(status, {}, bytes("owned\n"), owned).ok() ||
        !definition::borrowed(status, {}, span, httpserver::body_lease(storage), borrowed).ok() ||
        !definition::reopen_file(status, {}, argv[1], reopen).ok() ||
        !definition::reopen_file(status, {}, std::string(argv[1]) + ".missing", missing).ok() ||
        !definition::factory(
             status, {},
             []() -> httpserver::body_producer {
                 // Each send owns its cursor and immutable storage; the factory is concurrent-safe.
                 return [text = std::string("fresh\n"), done = false]() mutable -> httpserver::body_chunk {
                     if (done) {
                         return {http::outcome::okay(), {}, true};
                     }
                     done = true;
                     return {http::outcome::okay(), std::as_bytes(std::span(text)), true};
                 };
             },
             factory)
             .ok()) {
        return 1;
    }
    auto file_closes = std::make_shared<std::atomic<unsigned>>(0);
    auto pipe_closes = std::make_shared<std::atomic<unsigned>>(0);
    auto* handle = std::tmpfile();
    if (!handle) {
        return 1;
    }
    if (std::fwrite("skip-position\n", 1, 14, handle) != 14 || std::fseek(handle, 5, SEEK_SET) != 0) {
        std::fclose(handle);
        return 1;
    }
    if (!definition::owned_file(
             status, {}, handle,
             [file_closes](std::FILE* f) {
                 std::fclose(f);
                 ++*file_closes;
             },
             file)
             .ok()) {
        return 1;
    }
    auto* endpoint = pipe_body();
    if (!endpoint) {
        return 1;
    }
    if (!definition::owned_pipe(
             status, {}, endpoint,
             [pipe_closes](std::FILE* f) {
                 std::fclose(f);
                 ++*pipe_closes;
             },
             pipe)
             .ok()) {
        return 1;
    }
    srv::server_options options;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    std::atomic<unsigned> sends{0};
    for (const auto& [path, response] :
         std::initializer_list<std::pair<const char*, definition>>{{"/icy", icy},
                                                                   {"/bytes", owned},
                                                                   {"/borrowed", borrowed},
                                                                   {"/reopen", reopen},
                                                                   {"/factory", factory},
                                                                   {"/file", file},
                                                                   {"/pipe", pipe},
                                                                   {"/missing", missing}}) {
        if (!server
                 .route(http::method::known(http::method_id::get), path,
                        [response, &sends](httpserver::exchange& x) -> httpserver::task<void> {
                            httpserver::response_overlay overlay;
                            overlay.headers.append("X-Send", std::to_string(++sends));
                            // Trailers require EOF framing; owned/reopened sources pin a length.
                            if (response.kind() == httpserver::response_source_kind::factory ||
                                response.kind() == httpserver::response_source_kind::owned_pipe) {
                                overlay.trailers.append("X-End", "definition");
                            }
                            const auto report = co_await httpserver::send_definition(x, response, overlay);
                            if (!report.status.ok() && !x.terminal() && !x.disconnected()) {
                                co_await v3_example::reply(
                                    x, "", report.status.code() == http::outcome_code::invalid_state ? 409 : 404);
                            }
                        })
                 .ok()) {
            return 1;
        }
    }
    if (!server
             .route(http::method::known(http::method_id::get), "/closes",
                    [&](httpserver::exchange& x) -> httpserver::task<void> {
                        co_await v3_example::reply(x, "file=" + std::to_string(file_closes->load()) +
                                                          " pipe=" + std::to_string(pipe_closes->load()) + "\n");
                    })
             .ok()) {
        return 1;
    }
    const int result = v3_example::run(server, argc - 1, argv + 1);
    // Close callbacks outlive all definition copies, including unused ones.
    file = {};
    pipe = {};
    return result;
}
