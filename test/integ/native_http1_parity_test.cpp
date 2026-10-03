/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-120: committed v2 expectations replayed through native sockets.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <future>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <httpserver/response_definition.hpp>
#include <httpserver/server/hooks.hpp>
#include <httpserver/server/route_sync.hpp>
#include <httpserver/server/server.hpp>

#include "./raw_http_client.hpp"
#include "../parity/normalize.hpp"
#include "./littletest.hpp"
#include "../unit/response_source_rig.hpp"

namespace {
namespace http = httpserver::http;
namespace srv = httpserver::server;

std::vector<std::byte> bytes(const std::string& text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return {first, first + text.size()};
}

srv::sync_response value(std::string body, bool icy = false) {
    srv::sync_response out;
    out.status = icy ? http::status::from_code(200).with_shoutcast()
                     : http::status::from_code(200);
    out.fields.append("Content-Type", "text/plain");
    out.body = bytes(body);
    return out;
}

srv::server_options options() {
    srv::server_options out;
    srv::listener_options listener;
    listener.address = "127.0.0.1";
    listener.port = 0;
    out.add_listener(listener);
    out.concurrency().workers = 4;
    return out;
}

class fixture {
 public:
    fixture() : server(options()) {
        const auto get = http::method::known(http::method_id::get);
        if (!server.route_sync(get, "/stream", [](const auto&, auto) {
            return value("OK", true);
        }, 1024).ok()) throw std::runtime_error("register stream");
        if (!server.route_sync(get, "/__smoke", [](const auto&, auto) {
            return value("smoke-ok");
        }, 1024).ok()) throw std::runtime_error("register smoke");
        httpserver::response_definition icy_definition;
        http::fields icy_fields;
        icy_fields.append("Content-Type", "text/plain");
        if (!httpserver::response_definition::owned_bytes(
                http::status::from_code(200).with_shoutcast(), icy_fields, bytes("OK"),
                icy_definition).ok()) throw std::runtime_error("define ICY");
        http::method_set get_head;
        get_head.set(http::method_id::get);
        get_head.set(http::method_id::head);
        if (!server.route(get_head,
                "/definition", [icy_definition](httpserver::exchange& x)
                    -> httpserver::task<void> {
            static_cast<void>(co_await httpserver::send_definition(x, icy_definition, {}));
        }).ok()) throw std::runtime_error("register definition");
        auto arrived = std::make_shared<std::atomic<int>>(0);
        if (!server.route(get, "/concurrent",
                [icy_definition, arrived](httpserver::exchange& x)
                    -> httpserver::task<void> {
            arrived->fetch_add(1);
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(5);
            while (arrived->load() < 2) {
                if (std::chrono::steady_clock::now() >= deadline) co_return;
                std::this_thread::yield();
            }
            static_cast<void>(co_await httpserver::send_definition(x, icy_definition, {}));
        }).ok()) throw std::runtime_error("register concurrent");
        if (!server.route_sync(get, "/hooked", [](const auto&, auto) {
            return value("OK");
        }, 1024).ok()) throw std::runtime_error("register hook route");
        server.hooks().add<srv::hook_phase::after_handler>(
            [](srv::after_handler_ctx& ctx) -> srv::hook_action {
                if (ctx.request.route_path == "/hooked") {
                    ctx.status = ctx.status.with_shoutcast();
                }
                return srv::hook_action::pass();
            }).detach();
        for (const auto& entry : std::vector<std::pair<std::string, std::string>>{
                 {"/file", "test_content"}, {"/empty_file", "test_content_empty"},
                 {"/missing", "no_such_file_120"}}) {
            httpserver::response_definition definition;
            http::fields fields;
            fields.append("Content-Type", "application/octet-stream");
            if (!httpserver::response_definition::reopen_file(
                    http::status::from_code(200), fields,
                    std::string(PARITY_DATA_ROOT) + "/" + entry.second,
                    definition).ok()) throw std::runtime_error("define file");
            if (!server.route(get, entry.first,
                    [definition](httpserver::exchange& x) -> httpserver::task<void> {
                const auto sent = co_await httpserver::send_definition(x, definition, {});
                if (!sent.status.ok()
                        && x.state() != httpserver::exchange_state::responded) {
                    co_await srv::detail::commit_sync_value(x, [] {
                        auto out = value("Internal Server Error");
                        out.status = http::status::from_code(500);
                        return out;
                    }());
                }
            }).ok()) throw std::runtime_error("register file");
        }
        for (const std::string route : {"/pipe", "/iovec", "/deferred"}) {
            if (!server.route(get, route, [route](httpserver::exchange& x)
                    -> httpserver::task<void> {
                httpserver::response_definition definition;
                http::fields fields;
                http::outcome made;
                if (route == "/pipe") {
                    httpserver_test::filled_pipe source("abcXYZ");
                    made = httpserver::response_definition::owned_pipe(
                        http::status::from_code(200), fields, source.release(), definition);
                } else {
                    // Factory pulls retain the two borrowed scatter/gather spans;
                    // each send owns its cursor. Deferred uses the same producer
                    // with unknown length, iovec declares its known total.
                    if (route == "/iovec") fields.append("Content-Length", "6");
                    made = httpserver::response_definition::factory(
                        http::status::from_code(200), fields, [] {
                            return httpserver::body_producer([part = 0]() mutable {
                                static const std::string pieces[] = {"abc", "XYZ"};
                                if (part == 2) return httpserver::body_chunk{
                                    http::outcome::okay(), {}, true};
                                const auto& text = pieces[part++];
                                return httpserver::body_chunk{http::outcome::okay(),
                                    httpserver_test::byte_span(text), false};
                            });
                        }, definition);
                }
                if (!made.ok()) co_return;
                static_cast<void>(co_await httpserver::send_definition(x, definition, {}));
            }).ok()) throw std::runtime_error("register body source");
        }
        auto keeper = std::make_shared<const std::string>("abcXYZ");
        httpserver::response_definition borrowed;
        if (!httpserver::response_definition::borrowed(
                http::status::from_code(200), {}, httpserver_test::byte_span(*keeper),
                httpserver::body_lease(keeper), borrowed).ok()) {
            throw std::runtime_error("define borrowed");
        }
        if (!server.route(get, "/borrowed", [borrowed](httpserver::exchange& x)
                -> httpserver::task<void> {
            static_cast<void>(co_await httpserver::send_definition(x, borrowed, {}));
        }).ok()) throw std::runtime_error("register borrowed");
        if (!server.listen().ok()) throw std::runtime_error("listen");
    }
    ~fixture() { server.stop(); }
    std::uint16_t port() const { return server.get_bound_port(0); }
    srv::native_server server;
};

std::string replay(const parity::tcase& test, raw_http::connection& client) {
    for (const auto& segment : test.sends) {
        if (!client.send(segment.bytes)) return "send failed";
    }
    std::deque<parity::observed_response> seen;
    if (!client.receive(1, seen)) return "response deadline";
    if (seen.size() != 1) return "unexpected extra response";
    const auto normalized = parity::normalize(seen.front());
    bool probe_keepalive = false;
    for (const auto& expected : test.expects) {
        if (expected.kind == parity::expect_kind::connection) {
            if (expected.value == "keep-alive") {
                if (client.peer_closed()) return "connection unexpectedly closed";
                probe_keepalive = true;
            } else {
                if (!client.receive_close(seen)) return "connection did not close";
            }
            continue;
        }
        const auto match = parity::check_expectation(expected, normalized,
                                                     PARITY_TRANSCRIPT_DIR);
        if (!match.ok) return test.name + ": " + match.diff;
    }
    if (probe_keepalive) {
        if (!client.send("GET /__smoke HTTP/1.1\r\nHost: localhost\r\n\r\n")) {
            return "keepalive probe send failed";
        }
        seen.clear();
        if (!client.receive(1, seen) || seen.size() != 1
                || seen.front().body != "smoke-ok"
                || seen.front().raw_status_line != "HTTP/1.1 200 OK") {
            return "keepalive/ordinary HTTP probe failed";
        }
    }
    return {};
}

parity::tcase required_case(const parity::transcript& corpus,
                           const std::string& name) {
    const auto found = std::find_if(corpus.cases.begin(), corpus.cases.end(),
        [&name](const auto& test) { return test.name == name; });
    if (found == corpus.cases.end()) {
        throw std::runtime_error("missing required parity case: " + name);
    }
    return *found;
}

std::string head_wire_until_close(std::uint16_t port) {
    namespace pollsys = raw_http::pollsys;
    struct socket_owner {
        pollsys::native_socket_t socket = pollsys::open_stream();
        ~socket_owner() { pollsys::close_socket(socket); }
    } peer;
    if (!pollsys::connect_loopback(peer.socket, port)) {
        throw std::runtime_error("raw HEAD connect failed");
    }
    const std::string request =
        "HEAD /definition HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    const auto sent = pollsys::write_some(peer.socket,
        reinterpret_cast<const std::byte*>(request.data()), request.size());
    if (sent.status != pollsys::sys_status::ok || sent.transferred != request.size()) {
        throw std::runtime_error("raw HEAD send failed");
    }
    if (!pollsys::set_nonblocking(peer.socket, true)) {
        throw std::runtime_error("raw HEAD nonblocking setup failed");
    }
    const auto deadline = std::chrono::steady_clock::now() + raw_http::kExchangeBudget;
    std::string wire;
    std::byte buffer[8192];
    while (std::chrono::steady_clock::now() < deadline) {
        const auto got = pollsys::read_some(peer.socket, buffer, sizeof buffer);
        if (got.status == pollsys::sys_status::closed_reset) return wire;
        if (got.status == pollsys::sys_status::ok && got.transferred > 0) {
            wire.append(reinterpret_cast<const char*>(buffer), got.transferred);
        } else if (got.status != pollsys::sys_status::would_block) {
            throw std::runtime_error("raw HEAD read failed");
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    throw std::runtime_error("raw HEAD close deadline; bytes=" + wire);
}
}  // namespace

LT_BEGIN_SUITE(native_http1_parity_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(native_http1_parity_suite)

LT_BEGIN_AUTO_TEST(native_http1_parity_suite, shoutcast_then_plain_same_connection)
    fixture server;
    raw_http::connection client;
    LT_CHECK(client.connect(server.port()));
    const auto corpus = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR) + "/shoutcast.tseq");
    for (const auto& test : corpus.cases) {
        const std::string failure = replay(test, client);
        if (!failure.empty()) std::cerr << failure << "\n";
        LT_CHECK(failure.empty());
    }
    // An additional response pins the ordinary status-line token explicitly.
    LT_CHECK(client.send("GET /__smoke HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    std::deque<parity::observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.front().raw_status_line, std::string("HTTP/1.1 200 OK"));
LT_END_AUTO_TEST(shoutcast_then_plain_same_connection)

LT_BEGIN_AUTO_TEST(native_http1_parity_suite, file_transcripts_use_real_sources)
    fixture server;
    raw_http::connection client;
    LT_CHECK(client.connect(server.port()));
    const auto corpus = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR) + "/file_resp.tseq");
    for (const auto& test : corpus.cases) {
        const std::string failure = replay(test, client);
        if (!failure.empty()) std::cerr << failure << "\n";
        LT_CHECK(failure.empty());
    }
