/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/drain_scope.hpp>
#include <httpserver/detail/tls_socket_backend.hpp>
#include <parity/normalize.hpp>
#include <parity/v3_dispatch_replay.hpp>
#include <parity/v3_behavior_profiles.hpp>
#include <parity/behavior_replay.hpp>
#include <parity/behavior_observation.hpp>
#include "./digest_client.hpp"

#include "../unit/tls_policy_tcp_fixture.hpp"
#include "../unit/package_backend_witness.hpp"
#include "./littletest.hpp"

namespace {
namespace hd = httpserver::detail;
namespace http = httpserver::http;
namespace srv = httpserver::server;
using clock_type = std::chrono::steady_clock;
using std::chrono_literals::operator""s;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// Real TCP socket BIO peer, independent of the native TLS byte-stream pump.
// Only TLS completions use the manual executor; native HTTP tasks use the pool.
struct fixture {
    httpserver::manual_executor ex;
    hd::io_connection_owner owner{ex};
    hd::worker_pool pool{2};
    std::unique_ptr<hd::io_socket_backend> raw_storage;
    hd::io_socket_backend& raw;
    io_loopback::listener listener = io_loopback::listener::open();
    tcp_policy::socket_owner peer_socket;
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context{SSL_CTX_new(TLS_client_method()), SSL_CTX_free};
    std::unique_ptr<SSL, decltype(&SSL_free)> peer{nullptr, SSL_free};
    srv::resource_budget budget = srv::resource_budget::root(srv::budget_limits{});
    parity::behavior_profile profile{budget};
    srv::route_registry& routes = profile.routes;
    srv::hook_bus& hooks = profile.hooks;
    httpserver::net::peer_address peer_address;
    hd::drain_scope scope;
    std::unique_ptr<hd::tls_socket_backend> transport;
    std::shared_ptr<hd::connection_engine> engine;
    std::atomic<bool> stopped{false};
    bool external;
    bool encrypted;
    bool peer_closed = false;
    bool poll_oracle;

