/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <httpserver.hpp>
#include "./host_poll.hpp"

namespace srv = httpserver::server;
namespace http = httpserver::http;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;
using std::chrono_literals::operator""h;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
std::string pem(const std::string& directory, const std::string& name) {
    std::ifstream in(directory + "/" + name);
    require(static_cast<bool>(in), "fixture unavailable");
    return {std::istreambuf_iterator<char>(in), {}};
}
srv::tls_host_credentials certificate(const std::string& directory, const std::string& name) {
    return {name + ".example", pem(directory, name + ".pem") + pem(directory, name == "a-replacement" ? "rotation-root.pem" : "intermediate.pem"),
            pem(directory, name + "-key.pem"), {"http/1.1"}};
}
struct lookup_state {
    std::mutex mutex;
    std::condition_variable changed;
    bool release = false;
    std::atomic<unsigned> entered{0}, retired{0}, cancelled{0}, bad_context{0};
    std::atomic<unsigned> self_wait{0};
};
int main(int argc, char** argv) {
    try {
        require(argc == 6, "arguments: tls loop fixtures scenario port");
        const bool enabled = std::string(argv[1]) == "yes";
        const auto loop = std::string(argv[2]) == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
        const std::string directory = argv[3], scenario = argv[4];
        const auto port = static_cast<std::uint16_t>(std::stoi(argv[5]));
        auto state = std::make_shared<lookup_state>();
        std::shared_ptr<srv::tls_psk_runtime> runtime;
        const auto created = srv::tls_psk_runtime::create({1, 1}, runtime);
        if (!enabled) {
            require(created.code() == http::outcome_code::not_supported && !runtime, "TLS-off runtime");
            srv::tls_credentials store;
            require(store.publish_acme({}).code() == http::outcome_code::not_supported, "TLS-off ACME publish");
            require(store.remove_acme("a.example").code() == http::outcome_code::not_supported, "TLS-off ACME remove");
            std::array key{std::byte{1}, std::byte{2}};
            srv::tls_psk_key owned(key), moved(std::move(owned));
            require(owned.bytes().empty() && moved.bytes().size() == 2, "TLS-off secure key");
            moved.clear();
            require(moved.bytes().empty(), "TLS-off key clear");
            std::cout << "PROFILES_OFF 5" << std::endl;
            return 0;
        }
        require(created.ok(), "lookup runtime");
        const std::weak_ptr<srv::tls_psk_runtime> retained_runtime = runtime;
        const bool psk = scenario.starts_with("psk");
        auto material = certificate(directory, "a");
        if (scenario.starts_with("mtls")) {
            material.profile = srv::tls_profile::mutual_tls;
            material.trust_roots_pem = pem(directory, "client-root.pem");
            material.client_auth.mode = scenario == "mtls-request" ? srv::tls_client_certificate_mode::request
                                                                  : srv::tls_client_certificate_mode::require;
        }
        if (psk) {
            material.certificate_chain_pem.clear();
            material.private_key_pem.clear();
            material.profile = srv::tls_profile::external_psk;
            material.psk.emplace();
            material.psk->runtime = runtime;
            material.psk->maximum_identity_bytes = 64;
            material.psk->maximum_key_bytes = 32;
            material.psk->lookup = [state, runtime, scenario](auto identity, const srv::tls_psk_context& context) {
                ++state->entered;
                if (context.selected_host != "a.example" || context.credential_generation == 0
                    || context.deadline <= std::chrono::steady_clock::now() || context.maximum_key_bytes != 32) ++state->bad_context;
                const std::string text(reinterpret_cast<const char*>(identity.data()), identity.size());
                const std::string binary("binary\0identity", 15);
                if (scenario == "psk-block" || text == "block") {
                    std::unique_lock lock(state->mutex);
                    state->changed.wait(lock, [&] { return state->release; });
                    if (context.cancellation.stop_requested()) ++state->cancelled;
                }
                if ((text == "version12" && context.version != srv::tls_psk_version::tls12)
                    || (text == "version13" && context.version != srv::tls_psk_version::tls13)) ++state->bad_context;
                if (text == "self") state->self_wait = static_cast<unsigned>(runtime->wait_retired(std::chrono::steady_clock::now() + 100ms).code());
                ++state->retired;
                if (text == "empty") return srv::tls_psk_result{srv::tls_psk_status::accepted, {}};
                if (text == "oversized") {
                    std::array<std::byte, 33> oversized{};
                    return srv::tls_psk_result{srv::tls_psk_status::accepted, srv::tls_psk_key(oversized)};
                }
                if (text == "throw") throw std::runtime_error("private lookup failure");
                if (text != "identity" && text != binary && text != "block" && text != "self" && text != "version12" && text != "version13") return srv::tls_psk_result{};
                std::array<std::byte, 32> key;
                key.fill(std::byte{0x42});
                return srv::tls_psk_result{srv::tls_psk_status::accepted, srv::tls_psk_key(key)};
            };
        }
        auto store = std::make_shared<srv::tls_credentials>();
        srv::tls_credentials_config initial{{material}, 0};
        if (scenario == "certificate") initial.hosts.push_back(certificate(directory, "b"));
        require(store->replace(initial).ok(), "profile publication");
        srv::server_options options;
        options.loop() = loop;
        options.concurrency().workers = 2;
        options.timeouts().handshake = scenario == "acme" ? 2s : 500ms;
        options.timeouts().header = 2s;
        options.tls().provider = srv::tls_provider::system_default;
        options.tls().profile = material.profile;
        options.tls().client_auth = material.client_auth;
        options.add_listener({"127.0.0.1", port, true, store});
        srv::native_server server(options);
        std::atomic<unsigned> handled{0}, observed{0}, accepted{0}, metadata_errors{0};
        auto accept_hook = server.hooks().add<srv::hook_phase::accept_decision>([&](srv::accept_decision_ctx&) {
            ++accepted; return srv::hook_action::pass();
        });
        const auto expected_peer = pem(directory, "client-valid-fingerprint.txt");
        const auto valid_metadata = [&](const srv::tls_peer_metadata* peer) {
            if (!peer) return false;
            if (!peer->has_client_certificate) return !peer->client_certificate_verified && peer->fingerprint_sha256.empty();
            return peer->client_certificate_verified && peer->fingerprint_sha256 == expected_peer
                && !peer->subject_dn.empty() && !peer->issuer_dn.empty() && peer->not_after > peer->not_before;
        };
        auto hook = server.hooks().add<srv::hook_phase::request_received>([&](srv::request_received_ctx& context) {
            ++observed;
            if (!valid_metadata(context.tls.get())) ++metadata_errors;
            return srv::hook_action::pass();
        });
        std::atomic<bool> handler_returned{false};
        srv::drain_ticket handler_ticket;
        auto route = [&](httpserver::exchange& exchange) -> httpserver::task<void> {
            ++handled;
            if (!valid_metadata(exchange.tls().get())) ++metadata_errors;
            std::string body = "profile-ok\n" + exchange.tls()->fingerprint_sha256 + "\n";
            http::fields fields;
            fields.append("Content-Length", std::to_string(body.size()));
            require(exchange.start_response(http::status::from_code(200), fields).ok(), "start response");
            co_await exchange.writer().write({reinterpret_cast<const std::byte*>(body.data()), body.size()});
            co_await exchange.writer().finish();
            if (scenario == "psk-handler-stop") {
                server.request_stop();
                handler_returned.store(true, std::memory_order_release);
            }
            if (scenario == "psk-handler-drain") {
                require(server.begin_drain(100ms, handler_ticket).ok(), "handler drain");
                handler_returned.store(true, std::memory_order_release);
            }
        };
        require(server.route(http::method::known(http::method_id::get), "/installed", route).ok(), "route");
        require(server.listen().ok(), "profile listener");
        std::unique_ptr<srv::native_server> companion;
        if (scenario == "psk-shared") {
            auto second_store = std::make_shared<srv::tls_credentials>();
            require(second_store->replace({{material}, 0}).ok(), "shared second store");
            srv::server_options second;
            second.loop() = loop;
            second.concurrency().workers = 1;
            second.tls() = options.tls();
            second.add_listener({"127.0.0.1", 0, true, second_store});
            companion = std::make_unique<srv::native_server>(second);
            require(companion->route(http::method::known(http::method_id::get), "/installed", route).ok(), "shared route");
            require(companion->listen().ok(), "shared listener");
        }
        std::atomic<bool> stopping{false};
        std::jthread poller;
        if (loop == srv::loop_mode::external) {
            poller = std::jthread([&] {
            while (!stopping) {
                for (auto* current : {&server, companion.get()}) {
                    if (!current) continue;
                    auto* driver = current->readiness();
                    auto interests = driver->interests();
                    auto events = v3_host::wait(interests, v3_host::clock_type::now() + 10ms);
                    require(driver->dispatch(events, v3_host::clock_type::now()).ok(), "external dispatch");
                }
            }
            });
        }
        std::cout << server.get_bound_port(0);
        if (companion) std::cout << ' ' << companion->get_bound_port(0);
        std::cout << std::endl;
        for (std::string line; std::getline(std::cin, line);) {
            if (line == "stop") break;
            if (line == "stats") {
                std::cout << accepted << ' ' << handled << ' ' << observed << ' ' << metadata_errors
                << ' ' << state->entered << ' ' << state->retired << ' ' << state->cancelled << ' ' << state->bad_context << ' ' << state->self_wait << std::endl;
            } else if (line == "release") {
                { std::lock_guard lock(state->mutex); state->release = true; }
                state->changed.notify_all(); std::cout << "RELEASED" << std::endl;
            } else if (line == "incompatible") {
                auto candidate = certificate(directory, "b");
                require(store->replace({{candidate}, 0}).code() == http::outcome_code::invalid_argument, "bound policy weakened");
                std::cout << "PRESERVED" << std::endl;
            } else if (line == "rotate" || line == "rotate-same-host") {
                auto candidate = material;
                if (!psk) {
                    auto replacement = certificate(directory, line == "rotate-same-host" ? "a-replacement" : "b");
                    candidate.certificate_chain_pem = replacement.certificate_chain_pem;
                    candidate.private_key_pem = replacement.private_key_pem;
                }
                require(store->replace({{candidate}, 0}).ok(), "compatible rotation"); std::cout << "ROTATED" << std::endl;
            } else if (line == "handler_returned") {
                if (!handler_returned.load(std::memory_order_acquire)) {
                    std::cout << "PENDING" << std::endl;
                } else {
                    if (scenario == "psk-handler-stop") require(!server.is_running(), "handler stop admission");
                    std::cout << "RETURNED" << std::endl;
                }
            } else if (line == "request_stop") {
                server.request_stop(); std::cout << "STOPPED" << std::endl;
            } else if (line == "drain") {
                srv::drain_ticket ticket; require(server.begin_drain(100ms, ticket).ok(), "begin drain");
                srv::drain_result result; require(ticket.wait(result).ok(), "drain wait"); std::cout << "DRAINED" << std::endl;
            } else if (line == "drop") {
                store.reset(); initial.hosts.clear(); material = {}; runtime.reset();
                std::cout << "DROPPED" << std::endl;
            } else if (line == "retire") {
                auto owner = retained_runtime.lock(); require(static_cast<bool>(owner), "retained callback runtime");
                owner->request_stop();
                std::cout << static_cast<unsigned>(owner->wait_retired(std::chrono::steady_clock::now() + 20ms).code()) << std::endl;
            } else if (line == "publish" || line == "invalid_acme" || line == "expire") {
                srv::tls_acme_challenge challenge;
                challenge.host = "a.example";
                challenge.certificate_pem = pem(directory, "acme.pem");
                challenge.private_key_pem = pem(directory, "acme-key.pem");
                challenge.key_authorization_sha256.fill(std::byte{0x42});
                challenge.expires_at = std::chrono::system_clock::now() + (line == "expire" ? 5s : 1h);
                if (line == "invalid_acme") challenge.key_authorization_sha256[0] = std::byte{0};
                const auto result = store->publish_acme(challenge);
                require(result.code() == (line == "invalid_acme" ? http::outcome_code::invalid_argument : http::outcome_code::ok), "ACME outcome");
                std::cout << "PUBLISHED";
                if (line == "expire") std::cout << ' ' << std::chrono::duration_cast<std::chrono::milliseconds>(challenge.expires_at.time_since_epoch()).count();
                std::cout << std::endl;
            } else if (line.starts_with("variant ")) {
                const auto variant = line.substr(8);
                srv::tls_acme_challenge challenge;
                challenge.host = "a.example";
                challenge.certificate_pem = pem(directory, variant + ".pem");
                challenge.private_key_pem = pem(directory, "acme-key.pem");
                challenge.key_authorization_sha256.fill(std::byte{0x42});
                challenge.expires_at = std::chrono::system_clock::now() + 1h;
                if (variant == "mismatch-key") {
                    challenge.certificate_pem = pem(directory, "acme.pem"); challenge.private_key_pem = pem(directory, "b-key.pem");
                }
                if (variant == "malformed-key" || variant == "encrypted-key") {
                    challenge.certificate_pem = pem(directory, "acme.pem"); challenge.private_key_pem = pem(directory, variant + ".pem");
                }
                if (variant == "past-expiry") {
                    challenge.certificate_pem = pem(directory, "acme.pem"); challenge.expires_at = std::chrono::system_clock::now() - 1s;
                }
                if (variant == "late-expiry") {
                    challenge.certificate_pem = pem(directory, "acme.pem"); challenge.expires_at = std::chrono::system_clock::now() + 72h;
                }
                require(store->publish_acme(challenge).code() == http::outcome_code::invalid_argument, "invalid ACME candidate accepted");
                std::cout << "REJECTED" << std::endl;
            } else if (line == "race") {
                srv::tls_acme_challenge challenge;
                challenge.host = "a.example";
                challenge.certificate_pem = pem(directory, "acme.pem");
                challenge.private_key_pem = pem(directory, "acme-key.pem");
                challenge.key_authorization_sha256.fill(std::byte{0x42});
                challenge.expires_at = std::chrono::system_clock::now() + 1h;
                std::atomic<unsigned> failures{0};
                std::thread replace([&] {
                    for (int i = 0; i < 3; ++i) {
                        if (!store->replace({{material}, 0}).ok()) ++failures;
                    }
                });
                std::thread publish([&] { for (int i = 0; i < 3; ++i) if (!store->publish_acme(challenge).ok()) ++failures; });
                std::thread remove([&] { for (int i = 0; i < 3; ++i) if (!store->remove_acme("a.example").ok()) ++failures; });
                replace.join(); publish.join(); remove.join();
                require(failures == 0 && store->remove_acme("a.example").ok(), "concurrent public writers");
                std::cout << "RACED" << std::endl;
            } else if (line == "remove") {
                require(store->remove_acme("A.EXAMPLE.").ok(), "remove ACME"); std::cout << "REMOVED" << std::endl;
            } else {
                throw std::runtime_error("unknown command");
            }
        }
        { std::lock_guard lock(state->mutex); state->release = true; }
        state->changed.notify_all();
        runtime = retained_runtime.lock();
        require(static_cast<bool>(runtime), "runtime retained through callback retirement");
        server.request_stop(); stopping = true;
        if (poller.joinable()) poller.join();
        server.stop();
        if (companion) companion->stop();
        runtime->request_stop(); require(runtime->wait_retired(std::chrono::steady_clock::now() + 2s).ok(), "callback retirement");
        require(metadata_errors == 0 && state->bad_context == 0, "metadata/context errors");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
