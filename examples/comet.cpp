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

#include <httpserver.hpp>
#include <iostream>
#include <vector>

using namespace httpserver;

std::string topics_array[] = { "topic_1" };
std::vector<std::string> topics(topics_array, topics_array + sizeof(topics_array) / sizeof(std::string));

class comet_send_resource : public http_resource {
	public:
        const http_response render(const http_request& req)
        {
            return http_response_builder("Hi", 200).long_polling_send_response(topics_array[0]);
        }
};

class comet_listen_resource : public http_resource {
	public:
        const http_response render(const http_request& req)
        {
            return http_response_builder("OK", 200).long_polling_receive_response(topics);
        }
};

int main()
{
    //it is possible to create a webserver passing a great number of parameters.
    //In this case we are just passing the port and the number of thread running.
    webserver ws = create_webserver(8080).comet();

    comet_send_resource csr;
    comet_listen_resource clr;
    //this way we are registering the hello_world_resource to answer for the endpoint
    //"/hello". The requested method is called (if the request is a GET we call the render_GET
    //method. In case that the specific render method is not implemented, the generic "render"
    //method is called.
    ws.register_resource("/send", &csr, true);
    ws.register_resource("/listen", &clr, true);

    //This way we are putting the created webserver in listen. We pass true in order to have
    //a blocking call; if we want the call to be non-blocking we can just pass false to the
    //method.
    ws.start(true);
    return 0;
}