    explicit fixture(bool external_mode, const std::string& profile_name = "routing_basic", bool tls = true, bool poll = false)
        : raw_storage(poll ? std::make_unique<hd::io_poll_backend>() :
              hd::make_socket_backend(external_mode ? srv::loop_mode::external : srv::loop_mode::managed)),
          raw(*raw_storage), external(external_mode), encrypted(tls), poll_oracle(poll) {
        if (!external && !poll_oracle) {
            std::cout << "PACKAGE_BACKEND " << package_test::native_backend(raw) << '\n';
        }
        require(listener.ok(), "TCP listener");
        profile.install(profile_name, PARITY_DATA_ROOT);
        if (external) raw.activate_external();
        peer_socket.value = io_loopback::connect_to(listener.port());
        require(peer_socket.value != hd::pollsys::k_invalid_socket, "TCP connect");
        tcp_policy::socket_owner accepted;
        require(wait([&] { hd::pollsys::accept_one(listener.socket(), &accepted.value, &peer_address); },
                     [&] { return accepted.value != hd::pollsys::k_invalid_socket; }), "TCP accept");
        raw.adopt_connection(1, std::exchange(accepted.value, hd::pollsys::k_invalid_socket));
        require(hd::pollsys::set_nonblocking(peer_socket.value, true), "peer nonblocking");
        if (encrypted) {
            peer.reset(SSL_new(context.get()));
            require(peer && SSL_set_fd(peer.get(), static_cast<int>(peer_socket.value)) == 1, "TLS peer");
            transport = std::make_unique<hd::tls_socket_backend>(raw, ex, 1,
                hd::tls_context::server_pem(tls_test::pem("cert.pem"), tls_test::pem("key.pem")),
                hd::tls_handshake_context{hd::tls_transport::tcp, listener.port()});
        }
    }
    ~fixture() {
        if (engine) engine->shutdown();
        if (transport) transport->close();
        raw.close();
        if (engine && !wait([] {}, [&] { return stopped.load(); })) {
            std::terminate();
        }
        ex.run_pending();
        pool.drain_and_join();
        // Pool tasks can enqueue a final TLS close; retire it before teardown.
        ex.run_pending();
        engine.reset();
        transport.reset();
        ex.run_pending();
    }
    void tick() {
        ex.run_pending();
        if (external) {
            tcp_policy::dispatch(transport ? static_cast<hd::io_socket_backend&>(*transport) : raw);
        } else {
            std::this_thread::yield();
        }
    }
    template<class Step, class Done> bool wait(Step step, Done done) {
        const auto deadline = clock_type::now() + 5s;
        while (!done() && clock_type::now() < deadline) {
            step();
            tick();
        }
        ex.run_pending();
        return done();
    }
    bool classify(int rc) {
        if (rc == 1) return true;
        const auto error = SSL_get_error(peer.get(), rc);
        require(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE, "TLS peer operation");
        return false;
    }
    void start(hd::connection_engine_config config = {}) {
        if (encrypted) {
            hd::tls_handshake_operation handshake(owner, 1, clock_type::now() + 5s);
            handshake.submit(*transport);
            bool connected = false;
            require(wait([&] {
                if (!connected) {
                    ERR_clear_error();
                    connected = classify(SSL_connect(peer.get()));
                }
            }, [&] { return connected && handshake.state()->applied(); }), "TLS handshake deadline");
            require(handshake.state()->stored_result().code == http::outcome_code::ok, "TLS handshake result");
        }
        config.pages = std::make_shared<const hd::error_page_factories>(profile.pages);
        auto& backend = transport ? static_cast<hd::io_socket_backend&>(*transport) : raw;
        engine = std::make_shared<hd::connection_engine>(backend, pool, routes, hooks, budget, scope,
            std::move(config), 1, [this] { stopped = true; }, peer_address, profile.context().peers);
        engine->start();
    }
    void send(const std::string& wire) {
        if (!encrypted) {
            std::size_t sent = 0;
            require(wait([&] {
                const auto result = hd::pollsys::write_some(peer_socket.value, reinterpret_cast<const std::byte*>(wire.data()) + sent, wire.size() - sent);
                if (result.status == hd::pollsys::sys_status::ok) sent += result.transferred;
                else require(result.status == hd::pollsys::sys_status::would_block, "cleartext peer write");
            }, [&] { return sent == wire.size(); }), "cleartext send deadline");
            return;
        }
        bool sent = false;
        require(wait([&] {
            if (!sent) {
                std::size_t count = 0;
                ERR_clear_error();
                sent = classify(SSL_write_ex(peer.get(), wire.data(), wire.size(), &count));
                if (sent) require(count == wire.size(), "partial peer write");
            }
        }, [&] { return sent; }), "TLS send deadline");
    }
    std::string read_once() {
        std::array<char, 8192> bytes{};
        std::size_t count = 0;
        if (!encrypted) {
            const auto result = hd::pollsys::read_some(peer_socket.value, reinterpret_cast<std::byte*>(bytes.data()), bytes.size());
            if (result.status == hd::pollsys::sys_status::closed_reset) peer_closed = true;
            else if (result.status == hd::pollsys::sys_status::ok) count = result.transferred;
            else require(result.status == hd::pollsys::sys_status::would_block, "cleartext peer read");
        } else {
            ERR_clear_error();
            const auto rc = SSL_read_ex(peer.get(), bytes.data(), bytes.size(), &count);
            if (rc != 1) {
                const auto error = SSL_get_error(peer.get(), rc);
                if (error == SSL_ERROR_ZERO_RETURN || (error == SSL_ERROR_SYSCALL && rc == 0) ||
                        (error == SSL_ERROR_SSL && ERR_GET_REASON(ERR_peek_last_error()) == SSL_R_UNEXPECTED_EOF_WHILE_READING))
                    peer_closed = true;
                else require(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE, "TLS peer read");
            }
        }
        return std::string(bytes.data(), count);
    }
    std::vector<parity::observed_response> receive(std::size_t expected, bool head = false) {
        parity::response_frame_parser parser;
        parser.set_head_only(head);
        std::vector<parity::observed_response> responses;
        std::string observed_wire;
        const bool delivered = wait([&] {
            const auto bytes = read_once();
            observed_wire += bytes;
            for (auto& response : parser.feed(bytes)) responses.push_back(std::move(response));
            if (peer_closed) {
                const bool delimited = parser.pending_until_close();
                for (auto& response : parser.finish()) {
                    if (delimited) response.framing = "close-delimited";
                    responses.push_back(std::move(response));
                }
            }
            require(!parser.failed(), "response framing");
        }, [&] { return responses.size() >= expected; });
        if (!delivered) throw std::runtime_error("TLS response deadline; head=" + std::to_string(head) + "; wire=" + observed_wire);
        require(responses.size() == expected, "extra response");
        return responses;
    }
    void stop() {
        engine->shutdown();
        require(wait([] {}, [&] { return stopped.load(); }), "native engine retirement");
        require(scope.active() == 0, "drain units retired");
        require(raw.native_handle(1) == hd::pollsys::k_invalid_socket, "socket released");
        if (encrypted) std::cout << "PACKAGE_TLS " << (external ? "external" : poll_oracle ? "poll-oracle" : "managed") << '\n';
    }
};

void emit_observation(const fixture& f, const parity::tcase& test, const std::string& corpus, bool http10,
                      const std::vector<parity::observed_response>& responses, const parity::observed_response* followup = nullptr) {
    const std::string mode = std::string(http10 ? "h1.0" : "h1.1") + (f.encrypted ? "-tls" : "-tcp");
    const std::string identity = corpus + ".tseq:" + test.name;
    const auto connection = std::to_string(f.listener.port());
    std::cout << (f.external || f.poll_oracle ? "PACKAGE_OBSERVATION" : "TASK179_OBSERVATION") << " {\"driver\":\""
              << (f.external ? "external" : f.poll_oracle ? "poll-oracle" : "managed") << "\",\"id\":";
    parity::behavior_json_text(std::cout, mode + ":" + identity);
    std::cout << ",\"case\":";
    parity::behavior_json_text(std::cout, identity);
    std::cout << ",\"command\":\"h1-corpus\",\"mode\":";
    parity::behavior_json_text(std::cout, mode);
    std::cout << ",\"kind\":";
    parity::behavior_json_text(std::cout, f.encrypted ? "tls" : "tcp");
    std::cout << ",\"connection\":";
    parity::behavior_json_text(std::cout, connection);
    std::cout << ",\"closed\":" << (f.peer_closed ? "true" : "false") << ",\"responses\":[";
    for (std::size_t i = 0; i < responses.size(); ++i) {
        if (i) std::cout << ',';
        parity::behavior_json_response(std::cout, responses[i]);
    }
    std::cout << "],\"followup\":";
    if (followup) {
        std::cout << "{\"connection\":";
        parity::behavior_json_text(std::cout, connection);
        std::cout << ",\"status\":" << followup->status << '}';
    } else {
        std::cout << "null";
    }
    if (corpus == "websocket") std::cout << ",\"websocket\":{\"echo_hex\":\"706172697479\",\"clean_close\":true}";
    std::cout << "}\n";
}

void check_case(fixture& f, const parity::tcase& test, bool http10, const std::string& corpus = {}) {
    std::string authorization;
    if (test.curl_auth == "digest") {
        f.send("GET /digest HTTP/1.1\r\nHost: localhost\r\n\r\n");
        const auto challenge_response = parity::normalize(f.receive(1).front());
        const auto* offered = parity::find_header(challenge_response, "WWW-Authenticate");
        require(offered && challenge_response.status == 401, "live TLS Digest challenge");
        const auto challenge = httpserver_test::parse_www_authenticate(offered->value);
        require(challenge.has_value(), "TLS Digest challenge parse");
        const std::string cnonce = httpserver_test::make_cnonce();
        const auto digest = httpserver_test::compute_response_cleartext(*challenge, httpserver_test::digest_hash::md5,
            "GET", "/digest", "bob", "builder", cnonce, "00000001");
        authorization = "Authorization: " + httpserver_test::build_authorization_header(
            *challenge, "bob", "/digest", cnonce, "00000001", digest) + "\r\n";
    }
    bool head = false;
    std::string original;
    for (const auto& segment : test.sends) original += segment.bytes;
    std::vector<std::pair<std::size_t, std::size_t>> heads;
    std::size_t start = 0;
    for (const auto& request : parity::behavior_requests(test)) {
        const auto end = original.find("\r\n\r\n", start) + 4;
        heads.push_back({start, end});
        start = end + request.body.size();
    }
    std::size_t offset = 0;
    for (const auto& segment : test.sends) {
        std::string wire = segment.bytes;
        if (wire.starts_with("GET /digest ")) wire += authorization;
        head = head || wire.starts_with("HEAD ");
        if (test.transport == "curl" && wire == "\r\n") wire = "Host: localhost\r\n\r\n";
        const bool header_segment = std::any_of(heads.begin(), heads.end(), [&](const auto& range) {
            return offset >= range.first && offset < range.second;
        });
        if (http10 && header_segment) {
            const auto version = wire.find("HTTP/1.1");
            if (version != std::string::npos) wire.replace(version, 8, "HTTP/1.0");
            // The committed corpus is HTTP/1.1. HTTP/1.0 needs explicit persistence.
            const auto end = wire.find("\r\n\r\n");
            if (end != std::string::npos) wire.insert(end + 2, "Connection: keep-alive\r\n");
            else if (wire == "\r\n") wire = "Connection: keep-alive\r\n\r\n";
        }
        offset += segment.bytes.size();
        f.send(wire);
    }
    const auto count = std::count_if(test.expects.begin(), test.expects.end(), [](const auto& e) {
        return e.kind == parity::expect_kind::status || e.kind == parity::expect_kind::status_line;
    });
    auto responses = f.receive(static_cast<std::size_t>(count), head);
    std::size_t index = 0;
    bool first = true;
    const bool close_delimited = http10 && responses.front().framing == "close-delimited";
    for (auto expected : test.expects) {
        if (expected.kind == parity::expect_kind::status || expected.kind == parity::expect_kind::status_line) {
            if (!first) ++index;
            first = false;
        }
        if (expected.kind == parity::expect_kind::connection) {
            require(expected.value == "keep-alive", "unexpected corpus close expectation");
            continue;
        }
        if (close_delimited && expected.kind == parity::expect_kind::header && parity::field_lower(expected.name) == "transfer-encoding") {
            require(!parity::find_header(parity::normalize(responses.at(index)), "Transfer-Encoding"), "HTTP/1.0 prohibited chunking");
            continue;
        }
        if (close_delimited && expected.kind == parity::expect_kind::framing) expected.value = "close-delimited";
        const auto match = parity::check_expectation(expected, parity::normalize(responses.at(index)), PARITY_TRANSCRIPT_DIR);
        if (!match.ok) throw std::runtime_error(test.name + ": " + match.diff);
    }
    // An observed second response proves persistence and catches HEAD payload leakage.
    if (close_delimited) {
        require(f.peer_closed, "HTTP/1.0 close-delimited EOF");
        if (!corpus.empty()) emit_observation(f, test, corpus, http10, responses);
    } else {
        f.send(http10 ? "GET /__smoke HTTP/1.0\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"
                      : "GET /__smoke HTTP/1.1\r\nHost: localhost\r\n\r\n");
        const auto followup = f.receive(1);
        require(followup.front().status == 200 && followup.front().body == "smoke-ok", "persistent followup");
        if (!corpus.empty()) emit_observation(f, test, corpus, http10, responses, &followup.front());
    }
    std::cout << "observed TCP" << (f.encrypted ? "/TLS" : "") << " HTTP/1." << (http10 ? '0' : '1') << " " << test.name << "\n";
}

void corpus(bool external, bool http10) {
    fixture f(external);
    f.start();
    for (const char* name : {"routing", "tls"}) {
        const auto transcript = parity::parse_transcript_file(std::string(PARITY_TRANSCRIPT_DIR) + "/" + name + ".tseq");
        for (const auto& test : transcript.cases) check_case(f, test, http10);
    }
    f.stop();
}
void profiles(bool external, bool http10, bool encrypted = true, bool poll = false) {
    for (const char* name : {"routing", "hooks", "auth_basic", "auth_digest", "forms", "file_resp", "ip_controls", "tls", "shoutcast"}) {
        if (!encrypted && std::string_view(name) == "tls") continue;
        const auto transcript = parity::parse_transcript_file(std::string(PARITY_TRANSCRIPT_DIR) + "/" + name + ".tseq");
        if (http10) {
            for (const auto& test : transcript.cases) {
                fixture f(external, transcript.profile, encrypted, poll);
                f.start();
                check_case(f, test, true, name);
                f.stop();
            }
        } else {
            fixture f(external, transcript.profile, encrypted, poll);
            f.start();
            for (const auto& test : transcript.cases) check_case(f, test, false, name);
            f.stop();
        }
    }
}
}  // namespace

