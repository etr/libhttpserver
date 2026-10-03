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

#ifndef SRC_HTTPSERVER_DETAIL_WEBSOCKET_DRIVER_HPP_
#define SRC_HTTPSERVER_DETAIL_WEBSOCKET_DRIVER_HPP_
#include <memory>
#include <span>
#include <httpserver/websocket/session.hpp>
namespace httpserver::detail {
// Ordered-stream adapter seam. The driver and application session share
// protocol state; destroying either owner cancels the session. A single
// transport driver must serialize copy_output/consume_output pairs.
class websocket_driver {
 public:
    explicit websocket_driver(websocket::options limits = {}, websocket::session::close_callback callback = {});
    ~websocket_driver();
    websocket_driver(const websocket_driver&) = delete;
    websocket_driver& operator=(const websocket_driver&) = delete;
    websocket::session take_session();
    // Retain and retry the unconsumed suffix when input admission blocks.
    websocket::feed_result feed(std::span<const std::byte> input);
    // Copy pins output; consume only bytes actually written. Controls
    // take priority between frames, never inside a partially sent frame.
    std::size_t copy_output(std::span<std::byte> destination);
    http::outcome consume_output(std::size_t count);
    websocket::queue_usage usage() const;
    void eof();
    void transport_failed(http::outcome reason);
    void cancel(http::outcome reason = {http::outcome_code::cancelled, "driver cancelled"});
 private:
    std::shared_ptr<websocket::detail::session_state> state_;
    bool session_taken_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_WEBSOCKET_DRIVER_HPP_
