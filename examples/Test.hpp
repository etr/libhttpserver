#ifndef _TEST_HPP_
#define _TEST_HPP_
#include <httpserver.hpp>

using namespace httpserver;

class Test : virtual public http_resource {
	public:
        Test();
		virtual http_response render_GET(const http_request&);
        virtual http_response render_PUT(const http_request&);
        virtual http_response render_POST(const http_request&);
};

class Test2 : virtual public http_resource {
	public:
        Test2();
        virtual http_response render_GET(const http_request&);
};

#endif
