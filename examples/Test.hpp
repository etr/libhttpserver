#ifndef _TEST_HPP_
#define _TEST_HPP_
#include <httpserver.hpp>

using namespace httpserver;

class Test : public http_resource<Test> {
	public:
        Test();
		void render_GET(const http_request&, http_response**);
        void render_PUT(const http_request&, http_response**);
        void render_POST(const http_request&, http_response**);
};

class Test2 : public http_resource<Test2> {
	public:
        Test2();
        void render_GET(const http_request&, http_response**);
};

#endif
