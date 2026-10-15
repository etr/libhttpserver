/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_PARITY_BEHAVIOR_PROFILES_HPP_
#define TEST_PARITY_BEHAVIOR_PROFILES_HPP_
#include <memory>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/auth/basic_auth.hpp>
#include <httpserver/auth/digest_auth.hpp>
#include <httpserver/forms/multipart.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/response_definition.hpp>
#include <parity/dispatch_replay.hpp>
#include "../unit/response_source_rig.hpp"

namespace parity {
namespace hs = httpserver::server;
namespace http = httpserver::http;

inline hs::sync_response behavior_text(std::string_view body, unsigned code = 200) {
    hs::sync_response value;
    value.status = http::status::from_code(code);
    value.fields.append("Content-Type", "text/plain");
    const auto bytes = std::as_bytes(std::span(body));
    value.body.assign(bytes.begin(), bytes.end());
    return value;
}

// Test-local equivalent applications. Handlers exercise production guards,
// streaming adapters and response sources; no golden response is replayed.
class behavior_profile {
 public:
    hs::route_registry routes;
    hs::hook_bus hooks;
    hs::peer_policy peers;
    httpserver::detail::error_page_factories pages;
    std::vector<std::unique_ptr<httpserver::websocket::session>> sessions;
    explicit behavior_profile(const hs::resource_budget& budget) : routes(v3_replay::build_routing_basic(budget)) {
        require(routes.route(http::method::known(http::method_id::get), "/__smoke", v3_replay::text_route("smoke-ok")));
    }
    httpserver::detail::request_dispatch_context context() const {
        return {&hooks, pages, policy_ ? &peers : nullptr};
    }
    void install(const std::string& profile, const std::string& data_root) {
        const auto get = http::method::known(http::method_id::get);
        const auto post = http::method::known(http::method_id::post);
        if (profile == "routing_basic" || profile == "tls") return;
        if (profile == "routing_hooks") {
            require(routes.route(http::method::known(http::method_id::del), "/admin", v3_replay::text_route("admin-ok")));
            v3_replay::install_routing_hooks(hooks);
            pages = v3_replay::custom_hook_pages();
        } else if (profile == "auth_basic") {
            httpserver::auth::basic_auth_policy policy;
            require(httpserver::auth::basic_auth_policy::create("transcript", "alice", "wonderland", policy));
            require(routes.route(get, "/secret", httpserver::auth::make_basic_guard(std::move(policy), v3_replay::text_route("secret-ok"))));
        } else if (profile == "auth_digest") {
            httpserver::auth::digest_auth_policy policy;
            httpserver::auth::digest_auth_options options;
            options.challenge_body = "digest required";
            require(httpserver::auth::digest_auth_policy::create("transcript", "bob", "builder", options, policy));
            require(routes.route(get, "/digest", httpserver::auth::make_digest_guard(std::move(policy), v3_replay::text_route("digest-ok"))));
        } else if (profile == "forms") {
            require(routes.route(post, "/echo_form", httpserver::forms::make_urlencoded_route({},
                [](const http::request_head&, const httpserver::forms::form_fields& fields) {
                    return behavior_text("a=" + std::string(fields.value("a").value_or("")) + ";b=" + std::string(fields.value("b").value_or("")));
                })));
            require(routes.route(post, "/upload", httpserver::forms::make_multipart_route({},
                [](const http::request_head&, const httpserver::forms::form_fields& fields) {
                    return behavior_text("note=" + std::string(fields.value("note").value_or("")));
                })));
        } else if (profile == "file_resp") {
            install_files(data_root);
        } else if (profile == "ip_controls") {
            peers.set_mode(hs::peer_policy_mode::reject_all);
            require(peers.allow("127.0.0.1"));
            require(peers.allow("::1"));
            policy_ = true;
            require(routes.route(get, "/__deny", [this](httpserver::exchange& x) -> httpserver::task<void> {
                require(peers.deny("127.0.0.1"));
                require(peers.deny("::1"));
                co_await hs::detail::commit_sync_value(x, behavior_text("", 204));
            }));
        } else if (profile == "shoutcast") {
            require(routes.route(get, "/stream", [](httpserver::exchange& x) -> httpserver::task<void> {
                auto value = behavior_text("OK");
                value.status = value.status.with_shoutcast();
                co_await hs::detail::commit_sync_value(x, std::move(value));
            }));
        } else if (profile == "websocket") {
            auto upgrade = [this](httpserver::exchange& x) -> httpserver::task<void> {
                auto upgraded = co_await x.upgrade({});
                if (!upgraded.status.ok()) {
                    std::cerr << "parity upgrade refused: " << upgraded.status.message() << "\n";
                    upgraded.rejection_fields.append("Content-Length", "0");
                    x.respond(upgraded.rejection_status, upgraded.rejection_fields);
                    co_return;
                }
                sessions.push_back(std::make_unique<httpserver::websocket::session>(std::move(*upgraded.session)));
                auto& session = *sessions.back();
                for (;;) {
                    auto received = co_await session.receive();
                    if (!received.status.ok() || !received.value) co_return;
                    auto& message = *received.value;
                    if (session.try_send(message.kind, message.data).disposition != httpserver::websocket::send_disposition::accepted)
                        throw std::runtime_error("parity WebSocket send refused");
                }
            };
            require(routes.route(get, "/ws", upgrade));
            require(routes.route(http::method::known(http::method_id::connect), "/ws", upgrade));
        } else {
            throw std::runtime_error("unknown behavior profile " + profile);
        }
    }