LT_BEGIN_SUITE(native_http1_tls_parity_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(native_http1_tls_parity_suite)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, http11_managed_corpus)
    corpus(false, false);
    LT_CHECK(true);
LT_END_AUTO_TEST(http11_managed_corpus)
LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, http10_managed_corpus)
    corpus(false, true);
    LT_CHECK(true);
LT_END_AUTO_TEST(http10_managed_corpus)
LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, http11_external_corpus)
    corpus(true, false);
    LT_CHECK(true);
LT_END_AUTO_TEST(http11_external_corpus)
LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, http10_external_corpus)
    corpus(true, true);
    LT_CHECK(true);
LT_END_AUTO_TEST(http10_external_corpus)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http11_managed)
    profiles(false, false);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http11_managed)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http10_cleartext)
    profiles(false, true, false);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http10_cleartext)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http10_tls)
    profiles(false, true);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http10_tls)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http11_cleartext)
    profiles(false, false, false);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http11_cleartext)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http11_external)
    profiles(true, false);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http11_external)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http10_external)
    profiles(true, true);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http10_external)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http11_poll_oracle)
    profiles(false, false, true, true);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http11_poll_oracle)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http10_poll_oracle)
    profiles(false, true, true, true);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http10_poll_oracle)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http11_external_cleartext)
    profiles(true, false, false);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http11_external_cleartext)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, all_profiles_http10_external_cleartext)
    profiles(true, true, false);
    LT_CHECK(true);
