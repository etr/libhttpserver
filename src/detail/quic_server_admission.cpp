/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <httpserver/detail/quic_server_admission.hpp>
#include <httpserver/detail/quic_crypto.hpp>
namespace httpserver::detail {
namespace {
std::span<const std::byte> cid_bytes(const quic_cid& cid) { return std::span(cid.bytes).first(cid.size); }
bool tokenless_destination_allowed(const quic_admission_limits& limits, const quic_cid& destination) {
    return destination.size >= 8 && (limits.require_retry || destination.size == limits.cid_length);
}
bool authenticate_initial(std::span<const std::byte> bytes, std::span<const std::byte> destination) {
    quic_initial_keys keys;
    if (derive_quic_initial_keys(destination, keys) != quic_crypto_code::ok) return false;
    std::vector<std::byte> output(bytes.size()), scratch(bytes.size());
    return unprotect_quic_packet(keys.client, bytes, std::nullopt, std::nullopt, output, scratch).code == quic_crypto_code::ok;
}
quic_admission_code routed_code(datagram_dispatch_code code) {
    switch (code) {
        case datagram_dispatch_code::routed: return quic_admission_code::routed;
        case datagram_dispatch_code::retired: return quic_admission_code::retired;
        case datagram_dispatch_code::queue_full: return quic_admission_code::queue_full;
        default: return quic_admission_code::dropped;
    }
}
}  // namespace
struct quic_server_admission::pending_entry {
    quic_admission_facts facts;
    quic_cid source;
    std::uint64_t created;
};
quic_admission_reply::quic_admission_reply(std::shared_ptr<const io_datagram> packet,
                                         quic_path_identity path, std::size_t received)
    : packet_(std::move(packet)), budget_(std::move(path)) {
    budget_.receive_datagram(received);
    send_ = *budget_.reserve_send(packet_->bytes.size());
}
quic_server_admission::quic_server_admission(quic_admission_limits limits, std::span<const std::byte> key)
    : limits_(limits), routes_(limits.cid_length, limits.max_routes), tokens_(key) {
    if (limits.pending_lifetime_seconds == 0) {
        throw std::invalid_argument("httpserver: QUIC admission requires a pending lifetime");
    }
}
quic_server_admission::result quic_server_admission::make_reply(const io_datagram& packet,
    const quic_path_identity& path, std::vector<std::byte> bytes) {
    if (bytes.empty() || bytes.size() > packet.bytes.size() * 3) return {};
    auto response = std::make_shared<io_datagram>();
    response->bytes = std::move(bytes);
    response->peer = packet.peer;
    response->local = packet.local;
    response->interface_index = packet.interface_index;
    response->socket_id = packet.socket_id;
    result outcome;
    outcome.code = quic_admission_code::reply;
    outcome.reply = std::shared_ptr<quic_admission_reply>(new quic_admission_reply(response, path, packet.bytes.size()));
    return outcome;
}
quic_server_admission::result quic_server_admission::version_reply(const io_datagram& packet,
    const quic_invariant_header& header, const quic_path_identity& path) {
    constexpr std::array<std::byte, 4> versions{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}};
    quic_packet_write write;
    write.kind = quic_packet_kind::version_negotiation;
    write.destination = cid_bytes(header.source);
    write.source = cid_bytes(header.destination);
    write.versions = versions;
    std::vector<std::byte> bytes(64);
    auto encoded = encode_quic_packet(write, bytes);
    if (encoded.code != quic_codec_code::ok) return {};
    bytes.resize(encoded.value);
    unsigned char first = 0;
    if (RAND_bytes(&first, 1) != 1) return {};
    bytes[0] = std::byte(first | 0x80);
    return make_reply(packet, path, std::move(bytes));
}
quic_server_admission::result quic_server_admission::retry_reply(const io_datagram& packet,
    const quic_invariant_header& header, const quic_path_identity& path, std::uint64_t now) {
    quic_token_claims claims{header.destination, {}};
    claims.retry_source.size = limits_.cid_length;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(claims.retry_source.bytes.data()), claims.retry_source.size) != 1) return {};
    auto token = tokens_.issue(claims, path, limits_.listener_id, now);
    if (!token) return {};
    quic_packet_write write;
    write.kind = quic_packet_kind::retry;
    write.destination = cid_bytes(header.source);
    write.source = cid_bytes(claims.retry_source);
    write.token = *token;
    std::array<std::byte, 16> tag{};
    write.tag = tag;
    std::vector<std::byte> bytes(160);
    auto encoded = encode_quic_packet(write, bytes);
    if (encoded.code != quic_codec_code::ok) return {};
    bytes.resize(encoded.value);
    if (compute_quic_retry_tag(cid_bytes(header.destination), std::span(bytes).first(bytes.size() - tag.size()), tag) != quic_crypto_code::ok) return {};
    std::copy(tag.begin(), tag.end(), bytes.end() - tag.size());
    return make_reply(packet, path, std::move(bytes));
}
quic_server_admission::result quic_server_admission::receive(std::shared_ptr<const io_datagram> packet, std::uint64_t now) {
    expire(now);
    if (!packet || packet->bytes.size() > k_max_datagram_bytes) return {};
    auto path = quic_datagram_path(*packet);
    if (!path) return {};
    auto routed = routes_.dispatch(packet);
    if (routed.code != datagram_dispatch_code::unknown) return {routed_code(routed.code), {}, {}};
    return receive_unknown(std::move(packet), *path, now);
}
quic_server_admission::result quic_server_admission::receive_unknown(std::shared_ptr<const io_datagram> packet,
    const quic_path_identity& path, std::uint64_t now) {
    auto header = extract_quic_invariant_header(packet->bytes, limits_.cid_length);
    if (!header || !header->long_header || header->destination.size == 0) return {};
    if (packet->bytes.size() < 1200 || header->version == 0) return {};
    if (header->version != 1) return version_reply(*packet, *header, path);
    return receive_initial(std::move(packet), path, *header, now);
}
quic_server_admission::result quic_server_admission::receive_initial(std::shared_ptr<const io_datagram> packet,
    const quic_path_identity& path, const quic_invariant_header& header, std::uint64_t now) {
    auto envelope = parse_quic_envelope(packet->bytes);
    if (envelope.code != quic_codec_code::ok || envelope.value.kind != quic_packet_kind::initial) return {};
    std::optional<quic_token_claims> claims;
    if (envelope.value.token.empty()) {
        if (!tokenless_destination_allowed(limits_, header.destination)) return {};
    } else {
        claims = tokens_.verify(envelope.value.token, path, limits_.listener_id, header.destination, now);
        if (!claims) return {};
    }
    if (!authenticate_initial(envelope.value.packet, envelope.value.destination)) return {};
    if (limits_.require_retry && !claims) return retry_reply(*packet, header, path, now);
    return retain(std::move(packet), path, header, claims, now);
}
quic_server_admission::result quic_server_admission::retain(std::shared_ptr<const io_datagram> packet,
    const quic_path_identity& path, const quic_invariant_header& header,
    const std::optional<quic_token_claims>& claims, std::uint64_t now) {
    for (const auto& entry : pending_) {
        if (entry->facts.budget->path() == path && entry->facts.destination == header.destination && entry->source == header.source) {
            entry->facts.budget->receive_datagram(packet->bytes.size());
            result outcome{quic_admission_code::pending, {}, {}};
            outcome.pending.entry_ = entry;
            return outcome;
        }
    }
    if (pending_.size() >= limits_.max_pending || packet->bytes.capacity() > limits_.max_retained_bytes - retained_bytes_) {
        return {quic_admission_code::capacity_exhausted, {}, {}};
    }
    auto entry = std::make_shared<pending_entry>();
    entry->facts.initial = std::move(packet);
    entry->facts.budget = std::make_shared<quic_amplification_budget>(path);
    entry->facts.budget->receive_datagram(entry->facts.initial->bytes.size());
    if (claims) entry->facts.budget->mark_address_validated();
    entry->facts.original_destination = claims ? claims->original_destination : header.destination;
    entry->facts.destination = header.destination;
    entry->source = header.source;
    entry->created = now;
    pending_.push_back(entry);
    retained_bytes_ += entry->facts.initial->bytes.capacity();
    result outcome{quic_admission_code::pending, {}, {}};
    outcome.pending.entry_ = entry;
    return outcome;
}
std::optional<quic_admission_facts> quic_server_admission::inspect(const pending_handle& handle) const {
    auto entry = handle.entry_.lock();
    if (!entry || std::find(pending_.begin(), pending_.end(), entry) == pending_.end()) return std::nullopt;
    return entry->facts;
}
bool quic_server_admission::cancel(const pending_handle& handle) {
    auto entry = handle.entry_.lock();
    auto found = std::find(pending_.begin(), pending_.end(), entry);
    if (found == pending_.end()) return false;
    retained_bytes_ -= entry->facts.initial->bytes.capacity();
    pending_.erase(found);
    return true;
}
void quic_server_admission::expire(std::uint64_t now) {
    std::erase_if(pending_, [&](const auto& entry) {
        if (now < entry->created || now - entry->created < limits_.pending_lifetime_seconds) return false;
        retained_bytes_ -= entry->facts.initial->bytes.capacity();
        return true;
    });
}
std::optional<quic_server_admission::promotion> quic_server_admission::promote(const pending_handle& handle,
    io_connection_owner::datagram_port owner, std::shared_ptr<datagram_sink> sink, std::uint64_t now) {
    expire(now);
    auto facts = inspect(handle);
    if (!facts) return std::nullopt;
    auto registered = routes_.register_cid(facts->destination, std::move(owner), std::move(sink));
    cancel(handle);
    if (!registered) return std::nullopt;
    if (routes_.dispatch(facts->initial).code != datagram_dispatch_code::routed) {
        routes_.remove(*registered);
        return std::nullopt;
    }
    return promotion{std::move(*registered), std::move(*facts)};
}
}  // namespace httpserver::detail
