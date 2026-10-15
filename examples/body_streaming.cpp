/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <iostream>
#include <string>
#include <utility>

#include "./example_host.hpp"
#include "./example_reply.hpp"
namespace http = httpserver::http;
namespace srv = httpserver::server;
int main(int argc, char** argv) {
    srv::server_options options;
    options.concurrency().workers = 2;
    // This includes retained output allocation and its bounded growth scratch.
    options.budgets().set(srv::resource::response_queue_bytes, 16 * 1024 * 1024);
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    const auto post = http::method::known(http::method_id::post);
    if (!server
             .route(post, "/reject",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        co_await v3_example::reply(x, "rejected before admission\n", 403);
                    })
             .ok()) {
        return 1;
    }
    if (!server
             .route(post, "/upload",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        if (!x.admit_body({1024}).ok()) {
                            co_return;
                        }
                        std::array<std::byte, 64> buffer;
                        std::size_t total = 0;
                        for (;;) {
                            const auto read = co_await x.body().read_some(buffer);
                            if (!read.status.ok()) {
                                if (read.status.code() == http::outcome_code::limit_exceeded) {
                                    co_await v3_example::reply(x, "", 413);
                                }
                                co_return;
                            }
                            total += read.data.size();
                            if (total > 1024) {
                                co_await v3_example::reply(x, "", 413);
                                co_return;
                            }
                            if (read.end_of_body) {
                                break;
                            }
                        }
                        std::string text = "bytes=" + std::to_string(total) + "\n";
                        for (const auto& value : x.body().trailers().all("X-End")) {
                            text += "trailer=" + value + "\n";
                        }
                        // start_response closes the reader: consume EOF/trailers first.
                        co_await v3_example::reply(x, std::move(text));
                    })
             .ok()) {
        return 1;
    }
    if (!server
             .route(http::method::known(http::method_id::get), "/download",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        if (!x.start_response(http::status::from_code(200), {}).ok()) {
                            co_return;
                        }
                        const std::string chunk = "generated-chunk\n";
                        for (int i = 0; i < 64; ++i) {
                            if (!(co_await x.writer().write(std::as_bytes(std::span(chunk)))).status.ok()) {
                                co_return;
                            }
                        }
                        http::fields trailers;
                        trailers.append("X-End", "first");
                        trailers.append("X-End", "second");
                        co_await x.writer().finish(trailers);
                    })
             .ok()) {
        return 1;
    }
    if (!server
             .route(http::method::known(http::method_id::get), "/download/slow",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        if (!x.start_response(http::status::from_code(200), {}).ok()) {
                            co_return;
                        }
                        std::cout << "DOWNLOAD_STARTED" << std::endl;
                        const std::string chunk(32768, 'x');
                        for (unsigned i = 0; i < 128; ++i) {
                            if (!(co_await x.writer().write(std::as_bytes(std::span(chunk)))).status.ok()) {
                                co_return;
                            }
                        }
                        http::fields trailers;
                        trailers.append("X-End", "slow-complete");
                        if (!(co_await x.writer().finish(trailers)).status.ok()) {
                            co_return;
                        }
                        std::cout << "DOWNLOAD_COMPLETE" << std::endl;
                    })
             .ok()) {
        return 1;
    }
    return v3_example::run(server, argc, argv);
}