LT_END_AUTO_TEST(all_profiles_http10_external_cleartext)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, websocket_fixed_key_and_echo_over_cleartext_and_tls)
    for (const bool encrypted : {false, true}) {
        fixture f(false, "websocket", encrypted);
        f.start();
        const auto transcript = parity::parse_transcript_file(std::string(PARITY_TRANSCRIPT_DIR) + "/websocket.tseq");
        for (const auto& segment : transcript.cases.front().sends) f.send(segment.bytes);
        const auto responses = f.receive(1);
        const auto response = parity::normalize(responses.front());
        for (const auto& expected : transcript.cases.front().expects) {
            if (expected.kind == parity::expect_kind::connection) continue;
            const auto result = parity::check_expectation(expected, response, PARITY_TRANSCRIPT_DIR);
            if (!result.ok) throw std::runtime_error(result.diff);
        }
        // Fixed independent RFC 6455 client frames: mask 01020304, text "parity", close 1000.
        f.send(std::string("\x81\x86\x01\x02\x03\x04\x71\x63\x71\x6d\x75\x7b", 12));
        std::string wire;
        require(f.wait([&] { wire += f.read_once(); }, [&] { return wire.size() >= 8; }), "WebSocket echo deadline");
        require(wire == std::string("\x81\x06parity", 8), "WebSocket echo bytes");
        f.send(std::string("\x88\x82\x01\x02\x03\x04\x02\xea", 8));
        require(f.wait([&] { wire += f.read_once(); }, [&] { return f.peer_closed; }), "WebSocket close EOF");
        require(wire == std::string("\x81\x06parity\x88\x02\x03\xe8", 12), "WebSocket close bytes");
        emit_observation(f, transcript.cases.front(), "websocket", false, responses);
        f.stop();
    }
    LT_CHECK(true);
