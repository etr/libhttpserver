/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <limits>
#include <utility>
#include <httpserver/detail/quic_amplification.hpp>
namespace httpserver::detail {
namespace {
bool canonical_endpoint(datagram_endpoint& endpoint) {
    using net::address_family;
    auto& address = endpoint.peer.address;
    if (endpoint.peer.port == 0) return false;
    if (address.family == address_family::ipv4) {
        address = net::detail::address_from_bytes(address.family, address.bytes.data() + 12);
    } else if (address.family == address_family::ipv6) {
        address = net::detail::address_from_bytes(address.family, address.bytes.data());
    } else {
        return false;
    }
    if (address.family == address_family::ipv4) endpoint.scope = 0;
    return true;
}
}  // namespace
std::optional<quic_path_identity> quic_datagram_path(const io_datagram& packet) {
    quic_path_identity path{packet.peer, packet.local, packet.interface_index, packet.socket_id};
    if (!canonical_endpoint(path.peer)) return std::nullopt;
    if (path.local && !canonical_endpoint(*path.local)) return std::nullopt;
    return path;
}
struct quic_amplification_budget::state {
    quic_path_identity path;
    struct slot { std::uint64_t generation = 0, bytes = 0; };
    std::array<slot, 64> sends{};
    std::uint64_t received = 0, debited = 0, next = 1;
    bool validated = false;
};
quic_amplification_budget::quic_amplification_budget(quic_path_identity path)
    : state_(std::make_shared<state>()) { state_->path = std::move(path); }
void quic_amplification_budget::receive_datagram(std::size_t bytes) {
    constexpr auto cap = std::numeric_limits<std::uint64_t>::max() / 3;
    state_->received += std::min<std::uint64_t>(bytes, cap - state_->received);
}
std::optional<quic_amplification_budget::reservation> quic_amplification_budget::reserve_send(std::size_t bytes) {
    auto limit = state_->validated ? std::numeric_limits<std::uint64_t>::max() : state_->received * 3;
    if (bytes > limit - state_->debited || state_->next == 0) return std::nullopt;
    auto slot = std::find_if(state_->sends.begin(), state_->sends.end(), [](const auto& send) { return send.generation == 0; });
    if (slot == state_->sends.end()) return std::nullopt;
    reservation result;
    result.owner_ = state_;
    result.generation_ = state_->next++;
    *slot = {result.generation_, bytes};
    state_->debited += bytes;
    return result;
}
bool quic_amplification_budget::consume(const reservation& send, bool refund) {
    if (send.owner_.lock() != state_) return false;
    auto slot = std::find_if(state_->sends.begin(), state_->sends.end(), [&](const auto& item) { return item.generation == send.generation_; });
    if (slot == state_->sends.end() || send.generation_ == 0) return false;
    if (refund) state_->debited -= slot->bytes;
    *slot = {};
    return true;
}
bool quic_amplification_budget::complete_send(const reservation& send) { return consume(send, false); }
bool quic_amplification_budget::cancel_unsent(const reservation& send) { return consume(send, true); }
void quic_amplification_budget::mark_address_validated() { state_->validated = true; }
bool quic_amplification_budget::validated() const { return state_->validated; }
const quic_path_identity& quic_amplification_budget::path() const { return state_->path; }
std::uint64_t quic_amplification_budget::received() const { return state_->received; }
std::uint64_t quic_amplification_budget::debited() const { return state_->debited; }
}  // namespace httpserver::detail
