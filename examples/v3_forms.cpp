/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include <httpserver/forms/multipart.hpp>

#include "./v3_example_host.hpp"
#include "./v3_example_reply.hpp"
namespace forms = httpserver::forms;
namespace http = httpserver::http;
namespace srv = httpserver::server;
std::string describe(const forms::form_fields& fields) {
    std::string text;
    for (const auto& [name, value] : fields.entries()) {
        text += name + "=" + value + "\n";
    }
    return text;
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3 || !std::filesystem::is_directory(argv[1])) {
        std::cerr << "usage: v3_forms TEMP_DIRECTORY [seconds]\n";
        return 2;
    }
    const std::string directory = argv[1];
    srv::server_options options;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    const auto post = http::method::known(http::method_id::post);
    if (!server
             .route(post, "/form",
                    forms::make_urlencoded_route({1024, 8},
                                                 [](const auto&, const forms::form_fields& fields) {
                                                     return v3_example::value(describe(fields));
                                                 }))
             .ok()) {
        return 1;
    }
    if (!server
             .route(post, "/form/read",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        const auto read = co_await forms::read_urlencoded(x, {1024, 8});
                        if (!read.ok()) {
                            if (read.reject_status().valid()) {
                                x.respond(read.reject_status(), read.reject_fields());
                            }
                            co_return;
                        }
                        co_await v3_example::reply(x, describe(read.fields));
                    })
             .ok()) {
        return 1;
    }
    for (bool keep : {false, true}) {
        if (!server
                 .route(post, keep ? "/multipart/keep" : "/multipart",
                        [directory, keep](httpserver::exchange& x) -> httpserver::task<void> {
                            std::string text;
                            {
                                forms::temp_file_options files;
                                files.directory = directory;
                                files.random_names = true;
                                // Only an explicitly selected, completed safe.txt upload is retained.
                                files.should_keep = [keep](const auto&, const auto& filename, const auto&) {
                                    return keep && filename == "safe.txt";
                                };
                                forms::temp_file_part_sink sink(std::move(files));
                                const auto result = co_await forms::read_multipart(x, {4096, 8, 1024, 512}, sink);
                                if (!result.ok()) {
                                    if (result.reject_status().valid()) {
                                        x.respond(result.reject_status(), result.reject_fields());
                                    }
                                    co_return;
                                }
                                text = describe(sink.take_fields());
                                for (const auto& file : sink.completed()) {
                                    const auto basename = file.filename.substr(file.filename.find_last_of("/\\") + 1);
                                    if (basename.empty() || basename == "." || basename == ".." ||
                                        basename.find('\0') != std::string::npos) {
                                        x.respond(http::status::from_code(400), {});
                                        co_return;
                                    }
                                    text += "file=" + basename + " bytes=" + std::to_string(file.file_size) + "\n";
                                }
                            }  // Cleanup/keep policy runs before the success response is observed.
                            co_await v3_example::reply(x, std::move(text));
                        })
                 .ok()) {
            return 1;
        }
    }
    return v3_example::run(server, argc - 1, argv + 1);
}