LT_END_AUTO_TEST(websocket_fixed_key_and_echo_over_cleartext_and_tls)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, runtime_policy_refuses_next_request_without_application_bytes)
    for (const bool encrypted : {false, true}) {
        for (const bool http10 : {false, true}) {
            fixture f(false, "ip_controls", encrypted);
            f.start();
            const std::string version = http10 ? "HTTP/1.0" : "HTTP/1.1";
            const auto send = [&](std::string_view path) {
                f.send("GET " + std::string(path) + " " + version + "\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n");
            };
            send("/__smoke");
            require(f.receive(1).front().status == 200, "allow-listed peer");
            send("/__deny");
            require(f.receive(1).front().status == 204, "runtime policy mutation");
            send("/__smoke");
            std::string wire;
            require(f.wait([&] { wire += f.read_once(); }, [&] { return f.peer_closed; }), "refusal EOF");
            require(wire.empty(), "refusal emitted application bytes");
            const std::string mode = std::string(http10 ? "h1.0" : "h1.1") + (encrypted ? "-tls" : "-tcp");
            std::cout << "TASK179_SUPPORTING {\"id\":\"" << mode << ":runtime-policy\",\"command\":\"h1-corpus\","
                      << "\"suite\":\"behavior-wire\",\"name\":\"runtime_deny_refuses_next_request\",\"mode\":\"" << mode
                      << "\",\"kind\":\"" << (encrypted ? "tls" : "tcp") << "\",\"witness\":{\"allow_status\":200,\"mutation_status\":204,"
                      << "\"refused\":true,\"application_bytes\":0,\"connection\":\"" << f.listener.port() << "\"}}\n";
            f.stop();
        }
    }
    LT_CHECK(true);
