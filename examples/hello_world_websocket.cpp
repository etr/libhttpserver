/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino

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

#include <iostream>

#include <httpserver.hpp>

#define CHAT_PAGE                                                             \
  "<html>\n"                                                                  \
  "<head>\n"                                                                  \
  "<title>WebSocket chat</title>\n"                                           \
  "<script>\n"                                                                \
  "document.addEventListener('DOMContentLoaded', function() {\n"              \
  "  const ws = new WebSocket('ws://' + window.location.host + '/ws');\n"     \
  "  const btn = document.getElementById('send');\n"                          \
  "  const msg = document.getElementById('msg');\n"                           \
  "  const log = document.getElementById('log');\n"                           \
  "  ws.onopen = function() {\n"                                              \
  "    log.value += 'Connected\\n';\n"                                        \
  "  };\n"                                                                    \
  "  ws.onclose = function() {\n"                                             \
  "    log.value += 'Disconnected\\n';\n"                                     \
  "  };\n"                                                                    \
  "  ws.onmessage = function(ev) {\n"                                         \
  "    log.value += ev.data + '\\n';\n"                                       \
  "  };\n"                                                                    \
  "  btn.onclick = function() {\n"                                            \
  "    log.value += '<You>: ' + msg.value + '\\n';\n"                         \
  "    ws.send(msg.value);\n"                                                 \
  "  };\n"                                                                    \
  "  msg.onkeyup = function(ev) {\n"                                          \
  "    if (ev.keyCode === 13) {\n"                                            \
  "      ev.preventDefault();\n"                                              \
  "      ev.stopPropagation();\n"                                             \
  "      btn.click();\n"                                                      \
  "      msg.value = '';\n"                                                   \
  "    }\n"                                                                   \
  "  };\n"                                                                    \
  "});\n"                                                                     \
  "</script>\n"                                                               \
  "</head>\n"                                                                 \
  "<body>\n"                                                                  \
  "<input type='text' id='msg' autofocus/>\n"                                 \
  "<input type='button' id='send' value='Send' /><br /><br />\n"              \
  "<textarea id='log' rows='20' cols='28'></textarea>\n"                      \
  "</body>\n"                                                                 \
  "</html>"

class hello_world_resource : public httpserver::http_resource, public httpserver::websocket_handler {
 public:
     const std::shared_ptr<httpserver::http_response> render(const httpserver::http_request&);
     virtual std::thread handle_websocket(httpserver::websocket* ws) override;
};

// Using the render method you are able to catch each type of request you receive
const std::shared_ptr<httpserver::http_response> hello_world_resource::render(const httpserver::http_request& req) {
    // It is possible to send a response initializing an http_string_response that reads the content to send in response from a string.
    return std::shared_ptr<httpserver::http_response>(new httpserver::string_response(CHAT_PAGE, 200, "text/html"));
}

std::thread hello_world_resource::handle_websocket(httpserver::websocket* ws) {
    return std::thread([ws]{
        while (!ws->disconnect()) {
            ws->send("hello world");
            usleep(1000 * 1000);
            std::string message;
            if (ws->receive(message, 100)) {
                ws->send("server received: " + message);
            }
        }
    });
}

int main() {
    // It is possible to create a webserver passing a great number of parameters. In this case we are just passing the port and the number of thread running.
    httpserver::webserver ws = httpserver::create_webserver(8080).start_method(httpserver::http::http_utils::INTERNAL_SELECT).max_threads(1);

    hello_world_resource hwr;
    // This way we are registering the hello_world_resource to answer for the endpoint
    // "/hello". The requested method is called (if the request is a GET we call the render_GET
    // method. In case that the specific render method is not implemented, the generic "render"
    // method is called.
    ws.register_resource("/hello", &hwr, true);
    ws.register_resource("/ws", &hwr, true);

    // This way we are putting the created webserver in listen. We pass true in order to have
    // a blocking call; if we want the call to be non-blocking we can just pass false to the method.
    ws.start(true);
    return 0;
}
