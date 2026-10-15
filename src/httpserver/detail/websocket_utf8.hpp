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
    bool push(unsigned byte) noexcept;
    bool complete() const noexcept { return remaining_ == 0; }
    static bool valid(std::span<const std::byte> data) noexcept;

 private:
    bool lead(unsigned b) noexcept;
    unsigned remaining_ = 0;
    unsigned lower_ = 0x80;
    unsigned upper_ = 0xbf;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_WEBSOCKET_UTF8_HPP_
