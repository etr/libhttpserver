/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#include <httpserver/detail/websocket_codec.hpp>
#include <limits>
#include <deque>
#include <vector>
#include <utility>
namespace httpserver::detail {
namespace ws = websocket;
namespace {
unsigned octet(std::byte b) { return std::to_integer<unsigned>(b); }
http::outcome protocol(const char* text) { return {http::outcome_code::protocol_error, text}; }
bool valid_opcode(unsigned opcode) {
    switch (opcode) {
        case 0: case 1: case 2: case 8: case 9: case 10: return true;
        default: return false;
    }
}
bool canonical_length(std::size_t length_end, std::uint64_t value) {
    if (length_end == 4) return value >= 126;
    if (length_end == 10) return value > 65535;
    return true;
}
}  // namespace
bool websocket_close_code(unsigned code) noexcept {
    if (code >= 3000 && code <= 4999) return true;
    if (code < 1000 || code > 1014) return false;
    return code != 1004 && code != 1005 && code != 1006;
}
http::outcome websocket_close_payload(std::span<const std::byte> payload) {
    if (payload.empty()) return http::outcome::okay();
    if (payload.size() == 1) return protocol("one-byte Close payload");
    if (!websocket_close_code((octet(payload[0]) << 8) | octet(payload[1]))) return protocol("invalid Close status");
    if (!websocket_utf8::valid(payload.subspan(2))) return protocol("invalid Close UTF-8");
    return http::outcome::okay();
}
std::vector<std::byte> websocket_encode(unsigned opcode, std::span<const std::byte> payload) {
    std::vector<std::byte> out;
    const std::size_t n = payload.size();
    out.reserve(n + (n < 126 ? 2 : n <= 65535 ? 4 : 10));
    out.push_back(std::byte(0x80 | opcode));
    if (n < 126) {
        out.push_back(std::byte(n));
    } else if (n <= 65535) {
        out.push_back(std::byte{126}); out.push_back(std::byte(n >> 8)); out.push_back(std::byte(n));
    } else {
        out.push_back(std::byte{127});
        for (int shift = 56; shift >= 0; shift -= 8) out.push_back(std::byte(static_cast<std::uint64_t>(n) >> shift));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
void websocket_codec::clear() {
    std::deque<ws::message>().swap(messages_);
    assembled_ = {}; std::vector<std::byte>().swap(control_payload_);
    control_.reset(); active_message_ = false; used_ = 0; reset_frame();
}
http::outcome websocket_codec::fail(http::outcome why) {
    clear(); failure_ = std::move(why); return failure_;
}
void websocket_codec::reset_frame() {
    header_size_ = 0; length_end_ = 2; header_end_ = 2;
    length_ = 0; payload_read_ = 0; admitted_ = false;
}
http::outcome websocket_codec::check_sequence() const {
    if (opcode_ == 0 && !active_message_) return protocol("unexpected continuation");
    if ((opcode_ == 1 || opcode_ == 2) && active_message_) return protocol("fragmented message interrupted");
    return http::outcome::okay();
}
http::outcome websocket_codec::check_base() {
    const unsigned first = octet(header_[0]), second = octet(header_[1]);
    opcode_ = first & 15; final_ = (first & 128) != 0;
    if ((first & 112) != 0 || (second & 128) == 0) return protocol("RSV or incoming mask violation");
    if (!valid_opcode(opcode_))
        return protocol("reserved opcode");
    const unsigned size = second & 127;
    if (opcode_ >= 8 && (!final_ || size > 125)) return protocol("invalid control framing");
    auto sequence = check_sequence();
    if (!sequence.ok()) return sequence;
    length_end_ = size < 126 ? 2 : size == 126 ? 4 : 10;
    header_end_ = length_end_ + 4;
    return http::outcome::okay();
}
http::outcome websocket_codec::check_length() {
    std::uint64_t value = octet(header_[1]) & 127;
    if (length_end_ != 2) {
        value = 0;
        for (std::size_t i = 2; i < length_end_; ++i) value = (value << 8) | octet(header_[i]);
        if (length_end_ == 10 && (octet(header_[2]) & 128)) return protocol("length high bit set");
        if (!canonical_length(length_end_, value)) return protocol("noncanonical length");
    }
    if (value > std::numeric_limits<std::size_t>::max()) return protocol("length overflow");
    if (opcode_ < 8 && value > limits_.max_message_bytes - assembled_.data.size())
        return {http::outcome_code::limit_exceeded, "message limit exceeded"};
    length_ = static_cast<std::size_t>(value);
    return http::outcome::okay();
}
bool websocket_codec::admit() {
    if (opcode_ >= 8) {
        admitted_ = true; return true;
    }
    if (!active_message_ && messages_.size() >= limits_.incoming_messages) return false;
    if (length_ > limits_.incoming_bytes - used_) return false;
    if (!active_message_) {
        active_message_ = true;
        assembled_.kind = static_cast<ws::message_kind>(opcode_); utf8_ = {};
    }
    used_ += length_; admitted_ = true;
    return true;
}
bool websocket_codec::payload_byte(std::byte b) {
    const std::byte decoded = b ^ header_[length_end_ + payload_read_ % 4];
    ++payload_read_;
    if (opcode_ >= 8) {
        control_payload_.push_back(decoded); return true;
    }
    if (assembled_.kind == ws::message_kind::text && !utf8_.push(octet(decoded))) return false;
    assembled_.data.push_back(decoded); return true;
}
http::outcome websocket_codec::finish() {
    if (opcode_ >= 8) {
        if (opcode_ == 8) {
            auto valid = websocket_close_payload(control_payload_);
            if (!valid.ok()) return valid;
        }
        control_ = websocket_control{opcode_, std::move(control_payload_)};
        control_payload_ = {};
    } else if (final_) {
        if (assembled_.kind == ws::message_kind::text && !utf8_.complete()) return protocol("incomplete text UTF-8");
        messages_.push_back(std::move(assembled_)); assembled_ = {}; active_message_ = false;
    }
    reset_frame(); return http::outcome::okay();
}
http::outcome websocket_codec::read_header(std::span<const std::byte> input, std::size_t& consumed) {
    while (header_size_ < header_end_) {
        if (consumed == input.size()) break;
        header_[header_size_++] = input[consumed++];
        if (header_size_ == 2) {
            auto status = check_base();
            if (!status.ok()) return status;
        }
        if (header_size_ >= 2 && header_size_ == length_end_) {
            auto status = check_length();
            if (!status.ok()) return status;
        }
    }
    return http::outcome::okay();
}
http::outcome websocket_codec::read_payload(std::span<const std::byte> input, std::size_t& consumed) {
    while (payload_read_ < length_) {
        if (consumed == input.size()) break;
        if (!payload_byte(input[consumed++])) return protocol("invalid text UTF-8");
    }
    return http::outcome::okay();
}
ws::feed_result websocket_codec::feed(std::span<const std::byte> input) {
    ws::feed_result result{failure_, 0, false};
    if (!failure_.ok()) return result;
    if (control_) {
        result.blocked = true;
        return result;
    }
    auto header_status = read_header(input, result.consumed);
    if (!header_status.ok()) {
        result.status = fail(std::move(header_status));
        return result;
    }
    if (header_size_ < header_end_) return result;
    if (!admitted_ && !admit()) {
        result.blocked = true;
        return result;
    }
    auto payload_status = read_payload(input, result.consumed);
    if (!payload_status.ok()) {
        result.status = fail(std::move(payload_status));
        return result;
    }
    if (payload_read_ < length_) return result;
    auto status = finish();
    if (!status.ok()) result.status = fail(std::move(status));
    return result;
}
std::optional<ws::message> websocket_codec::pop() {
    if (messages_.empty()) return std::nullopt;
    auto out = std::move(messages_.front()); messages_.pop_front(); used_ -= out.data.size(); return out;
}
std::optional<websocket_control> websocket_codec::take_control() {
    return std::exchange(control_, std::nullopt);
}
}  // namespace httpserver::detail