LT_END_AUTO_TEST(runtime_policy_refuses_next_request_without_application_bytes)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, cancelling_plaintext_read_keeps_established_tls_usable)
    fixture f(false);
    hd::tls_handshake_operation handshake(f.owner, 1, clock_type::now() + 5s);
    handshake.submit(*f.transport);
    bool connected = false;
    require(f.wait([&] {
        if (!connected) {
            ERR_clear_error();
            connected = f.classify(SSL_connect(f.peer.get()));
        }
    }, [&] { return connected && handshake.state()->applied(); }), "standalone TLS handshake");
    std::array<std::byte, 64> bytes{};
    hd::read_operation pending(f.owner, 1, bytes);
    pending.submit(*f.transport);
    f.tick();
    LT_CHECK(f.transport->request_cancel(*pending.state()) == http::outcome_code::ok);
    LT_ASSERT(f.wait([] {}, [&] { return pending.state()->applied(); }));
    LT_CHECK(pending.state()->stored_result().code == http::outcome_code::cancelled);
    f.send("after-cancel");
    hd::read_operation resumed(f.owner, 1, bytes);
    resumed.submit(*f.transport);
    LT_ASSERT(f.wait([] {}, [&] { return resumed.state()->applied(); }));
    LT_CHECK(resumed.state()->stored_result().code == http::outcome_code::ok);
    LT_CHECK_EQ(std::string(reinterpret_cast<const char*>(bytes.data()), resumed.state()->stored_result().transferred), std::string("after-cancel"));
    hd::read_operation second(f.owner, 1, bytes);
    second.submit(*f.transport);
    f.tick();
    hd::cancel_operation cancel(f.owner, 1, second);
    cancel.submit(*f.transport);
    LT_ASSERT(f.wait([] {}, [&] { return second.state()->applied() && cancel.state()->applied(); }));
    LT_CHECK(second.state()->stored_result().code == http::outcome_code::cancelled);
    LT_CHECK(cancel.state()->stored_result().code == http::outcome_code::ok);
    f.send("again");
    hd::read_operation third(f.owner, 1, bytes);
    third.submit(*f.transport);
    LT_ASSERT(f.wait([] {}, [&] { return third.state()->applied(); }));
    LT_CHECK(third.state()->stored_result().code == http::outcome_code::ok);
    LT_CHECK_EQ(std::string(reinterpret_cast<const char*>(bytes.data()), third.state()->stored_result().transferred), std::string("again"));
LT_END_AUTO_TEST(cancelling_plaintext_read_keeps_established_tls_usable)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, timer_cancellation_does_not_abort_tls)
    fixture f(true);
    f.start();
    hd::timer_operation first(f.owner, 1, clock_type::now() + 5s);
    first.submit(*f.transport);
    LT_CHECK(f.transport->request_cancel(*first.state()) == http::outcome_code::ok);
    LT_ASSERT(f.wait([] {}, [&] { return first.state()->applied(); }));
    LT_CHECK(first.state()->stored_result().code == http::outcome_code::cancelled);
    hd::timer_operation second(f.owner, 1, clock_type::now() + 5s);
    second.submit(*f.transport);
    hd::cancel_operation cancel(f.owner, 1, second);
    cancel.submit(*f.transport);
    LT_ASSERT(f.wait([] {}, [&] { return second.state()->applied() && cancel.state()->applied(); }));
    LT_CHECK(cancel.state()->stored_result().code == http::outcome_code::ok);
    LT_CHECK(second.state()->stored_result().code == http::outcome_code::cancelled);
    hd::wake_operation wake(f.owner, 1);
    wake.submit(*f.transport);
    f.transport->wake();
    LT_ASSERT(f.wait([] {}, [&] { return wake.state()->applied(); }));
    LT_CHECK(wake.state()->stored_result().code == http::outcome_code::ok);
    f.send("GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");
    LT_CHECK_EQ(f.receive(1).front().body, std::string("OK"));
    f.stop();
