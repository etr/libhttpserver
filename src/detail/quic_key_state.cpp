/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/rand.h>
#include <algorithm>
#include <limits>
#include <utility>
#include <httpserver/detail/quic_key_state.hpp>
#include "./quic_crypto_provider.hpp"
namespace httpserver::detail {
namespace {
bool valid_level(quic_key_level level) noexcept { return static_cast<std::size_t>(level) < 3; }
bool valid_direction(quic_key_direction direction) noexcept { return direction == quic_key_direction::read || direction == quic_key_direction::write; }
std::size_t index_of(quic_key_level level) noexcept { return static_cast<std::size_t>(level); }
std::uint64_t confidentiality_limit(quic_cipher_suite suite) noexcept {
    return suite == quic_cipher_suite::chacha20_poly1305_sha256 ? k_quic_max_integer : std::uint64_t{1} << 23;
}
quic_crypto_code make_dummy_keys(const quic_packet_keys& current, quic_packet_keys& output,
                                 quic_key_observer observer, void* argument) noexcept {
    const auto spec = quic_suite(current.suite);
    quic_temporary<44> random;
    auto material = std::span(random.bytes).first(spec->key_size + 12);
    if (RAND_priv_bytes_ex(nullptr, quic_data(material), material.size(), 0) != 1) return quic_crypto_code::provider_failure;
    try {
        quic_packet_keys staged;
        staged.suite = current.suite;
        staged.key = secure_bytes(material.first(spec->key_size), observer, argument);
        staged.iv = secure_bytes(material.last(12), observer, argument);
        staged.hp = secure_bytes(current.hp.bytes(), observer, argument);
        output = std::move(staged);
        return quic_crypto_code::ok;
    } catch (...) {
        return quic_crypto_code::provider_failure;
    }
}
// These fields remain untrusted until unprotect_quic_packet authenticates them.
quic_crypto_result inspect_header(const quic_packet_keys& keys, std::span<const std::byte> datagram,
                                  std::optional<std::size_t> cid_length, std::optional<std::uint64_t> largest) noexcept {
    auto envelope = parse_quic_envelope(datagram, cid_length);
    if (envelope.code != quic_codec_code::ok) return {};
    if (envelope.value.kind != quic_packet_kind::one_rtt) return {quic_crypto_code::invalid_key_transition};
    auto packet = envelope.value.packet;
    auto offset = envelope.value.packet_number_offset;
    if (packet.size() - offset < 20) return {quic_crypto_code::truncated};
    quic_temporary<5> mask;
    auto code = quic_header_mask(keys, packet.subspan(offset + 4, 16), mask.bytes);
    if (code != quic_crypto_code::ok) return {code};
    auto first = std::to_integer<std::uint8_t>(packet[0] ^ (mask.bytes[0] & std::byte{0x1f}));
    auto width = std::size_t{1} + (first & 3);
    quic_temporary<4> pn;
    for (std::size_t i = 0; i < width; ++i) pn.bytes[i] = packet[offset + i] ^ mask.bytes[i + 1];
    auto header = decode_quic_unprotected_header(envelope.value, {first, std::span(pn.bytes).first(width)}, largest);
    if (header.code != quic_codec_code::ok) return {};
    return {quic_crypto_code::ok, 0, 0, header.value};
}
}  // namespace
quic_key_state::quic_key_state(quic_usage_limits limits, quic_key_observer observer, void* argument) noexcept
    : limits_(limits), observer_(observer), argument_(argument) {
    limits_.encrypted_packets = std::min(limits_.encrypted_packets, k_quic_max_integer);
    limits_.authentication_failures = std::min(limits_.authentication_failures, std::uint64_t{1} << 52);
}
void quic_key_state::constrain_integrity(quic_cipher_suite suite) noexcept {
    const auto maximum = suite == quic_cipher_suite::chacha20_poly1305_sha256 ? std::uint64_t{1} << 36 : std::uint64_t{1} << 52;
    limits_.authentication_failures = std::min(limits_.authentication_failures, maximum);
}
quic_crypto_code quic_key_state::install_initial(quic_endpoint_role role, std::span<const std::byte> dcid) noexcept {
    auto& state = levels_[0];
    if (state.discarded) return quic_crypto_code::invalid_key_transition;
    if (role != quic_endpoint_role::client && role != quic_endpoint_role::server) return quic_crypto_code::malformed;
    quic_initial_keys staged;
    auto code = derive_quic_initial_keys(dcid, staged, observer_, argument_);
    if (code != quic_crypto_code::ok) return code;
    const bool client = role == quic_endpoint_role::client;
    state.read = std::move(client ? staged.server : staged.client);
    state.write = std::move(client ? staged.client : staged.server);
    state.encrypted = 0;
    return quic_crypto_code::ok;
}
quic_crypto_code quic_key_state::traffic_install_allowed(quic_key_level level, quic_key_direction direction) const noexcept {
    if (!valid_level(level) || !valid_direction(direction)) return quic_crypto_code::malformed;
    if (level == quic_key_level::initial) return quic_crypto_code::invalid_key_transition;
    if (levels_[index_of(level)].discarded) return quic_crypto_code::invalid_key_transition;
    if (level == quic_key_level::application && keys(level, direction)) return quic_crypto_code::invalid_key_transition;
    return quic_crypto_code::ok;
}
quic_crypto_code quic_key_state::install_traffic_secret(quic_key_level level, quic_key_direction direction, quic_cipher_suite suite, std::span<const std::byte> secret) noexcept {
    auto code = traffic_install_allowed(level, direction);
    if (code != quic_crypto_code::ok) return code;
    auto& state = levels_[index_of(level)];
    auto& destination = direction == quic_key_direction::read ? state.read : state.write;
    quic_packet_keys staged, next, dummy;
    code = derive_quic_packet_keys(suite, secret, staged, observer_, argument_);
    if (code != quic_crypto_code::ok) return code;
    staged.level = level;
    if (level == quic_key_level::application && direction == quic_key_direction::read) {
        code = derive_quic_next_keys(staged, next, observer_, argument_);
        if (code != quic_crypto_code::ok) return code;
        code = make_dummy_keys(staged, dummy, observer_, argument_);
        if (code != quic_crypto_code::ok) return code;
        application_.next_read = std::move(next);
        application_.dummy_read = std::move(dummy);
    }
    destination = std::move(staged);
    if (direction == quic_key_direction::write) state.encrypted = 0;
    constrain_integrity(suite);
    return quic_crypto_code::ok;
}
quic_crypto_code quic_key_state::prepare_next_application_keys(quic_key_direction direction) noexcept {
    if (!valid_direction(direction)) return quic_crypto_code::malformed;
    auto current = keys(quic_key_level::application, direction);
    if (!current) return quic_crypto_code::keys_unavailable;
    auto& destination = direction == quic_key_direction::read ? application_.next_read : application_.next_write;
    if (destination) return quic_crypto_code::ok;
    quic_packet_keys staged;
    auto code = derive_quic_next_keys(*current, staged, observer_, argument_);
    if (code == quic_crypto_code::ok) destination = std::move(staged);
    return code;
}
quic_crypto_code quic_key_state::commit_write_update() noexcept {
    if (application_.write_generation == std::numeric_limits<std::uint64_t>::max()) return quic_crypto_code::limit_reached;
    auto code = prepare_next_application_keys(quic_key_direction::write);
    if (code != quic_crypto_code::ok) return code;
    levels_[2].write = std::move(application_.next_write);
    application_.next_write.reset();
    levels_[2].encrypted = 0;
    ++application_.write_generation;
    return quic_crypto_code::ok;
}
quic_crypto_code quic_key_state::advance_write_keys(bool confirmed, bool acknowledged) noexcept {
    if (!confirmed || !acknowledged) return quic_crypto_code::invalid_key_transition;
    return commit_write_update();
}
quic_crypto_code quic_key_state::respond_to_peer_update(bool confirmed) noexcept {
    if (!confirmed) return quic_crypto_code::invalid_key_transition;
    if (application_.read_generation == application_.write_generation) return quic_crypto_code::ok;
    if (application_.write_generation == std::numeric_limits<std::uint64_t>::max()) return quic_crypto_code::limit_reached;
    if (application_.read_generation != application_.write_generation + 1) return quic_crypto_code::invalid_key_transition;
    return commit_write_update();
}
quic_crypto_code quic_key_state::write_allowed(quic_key_level level, std::uint64_t number) const noexcept {
    const auto& state = levels_[index_of(level)];
    const auto limit = std::min(limits_.encrypted_packets, confidentiality_limit(state.write->suite));
    if (state.encrypted >= limit) return level == quic_key_level::application ? quic_crypto_code::update_required : quic_crypto_code::limit_reached;
    if (state.largest_sent && number <= *state.largest_sent) return quic_crypto_code::invalid_key_transition;
    if (level == quic_key_level::application && application_.write_generation < application_.read_generation) return quic_crypto_code::invalid_key_transition;
    return quic_crypto_code::ok;
}
quic_crypto_result quic_key_state::protect_packet(quic_key_level level, quic_packet_write packet, std::span<std::byte> output, std::span<std::byte> scratch) noexcept {
    if (!valid_level(level)) return {};
    if (failures_ >= limits_.authentication_failures) return {quic_crypto_code::limit_reached};
    auto& state = levels_[index_of(level)];
    if (!state.write) return {quic_crypto_code::keys_unavailable};
    auto code = write_allowed(level, packet.packet_number);
    if (code != quic_crypto_code::ok) return {code};
    if (level == quic_key_level::application) packet.key_phase = (application_.write_generation & 1) != 0;
    auto result = protect_quic_packet(*state.write, packet, output, scratch);
    if (result.code == quic_crypto_code::ok) {
        ++state.encrypted;
        state.largest_sent = packet.packet_number;
    }
    return result;
}
quic_crypto_result quic_key_state::account_open(quic_crypto_result result) noexcept {
    if (result.code == quic_crypto_code::authentication_failed) {
        ++failures_;
        if (failures_ >= limits_.authentication_failures) return {quic_crypto_code::limit_reached};
    }
    return result;
}
quic_crypto_result quic_key_state::open_packet(quic_key_level level, std::span<const std::byte> datagram, std::optional<std::size_t> cid_length,
                                             std::optional<std::uint64_t> largest, bool complete, std::span<std::byte> output, std::span<std::byte> scratch) noexcept {
    if (!valid_level(level)) return {};
    if (failures_ >= limits_.authentication_failures) return {quic_crypto_code::limit_reached};
    auto key = keys(level, quic_key_direction::read);
    if (!key) return {quic_crypto_code::keys_unavailable};
    if (level != quic_key_level::application) return account_open(unprotect_quic_packet(*key, datagram, cid_length, largest, output, scratch));
    if (!complete) return {quic_crypto_code::invalid_key_transition};
    return account_open(open_application(datagram, cid_length, largest, output, scratch));
}
quic_crypto_code quic_key_state::select_application_keys(const quic_clear_header& header, const quic_packet_keys*& selected, bool& promoting) const noexcept {
    const auto number = header.packet_number;
    const bool same_phase = header.key_phase == ((application_.read_generation & 1) != 0);
    if (same_phase) {
        selected = &*levels_[2].read;
        return quic_crypto_code::ok;
    }
    if (application_.maximum_received && number <= *application_.maximum_received) {
        selected = application_.previous_read ? &*application_.previous_read : nullptr;
        return quic_crypto_code::ok;
    }
    selected = application_.next_read ? &*application_.next_read : nullptr;
    promoting = true;
    return quic_crypto_code::ok;
}
quic_crypto_code quic_key_state::check_application_header(const quic_clear_header& header, void* argument) noexcept {
    const auto& state = *static_cast<quic_key_state*>(argument);
    const auto& application = state.application_;
    const auto number = header.packet_number;
    const bool same_phase = header.key_phase == ((application.read_generation & 1) != 0);
    if (same_phase) {
        if (application.previous_maximum && number <= *application.previous_maximum) return quic_crypto_code::invalid_key_transition;
    } else if (application.minimum_received && number < *application.minimum_received) {
        return quic_crypto_code::ok;
    } else if (application.maximum_received && number <= *application.maximum_received) {
        return quic_crypto_code::invalid_key_transition;
    }
    return quic_crypto_code::ok;
}
void quic_key_state::record_application_open(const quic_clear_header& header, bool promoting) noexcept {
    auto& current = levels_[2].read;
    const auto number = header.packet_number;
    if (promoting) {
        application_.previous_read = std::move(current);
        current = std::move(application_.next_read);
        application_.next_read.reset();
        application_.previous_maximum = application_.maximum_received;
        application_.minimum_received = application_.maximum_received = number;
        ++application_.read_generation;
    } else if (header.key_phase == ((application_.read_generation & 1) != 0)) {
        application_.minimum_received = std::min(number, application_.minimum_received.value_or(number));
        application_.maximum_received = std::max(number, application_.maximum_received.value_or(number));
    } else {
        application_.previous_maximum = std::max(number, application_.previous_maximum.value_or(number));
    }
}
quic_crypto_result quic_key_state::open_application(std::span<const std::byte> datagram, std::optional<std::size_t> cid_length,
                                                  std::optional<std::uint64_t> largest, std::span<std::byte> output, std::span<std::byte> scratch) noexcept {
    const auto inspected = inspect_header(*levels_[2].read, datagram, cid_length, largest);
    if (inspected.code != quic_crypto_code::ok) return inspected;
    bool promoting = false;
    const quic_packet_keys* selected = nullptr;
    auto code = select_application_keys(inspected.header, selected, promoting);
    if (code != quic_crypto_code::ok) return {code};
    if (!selected) return quic_open_checked(*application_.dummy_read, datagram, cid_length, largest, output, scratch, {quic_reject_unavailable, nullptr});
    if (promoting && application_.read_generation == std::numeric_limits<std::uint64_t>::max()) return {quic_crypto_code::limit_reached};
    auto result = quic_open_checked(*selected, datagram, cid_length, largest, output, scratch, {check_application_header, this});
    if (result.code == quic_crypto_code::ok) record_application_open(result.header, promoting);
    return result;
}
void quic_key_state::retire_previous_read_keys() noexcept { application_.previous_read.reset(); }
void quic_key_state::discard_level(quic_key_level level) noexcept {
    if (!valid_level(level)) return;
    auto& state = levels_[index_of(level)];
    state.read.reset();
    state.write.reset();
    state.discarded = true;
    if (level == quic_key_level::application) {
        application_.next_read.reset();
        application_.previous_read.reset();
        application_.next_write.reset();
        application_.dummy_read.reset();
    }
}
void quic_key_state::clear() noexcept {
    for (auto level : {quic_key_level::initial, quic_key_level::handshake, quic_key_level::application}) discard_level(level);
}
const quic_packet_keys* quic_key_state::keys(quic_key_level level, quic_key_direction direction) const noexcept {
    if (!valid_level(level) || !valid_direction(direction)) return nullptr;
    const auto& state = levels_[index_of(level)];
    const auto& key = direction == quic_key_direction::read ? state.read : state.write;
    return key ? &*key : nullptr;
}
std::uint64_t quic_key_state::generation(quic_key_direction direction) const noexcept {
    return direction == quic_key_direction::read ? application_.read_generation : application_.write_generation;
}
}  // namespace httpserver::detail
