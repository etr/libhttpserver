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

#ifndef TEST_FUZZ_PROTOCOL_FUZZ_HPP_
#define TEST_FUZZ_PROTOCOL_FUZZ_HPP_
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
constexpr std::size_t protocol_fuzz_max_input = 65536;
inline void fuzz_check(bool condition) { if (!condition) std::abort(); }
void fuzz_http1_head(std::span<const std::byte>);
void fuzz_http1_body(std::span<const std::byte>);
void fuzz_websocket_codec(std::span<const std::byte>);
#endif  // TEST_FUZZ_PROTOCOL_FUZZ_HPP_
