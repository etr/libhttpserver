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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_WEBSOCKET_HANDLER_HPP_
#define SRC_HTTPSERVER_WEBSOCKET_HANDLER_HPP_

#include <thread>

namespace httpserver { class websocket; }

namespace httpserver {

/**
 * Class representing a callable websocket resource.
**/
class websocket_handler {
 public:
     /**
      * Class destructor
     **/
     virtual ~websocket_handler() = default;

     /**
      * Method used to handle a websocket connection
      * @param ws Websocket
      * @return A thread object handling the websocket
     **/
     virtual std::thread handle_websocket(websocket* ws) = 0;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_WEBSOCKET_HANDLER_HPP_
