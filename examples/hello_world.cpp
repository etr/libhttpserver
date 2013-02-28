#include <httpserver.hpp>

using namespace httpserver;

class hello_world_resource : public http_resource<hello_world_resource> {
	public:
        void render(const http_request&, http_response**);
};

//using the render method you are able to catch each type of request you receive
void hello_world_resource::render(const http_request& req, http_response** res)
{
    //it is possible to send a response initializing an http_string_response
    //that reads the content to send in response from a string.
    *res = new http_string_response("Hello World!!!", 200);
}

int main()
{
    //it is possible to create a webserver passing a great number of parameters.
    //In this case we are just passing the port and the number of thread running.
	webserver ws = create_webserver(8080).max_threads(5);

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
