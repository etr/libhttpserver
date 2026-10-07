/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef TEST_UNIT_TLS_IO_FIXTURE_HPP_
#define TEST_UNIT_TLS_IO_FIXTURE_HPP_
#include <algorithm>
#include <cstddef>
#include <deque>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <httpserver/detail/fake_io_backend.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/tls_io_backend.hpp>
namespace tls_test {
namespace hd = httpserver::detail;
inline std::string pem(const char* name) {
    std::ifstream file(std::string(TLS_TEST_DIR) + "/" + name);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
// Deterministic full-duplex raw stream. SSL/BIO code is real; only transport
// completions are scripted. Fragmentation includes partial ciphertext writes.
struct pair {
    httpserver::manual_executor ex;
    hd::io_connection_owner owner{ex};
    hd::fake_io_backend client_raw, server_raw;
    hd::tls_io_backend client{client_raw, ex, 1, hd::tls_context::client(), false};
    hd::tls_io_backend server{server_raw, ex, 1, hd::tls_context::server_pem(pem("cert.pem"), pem("key.pem")), true};
    std::deque<std::byte> to_client, to_server;
    std::size_t fragment = 31;
    std::size_t sends = 0;
    bool hold_writes = false;
    ~pair() {
        client.close();
        server.close();
        ex.run_pending();
    }
    bool transfer(hd::fake_io_backend& raw, std::deque<std::byte>& inbound, std::deque<std::byte>& outbound) {
        bool progress = false;
        for (const auto& op : raw.pending_ops()) {
            if (op->kind() == hd::io_op_kind::write && !hold_writes) {
                const auto bytes = std::get<hd::write_payload>(op->payload()).bytes;
                const auto count = std::min(fragment, bytes.size());
                outbound.insert(outbound.end(), bytes.begin(), bytes.begin() + count);
                raw.complete(*op, {httpserver::http::outcome_code::ok, count});
                ++sends;
                progress = true;
            } else if (op->kind() == hd::io_op_kind::read && !inbound.empty()) {
                const auto buffer = std::get<hd::read_payload>(op->payload()).buffer;
                const auto count = std::min({fragment, buffer.size(), inbound.size()});
                for (std::size_t i = 0; i < count; ++i) {
                    buffer[i] = inbound.front();
                    inbound.pop_front();
                }
                raw.complete(*op, {httpserver::http::outcome_code::ok, count});
                progress = true;
            }
        }
        return progress;
    }
    std::size_t drive() {
        for (std::size_t rounds = 0; rounds < 100000; ++rounds) {
            ex.run_pending();
            const bool a = transfer(client_raw, to_client, to_server);
            const bool b = transfer(server_raw, to_server, to_client);
            if (!a && !b && ex.pending() == 0) {
                return rounds;
            }
        }
        throw std::runtime_error("TLS transport failed to park");
    }
    bool handshake() {
        hd::tls_handshake_operation a(owner, 1), b(owner, 1);
        a.submit(client);
        b.submit(server);
        drive();
        return a.state()->applied() && b.state()->applied() && a.state()->stored_result().code == httpserver::http::outcome_code::ok &&
               b.state()->stored_result().code == httpserver::http::outcome_code::ok;
    }
};
}  // namespace tls_test
#endif  // TEST_UNIT_TLS_IO_FIXTURE_HPP_
