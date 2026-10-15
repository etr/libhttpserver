/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <httpserver/server/tls_credentials.hpp>

#include "./example_reply.hpp"
#include "./tls_unavailable.hpp"
namespace srv = httpserver::server;
namespace http = httpserver::http;
std::string pem(const char* path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot read supplied PEM file");
    }
    return {std::istreambuf_iterator<char>(input), {}};
}
std::vector<std::byte> hex(std::string_view text) {
    if (text.empty() || text.size() % 2 || text.size() > 128) {
        throw std::runtime_error("key/digest must be 1..64 hex bytes");
    }
    std::vector<std::byte> bytes;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        unsigned value;
        auto parsed = std::from_chars(text.data() + i, text.data() + i + 2, value, 16);
        if (parsed.ec != std::errc() || parsed.ptr != text.data() + i + 2) {
            throw std::runtime_error("invalid hex material");
        }
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}
int main(int argc, char** argv) {
    if (!httpserver::query_features().tls_provider) {
        return v3_example::tls_unavailable();
    }
    try {
        if (argc < 2) {
            throw std::runtime_error("mode required");
        }
        const std::string mode = argv[1];
        srv::tls_credentials_config config;
        srv::tls_host_credentials host;
        host.host = "a.example";
        std::shared_ptr<srv::tls_psk_runtime> runtime;
        srv::tls_acme_challenge challenge;
        std::uint16_t port = 0;
        if (mode == "psk" && argc == 4) {
            const std::string identity = argv[2];
            auto key = std::make_shared<const srv::tls_psk_key>(hex(argv[3]));
            if (identity.empty() || identity.size() > 64) {
                throw std::runtime_error("identity must be 1..64 bytes");
            }
            if (!srv::tls_psk_runtime::create({1, 4}, runtime).ok()) {
                return 1;
            }
            host.profile = srv::tls_profile::external_psk;
            host.psk.emplace();
            host.psk->runtime = runtime;
            host.psk->maximum_identity_bytes = 64;
            host.psk->maximum_key_bytes = 64;
            host.psk->lookup = [identity, key](auto supplied,
                                               const srv::tls_psk_context& context) -> srv::tls_psk_result {
                if (context.cancellation.stop_requested()) {
                    return {srv::tls_psk_status::cancelled, {}};
                }
                if (context.deadline <= std::chrono::steady_clock::now()) {
                    return {srv::tls_psk_status::timeout, {}};
                }
                if (supplied.size() > 64 || key->bytes().size() > context.maximum_key_bytes) {
                    return {};
                }
                const std::string_view received(reinterpret_cast<const char*>(supplied.data()), supplied.size());
                if (received != identity) {
                    return {};
                }
                return {srv::tls_psk_status::accepted, srv::tls_psk_key(key->bytes())};
            };
        } else if (mode == "certificate" && argc == 6) {
            host.certificate_chain_pem = pem(argv[2]);
            host.private_key_pem = pem(argv[3]);
            config.hosts.push_back({"b.example", pem(argv[4]), pem(argv[5]), {"http/1.1"}});
        } else if (mode == "mtls" && argc == 6) {
            const std::string auth = argv[5];
            if (auth != "request" && auth != "require") {
                throw std::runtime_error("invalid client auth mode");
            }
            host.certificate_chain_pem = pem(argv[2]);
            host.private_key_pem = pem(argv[3]);
            host.profile = srv::tls_profile::mutual_tls;
            host.trust_roots_pem = pem(argv[4]);
            host.client_auth.mode = auth == "require" ? srv::tls_client_certificate_mode::require
                                                      : srv::tls_client_certificate_mode::request;
        } else if (mode == "acme" && (argc == 7 || argc == 8)) {
            host.certificate_chain_pem = pem(argv[2]);
            host.private_key_pem = pem(argv[3]);
            challenge.host = host.host;
            challenge.certificate_pem = pem(argv[4]);
            challenge.private_key_pem = pem(argv[5]);
            const auto digest = hex(argv[6]);
            if (digest.size() != 32) {
                throw std::runtime_error("ACME digest must be 32 bytes");
            }
            std::copy(digest.begin(), digest.end(), challenge.key_authorization_sha256.begin());
            challenge.expires_at = std::chrono::system_clock::now() + std::chrono::hours(1);
            if (argc == 8) {
                unsigned value = 0;
                const std::string_view argument = argv[7];
                auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
                if (parsed.ec != std::errc() || parsed.ptr != argument.data() + argument.size() || value > 65535) {
                    throw std::runtime_error("invalid port");
                }
                port = static_cast<std::uint16_t>(value);
            }
        } else {
            throw std::runtime_error("invalid mode or argument count");
        }
        config.hosts.insert(config.hosts.begin(), host);
        config.default_host = 0;
        auto store = std::make_shared<srv::tls_credentials>();
        if (!store->replace(config).ok()) {
            throw std::runtime_error("credential validation failed");
        }
        srv::server_options options;
        options.concurrency().workers = 2;
        options.tls().provider = srv::tls_provider::system_default;
        options.tls().profile = host.profile;
        options.tls().client_auth = host.client_auth;
        options.timeouts().handshake = std::chrono::seconds(3);
        options.add_listener({"127.0.0.1", port, true, store});
        srv::native_server server(options);
        if (!server
                 .route(http::method::known(http::method_id::get), "/hello",
                        [](httpserver::exchange& x) -> httpserver::task<void> {
                            const auto peer = x.tls();
                            co_await v3_example::reply(x,
                                                       std::string("profile-ok verified=") +
                                                           (peer && peer->client_certificate_verified ? "1\n" : "0\n"));
                        })
                 .ok() ||
            !server.listen().ok()) {
            throw std::runtime_error("listen/route failed");
        }
        std::cout << "PORT=" << server.get_bound_port(0) << std::endl;
        for (std::string command; std::getline(std::cin, command) && command != "stop";) {
            if (mode == "acme" && (command == "publish" || command == "invalid")) {
                auto candidate = challenge;
                if (command == "invalid") {
                    candidate.key_authorization_sha256[0] ^= std::byte{1};
                }
                const auto result = store->publish_acme(candidate);
                if (command == "invalid" && result.code() == http::outcome_code::invalid_argument) {
                    std::cout << "PRESERVED" << std::endl;
                } else if (command == "publish" && result.ok()) {
                    std::cout << "PUBLISHED" << std::endl;
                } else {
                    std::cout << "PUBLICATION_FAILED" << std::endl;
                }
            } else if (mode == "acme" && command == "remove") {
                std::cout << (store->remove_acme(host.host).ok() ? "REMOVED" : "REMOVAL_FAILED") << std::endl;
            } else {
                std::cout << "UNKNOWN_COMMAND" << std::endl;
            }
        }
        server.stop();
        // Application callbacks own their captures; server stop alone does not join them.
        if (runtime) {
            runtime->request_stop();
            if (!runtime->wait_retired(std::chrono::steady_clock::now() + std::chrono::seconds(3)).ok()) {
                return 1;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nusage: tls_profiles certificate A_CHAIN A_KEY B_CHAIN B_KEY\n"
                  << "       tls_profiles mtls CHAIN KEY CLIENT_ROOTS request|require\n"
                  << "       tls_profiles psk IDENTITY HEX_KEY\n"
                  << "       tls_profiles acme CHAIN KEY CHALLENGE_CERT CHALLENGE_KEY HEX_DIGEST [port]\nstdin: "
                     "publish, invalid, remove, stop\n";
        return 2;
    }
}
