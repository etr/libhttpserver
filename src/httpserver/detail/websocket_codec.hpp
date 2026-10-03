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

#ifndef SRC_HTTPSERVER_DETAIL_WEBSOCKET_CODEC_HPP_
#define SRC_HTTPSERVER_DETAIL_WEBSOCKET_CODEC_HPP_
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>
#include <httpserver/detail/websocket_utf8.hpp>
#include <httpserver/websocket/message.hpp>
#include <httpserver/websocket/options.hpp>
namespace httpserver::detail {
struct websocket_control {
    unsigned opcode = 0;
    std::vector<std::byte> payload;
};
bool websocket_close_code(unsigned code) noexcept;
http::outcome websocket_close_payload(std::span<const std::byte> payload);
std::vector<std::byte> websocket_encode(unsigned opcode, std::span<const std::byte> payload);
// Requires client masking. Stops at each completed frame so the session
// can process controls without accumulating an unbounded control queue.
// Admissions reserve declared data length before payload allocation;
// active fragmented messages count together with completed messages.
class websocket_codec {
 public:
    explicit websocket_codec(websocket::options limits) : limits_(limits) { }
    websocket::feed_result feed(std::span<const std::byte> input);
    std::optional<websocket::message> pop();
    std::optional<websocket_control> take_control();
    std::size_t incoming_bytes() const noexcept { return used_; }
    std::size_t incoming_messages() const noexcept { return messages_.size() + (active_message_ ? 1 : 0); }
    bool input_ready() const noexcept {
        if (admitted_ || header_size_ < header_end_ || opcode_ >= 8) return true;
        return (active_message_ || messages_.size() < limits_.incoming_messages)
            && length_ <= limits_.incoming_bytes - used_;
    }
    bool has_message() const noexcept { return !messages_.empty(); }
    void clear();

 private:
    http::outcome read_header(std::span<const std::byte> input, std::size_t& consumed);
    http::outcome read_payload(std::span<const std::byte> input, std::size_t& consumed);
    http::outcome check_sequence() const;
    http::outcome check_base();
    http::outcome check_length();
    bool admit();
    http::outcome finish();
    http::outcome fail(http::outcome why);
    bool payload_byte(std::byte b);
    void reset_frame();
    websocket::options limits_;
    http::outcome failure_;
    std::array<std::byte, 14> header_{};
    std::size_t header_size_ = 0, length_end_ = 2, header_end_ = 2;
    std::size_t length_ = 0, payload_read_ = 0, used_ = 0;
    unsigned opcode_ = 0;
    bool final_ = false, admitted_ = false;
    bool active_message_ = false;
    websocket::message assembled_;
    websocket_utf8 utf8_;
    std::vector<std::byte> control_payload_;
    std::optional<websocket_control> control_;
    std::deque<websocket::message> messages_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_WEBSOCKET_CODEC_HPP_
