#include "Test.hpp"
#include <string>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <stdio.h>
using namespace std;

Test::Test() : HttpResource() 
{
}

Test2::Test2() : HttpResource() 
{
}

HttpResponse Test::render_GET(const HttpRequest& r)
{
	cout << r.getVersion() << endl;
	cout << r.getRequestor() << endl;
	cout << r.getRequestorPort() << endl;
	cout << "PROVA: " << r.getArg("prova") << endl;
	cout << "ALTRO: " << r.getArg("altro") << endl;
	string pp = r.getArg("prova");
	return HttpResponse(pp, 200);
}

HttpResponse Test::render_POST(const HttpRequest& r)
{
	cout << r.getContent() << endl;
	cout << "prova: " << r.getArg("prova") << endl;
	
	string pp = r.getArg("prova");
	cout << "DOPO" << endl;

	return HttpResponse(pp,200);
}

HttpResponse Test2::render_GET(const HttpRequest& r)
{
	cout << "D2" << endl;
	return HttpResponse("{\" var1 \" : \" "+r.getArg("var1")+" \", \" var2 \" : \" "+r.getArg("var2")+" \", \" var3 \" : \" "+r.getArg("var3")+" \"}", 200);
}

HttpResponse Test::render_PUT(const HttpRequest& r)
{
	return HttpResponse(r.getContent(), 200);
}

int main()
{
	Webserver* ws = new Webserver(9898, 5, 1);
	Test dt = Test();
	Test2 dt2 = Test2();
    ws->registerResource(string("base/{var1}/{var2}/drop_test/{var3}/tail"), &dt2, true);
    ws->registerResource(string("other"), &dt, true);
    ws->start(true);
	return 0;
}
