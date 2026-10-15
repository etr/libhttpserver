/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <utility>

#include <httpserver/auth/basic_auth.hpp>
#include <httpserver/auth/digest_auth.hpp>

#include "./example_host.hpp"
#include "./example_reply.hpp"
namespace http = httpserver::http;
namespace srv = httpserver::server;
int main(int argc, char** argv) {
    namespace auth = httpserver::auth;
    auth::basic_auth_policy basic;
    auth::digest_auth_policy digest;
    auth::digest_auth_options digest_options;
    digest_options.algorithm = auth::digest_algorithm::sha_256;
    if (!auth::basic_auth_policy::create("examples", "demo", "secret", basic).ok() ||
        !auth::digest_auth_policy::create("examples", "demo", "secret", digest_options, digest).ok()) {
        return 1;
    }
    srv::server_options options;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    const auto guarded = [](httpserver::exchange& x) -> httpserver::task<void> {
        co_await v3_example::reply(x, "authenticated\n");
    };
    if (!server
             .route(http::method::known(http::method_id::get), "/auth/basic",
                    auth::make_basic_guard(std::move(basic), guarded))
             .ok() ||
        !server
             .route(http::method::known(http::method_id::get), "/auth/digest",
                    auth::make_digest_guard(std::move(digest), guarded))
             .ok() ||
        !server
             .route(
                 http::method::known(http::method_id::get), "/public",
                 [](httpserver::exchange& x) -> httpserver::task<void> { co_await v3_example::reply(x, "public\n"); })
             .ok()) {
        return 1;
    }
    return v3_example::run(server, argc, argv);
}
