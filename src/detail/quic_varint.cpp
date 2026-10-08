/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <bit>
#include <httpserver/detail/quic_varint.hpp>
namespace httpserver {
namespace detail {
namespace {
bool valid_varint_width(std::size_t width) noexcept {
    return width == 1 || width == 2 || width == 4 || width == 8;
}
std::uint64_t closest_packet_number(std::uint64_t expected, std::uint64_t candidate, std::uint64_t window) noexcept {
    if (expected >= window / 2 && candidate <= expected - window / 2 && candidate <= k_quic_max_integer - window) return candidate + window;
    if (candidate > expected + window / 2 && candidate >= window) return candidate - window;
    return candidate;
}
}  // namespace
std::size_t quic_varint_width(std::uint64_t value) noexcept {
    if (value < (std::uint64_t{1} << 6)) return 1;
    if (value < (std::uint64_t{1} << 14)) return 2;
    if (value < (std::uint64_t{1} << 30)) return 4;
    return value <= k_quic_max_integer ? 8 : 0;
}
quic_decode_result<std::uint64_t> decode_quic_varint(std::span<const std::byte> bytes) noexcept {
    if (bytes.empty()) return {quic_codec_code::truncated};
    const auto first = std::to_integer<unsigned>(bytes[0]);
    const std::size_t width = std::size_t{1} << (first >> 6);
    if (width > bytes.size()) return {quic_codec_code::truncated};
    std::uint64_t value = first & 0x3f;
    for (std::size_t i = 1; i < width; ++i)
        value = (value << 8) | std::to_integer<unsigned>(bytes[i]);
    return {quic_codec_code::ok, width, value};
}
quic_encode_result encode_quic_varint(std::uint64_t value, std::span<std::byte> output, std::size_t width) noexcept {
    const auto minimum = quic_varint_width(value);
    if (!width) width = minimum;
    if (!minimum || !valid_varint_width(width) || width < minimum) return {};
    if (width > output.size()) return {quic_codec_code::no_space};
    for (std::size_t i = width; i > 0; --i) {
        output[i - 1] = std::byte(value & 0xff);
        value >>= 8;
    }
    const unsigned prefix = std::countr_zero(width);
    output[0] |= std::byte(prefix << 6);
    return {quic_codec_code::ok, width, width};
}
quic_encode_result encode_quic_packet_number(std::uint64_t full, std::size_t width, std::span<std::byte> output) noexcept {
    if (!width || width > 4 || full > k_quic_max_integer) return {};
    if (width > output.size()) return {quic_codec_code::no_space};
    for (std::size_t i = width; i > 0; --i) {
        output[i - 1] = std::byte(full & 0xff);
        full >>= 8;
    }
    return {quic_codec_code::ok, width, width};
}
quic_decode_result<std::uint64_t> reconstruct_quic_packet_number(std::uint64_t truncated, std::size_t width, std::optional<std::uint64_t> largest) noexcept {
    if (!width || width > 4 || (largest && *largest > k_quic_max_integer)) return {};
    const auto window = std::uint64_t{1} << (width * 8);
    if (truncated >= window) return {};
    const auto expected = largest ? *largest + 1 : 0;
    const auto candidate = closest_packet_number(expected, (expected & ~(window - 1)) | truncated, window);
    if (candidate > k_quic_max_integer) return {};
    return {quic_codec_code::ok, width, candidate};
}
std::uint64_t quic_cursor::varint() noexcept {
    if (code_ != quic_codec_code::ok) return 0;
    const auto decoded = decode_quic_varint(input_.subspan(position_));
    if (decoded.code != quic_codec_code::ok) {
        fail(decoded.code);
        return 0;
    }
    position_ += decoded.consumed;
    return decoded.value;
}
void quic_writer::varint(std::uint64_t value, std::size_t width) noexcept {
    std::array<std::byte, 8> storage{};
    const auto result = encode_quic_varint(value, storage, width);
    if (result.code != quic_codec_code::ok) {
        fail(result.code);
        return;
    }
    bytes(std::span(storage).first(result.consumed));
}
}  // namespace detail
}  // namespace httpserver