LT_END_AUTO_TEST(timer_cancellation_does_not_abort_tls)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, watchdog_releases_parked_encrypted_read)
    fixture f(false);
    hd::connection_engine_config config;
    config.timeouts.header = std::chrono::milliseconds(30);
    f.start(config);
    LT_ASSERT(f.wait([] {}, [&] { return f.stopped.load(); }));
    LT_CHECK_EQ(f.scope.active(), std::size_t{0});
    LT_CHECK(f.raw.native_handle(1) == hd::pollsys::k_invalid_socket);
LT_END_AUTO_TEST(watchdog_releases_parked_encrypted_read)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, handshake_cancellation_and_shared_socket_ownership)
    fixture f(true);
    auto other = io_loopback::pair::make();
    LT_ASSERT(other.ok());
    f.raw.adopt_connection(2, other.detach_local());
    hd::tls_handshake_operation handshake(f.owner, 1, clock_type::now() + 5s);
    handshake.submit(*f.transport);
    f.tick();
    hd::cancel_operation cancel(f.owner, 1, handshake);
    cancel.submit(*f.transport);
    LT_ASSERT(f.wait([] {}, [&] { return handshake.state()->applied() && cancel.state()->applied(); }));
    LT_CHECK(handshake.state()->stored_result().code == http::outcome_code::cancelled);
    LT_CHECK(cancel.state()->stored_result().code == http::outcome_code::ok);
    LT_CHECK_EQ(f.transport->close(), std::size_t{1});
    LT_CHECK_EQ(f.transport->close(), std::size_t{0});
    LT_CHECK(f.raw.native_handle(1) == hd::pollsys::k_invalid_socket);
    LT_CHECK(f.raw.native_handle(2) != hd::pollsys::k_invalid_socket);
    std::array<std::byte, 1> received{};
    hd::read_operation read(f.owner, 2, received);
    read.submit(f.raw);
    const std::array<std::byte, 1> data{std::byte{'x'}};
    LT_CHECK(hd::pollsys::write_some(other.peer(), data.data(), data.size()).status == hd::pollsys::sys_status::ok);
    LT_ASSERT(f.wait([] {}, [&] { return read.state()->applied(); }));
    LT_CHECK(read.state()->stored_result().code == http::outcome_code::ok);
    LT_CHECK(received == data);
    hd::wake_operation after_close(f.owner, 1);
    after_close.submit(*f.transport);
    LT_ASSERT(f.wait([] {}, [&] { return after_close.state()->applied(); }));
    LT_CHECK(after_close.state()->stored_result().code == http::outcome_code::connection_closed);
LT_END_AUTO_TEST(handshake_cancellation_and_shared_socket_ownership)

LT_BEGIN_AUTO_TEST(native_http1_tls_parity_suite, foreign_operations_cannot_touch_shared_driver_connection)
    fixture f(true);
    auto other = io_loopback::pair::make();
    LT_ASSERT(other.ok());
    f.raw.adopt_connection(2, other.detach_local());
    hd::timer_operation foreign(f.owner, 2, clock_type::now() + 5s);
    foreign.submit(f.raw);
    hd::cancel_operation cancel(f.owner, 1, foreign);
    cancel.submit(*f.transport);
    LT_ASSERT(f.wait([] {}, [&] { return cancel.state()->applied(); }));
    LT_CHECK(cancel.state()->stored_result().code == http::outcome_code::invalid_argument);
    LT_CHECK(!foreign.state()->is_terminal());
    f.raw.request_cancel(*foreign.state());
    LT_ASSERT(f.wait([] {}, [&] { return foreign.state()->applied(); }));
    std::array<std::byte, 1> bytes{};
    hd::read_operation read(f.owner, 2, bytes);
    read.submit(*f.transport);
    LT_ASSERT(f.wait([] {}, [&] { return read.state()->applied(); }));
    LT_CHECK(read.state()->stored_result().code == http::outcome_code::invalid_argument);
LT_END_AUTO_TEST(foreign_operations_cannot_touch_shared_driver_connection)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
