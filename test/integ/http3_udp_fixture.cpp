/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include "../support/http3_network_owner.hpp"
#include "../support/http3_semantic_routes.hpp"
#include "../support/http3_websocket_routes.hpp"
#include "../unit/io_udp_backend_contract.hpp"
namespace {
namespace h = httpserver;
namespace hd = h::detail;
using clock_type = h3net::clock_type;
std::string read_file(const char* name) {
    std::ifstream input(name);
    if (!input) throw std::runtime_error("credential input missing");
    return {std::istreambuf_iterator<char>(input), {}};
}
std::uint64_t sequence = 0;
void event(const char* kind, std::uint64_t connection, std::uint64_t stream) {
    std::cout << "{\"event\":\"" << kind << "\",\"connection\":" << connection << ",\"stream\":" << stream << ",\"sequence\":" << ++sequence << "}" << std::endl;
}
struct lifetime {
    h::stop_token token;
    std::uint64_t connection, stream;
    std::function<void()> cleanup;
    ~lifetime() {
        cleanup();
        if (token.stop_requested()) event("cancelled", connection, stream);
    }
};
class capture {
 public:
    explicit capture(const char* path) : output_(path, std::ios::binary) {
        const std::array<std::uint32_t, 6> header{0xa1b2c3d4, 0x00040002, 0, 0, 65535, 101};
        output_.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
        if (!output_) throw std::runtime_error("capture open failed");
    }
    void packet(std::span<const std::byte> bytes, std::uint16_t source, std::uint16_t destination) {
        if (total_ + bytes.size() > 16 * 1024 * 1024) throw std::runtime_error("capture bound");
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
        for (unsigned i = 0; i < 20; i += 2) checksum += (unsigned{wire[i]} << 8) | wire[i + 1];
        while (checksum >> 16) checksum = (checksum & 65535) + (checksum >> 16);
        put16(wire, 10, ~checksum);
        std::copy(bytes.begin(), bytes.end(), reinterpret_cast<std::byte*>(wire.data() + 28));
        auto time = std::chrono::system_clock::now().time_since_epoch();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(time).count();
        const std::array<std::uint32_t, 4> record{static_cast<std::uint32_t>(micros / 1000000), static_cast<std::uint32_t>(micros % 1000000), static_cast<std::uint32_t>(wire.size()),
                                                  static_cast<std::uint32_t>(wire.size())};
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
    h::server::peer_policy policy;
    std::map<std::uint64_t, std::shared_ptr<h3net::connection>> connections;
    std::vector<hd::quic_datagram_dispatch::cid_registration> registrations;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::shared_ptr<h::resume_signal>> holds;
    struct progress {
        std::size_t read = 0, started = 0, completed = 0, finished = 0;
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, progress> observations;
    std::map<const h::exchange*, std::uint64_t> semantic_streams;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::shared_ptr<h3wstest::observation>> websocket;
    std::future<h::server::drain_result> drain;
    std::uint64_t drain_connection = 0;
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
        if (!credentials.replace({{host}, 0}).ok()) throw std::runtime_error("credentials refused");
        if (!h::server::route_registry::create(root, routes).ok()) throw std::runtime_error("routes refused");
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
            if (!body.status.ok()) co_return;
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
            const auto key = std::pair{x.connection_id(), stream};
            auto pause = std::make_shared<h::resume_signal>();
            holds.emplace(key, pause);
            lifetime guard{x.cancellation(), x.connection_id(), stream, [this, key] { holds.erase(key); }};
            co_await pause->wait();
            x.respond(h::http::status::from_code(200), {});
            event("released", x.connection_id(), stream);
        });
        h3semantic::install(routes, [this](const char* kind, h::exchange& x, std::size_t count) {
            if (std::string_view(kind) == "entered") semantic_streams[&x] = connections.at(x.connection_id())->current_stream();
            const auto stream = semantic_streams.at(&x);
            auto& state = observations[{x.connection_id(), stream}];
            if (std::string_view(kind) == "read") state.read += count;
            if (std::string_view(kind) == "write_started") ++state.started;
            if (std::string_view(kind) == "write_completed") ++state.completed;
            if (std::string_view(kind) == "finished") ++state.finished;
            snapshot(kind, x.connection_id(), stream);
            if (std::string_view(kind) == "finished") semantic_streams.erase(&x);
        });
        h3wstest::install(routes, [this](h::exchange& x) {
            if (websocket.size() >= 16) throw std::runtime_error("WebSocket observation bound");
            const auto id = x.connection_id(), stream = connections.at(id)->current_stream();
            auto state = std::make_shared<h3wstest::observation>();
            websocket.emplace(std::pair{id, stream}, state);
            state->emit = [this, id, stream](const char* kind) { ws_snapshot(kind, id, stream); };
            return state;
        });
        auto socket = io_udp_contract::udp_socket();
        if (socket == hd::pollsys::k_invalid_socket) throw std::runtime_error("UDP bind failed");
        port = io_udp_contract::endpoint(socket).peer.port;
        backend.adopt_datagram(1, socket);
        backend.activate_external();
        arm_receive();
    }
    ~fixture() {
        backend.close();
        executor.run_pending();
        if (send) finish_send();
        for (auto& registration : registrations) admission.routes().remove(registration);
        connections.clear();
        executor.run_pending();
        if (listener.pending()) std::terminate();
        std::cout << "{\"event\":\"traffic\",\"port\":" << port << ",\"received_count\":" << received_count << ",\"sent_count\":" << sent_count << ",\"received_bytes\":" << received_bytes
                  << ",\"sent_bytes\":" << sent_bytes << "}" << std::endl;
    }
    void snapshot(const char* kind, std::uint64_t id, std::uint64_t stream) {
        const auto& p = observations.at({id, stream});
        std::cout << "{\"event\":\"" << kind << "\",\"sequence\":" << ++sequence << ",\"connection\":" << id << ",\"stream\":" << stream << ",\"read_bytes\":" << p.read << ",\"started\":" << p.started
                  << ",\"completed\":" << p.completed << ",\"finished\":" << p.finished << "}" << std::endl;
    }
    void ws_snapshot(const char* kind, std::uint64_t id, std::uint64_t stream) {
        const auto& s = *websocket.at({id, stream});
        std::cout << "{\"event\":\"" << kind << "\",\"sequence\":" << ++sequence
                  << ",\"connection\":" << id << ",\"stream\":" << stream
                  << ",\"received\":" << s.received << ",\"received_bytes\":" << s.received_bytes << ",\"kind\":" << s.kind
                  << ",\"accepted\":" << s.accepted << ",\"blocked\":" << s.blocked << ",\"resumed\":" << s.resumed
                  << ",\"waiting\":" << (s.waiting ? "true" : "false") << ",\"close_count\":" << s.close_count
                  << ",\"reason\":\"" << s.reason << "\",\"code\":" << (s.code ? std::to_string(*s.code) : "null")
                  << ",\"clean\":" << (s.clean ? "true" : "false") << ",\"retired\":" << (s.retired ? "true" : "false")
                  << ",\"subprotocol\":\"" << s.subprotocol << "\"}" << std::endl;
    }
    void command(const std::string& line) {
        std::istringstream input(line);
        std::string name;
        std::uint64_t id = 0, stream = 0;
        input >> name >> id >> stream;
        if (name == "release") {
            if (id) {
                holds.at({id, stream})->signal();
            } else {
                for (auto& [key, pause] : holds) pause->signal();
            }
        } else if (name == "reset") {
            if (!holds.empty()) throw std::runtime_error("reset with active held request");
        } else if (name == "ws_snapshot") {
            ws_snapshot("ws_snapshot", id, stream);
        } else if (name == "snapshot") {
            snapshot("snapshot", id, stream);
        } else if (name == "drain") {
            if (!holds.contains({id, stream})) throw std::runtime_error("drain target is not held");
            if (drain.valid()) throw std::runtime_error("overlapping drain commands");
            h::server::drain_ticket ticket;
            if (!connections.at(id)->begin_drain(clock_type::now() + std::chrono::seconds(5), ticket).ok()) throw std::runtime_error("drain refused");
            drain_connection = id;
            drain = std::async(std::launch::async, [ticket = std::move(ticket)]() mutable {
                h::server::drain_result result;
                if (!ticket.wait(result).ok()) throw std::runtime_error("drain wait failed");
                return result;
            });
            event("drain_started", id, stream);
        } else {
            throw std::runtime_error("unknown fixture command");
        }
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
        if (sending) sending->emitted(success, clock_type::now());
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
        if (executor.pending()) throw std::runtime_error("fixture executor capacity");
        if (send && send->state()->applied()) finish_send();
        // Successful completion commits a fresh emission timestamp before
        // scheduling the next packet.
        auto now = clock_type::now();
        if (receive->state()->applied()) {
            auto result = receive->state()->stored_result();
            if (result.code != h::http::outcome_code::ok || !result.datagram) throw std::runtime_error("UDP receive failed");
            auto packet = result.datagram;
            packets.packet(packet->bytes, packet->peer.peer.port, port);
            ++received_count;
            received_bytes += packet->bytes.size();
            auto outcome = admission.receive(packet, std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
            if (outcome.reply) {
                if (send) throw std::runtime_error("reply send capacity");
                reply = outcome.reply;
                submit({reply->packet()->bytes, packet->peer});
            } else if (outcome.code == hd::quic_admission_code::pending) {
                if (connections.size() >= 4) throw std::runtime_error("connection bound");
                auto facts = admission.inspect(outcome.pending);
                if (!facts) throw std::runtime_error("admission facts missing");
                const auto id = connections.size() + 1;
                auto connection = std::make_shared<h3net::connection>(credentials.acquire()->select_default(), *facts, routes, executor, id, policy);
                connections.emplace(id, connection);
                auto promoted = admission.promote(outcome.pending, listener.datagrams(), connection, std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
                if (!promoted) throw std::runtime_error("CID promotion refused");
                connection->adopt_routes(admission.routes(), listener.datagrams(), promoted->registration, connection);
                registrations.push_back(promoted->registration);
            }
            arm_receive();
        }
        if (drain.valid() && drain.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const auto result = drain.get();
            if (result.status != h::server::drain_status::completed || result.remaining) throw std::runtime_error("drain deadline expired");
            event("drained", drain_connection, 0);
        }
        for (auto& [id, connection] : connections) {
            connection->tick(now);
            if (!connection->failure().empty() && connection->failure() != "peer closed") throw std::runtime_error(connection->failure());
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
        if (executor.pending() || (send && send->state()->applied()) || receive->state()->applied()) return true;
        auto snapshot = backend.interests();
        if (snapshot.wake) snapshot.sockets.push_back(*snapshot.wake);
        std::vector<pollfd> descriptors{{STDIN_FILENO, POLLIN, 0}};
        for (auto& interest : snapshot.sockets)
            descriptors.push_back({static_cast<int>(interest.handle.value), static_cast<std::int16_t>((interest.readable ? POLLIN : 0) | (interest.writable ? POLLOUT : 0)), 0});
        auto due = lifetime;
        for (auto& [id, connection] : connections)
            if (auto deadline = connection->deadline()) due = std::min(due, *deadline);
        if (snapshot.next_deadline) due = std::min(due, *snapshot.next_deadline);
        int timeout = std::max(std::int64_t{0}, std::chrono::ceil<std::chrono::milliseconds>(due - clock_type::now()).count());
        if (poll(descriptors.data(), descriptors.size(), timeout) < 0) throw std::runtime_error("poll failed");
        if (descriptors[0].revents & (POLLIN | POLLHUP)) {
            std::string command;
            if (!std::getline(std::cin, command) || command == "quit") return false;
            this->command(command);
        }
        std::vector<h::server::readiness_event> events;
        for (unsigned i = 0; i < snapshot.sockets.size(); ++i)
            if (descriptors[i + 1].revents) {
                auto& interest = snapshot.sockets[i];
                auto revents = descriptors[i + 1].revents;
                events.push_back({interest.key, interest.generation, static_cast<bool>(revents & POLLIN), static_cast<bool>(revents & POLLOUT), static_cast<bool>(revents & POLLHUP),
                                  static_cast<bool>(revents & (POLLERR | POLLNVAL))});
            }
        if (!backend.dispatch(events, clock_type::now()).ok()) throw std::runtime_error("backend dispatch failed");
        return true;
    }
};
}  // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("usage: http3_udp_fixture certificate key packets.pcap");
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
