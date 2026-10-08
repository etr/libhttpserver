/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>
#include "fuzz/quic_state_fuzz.hpp"
#include "support/quic_network_harness.hpp"
namespace {
namespace qt = quic_test;
void require(bool value) { if (!value) std::abort(); }
void accounting(const qt::network_harness& rig, bool empty = false) {
    const auto state = rig.resources();
    require(state.packets <= 16 && state.bytes <= 16384 && state.timers <= 16 && state.owner_pending <= 24);
    require(rig.trace().size() <= 2097152);
    if (empty) require(state.packets == 0 && state.bytes == 0 && state.timers == 0 && state.owner_pending == 0);
}
std::vector<std::uint8_t> replay(std::span<const std::uint8_t> input) {
    qt::network_harness rig;
    std::vector<std::uint64_t> timers;
    std::vector<std::weak_ptr<const qt::hd::io_datagram>> lifetimes;
    std::size_t offset = 0;
    for (unsigned action = 0; action < 64 && offset < input.size(); ++action) {
        const auto kind = input[offset++];
        if (kind == 11) break;
        if (kind == 6) {
            rig.drain(); accounting(rig); continue;
        }
        if (kind > 10 || offset == input.size()) break;
        const auto argument = input[offset++];
        switch (kind) {
            case 0: rig.register_endpoint(argument); break;
            case 1: {
                if (offset == input.size()) break;
                const auto length = std::size_t(argument) | (std::size_t(input[offset++]) << 8);
                if (length > input.size() - offset) {
                    offset = input.size(); break;
                }
                qt::hd::io_datagram packet;
                const auto bytes = std::as_bytes(input.subspan(offset, length));
                packet.bytes.assign(bytes.begin(), bytes.end()); offset += length;
                packet.socket_id = 77;
                packet.peer.peer.port = 1234; packet.peer.scope = 2;
                packet.peer.peer.address.family = httpserver::net::address_family::ipv6;
                packet.peer.peer.address.bytes[15] = std::byte{1};
                packet.local = qt::hd::datagram_endpoint{}; packet.local->peer.port = 8000;
                packet.interface_index = 3;
                if (auto id = rig.send(packet)) lifetimes.push_back(rig.packet_lifetime(*id));
                break;
            }
            case 2:
            case 3:
            case 4: {
                const auto ids = rig.pending_ids();
                if (ids.empty()) break;
                const auto id = ids[argument % ids.size()];
                if (kind == 2) rig.drop(id);
                if (kind == 3) rig.deliver(id);
                if (kind == 4) {
                    if (auto copy = rig.duplicate(id)) lifetimes.push_back(rig.packet_lifetime(*copy));
                }
                break;
            }
            case 5: rig.advance(argument); break;
            case 7: {
                if (auto id = rig.timer(argument)) timers.push_back(*id);
                break;
            }
            case 8: {
                if (!timers.empty()) rig.cancel(timers[argument % timers.size()]);
                break;
            }
            case 9: rig.retire_endpoint(argument); break;
            case 10: rig.destroy_endpoint(argument); break;
        }
        accounting(rig);
    }
    rig.teardown(); accounting(rig, true);
    for (const auto& lifetime : lifetimes) require(lifetime.expired());
    return rig.trace();
}
}  // namespace
std::vector<std::uint8_t> quic_state_fuzz_input(std::span<const std::uint8_t> bytes) {
    bytes = bytes.first(std::min<std::size_t>(4096, bytes.size()));
    auto first = replay(bytes), second = replay(bytes);
    require(first == second);
    return first;
}
#ifdef QUIC_STATE_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* bytes, std::size_t size) {
    quic_state_fuzz_input({bytes, size}); return 0;
}
#endif
