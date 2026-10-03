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

#ifndef SRC_HTTPSERVER_DETAIL_WEBSOCKET_UTF8_HPP_
#define SRC_HTTPSERVER_DETAIL_WEBSOCKET_UTF8_HPP_
#include <cstddef>
#include <cstdint>
#include <span>
namespace httpserver::detail {
// RFC 3629 scalar validator; lower/upper constrain the first continuation
// to reject overlong encodings, surrogate scalars, and values > U+10FFFF.
class websocket_utf8 {
 public:
    bool push(unsigned byte) noexcept {
        if (remaining_) {
            if (byte < lower_ || byte > upper_) return false;
            --remaining_; lower_ = 0x80; upper_ = 0xbf;
            return true;
        }
        if (byte <= 0x7f) return true;
        return lead(byte);
    }
    bool complete() const noexcept { return remaining_ == 0; }
    static bool valid(std::span<const std::byte> data) noexcept {
        websocket_utf8 v;
        for (auto b : data) if (!v.push(std::to_integer<unsigned>(b))) return false;
        return v.complete();
    }

 private:
    bool lead(unsigned b) noexcept {
        lower_ = 0x80; upper_ = 0xbf;
        if (b < 0xc2) return false;
        if (b <= 0xdf) {
            remaining_ = 1;
            return true;
        }
        if (b <= 0xef) {
            remaining_ = 2;
            if (b == 0xe0) lower_ = 0xa0;
            if (b == 0xed) upper_ = 0x9f;
            return true;
        }
        if (b <= 0xf4) {
            remaining_ = 3;
            if (b == 0xf0) lower_ = 0x90;
            if (b == 0xf4) upper_ = 0x8f;
            return true;
        }
        return false;
    }
    unsigned remaining_ = 0;
    unsigned lower_ = 0x80;
    unsigned upper_ = 0xbf;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_WEBSOCKET_UTF8_HPP_