 private:
    bool policy_ = false;
    static void require(const http::outcome& result) {
        if (!result.ok()) throw std::runtime_error("profile registration: " + result.message());
    }
    void install_files(const std::string& root) {
        const auto get = http::method::known(http::method_id::get);
        for (const auto& [route, file] : std::vector<std::pair<std::string, std::string>>{
                {"/file", "test_content"}, {"/empty_file", "test_content_empty"}, {"/missing", "missing-task179"}}) {
            httpserver::response_definition definition;
            http::fields fields;
            fields.append("Content-Type", "application/octet-stream");
            require(httpserver::response_definition::reopen_file(http::status::from_code(200), fields, root + "/" + file, definition));
            require(routes.route(get, route, [definition](httpserver::exchange& x) -> httpserver::task<void> {
                const auto sent = co_await httpserver::send_definition(x, definition, {});
                if (!sent.status.ok() && x.state() != httpserver::exchange_state::responded)
                    co_await hs::detail::commit_sync_value(x, behavior_text("Internal Server Error", 500));
            }));
        }
        for (const std::string route : {"/pipe", "/iovec", "/deferred"}) {
            require(routes.route(get, route, [route](httpserver::exchange& x) -> httpserver::task<void> {
                httpserver::response_definition definition;
                http::fields fields;
                if (route == "/pipe") {
                    httpserver_test::filled_pipe source("abcXYZ");
                    require(httpserver::response_definition::owned_pipe(http::status::from_code(200), fields, source.release(), definition));
                } else if (route == "/iovec") {
                    auto storage = std::make_shared<std::string>("abcXYZ");
                    require(httpserver::response_definition::borrowed(http::status::from_code(200), fields,
                        httpserver_test::byte_span(*storage), httpserver::body_lease{storage}, definition));
                    storage.reset();  // The definition's lease alone retains the borrowed body.
                } else {
                    require(httpserver::response_definition::factory(http::status::from_code(200), fields, [] {
                        return httpserver::body_producer([part = 0]() mutable {
                            static const std::string pieces[] = {"abc", "XYZ"};
                            if (part == 2) return httpserver::body_chunk{http::outcome::okay(), {}, true};
                            return httpserver::body_chunk{http::outcome::okay(), httpserver_test::byte_span(pieces[part++]), false};
                        });
                    }, definition));
                }
                static_cast<void>(co_await httpserver::send_definition(x, definition, {}));
            }));
        }
    }
};
}  // namespace parity
#endif  // TEST_PARITY_BEHAVIOR_PROFILES_HPP_
