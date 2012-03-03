#ifndef _TEST_HPP_
#define _TEST_HPP_
#include <httpserver.h>

using namespace httpserver;

class Test : virtual public HttpResource {
	public:
        Test();
		virtual HttpResponse render_GET(const HttpRequest&);
        virtual HttpResponse render_PUT(const HttpRequest&);
        virtual HttpResponse render_POST(const HttpRequest&);
};

class Test2 : virtual public HttpResource {
	public:
        Test2();
        virtual HttpResponse render_GET(const HttpRequest&);
};

#endif
