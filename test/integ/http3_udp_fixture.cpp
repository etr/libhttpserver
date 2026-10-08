/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <poll.h>
#include <unistd.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include "../support/http3_network_owner.hpp"
#include "../unit/io_udp_backend_contract.hpp"
namespace {
namespace h = httpserver;
namespace hd = h::detail;
using clock_type = h3net::clock_type;
std::string read_file(const char* name) {
    std::ifstream input(name);
    if (!input)
        throw std::runtime_error("credential input missing");
    return {std::istreambuf_iterator<char>(input), {}};
}
void event(const char* kind, std::uint64_t connection, std::uint64_t stream) {
    std::cout << "{\"event\":\"" << kind << "\",\"connection\":" << connection << ",\"stream\":" << stream << "}" << std::endl;
}
struct lifetime {
    h::stop_token token;
    std::uint64_t connection, stream;
    ~lifetime() {
        if (token.stop_requested())
            event("cancelled", connection, stream);
    }
};
class capture {
 public:
    explicit capture(const char* path) : output_(path, std::ios::binary) {
        const std::array<std::uint32_t, 6> header{0xa1b2c3d4, 0x00040002, 0, 0, 65535, 101};
        output_.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
        if (!output_)
            throw std::runtime_error("capture open failed");
    }
    void packet(std::span<const std::byte> bytes, std::uint16_t source, std::uint16_t destination) {
        if (total_ + bytes.size() > 16 * 1024 * 1024)
            throw std::runtime_error("capture bound");
        total_ += bytes.size();
        std::vector<unsigned char> wire(28 + bytes.size());
        wire[0] = 0x45;
        wire[8] = 64;
        wire[9] = 17;
        wire[12] = wire[16] = 127;
        wire[15] = wire[19] = 1;
        put16(wire, 2, wire.size());
        put16(wire, 20, source);
        put16(wire, 22, destination);
        put16(wire, 24, bytes.size() + 8);
        unsigned checksum = 0;
        for (unsigned i = 0; i < 20; i += 2)
            checksum += (unsigned{wire[i]} << 8) | wire[i + 1];
        while (checksum >> 16)
            checksum = (checksum & 65535) + (checksum >> 16);
        put16(wire, 10, ~checksum);
        std::copy(bytes.begin(), bytes.end(), reinterpret_cast<std::byte*>(wire.data() + 28));
        auto time = std::chrono::system_clock::now().time_since_epoch();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(time).count();
        const std::array<std::uint32_t, 4> record{static_cast<std::uint32_t>(micros / 1000000), static_cast<std::uint32_t>(micros % 1000000),
                                                  static_cast<std::uint32_t>(wire.size()), static_cast<std::uint32_t>(wire.size())};
        output_.write(reinterpret_cast<const char*>(record.data()), sizeof(record));
        output_.write(reinterpret_cast<const char*>(wire.data()), wire.size());
        output_.flush();
    }

