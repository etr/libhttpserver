/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <httpserver.hpp>
#include <httpserver/features.hpp>
#include <httpserver/server/tls_credentials.hpp>
#include "./host_poll.hpp"
#include "./http3_semantics.hpp"
namespace srv = httpserver::server;
namespace http = httpserver::http;
std::string pem(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("missing certificate");
    return {std::istreambuf_iterator<char>(input), {}};
}
int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 4) return 2;
        const std::string directory = argv[1], loop = argv[2];
        const std::string mode = argc == 4 ? argv[3] : "basic";
        srv::server_options options;
        options.loop() = loop == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
        options.concurrency().workers = 4;
        options.budgets().set(srv::resource::header_fields, 4096);
        options.budgets().set(srv::resource::quic_reassembly_bytes, 16777216);
        options.protocols().enable(http::protocol::http_3);
        if (!httpserver::query_features().tls_provider) {
            options.add_listener({"127.0.0.1", 0, false});
            srv::native_server server(options);
            if (server.listen().code() != http::outcome_code::not_supported || server.get_bound_port(0)) throw std::runtime_error("TLS-off H3 admission");
            std::cout << "TLS_OFF_H3_REJECTED" << std::endl;
            return 0;
        }
        if (mode == "listener-storage-limit") options.budgets().set(srv::resource::quic_reassembly_bytes, 90000);
        if (mode == "connections-one" || mode == "stalled") options.budgets().set(srv::resource::connections, 1);
        if (mode == "stalled") options.timeouts().handshake = std::chrono::milliseconds(250);
        if (mode == "timeouts") {
            options.timeouts().handshake = std::chrono::milliseconds(250);
            options.timeouts().header = std::chrono::milliseconds(300);
            options.timeouts().body_idle = std::chrono::milliseconds(300);
            options.timeouts().suspension = std::chrono::milliseconds(300);
            options.timeouts().write_idle = std::chrono::milliseconds(300);
        }
        auto credentials = std::make_shared<srv::tls_credentials>();
        srv::tls_host_credentials host{"a.example", pem(directory + "/a.pem") + pem(directory + "/intermediate.pem"),
            pem(directory + "/a-key.pem"), {"h3"}};
        if (mode == "wrong-store") host.alpn = {"http/1.1"};
        if (mode == "mixed" || mode == "aggregate") {
            host.alpn = {"http/1.1", "h2", "h3"}; options.protocols().enable(http::protocol::http_2);
        }
        if (mode == "psk") {
            std::shared_ptr<srv::tls_psk_runtime> runtime;
            if (!srv::tls_psk_runtime::create({1, 0}, runtime).ok()) throw std::runtime_error("PSK runtime");
            host.profile = srv::tls_profile::external_psk;
            host.certificate_chain_pem.clear(); host.private_key_pem.clear();
            host.psk = srv::tls_psk_config{[](auto, const auto&) { return srv::tls_psk_result{}; }, runtime};
            if (credentials->replace({{host}, 0}).code() != http::outcome_code::invalid_argument) throw std::runtime_error("H3 PSK publication");
            std::cout << "PSK_H3_REJECTED" << std::endl; return 0;
        }
        if (mode == "post-auth") {
            host.client_auth.timing = srv::tls_client_auth_timing::post_handshake;
            if (credentials->replace({{host}, 0}).code() != http::outcome_code::not_supported) throw std::runtime_error("H3 post-handshake publication");
            std::cout << "POST_AUTH_H3_REJECTED" << std::endl; return 0;
        }
        if (mode == "unsupported-alpn") {
            host.alpn = {"h9"};
            if (credentials->replace({{host}, 0}).code() != http::outcome_code::invalid_argument) throw std::runtime_error("H3 unsupported ALPN publication");
            std::cout << "ALPN_H3_REJECTED" << std::endl; return 0;
        }
        if (mode == "mtls-request" || mode == "mtls-require") {
            host.profile = srv::tls_profile::mutual_tls;
            host.trust_roots_pem = pem(directory + "/client-root.pem");
            host.client_auth.mode = mode == "mtls-request" ? srv::tls_client_certificate_mode::request : srv::tls_client_certificate_mode::require;
        }
        auto hosts = std::vector<srv::tls_host_credentials>{host};
        if (mode == "credentials") hosts.push_back({"b.example", pem(directory + "/b.pem") + pem(directory + "/intermediate.pem"), pem(directory + "/b-key.pem"), {"h3"}});
        const auto published = credentials->replace({hosts, 0});
        if (!published.ok()) throw std::runtime_error("H3 credential publication refused: " + std::to_string(static_cast<unsigned>(published.code())));
        options.tls().provider = srv::tls_provider::system_default;
        options.tls().profile = host.profile;
        options.tls().client_auth = host.client_auth;
        auto endpoint = srv::listener_options{"127.0.0.1", 0, true, credentials, srv::listener_transport::udp};
        if (mode == "no-udp") endpoint.transport = srv::listener_transport::tcp;
        if (mode == "no-tls") endpoint.tls = false;
        if (mode == "no-h3") options.protocols().disable(http::protocol::http_3);
        if (mode == "invalid-transport") endpoint.transport = static_cast<srv::listener_transport>(255);
        const auto probe = [&](bool udp, std::uint16_t port) {
            srv::server_options config;
            config.loop() = options.loop(); config.concurrency().workers = 2;
            if (udp) { config.protocols().enable(http::protocol::http_3); config.tls() = options.tls(); }
            config.add_listener({"127.0.0.1", port, udp, udp ? credentials : nullptr, udp ? srv::listener_transport::udp : srv::listener_transport::tcp});
            auto result = std::make_unique<srv::native_server>(config);
            if (!result->listen().ok()) throw std::runtime_error("bind probe failed");
            return result;
        };
        std::unique_ptr<srv::native_server> occupied;
        std::uint16_t rollback_port = 0;
        if (mode == "rollback" || mode == "rollback-reverse") {
            const bool first_udp = mode == "rollback-reverse";
            occupied = probe(!first_udp, 0);
            auto free = probe(first_udp, 0); rollback_port = free->get_bound_port(0); free->stop(); free.reset();
            options.add_listener({"127.0.0.1", rollback_port, first_udp, first_udp ? credentials : nullptr,
                first_udp ? srv::listener_transport::udp : srv::listener_transport::tcp});
            options.add_listener({"127.0.0.1", occupied->get_bound_port(0), !first_udp, !first_udp ? credentials : nullptr,
                first_udp ? srv::listener_transport::tcp : srv::listener_transport::udp});
        } else {
            if (mode == "mixed") { auto free = probe(false, 0); endpoint.port = free->get_bound_port(0); free->stop(); free.reset(); }
            options.add_listener(endpoint);
            if (mode == "mixed") options.add_listener({"127.0.0.1", endpoint.port, true, credentials});
            if (mode == "aggregate") {
                options.add_listener({"127.0.0.1", 0, true, credentials});
                options.add_listener({"127.0.0.1", 0, true, credentials, srv::listener_transport::udp});
                options.add_listener({"127.0.0.1", 0, true, credentials});
            }
        }
        srv::diagnostics diagnostics([](const srv::diagnostic_event&) {}); options.diagnostic_channel() = diagnostics;
        if (mode == "policy") options.peer_policy().mode = srv::peer_policy_mode::reject_all;
        std::atomic<unsigned> received{0}, completed{0}, running_entries{0}, cancelled_entries{0}, accepts{0}, rejected{0};
        std::mutex held_mutex;
        std::condition_variable held_changed;
        bool released = false;
        httpserver::resume_signal application_release;
        std::atomic<unsigned> application_entries{0};
        std::array<httpserver::resume_signal, 3> upload_release;
        std::atomic<unsigned> upload_entries{0}, pending_writes{0};
        std::atomic<unsigned> ws_writable_pending{0}, ws_returns{0};
        std::atomic<unsigned> borrowed_live{0}, lease_returns{0}, file_closed{0}, pending_leases{0};
        unsigned upload_release_index = 0;
        auto server_owner = std::make_unique<srv::native_server>(options);
        auto& server = *server_owner;
        public_h3::install_semantics(server, directory);
        srv::drain_ticket admission_drain;
        auto accept = server.hooks().add<srv::hook_phase::accept_decision>([&](srv::accept_decision_ctx& context) {
            ++accepts; if (!context.accepted) ++rejected;
            if (mode == "admission-drain" && !server.begin_drain(std::chrono::seconds(2), admission_drain).ok()) throw std::runtime_error("admission drain initiation");
            return srv::hook_action::pass();
        });
        auto seen = server.hooks().add<srv::hook_phase::request_received>([&](srv::request_received_ctx& context) {
            if (!context.tls) throw std::runtime_error("missing hook TLS");
            ++received;
            if (context.request.route_path == "/short") return srv::hook_action::respond_with({http::status::from_code(418), {}, {}});
            return srv::hook_action::pass();
        });
        auto done = server.hooks().add<srv::hook_phase::request_completed>([&](srv::request_completed_ctx&) {
            ++completed; return srv::hook_action::pass();
        });
        server.route(http::method::known(http::method_id::post), "/body", [](httpserver::exchange& x) -> httpserver::task<void> {
            x.admit_body({16384});
            std::array<std::byte, 1024> buffer;
            std::string body;
            for (;;) {
                auto part = co_await x.body().read_some(buffer);
                if (!part.status.ok()) co_return;
                body.append(reinterpret_cast<const char*>(part.data.data()), part.data.size());
                if (part.end_of_body) break;
            }
            x.start_response(http::status::from_code(200), {});
            co_await x.writer().write(std::as_bytes(std::span(body)));
            http::fields trailers;
            for (auto value : x.body().trailers().all("x-upload-end")) trailers.append("x-upload-end", value);
            co_await x.writer().finish(trailers);
        });
        server.route(http::method::known(http::method_id::get), "/tls-peer", [](httpserver::exchange& x) -> httpserver::task<void> {
            if (!x.tls()) throw std::runtime_error("missing TLS peer metadata");
            http::fields fields; fields.append("x-client-certificate", x.tls()->has_client_certificate ? "present" : "absent");
            fields.append("x-client-verified", x.tls()->client_certificate_verified ? "yes" : "no");
            fields.append("x-client-fingerprint", x.tls()->fingerprint_sha256);
            fields.append("x-peer-port", std::to_string(x.peer().port)); fields.append("x-connection", std::to_string(x.connection_id()));
            x.respond(http::status::from_code(204), fields); co_return;
        });
        server.route(http::method::known(http::method_id::get), "/meta", [](httpserver::exchange& x) -> httpserver::task<void> {
            if (!x.tls() || !x.peer().port || !x.connection_id() || x.head().request_protocol != http::protocol::http_3)
                throw std::runtime_error("missing native H3 metadata");
            http::fields fields;
            fields.append("x-protocol", "h3"); fields.append("x-peer", x.peer().address.to_string());
            x.respond(http::status::from_code(204), fields); co_return;
        });
        server.route(http::method::known(http::method_id::get), "/running", [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++running_entries;
            { std::unique_lock lock(held_mutex); held_changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; }); }
            if (!x.disconnected()) x.respond(http::status::from_code(204), {});
            co_return;
        });
        server.route(http::method::known(http::method_id::get), "/held", [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++application_entries; co_await application_release.wait();
            if (!x.disconnected()) x.respond(http::status::from_code(204), {});
        });
        server.route(http::method::known(http::method_id::get), "/held-meta", [&](httpserver::exchange& x) -> httpserver::task<void> {
            const auto copied = x.peer().port; const auto identity = x.connection_id();
            ++application_entries; co_await application_release.wait();
            if (!x.disconnected()) {
                if (x.peer().port != copied || x.connection_id() != identity || !x.tls()) throw std::runtime_error("copied exchange identity changed");
                http::fields fields; fields.append("x-peer-port", std::to_string(x.peer().port)); fields.append("x-connection", std::to_string(x.connection_id()));
                x.respond(http::status::from_code(204), fields);
            }
        });
        server.route(http::method::known(http::method_id::get), "/drain-self", [&](httpserver::exchange& x) -> httpserver::task<void> {
            srv::drain_ticket ticket; srv::drain_result result;
            if (!server.begin_drain(std::chrono::seconds(2), ticket).ok()) throw std::runtime_error("handler drain initiation");
            const auto waited = ticket.wait(result);
            if (waited.code() != http::outcome_code::would_deadlock) throw std::runtime_error("counted handler drain wait");
            http::fields fields; fields.append("x-wait", "would-deadlock"); x.respond(http::status::from_code(204), fields);
            co_return;
        });
        server.route(http::method::known(http::method_id::get), "/stop-self", [&](httpserver::exchange&) -> httpserver::task<void> {
            const auto began=std::chrono::steady_clock::now();server.request_stop();
            if (std::chrono::steady_clock::now()-began>std::chrono::milliseconds(100)) throw std::runtime_error("handler request_stop blocked");
            std::cout << "HANDLER_STOP_REQUESTED" << std::endl;co_return;
        });
        server.route(http::method::known(http::method_id::get), "/release", [&](httpserver::exchange& x) -> httpserver::task<void> {
            { std::lock_guard lock(held_mutex); released = true; }
            held_changed.notify_all(); x.respond(http::status::from_code(204), {}); co_return;
        });
        server.route(http::method::known(http::method_id::post), "/cancel", [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++cancelled_entries; x.admit_body({16384});
            std::array<std::byte, 64> buffer;
            while (!(co_await x.body().read_some(buffer)).end_of_body && !x.disconnected()) {}
        });
        server.route(http::method::known(http::method_id::get), "/counts", [&](httpserver::exchange& x) -> httpserver::task<void> {
            const auto body = std::to_string(received.load()) + " " + std::to_string(completed.load()) + " " +
                std::to_string(running_entries.load()) + " " + std::to_string(cancelled_entries.load());
            x.start_response(http::status::from_code(200), {});
            co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
        });
        server.route(http::method::known(http::method_id::get), "/hello", [](httpserver::exchange& x) -> httpserver::task<void> {
            http::fields fields; fields.append("x-native-h3", "authenticated-route");
            x.start_response(http::status::from_code(200), fields);
            const std::string body = "hello native h3\n";
            co_await x.writer().write(std::as_bytes(std::span(body)));
            http::fields trailers; trailers.append("x-end", "native-fin");
            co_await x.writer().finish(trailers);
        });
        auto large = std::make_shared<std::vector<std::byte>>(524288);
        for (std::size_t i = 0; i < large->size(); ++i) (*large)[i] = static_cast<std::byte>(i % 256);
        server.route(http::method::known(http::method_id::get), "/large", [&, large](httpserver::exchange& x) -> httpserver::task<void> {
            x.start_response(http::status::from_code(200), {});
            struct pending { std::atomic<unsigned>& count; ~pending() { --count; } } guard{pending_writes};
            ++pending_writes;
            const auto written = co_await x.writer().write(*large);
            if (written.status.ok()) co_await x.writer().finish();
        });
        for (const bool file : {false,true}) server.route(http::method::known(http::method_id::get),file ? "/owned-file-large" : "/borrowed-large",[&,large,file](httpserver::exchange& x)->httpserver::task<void> {
            struct pending { std::atomic<unsigned>& active;std::atomic<unsigned>& returned;
                pending(std::atomic<unsigned>& a,std::atomic<unsigned>& r):active(a),returned(r){++active;}
                ~pending(){--active;++returned;}
            } guard{pending_leases,lease_returns};
            httpserver::response_definition definition;
            if (file) {
                auto* handle=std::tmpfile();if (!handle) throw std::runtime_error("owned file fixture");
                if (std::fwrite(large->data(),1,large->size(),handle)!=large->size()) {std::fclose(handle);throw std::runtime_error("owned file bytes");}
                std::rewind(handle);
                if (!httpserver::response_definition::owned_file(http::status::from_code(200),{},handle,[&](std::FILE* f){std::fclose(f);++file_closed;},definition).ok()) throw std::runtime_error("owned file definition");
            } else {
                struct buffer { std::atomic<unsigned>& live;std::vector<std::byte> bytes;
                    buffer(std::atomic<unsigned>& count,const std::vector<std::byte>& value):live(count),bytes(value){++live;}
                    ~buffer(){--live;}
                };
                auto keeper=std::make_shared<buffer>(borrowed_live,*large);
                if (!httpserver::response_definition::borrowed(http::status::from_code(200),{},keeper->bytes,httpserver::body_lease(keeper),definition).ok()) throw std::runtime_error("borrowed definition");
            }
            co_await httpserver::send_definition(x,definition);
        });
        server.route(http::method::known(http::method_id::post), "/upload-hold", [&](httpserver::exchange& x) -> httpserver::task<void> {
            const auto cycle = upload_entries.fetch_add(1);
            if (cycle >= upload_release.size()) throw std::runtime_error("upload cycle bound");
            x.admit_body({16384}); co_await upload_release[cycle].wait();
            std::array<std::byte, 4096> into{}; std::vector<std::byte> body;
            for (;;) {
                const auto part = co_await x.body().read_some(into);
                if (!part.status.ok()) co_return;
                if (body.size() + part.data.size() > 131072) throw std::runtime_error("upload witness body bound");
                body.insert(body.end(), part.data.begin(), part.data.end()); if (part.end_of_body) break;
            }
            x.start_response(http::status::from_code(200), {});
            co_await x.writer().write(body); co_await x.writer().finish();
        });
        server.route(http::method::known(http::method_id::connect), "/ws/echo", [&](httpserver::exchange& x) -> httpserver::task<void> {
            struct returned { std::atomic<unsigned>& count; ~returned() { ++count; } } guard{ws_returns};
            httpserver::ws_upgrade_options upgrade;
            upgrade.subprotocols = {"harness.v1"}; upgrade.require_subprotocol = true;
            upgrade.limits = {8192, 16384, 16384, 4, 4};
            auto opened = co_await x.upgrade(upgrade);
            if (!opened.status.ok()) { x.respond(opened.rejection_status, opened.rejection_fields); co_return; }
            auto session = std::move(*opened.session);
            httpserver::resume_signal closed;
            session.on_close([&](auto) { closed.signal(); });
            for (;;) {
                auto received = co_await session.receive();
                if (!received.status.ok() || !received.value) { co_await closed.wait(); co_return; }
                for (;;) {
                    auto sent = session.try_send(received.value->kind, received.value->data);
                    if (sent.disposition == httpserver::websocket::send_disposition::accepted) break;
                    if (!sent.status.ok()) co_return;
                    struct pending { std::atomic<unsigned>& count;
                        explicit pending(std::atomic<unsigned>& value) : count(value) { ++count; }
                        ~pending() { --count; }
                    } waiting{ws_writable_pending};
                    if (!(co_await session.writable()).ok()) co_return;
                }
            }
        });
        const auto listened = server.listen();
        if (mode == "listener-storage-limit") {
            if (listened.code() != http::outcome_code::limit_exceeded || server.get_bound_port(0) || accepts.load()) throw std::runtime_error("listener storage not reserved before bind");
            std::cout << "LIMIT_H3_UNBOUND" << std::endl; return 0;
        }
        if (mode == "rollback" || mode == "rollback-reverse") {
            if (listened.ok() || server.get_bound_port(0) || server.get_bound_port(1) || accepts.load()) throw std::runtime_error("partial H3 bind retained");
            auto reusable = probe(mode == "rollback-reverse", rollback_port); reusable->stop();
            std::cout << "ROLLBACK_H3_UNBOUND " << mode << std::endl; return 0;
        }
        if (mode == "no-udp" || mode == "no-tls" || mode == "no-h3" || mode == "invalid-transport" || mode == "wrong-store") {
            if (listened.code() != http::outcome_code::invalid_argument || server.get_bound_port(0) || accepts.load()) throw std::runtime_error("H3 prebind validation");
            std::cout << "PREBIND_H3_REJECTED " << mode << std::endl; return 0;
        }
        if (!listened.ok()) throw std::runtime_error("H3 listen refused: " + std::to_string(static_cast<unsigned>(listened.code())));
        if (mode == "mixed" && server.get_bound_port(0) != server.get_bound_port(1)) throw std::runtime_error("mixed TCP/UDP identity");
        std::cout << server.get_bound_port(0) << std::endl;
        if (mode == "aggregate") std::cout << "PORTS " << server.get_bound_port(0) << " " << server.get_bound_port(1) << " " << server.get_bound_port(2) << " " << server.get_bound_port(3) << std::endl;
        std::atomic<bool> stopping{false};
        auto* driver = server.readiness();
        std::thread host_thread;
        if (driver) host_thread = std::thread([&] {
            while (!stopping.load()) {
                const auto snapshot = driver->interests();
                auto events = v3_host::wait(snapshot, std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
                if (!driver->dispatch(events, std::chrono::steady_clock::now()).ok()) break;
            }
        });
        std::string command;
        srv::drain_ticket aggregate_ticket;
        while (std::getline(std::cin, command) && command != "stop") {
            if (command == "entries") {
                std::cout << "ENTRIES " << application_entries.load() << std::endl;
            } else if (command == "admissions") {
                std::cout << "ADMISSIONS " << accepts.load() << " " << rejected.load() << " " << diagnostics.counters().events[static_cast<std::size_t>(srv::diagnostic_kind::tls_completed)] << " " << received.load() << std::endl;
            } else if (command == "flow-state") {
                std::cout << "FLOW " << upload_entries.load() << " " << pending_writes.load() << std::endl;
            } else if (command == "ws-state") {
                std::cout << "WS " << ws_writable_pending.load() << " " << ws_returns.load() << std::endl;
            } else if (command == "lease-state") {
                std::cout << "LEASES " << borrowed_live.load() << " " << file_closed.load() << " " << lease_returns.load() << " " << pending_leases.load() << std::endl;
            } else if (command == "resume-upload") {
                if (upload_release_index >= upload_release.size()) throw std::runtime_error("upload release bound");
                upload_release[upload_release_index++].signal(); std::cout << "UPLOAD_RESUMED " << upload_release_index << std::endl;
            } else if (command == "signal") {
                application_release.signal(); std::cout << "SIGNALED" << std::endl;
            } else if (command == "begin-clean" || command == "begin-expiry") {
                if (!server.begin_drain(command == "begin-clean" ? std::chrono::seconds(3) : std::chrono::milliseconds(250), aggregate_ticket).ok()) throw std::runtime_error("aggregate drain initiation");
                std::cout << "DRAIN_BEGUN" << std::endl;
            } else if (command == "wait-drain") {
                srv::drain_result result; if (!aggregate_ticket.wait(result).ok()) throw std::runtime_error("aggregate drain wait");
                std::cout << "DRAIN_RESULT " << static_cast<unsigned>(result.status) << " " << result.remaining << std::endl;
            } else if (command == "destroy-ticket") {
                application_release.signal(); stopping = true;
                if (host_thread.joinable()) host_thread.join();
                server_owner.reset();
                srv::drain_result result;
                if (!aggregate_ticket.wait(result).ok() || received.load() != completed.load()) throw std::runtime_error("retained aggregate ticket");
                std::cout << "RETAINED " << static_cast<unsigned>(result.status) << " " << result.remaining << std::endl;
                std::cerr << "H3_RETAINED " << received.load() << " " << completed.load() << std::endl;
                return 0;
            } else if (command == "drain") {
                srv::drain_ticket ticket;
                if (!server.begin_drain(std::chrono::milliseconds(250), ticket).ok()) throw std::runtime_error("public drain initiation");
                srv::drain_result result;
                if (!ticket.wait(result).ok()) throw std::runtime_error("public drain wait");
                std::cout << "DRAIN " << static_cast<unsigned>(result.status) << " " << result.remaining << std::endl;
            } else if (command == "rotate") {
                auto replacement = host; replacement.certificate_chain_pem = pem(directory + "/a-replacement.pem") + pem(directory + "/rotation-root.pem");
                replacement.private_key_pem = pem(directory + "/a-replacement-key.pem");
                if (!credentials->replace({{replacement}, 0}).ok()) throw std::runtime_error("H3 credential rotation");
                std::cout << "ROTATED" << std::endl;
            } else if (command == "invalid-replace") {
                if (credentials->replace({}).code() != http::outcome_code::invalid_argument) throw std::runtime_error("H3 invalid publication");
                std::cout << "PRESERVED" << std::endl;
            } else if (command == "acme") {
                srv::tls_acme_challenge challenge; challenge.host = "a.example";
                challenge.certificate_pem = pem(directory + "/acme.pem"); challenge.private_key_pem = pem(directory + "/acme-key.pem");
                challenge.key_authorization_sha256.fill(std::byte{0x42});
                challenge.expires_at = std::chrono::system_clock::now() + std::chrono::hours(1);
                if (!credentials->publish_acme(challenge).ok()) throw std::runtime_error("H3 ACME publication");
                std::cout << "ACME_PUBLISHED" << std::endl;
            } else if (command == "remove-acme") {
                if (!credentials->remove_acme("a.example").ok()) throw std::runtime_error("H3 ACME removal");
                std::cout << "ACME_REMOVED" << std::endl;
            } else if (command == "reject") {
                server.peer_policy().set_mode(srv::peer_policy_mode::reject_all); std::cout << "REJECTING" << std::endl;
            } else if (command == "counters") {
                const auto counts = diagnostics.counters();
                std::cout << "ACTIVE " << counts.active_connections << " " << counts.active_streams << std::endl;
            }
        }
        { std::lock_guard lock(held_mutex); released = true; }
        held_changed.notify_all();
        application_release.signal();
        if (mode == "destroy") {
            stopping = true; if (host_thread.joinable()) host_thread.join();
            server_owner.reset();
            if (received.load() != completed.load()) throw std::runtime_error("destruction missed stream completion");
            std::cerr << "H3_DESTROYED " << received.load() << " " << completed.load() << std::endl;
            return 0;
        }
        const auto stopping_at = std::chrono::steady_clock::now(); server.request_stop();
        if (std::chrono::steady_clock::now() - stopping_at > std::chrono::milliseconds(100)) throw std::runtime_error("request_stop blocked");
        stopping = true;
        if (host_thread.joinable()) host_thread.join();
        server.stop();
        if (driver) {
            if (!driver->interests().sockets.empty() || driver->interests().wake || driver->dispatch({}, std::chrono::steady_clock::now()).code() != http::outcome_code::invalid_state)
                throw std::runtime_error("inactive external driver");
            std::cerr << "INACTIVE_DRIVER_REJECTED" << std::endl;
        }
        if (mode == "policy") {
            const auto counters = diagnostics.counters();
            const auto tls = counters.events[static_cast<std::size_t>(srv::diagnostic_kind::tls_completed)];
            if (!rejected.load() || received.load() || tls) throw std::runtime_error("H3 policy admission hooks");
            std::cerr << "POLICY_REFUSAL " << rejected.load() << " " << received.load() << " " << tls << std::endl;
        }
        std::cerr << "H3_ACCEPT_TOTAL " << accepts.load() << " " << rejected.load() << std::endl;
        std::cerr << "H3_TLS_COMPLETED " << diagnostics.counters().events[static_cast<std::size_t>(srv::diagnostic_kind::tls_completed)] << std::endl;
        std::cerr << "H3_STOP " << received.load() << " " << completed.load() << std::endl;
        server_owner.reset();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl; return 1;
    }
}
