/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <algorithm>
#include <string>
#include <stdexcept>
#include <utility>
#include <httpserver/detail/secure_compare.hpp>
#include <httpserver/detail/quic_datagram_dispatch.hpp>
namespace httpserver {
namespace detail {
quic_datagram_dispatch::quic_datagram_dispatch(std::optional<std::size_t> short_cid_length, std::size_t max_cids)
    : short_cid_length_(short_cid_length), max_cids_(max_cids) {
    if (short_cid_length && (*short_cid_length == 0 || *short_cid_length > k_quic_cid_bytes)) {
        throw std::invalid_argument("httpserver: shared QUIC listener requires a nonempty CID of at most 20 bytes");
    }
}
quic_datagram_dispatch::~quic_datagram_dispatch() {
    for (const auto& entry : resets_) entry.target->delivery->retired.store(true, std::memory_order_release);
    for (const auto& entry : routes_) {
        if (entry.second) entry.second->delivery->retired.store(true, std::memory_order_release);
    }
}
std::string quic_datagram_dispatch::key(const quic_cid& cid) {
    return std::string(reinterpret_cast<const char*>(cid.bytes.data()), cid.size);
}
std::optional<quic_datagram_dispatch::cid_registration> quic_datagram_dispatch::register_cid(
    const quic_cid& cid, io_connection_owner::datagram_port owner, std::shared_ptr<datagram_sink> sink) {
    if (!sink || cid.size == 0 || cid.size > k_quic_cid_bytes
        || (short_cid_length_ && cid.size != *short_cid_length_)) return std::nullopt;
    const auto identity = key(cid);
    std::lock_guard<std::mutex> lock(mu_);
    const auto found = routes_.find(identity);
    if (found != routes_.end()) return std::nullopt;
    if (routes_.size() >= max_cids_) return std::nullopt;
    auto registered = std::make_shared<route>(std::move(owner), std::move(sink));
    // Allocate the handle before publishing the generation.
    cid_registration handle(identity, registered);
    routes_[identity] = registered;
    retired_.erase(identity);
    std::erase(retired_order_, identity);
    return handle;
}
bool quic_datagram_dispatch::remove(const cid_registration& registration) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto found = routes_.find(registration.key_);
    const auto registered = registration.route_.lock();
    if (!registered || found == routes_.end() || found->second != registered) return false;
    retired_order_.push_back(registration.key_);
    try {
        retired_.insert(registration.key_);
    } catch (...) {
        retired_order_.pop_back();
        throw;
    }
    registered->delivery->retired.store(true, std::memory_order_release);
    routes_.erase(found);
    if (retired_order_.size() > max_cids_) {
        retired_.erase(retired_order_.front());
        retired_order_.pop_front();
    }
    return true;
}
std::optional<quic_datagram_dispatch::reset_registration> quic_datagram_dispatch::register_reset(
    const std::array<std::byte, 16>& token, const datagram_endpoint& remote,
    io_connection_owner::datagram_port owner, std::shared_ptr<datagram_sink> sink) {
    if (!sink) return {};
    std::lock_guard<std::mutex> lock(mu_);
    if (resets_.size() >= max_cids_) return {};
    bool duplicate = false;
    const std::string_view candidate(reinterpret_cast<const char*>(token.data()), 16);
    for (const auto& entry : resets_) {
        const std::string_view existing(reinterpret_cast<const char*>(entry.token.data()), 16);
        const bool equal = constant_time_equal(candidate, existing);
        duplicate |= equal && entry.remote == remote;
    }
    if (duplicate) return {};
    auto target = std::make_shared<route>(std::move(owner), std::move(sink));
    resets_.push_back({token, remote, target});
    return reset_registration(target);
}
bool quic_datagram_dispatch::remove(const reset_registration& registration) {
    std::lock_guard<std::mutex> lock(mu_);
    auto target = registration.route_.lock();
    auto found = std::find_if(resets_.begin(), resets_.end(), [&](const auto& item) { return item.target == target; });
    if (!target || found == resets_.end()) return false;
    target->delivery->retired.store(true, std::memory_order_release);
    resets_.erase(found);
    return true;
}
std::shared_ptr<quic_datagram_dispatch::route> quic_datagram_dispatch::lookup_cid(const quic_cid& cid, datagram_dispatch_code& missing) const {
    const auto identity = key(cid);
    const auto found = routes_.find(identity);
    if (found != routes_.end()) return found->second;
    if (retired_.contains(identity)) missing = datagram_dispatch_code::retired;
    return {};
}
std::shared_ptr<quic_datagram_dispatch::route> quic_datagram_dispatch::lookup_reset(const io_datagram& packet) const {
    if (packet.bytes.size() < 21) return {};
    // Scan every bounded entry; never use secret-derived hash keys or token
    // early exits. The token belongs to the whole UDP datagram.
    const std::string_view trailing(reinterpret_cast<const char*>(packet.bytes.data() + packet.bytes.size() - 16), 16);
    std::shared_ptr<route> registered;
    for (const auto& entry : resets_) {
        const std::string_view token(reinterpret_cast<const char*>(entry.token.data()), 16);
        const bool equal = constant_time_equal(trailing, token);
        if (equal && entry.remote == packet.peer) registered = entry.target;
    }
    return registered;
}
datagram_dispatch_result quic_datagram_dispatch::dispatch(std::shared_ptr<const io_datagram> packet) const {
    if (!packet || packet->bytes.size() > k_max_datagram_bytes) return {datagram_dispatch_code::malformed, std::move(packet)};
    const auto header = extract_quic_invariant_header(packet->bytes, short_cid_length_);
    std::shared_ptr<route> registered;
    datagram_dispatch_code missing = header ? datagram_dispatch_code::unknown : datagram_dispatch_code::malformed;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (header) registered = lookup_cid(header->destination, missing);
        if (!registered) registered = lookup_reset(*packet);
    }
    if (!registered) return {missing, std::move(packet)};
    const auto result = registered->owner.enqueue(registered->delivery, packet);
    if (result == datagram_enqueue_code::queued) return {datagram_dispatch_code::routed, {}};
    return {result == datagram_enqueue_code::full ? datagram_dispatch_code::queue_full
        : datagram_dispatch_code::retired, std::move(packet)};
}
}  // namespace detail
}  // namespace httpserver
