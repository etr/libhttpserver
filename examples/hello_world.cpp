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

using namespace httpserver;

class hello_world_resource : public http_resource {
	public:
        const http_response render(const http_request&);
        void set_some_data(const std::string &s) {data = s;}
        std::string data;
};

//using the render method you are able to catch each type of request you receive
const http_response hello_world_resource::render(const http_request& req)
{
    //it is possible to store data inside the resource object that can be altered
    //through the requests
    std::cout << "Data was: " << data << std::endl;
    std::string datapar = req.get_arg("data");
    set_some_data(datapar == "" ? "no data passed!!!" : datapar);
    std::cout << "Now data is:" << data << std::endl;

    //it is possible to send a response initializing an http_string_response
    //that reads the content to send in response from a string.
    return http_response_builder("Hello World!!!", 200).string_response();
}

int main()
{
    //it is possible to create a webserver passing a great number of parameters.
    //In this case we are just passing the port and the number of thread running.
    webserver ws = create_webserver(8080).start_method(http::http_utils::INTERNAL_SELECT).max_threads(5);

    hello_world_resource hwr;
    //this way we are registering the hello_world_resource to answer for the endpoint
    //"/hello". The requested method is called (if the request is a GET we call the render_GET
    //method. In case that the specific render method is not implemented, the generic "render"
    //method is called.
    ws.register_resource("/hello", &hwr, true);

    //This way we are putting the created webserver in listen. We pass true in order to have
    //a blocking call; if we want the call to be non-blocking we can just pass false to the
    //method.
    ws.start(true);
    return 0;
}
