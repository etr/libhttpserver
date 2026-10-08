/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/pem.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <filesystem>  // NOLINT(build/c++17): C++20 test endpoint.
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
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
std::string env(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}
namespace h = httpserver;
namespace hd = h::detail;
using clock_type = h3net::clock_type;
std::string read_file(const char* name) {
    std::ifstream input(name);
    if (!input)
        throw std::runtime_error("credential input missing");
    return {std::istreambuf_iterator<char>(input), {}};
}
std::string runner_key(const char* path) {
    const auto pem = read_file(path);
    if (pem.size() > 65536) throw std::runtime_error("runner key size bound");
    std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(pem.data(), pem.size()), BIO_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PrivateKey(input.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
    // The runner's ecparam output includes an EC PARAMETERS prefix; normalize
    // only this test-mounted input, preserving the production strict PEM parser.
    if (!key || !output || PEM_write_bio_PrivateKey(output.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1)
        throw std::runtime_error("runner key normalization failed");
    char* data = nullptr;
    const auto size = BIO_get_mem_data(output.get(), &data);
    return {data, static_cast<std::size_t>(size)};
}
class capture {
 public:
    explicit capture(const char* path) : output_(path, std::ios::binary) {
        const std::array<std::uint32_t, 6> header{0xa1b2c3d4, 0x00040002, 0, 0, 65535, 101};
        output_.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
        if (!output_)
            throw std::runtime_error("capture open failed");
    }
    void packet(std::span<const std::byte> bytes, const h::net::peer_address& source, const h::net::peer_address& destination) {
        if (total_ + bytes.size() > 64 * 1024 * 1024)
            throw std::runtime_error("capture bound");
        total_ += bytes.size();
        std::vector<unsigned char> wire(28 + bytes.size());
        wire[0] = 0x45;
        wire[8] = 64;
        wire[9] = 17;
        if (source.address.family != h::net::address_family::ipv4 || destination.address.family != h::net::address_family::ipv4)
            throw std::runtime_error("IPv4 capture endpoints required");
        for (unsigned i = 0; i < 4; ++i) {
            wire[12 + i] = std::to_integer<unsigned char>(source.address.bytes[12 + i]);
            wire[16 + i] = std::to_integer<unsigned char>(destination.address.bytes[12 + i]);
        }
        put16(wire, 2, wire.size());
        put16(wire, 20, source.port);
        put16(wire, 22, destination.port);
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
    std::map<std::size_t, std::unique_ptr<hd::quic_server_admission>> admissions;
    hd::tls_credentials_registry credentials;
    h::server::resource_budget root = h::server::resource_budget::root({});
    h::server::route_registry routes;
    h::server::peer_policy policy;
    std::map<std::uint64_t, std::shared_ptr<h3net::connection>> connections;
    std::vector<std::pair<std::size_t, hd::quic_datagram_dispatch::cid_registration>> registrations;
    std::map<std::uint64_t, h::net::peer_address> local_addresses;
    h::net::peer_address sending_local;
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
        host.host = "server";
        host.certificate_chain_pem = read_file(certificate);
        host.private_key_pem = runner_key(key);
        host.alpn = {"hq-interop"};
        if (!credentials.replace({{host}, 0}).ok())
            throw std::runtime_error("credentials refused");
        if (!h::server::route_registry::create(root, routes).ok())
            throw std::runtime_error("routes refused");
        // Native admission retains fixed-width routing; the test listener chooses
        // the existing configured owner for each valid client-chosen Initial CID.
        for (std::size_t width = 8; width <= 20; ++width) {
            hd::quic_admission_limits limits;
            limits.require_retry = false;
            limits.cid_length = width;
            limits.max_pending = limits.max_routes = 4;
            admissions.emplace(width, std::make_unique<hd::quic_server_admission>(limits));
        }
        auto socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_storage address{};
        const auto bind_port = std::stoul(env("LIBHTTPSERVER_INTEROP_PORT", "443"));
        if (bind_port > 65535) throw std::runtime_error("port out of range");
        hd::pollsys::fill_listener_address(env("LIBHTTPSERVER_INTEROP_BIND", "0.0.0.0"), bind_port, address);
        if (::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(sockaddr_in)) != 0) {
            hd::pollsys::close_socket(socket);
            socket = hd::pollsys::k_invalid_socket;
        }
        hd::pollsys::set_nonblocking(socket, true);
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
            admissions.at(registration.first)->routes().remove(registration.second);
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
            packets.packet(sent->bytes, sending_local, sent->peer.peer);
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
        if (send && send->state()->applied())
            finish_send();
        auto now = clock_type::now();
        if (receive->state()->applied()) {
            auto result = receive->state()->stored_result();
            if (result.code != h::http::outcome_code::ok || !result.datagram)
                throw std::runtime_error("UDP receive failed");
            auto packet = result.datagram;
            if (!packet->local) throw std::runtime_error("capture destination unavailable");
            packets.packet(packet->bytes, packet->peer.peer, packet->local->peer);
            ++received_count;
            received_bytes += packet->bytes.size();
            // Upstream simulator readiness uses the reserved WAIT version with
            // empty CIDs. Its stateless VN reply is confined to this test endpoint.
            const std::array probe{std::byte{0xc0}, std::byte{'W'}, std::byte{'A'}, std::byte{'I'}, std::byte{'T'}, std::byte{0}, std::byte{0}};
            if (packet->bytes.size() == 1207 && std::equal(probe.begin(), probe.end(), packet->bytes.begin()) &&
                std::all_of(packet->bytes.begin() + probe.size(), packet->bytes.end(), [](std::byte byte) { return byte == std::byte{0}; })) {
                if (send) throw std::runtime_error("probe send capacity");
                sending_local = packet->local->peer;
                submit({{std::byte{0xc0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                         std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}}, packet->peer});
                arm_receive();
                return;
            }
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            hd::quic_server_admission* admission = nullptr;
            hd::quic_server_admission::result outcome;
            auto header = hd::extract_quic_invariant_header(packet->bytes, 8);
            if (header && header->long_header) {
                if (auto found = admissions.find(header->destination.size); found != admissions.end()) {
                    admission = found->second.get();
                    outcome = admission->receive(packet, seconds);
                }
            } else {
                for (auto& [width, candidate] : admissions) {
                    outcome = candidate->receive(packet, seconds);
                    if (outcome.code != hd::quic_admission_code::dropped) {
                        admission = candidate.get();
                        break;
                    }
                }
            }
            if (outcome.reply) {
                if (send)
                    throw std::runtime_error("reply send capacity");
                sending_local = packet->local->peer;
                reply = outcome.reply;
                submit({reply->packet()->bytes, packet->peer});
            } else if (outcome.code == hd::quic_admission_code::pending) {
                if (connections.size() >= 4)
                    throw std::runtime_error("connection bound");
                auto facts = admission->inspect(outcome.pending);
                if (!facts)
                    throw std::runtime_error("admission facts missing");
                const auto id = connections.size() + 1;
                auto connection = std::make_shared<h3net::connection>(credentials.acquire()->select_default(), *facts, routes, executor, id, policy, env("LIBHTTPSERVER_INTEROP_WWW", "/www"));
                local_addresses[id] = packet->local->peer;
                connections.emplace(id, connection);
                auto promoted = admission->promote(outcome.pending, listener.datagrams(), connection,
                                                  std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
                if (!promoted)
                    throw std::runtime_error("CID promotion refused");
                connection->adopt_routes(admission->routes(), listener.datagrams(), promoted->registration, connection);
                registrations.emplace_back(facts->destination.size, promoted->registration);
            }
            arm_receive();
        }
        for (auto& [id, connection] : connections) {
            connection->tick(now);
            if (!connection->failure().empty() && connection->failure() != "peer closed")
                throw std::runtime_error(connection->failure());
            if (!send) {
                if (auto packet = connection->prepare(now)) {
                    sending_local = local_addresses.at(id);
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
        std::vector<pollfd> descriptors;
        for (auto& interest : snapshot.sockets)
            descriptors.push_back(
                {static_cast<int>(interest.handle.value), static_cast<std::int16_t>((interest.readable ? POLLIN : 0) | (interest.writable ? POLLOUT : 0)), 0});
        auto due = lifetime;
        for (auto& [id, connection] : connections)
            if (auto deadline = connection->deadline())
                due = std::min(due, *deadline);
        if (snapshot.next_deadline)
            due = std::min(due, *snapshot.next_deadline);
        int timeout = std::min<std::int64_t>(1000, std::max(std::int64_t{0}, std::chrono::ceil<std::chrono::milliseconds>(due - clock_type::now()).count()));
        if (poll(descriptors.data(), descriptors.size(), timeout) < 0 && !stopping)
            throw std::runtime_error("poll failed");
        std::vector<h::server::readiness_event> events;
        for (unsigned i = 0; i < snapshot.sockets.size(); ++i)
            if (descriptors[i].revents) {
                auto& interest = snapshot.sockets[i];
                auto revents = descriptors[i].revents;
                events.push_back({interest.key, interest.generation, static_cast<bool>(revents & POLLIN), static_cast<bool>(revents & POLLOUT),
                                  static_cast<bool>(revents & POLLHUP), static_cast<bool>(revents & (POLLERR | POLLNVAL))});
            }
        if (!backend.dispatch(events, clock_type::now()).ok())
            throw std::runtime_error("backend dispatch failed");
        return true;
    }
};
}  // namespace
int main() {
    try {
        if (env("LIBHTTPSERVER_INTEROP_TEST_MODE", "0") != "1") throw std::runtime_error("explicit test mode required");
        const auto certificate = env("LIBHTTPSERVER_INTEROP_CERT", "/certs/cert.pem");
        const auto key = env("LIBHTTPSERVER_INTEROP_KEY", "/certs/priv.key");
        read_file(certificate.c_str());
        read_file(key.c_str());
        hqnet::files root_check(env("LIBHTTPSERVER_INTEROP_WWW", "/www"));
        const auto logs = env("LIBHTTPSERVER_INTEROP_LOGS", "/logs");
        std::filesystem::create_directories(logs);
        if (chmod(logs.c_str(), 0700) != 0) throw std::runtime_error("log permissions failed");
        const auto pcap = logs + "/packets.pcap";
        std::signal(SIGTERM, stop);
        std::signal(SIGINT, stop);
        fixture f(certificate.c_str(), key.c_str(), pcap.c_str());
        std::cout << "READY " << f.port << std::endl;
        const auto deadline = clock_type::now() + std::chrono::seconds(120);
        do {
            f.pump();
        } while (!stopping && clock_type::now() < deadline && f.wait(deadline));
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "interop endpoint failure: " << e.what() << std::endl;
        return 1;
    }
}
