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

#if !defined(HTTPSERVER_COMPILATION)
#error "hpack_connection.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HPACK_CONNECTION_HPP_
#define SRC_HTTPSERVER_DETAIL_HPACK_CONNECTION_HPP_

#include <httpserver/detail/hpack_decoder.hpp>
#include <httpserver/detail/hpack_encoder.hpp>

namespace httpserver::detail {
// One owner per HTTP/2 connection, never per stream. Both directions charge
// the same connection budget but share no compression state. The future HTTP/2
// engine must serialize calls in wire order and close on a terminal failure.
class hpack_connection {
 public:
    explicit hpack_connection(const server::resource_budget& budget) : encoder_(budget), decoder_(budget) {}
    hpack_connection(const hpack_connection&) = delete;
    hpack_connection& operator=(const hpack_connection&) = delete;
    hpack_encoder& encoder() noexcept { return encoder_; }
    hpack_decoder& decoder() noexcept { return decoder_; }

 private:
    hpack_encoder encoder_;
    hpack_decoder decoder_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HPACK_CONNECTION_HPP_