LT_END_AUTO_TEST(file_transcripts_use_real_sources)

LT_BEGIN_AUTO_TEST(native_http1_parity_suite, immutable_icy_definition_concurrent_reuse)
    fixture server;
    auto call = [&server] {
        raw_http::connection client;
        if (!client.connect(server.port()) || !client.send(
                "GET /concurrent HTTP/1.1\r\nHost: localhost\r\n\r\n")) return false;
        std::deque<parity::observed_response> seen;
        return client.receive(1, seen) && seen.front().raw_status_line == "ICY 200 OK"
            && seen.front().body == "OK" && seen.front().framing == "content-length";
    };
    auto first = std::async(std::launch::async, call);
    auto second = std::async(std::launch::async, call);
    LT_CHECK(first.get());
    LT_CHECK(second.get());
LT_END_AUTO_TEST(immutable_icy_definition_concurrent_reuse)

LT_BEGIN_AUTO_TEST(native_http1_parity_suite, icy_http10_and_head_keep_framing)
    fixture server;
    raw_http::connection client;
    LT_CHECK(client.connect(server.port()));
    LT_CHECK(client.send("GET /definition HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
    std::deque<parity::observed_response> seen;
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), std::size_t{1});
    LT_CHECK_EQ(seen.front().raw_status_line, std::string("ICY 200 OK"));
    LT_CHECK_EQ(seen.front().body, std::string("OK"));
    seen.clear();
    client.set_head_only(true);
    LT_CHECK(client.send("HEAD /definition HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    LT_CHECK(client.receive(1, seen));
    LT_CHECK_EQ(seen.size(), std::size_t{1});
    LT_CHECK_EQ(seen.front().raw_status_line, std::string("ICY 200 OK"));
    LT_CHECK(seen.front().body.empty());
    const auto head = parity::normalize(seen.front());
    LT_CHECK(parity::find_header(head, "Content-Length") != nullptr);
    LT_CHECK_EQ(parity::find_header(head, "Content-Length")->value, std::string("2"));

    // Read through the requested close: headers-only parsing would discard
    // evidence of a payload arriving with the headers or in a later read.
    const auto wire = head_wire_until_close(server.port());
    LT_CHECK(wire.starts_with("ICY 200 OK\r\n"));
    const auto header_end = wire.find("\r\n\r\n");
    LT_ASSERT(header_end != std::string::npos);
    LT_CHECK_EQ(header_end + 4, wire.size());
LT_END_AUTO_TEST(icy_http10_and_head_keep_framing)

LT_BEGIN_AUTO_TEST(native_http1_parity_suite, leased_borrowed_body_matches_v2_iovec_pin)
    fixture server;
    raw_http::connection client;
    LT_CHECK(client.connect(server.port()));
    const auto corpus = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR) + "/file_resp.tseq");
    auto test = required_case(corpus, "iovec_body");
    test.sends.at(0).bytes = "GET /borrowed HTTP/1.1\r\n";
    const auto failure = replay(test, client);
    if (!failure.empty()) std::cerr << failure << "\n";
    LT_CHECK(failure.empty());
LT_END_AUTO_TEST(leased_borrowed_body_matches_v2_iovec_pin)

LT_BEGIN_AUTO_TEST(native_http1_parity_suite, after_handler_can_select_fixed_icy_metadata)
    fixture server;
    raw_http::connection client;
    LT_CHECK(client.connect(server.port()));
    const auto corpus = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR) + "/shoutcast.tseq");
    auto test = required_case(corpus, "icy_status_line");
    test.sends.at(0).bytes = "GET /hooked HTTP/1.1\r\n";
    const auto failure = replay(test, client);
    if (!failure.empty()) std::cerr << failure << "\n";
    LT_CHECK(failure.empty());
LT_END_AUTO_TEST(after_handler_can_select_fixed_icy_metadata)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
