/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_WEBSOCKET_HPP_
#define SRC_HTTPSERVER_WEBSOCKET_HPP_

#include <string>
#include <microhttpd.h>
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>

struct MHD_UpgradeResponseHandle;
struct MHD_WebSocketStream;

namespace httpserver {

class websocket {
public:
    void send(const std::string& message);
    std::string receive();
    bool receive(std::string& message, uint64_t timeout_milliseconds);
    bool disconnect() const;
private:
    /**
     * Sends all data of the given buffer via the TCP/IP socket
     *
     * @param fd  The TCP/IP socket which is used for sending
     * @param buf The buffer with the data to send
     * @param len The length in bytes of the data in the buffer
     */
    void send_raw(const char* buf, size_t len);
    void insert_into_receive_queue(const std::string& message);

    /* the TCP/IP socket for reading/writing */
    MHD_socket fd = 0;
    /* the UpgradeResponseHandle of libmicrohttpd (needed for closing the socket) */
    MHD_UpgradeResponseHandle* urh = nullptr;
    /* the websocket encode/decode stream */
    MHD_WebSocketStream* ws = nullptr;
    /* the possibly read data at the start (only used once) */
    char *extra_in = nullptr;
    size_t extra_in_size = 0;
    /* specifies whether the websocket shall be closed (1) or not (0) */
    bool disconnect_ = false;
    class websocket_handler* ws_handler = nullptr;
    std::mutex receive_mutex_;
    std::condition_variable receive_cv_;
    std::list<std::string> received_messages_;
    friend class webserver;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_STRING_UTILITIES_HPP_