 private:
    static void put16(std::vector<unsigned char>& bytes, unsigned at, unsigned value) {
        bytes[at] = value >> 8;
        bytes[at + 1] = value;
    }
    std::ofstream output_;
    std::size_t total_ = 0;
};
struct fixture {
    h::manual_executor executor;
    hd::io_connection_owner listener{executor};
    hd::io_poll_backend backend{h::server::loop_mode::external};
    hd::quic_server_admission admission;
    hd::tls_credentials_registry credentials;
    h::server::resource_budget root = h::server::resource_budget::root({});
    h::server::route_registry routes;
    std::map<std::uint64_t, std::shared_ptr<h3net::connection>> connections;
    std::vector<hd::quic_datagram_dispatch::cid_registration> registrations;
    h::resume_signal hold;
    std::uint16_t port = 0;
    std::size_t received_count = 0, sent_count = 0, received_bytes = 0, sent_bytes = 0;
    capture packets;
    std::unique_ptr<hd::udp_receive_operation> receive;
    std::unique_ptr<hd::udp_send_operation> send;
    std::shared_ptr<h3net::connection> sending;
    std::shared_ptr<hd::quic_admission_reply> reply;
    std::optional<h3net::transmission> sent;
    fixture(const char* certificate, const char* key, const char* pcap) : packets(pcap) {
        hd::tls_host_credentials host;
        host.host = "localhost";
        host.certificate_chain_pem = read_file(certificate);
        host.private_key_pem = read_file(key);
        host.alpn = {"h3"};
        if (!credentials.replace({{host}, 0}).ok())
            throw std::runtime_error("credentials refused");
        if (!h::server::route_registry::create(root, routes).ok())
            throw std::runtime_error("routes refused");
        auto get = h::http::method::known(h::http::method_id::get);
        auto post = h::http::method::known(h::http::method_id::post);
        routes.route(get, "/hello", [this](h::exchange& x) -> h::task<void> {
            event("get", x.connection_id(), connections.at(x.connection_id())->current_stream());
            x.start_response(h::http::status::from_code(200), {});
            const std::string body = "http3 fixture";
            co_await x.writer().write(std::as_bytes(std::span(body)));
            co_await x.writer().finish();
        });
        routes.route(post, "/echo", [this](h::exchange& x) -> h::task<void> {
            event("post", x.connection_id(), connections.at(x.connection_id())->current_stream());
            x.admit_body({65536});
            auto body = co_await x.body().collect(65536);
            if (!body.status.ok())
                co_return;
            x.start_response(h::http::status::from_code(200), {});
            co_await x.writer().write(body.data);
            co_await x.writer().finish();
        });
        routes.route(get, "/health", [this](h::exchange& x) -> h::task<void> {
            event("health", x.connection_id(), connections.at(x.connection_id())->current_stream());
            x.respond(h::http::status::from_code(204), {});
            co_return;
        });
        routes.route(get, "/hold", [this](h::exchange& x) -> h::task<void> {
            const auto stream = connections.at(x.connection_id())->current_stream();
            event("held", x.connection_id(), stream);
            lifetime guard{x.cancellation(), x.connection_id(), stream};
            co_await hold.wait();
            x.respond(h::http::status::from_code(200), {});
            event("released", x.connection_id(), stream);
        });
        auto socket = io_udp_contract::udp_socket();
        if (socket == hd::pollsys::k_invalid_socket)
            throw std::runtime_error("UDP bind failed");
        port = io_udp_contract::endpoint(socket).peer.port;
        backend.adopt_datagram(1, socket);
        backend.activate_external();
        arm_receive();
    }
    ~fixture() {
        backend.close();
        executor.run_pending();
        if (send)
            finish_send();
        for (auto& registration : registrations)
            admission.routes().remove(registration);
        connections.clear();
        executor.run_pending();
        if (listener.pending())
            std::terminate();
        std::cout << "{\"event\":\"traffic\",\"port\":" << port << ",\"received_count\":" << received_count << ",\"sent_count\":" << sent_count
                  << ",\"received_bytes\":" << received_bytes << ",\"sent_bytes\":" << sent_bytes << "}" << std::endl;
    }
    void arm_receive() {
        receive = std::make_unique<hd::udp_receive_operation>(listener, 1, 65507);
        receive->submit(backend);
    }
    void submit(h3net::transmission packet) {
        sent = std::move(packet);
        send = std::make_unique<hd::udp_send_operation>(listener, 1, sent->bytes, sent->peer);
        send->submit(backend);
    }
    void finish_send() {
        auto result = send->state()->stored_result();
        bool success = result.code == h::http::outcome_code::ok && result.transferred == sent->bytes.size();
        if (success) {
            packets.packet(sent->bytes, port, sent->peer.peer.port);
            ++sent_count;
            sent_bytes += sent->bytes.size();
        }
        if (sending)
            sending->emitted(success, clock_type::now());
        if (reply) {
            if (success)
                reply->complete_send();
            else
                reply->cancel_unsent();
        }
        send.reset();
        sent.reset();
        sending.reset();
        reply.reset();
    }
    void pump() {
        for (unsigned turn = 0; turn < 256 && executor.run_one(); ++turn) {
        }
        if (executor.pending())
            throw std::runtime_error("fixture executor capacity");
        auto now = clock_type::now();
        if (send && send->state()->applied())
            finish_send();
        if (receive->state()->applied()) {
            auto result = receive->state()->stored_result();
            if (result.code != h::http::outcome_code::ok || !result.datagram)
                throw std::runtime_error("UDP receive failed");
            auto packet = result.datagram;
            packets.packet(packet->bytes, packet->peer.peer.port, port);
            ++received_count;
            received_bytes += packet->bytes.size();
            auto outcome = admission.receive(packet, std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
            if (outcome.reply) {
                if (send)
                    throw std::runtime_error("reply send capacity");
                reply = outcome.reply;
                submit({reply->packet()->bytes, packet->peer});
            } else if (outcome.code == hd::quic_admission_code::pending) {
                if (connections.size() >= 4)
                    throw std::runtime_error("connection bound");
                auto facts = admission.inspect(outcome.pending);
                if (!facts)
                    throw std::runtime_error("admission facts missing");
                const auto id = connections.size() + 1;
                auto connection = std::make_shared<h3net::connection>(credentials.acquire()->select_default(), *facts, routes, executor, id);
                connections.emplace(id, connection);
                auto promoted = admission.promote(outcome.pending, listener.datagrams(), connection,
                                                  std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
                if (!promoted)
                    throw std::runtime_error("CID promotion refused");
                registrations.push_back(promoted->registration);
            }
            arm_receive();
        }
        for (auto& [id, connection] : connections) {
            connection->tick(now);
            if (!connection->failure().empty() && connection->failure() != "peer closed")
                throw std::runtime_error(connection->failure());
            if (!send) {
                if (auto packet = connection->prepare(now)) {
                    sending = connection;
                    submit(std::move(*packet));
                }
            }
        }
    }
    bool wait(clock_type::time_point lifetime) {
        // Datagram operations may complete synchronously. Consume their actual
        // result before parking: a prepared send has no recovery timer until
        // its successful completion is committed in the next owner turn.
        if (executor.pending() || (send && send->state()->applied()) || receive->state()->applied())
            return true;
        auto snapshot = backend.interests();
        if (snapshot.wake)
            snapshot.sockets.push_back(*snapshot.wake);
        std::vector<pollfd> descriptors{{STDIN_FILENO, POLLIN, 0}};
        for (auto& interest : snapshot.sockets)
            descriptors.push_back(
                {static_cast<int>(interest.handle.value), static_cast<std::int16_t>((interest.readable ? POLLIN : 0) | (interest.writable ? POLLOUT : 0)), 0});
        auto due = lifetime;
        for (auto& [id, connection] : connections)
            if (auto deadline = connection->deadline())
                due = std::min(due, *deadline);
        if (snapshot.next_deadline)
            due = std::min(due, *snapshot.next_deadline);
        int timeout = std::max(std::int64_t{0}, std::chrono::ceil<std::chrono::milliseconds>(due - clock_type::now()).count());
        if (poll(descriptors.data(), descriptors.size(), timeout) < 0)
            throw std::runtime_error("poll failed");
        if (descriptors[0].revents & (POLLIN | POLLHUP)) {
            std::string command;
            if (!std::getline(std::cin, command) || command == "quit")
                return false;
            if (command == "release")
                hold.signal();
            else if (command == "reset")
                hold = h::resume_signal{};
            else
                throw std::runtime_error("unknown fixture command");
        }
        std::vector<h::server::readiness_event> events;
        for (unsigned i = 0; i < snapshot.sockets.size(); ++i)
            if (descriptors[i + 1].revents) {
                auto& interest = snapshot.sockets[i];
                auto revents = descriptors[i + 1].revents;
                events.push_back({interest.key, interest.generation, static_cast<bool>(revents & POLLIN), static_cast<bool>(revents & POLLOUT),
                                  static_cast<bool>(revents & POLLHUP), static_cast<bool>(revents & (POLLERR | POLLNVAL))});
            }
        if (!backend.dispatch(events, clock_type::now()).ok())
            throw std::runtime_error("backend dispatch failed");
        return true;
    }
};
}  // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::runtime_error("usage: http3_udp_fixture certificate key packets.pcap");
        fixture f(argv[1], argv[2], argv[3]);
        std::cout << "READY " << f.port << std::endl;
        const auto deadline = clock_type::now() + std::chrono::seconds(120);
        do {
            f.pump();
        } while (clock_type::now() < deadline && f.wait(deadline));
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fixture failure: " << e.what() << std::endl;
        return 1;
    }
}
