/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_codec.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_CODEC_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_CODEC_HPP_
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <cstdint>
#include <span>
namespace httpserver {
namespace detail {
constexpr std::uint64_t k_quic_max_integer = (std::uint64_t{1} << 62) - 1;
enum class quic_codec_code { ok, truncated, malformed, unsupported_version, limit_exceeded, no_space };
template <class T>
struct quic_decode_result {
    quic_codec_code code = quic_codec_code::malformed;
    std::size_t consumed = 0;
    T value{};
};
using quic_encode_result = quic_decode_result<std::size_t>;
struct quic_codec_limits {
    std::size_t max_input_bytes = 65535;
    std::size_t max_frames = 4096;
    std::size_t max_parameters = 256;
    std::size_t max_ack_ranges = 256;
};
// Cursors stop on the first failure. Public codecs use a local cursor and
// commit their result only on success, so an error always consumes zero bytes.
class quic_cursor {
 public:
    explicit quic_cursor(std::span<const std::byte> input) noexcept : input_(input) {}
    std::size_t position() const noexcept { return position_; }
    std::size_t remaining() const noexcept { return input_.size() - position_; }
    quic_codec_code code() const noexcept { return code_; }
    void fail(quic_codec_code code) noexcept {
        if (code_ == quic_codec_code::ok) code_ = code;
    }
    std::span<const std::byte> take(std::uint64_t length) noexcept {
        if (code_ != quic_codec_code::ok) return {};
        if (length > remaining()) {
            fail(quic_codec_code::truncated);
            return {};
        }
        auto result = input_.subspan(position_, static_cast<std::size_t>(length));
        position_ += result.size();
        return result;
    }
    std::uint64_t fixed(std::size_t length) noexcept {
        if (length > 8) {
            fail(quic_codec_code::malformed);
            return 0;
        }
        std::uint64_t value = 0;
        for (auto b : take(length))
            value = (value << 8) | std::to_integer<unsigned>(b);
        return value;
    }
    std::uint64_t varint() noexcept;

 private:
    std::span<const std::byte> input_;
    std::size_t position_ = 0;
    quic_codec_code code_ = quic_codec_code::ok;
};
// Default construction measures only. Encoders validate/measure once before
// writing; callers must keep inputs stable and disjoint from output.
class quic_writer {
 public:
    quic_writer() noexcept = default;
    explicit quic_writer(std::span<std::byte> output) noexcept : output_(output), measuring_(false) {}
    std::size_t position() const noexcept { return position_; }
    quic_codec_code code() const noexcept { return code_; }
    void fail(quic_codec_code code) noexcept {
        if (code_ == quic_codec_code::ok) code_ = code;
    }
    void bytes(std::span<const std::byte> value) noexcept {
        const auto begin = position_;
        if (!reserve(value.size())) return;
        if (!measuring_) std::copy(value.begin(), value.end(), output_.begin() + begin);
    }
    void zeros(std::size_t count) noexcept {
        const auto begin = position_;
        if (!reserve(count)) return;
        if (!measuring_) std::fill_n(output_.begin() + begin, count, std::byte{0});
    }
    void fixed(std::uint64_t value, std::size_t width) noexcept {
        if (width > 8) {
            fail(quic_codec_code::malformed);
            return;
        }
        std::array<std::byte, 8> storage{};
        for (std::size_t i = width; i > 0; --i) {
            storage[i - 1] = std::byte(value & 0xff);
            value >>= 8;
        }
        bytes(std::span(storage).first(width));
    }
    void varint(std::uint64_t value, std::size_t width = 0) noexcept;

 private:
    bool reserve(std::size_t size) noexcept {
        if (code_ != quic_codec_code::ok) return false;
        if (size > std::numeric_limits<std::size_t>::max() - position_) {
            fail(quic_codec_code::limit_exceeded);
            return false;
        }
        if (!measuring_ && size > output_.size() - position_) {
            fail(quic_codec_code::no_space);
            return false;
        }
        position_ += size;
        return true;
    }
    std::span<std::byte> output_;
    bool measuring_ = true;
    std::size_t position_ = 0;
    quic_codec_code code_ = quic_codec_code::ok;
};
template <class Emit>
quic_encode_result quic_transactional_write(std::span<std::byte> output, Emit emit) noexcept {
    quic_writer measure;
    emit(measure);
    if (measure.code() != quic_codec_code::ok) return {measure.code()};
    if (measure.position() > output.size()) return {quic_codec_code::no_space};
    quic_writer writer(output);
    emit(writer);
    return {quic_codec_code::ok, writer.position(), writer.position()};
}
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_CODEC_HPP_
