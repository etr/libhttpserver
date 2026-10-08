/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <algorithm>
#include <string>
#include <stdexcept>
#include <utility>
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
datagram_dispatch_result quic_datagram_dispatch::dispatch(std::shared_ptr<const io_datagram> packet) const {
    if (!packet || packet->bytes.size() > k_max_datagram_bytes) return {datagram_dispatch_code::malformed, std::move(packet)};
    const auto header = extract_quic_invariant_header(packet->bytes, short_cid_length_);
    if (!header) return {datagram_dispatch_code::malformed, std::move(packet)};
    std::shared_ptr<route> registered;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto found = routes_.find(key(header->destination));
        if (found == routes_.end()) return {retired_.contains(key(header->destination))
            ? datagram_dispatch_code::retired : datagram_dispatch_code::unknown, std::move(packet)};
        registered = found->second;
    }
    if (!registered) return {datagram_dispatch_code::retired, std::move(packet)};
    const auto result = registered->owner.enqueue(registered->delivery, packet);
    if (result == datagram_enqueue_code::queued) return {datagram_dispatch_code::routed, {}};
    return {result == datagram_enqueue_code::full ? datagram_dispatch_code::queue_full
        : datagram_dispatch_code::retired, std::move(packet)};
}
}  // namespace detail
}  // namespace httpserver
