#include "Test.hpp"
#include <string>
#include <vector>
#include <unistd.h>
#include <boost/thread.hpp>
#include <iostream>
#include <fstream>
#include <stdio.h>
using namespace std;

Test::Test() : http_resource() 
{
}

Test2::Test2() : http_resource() 
{
}

http_response Test::render_GET(const http_request& r)
{
	cout << r.get_version() << endl;
	cout << r.get_requestor() << endl;
	cout << r.get_requestor_port() << endl;
	cout << "PROVA: " << r.get_arg("prova") << endl;
	cout << "ALTRO: " << r.get_arg("altro") << endl;
	string pp = r.get_arg("prova");
	return http_file_response("/home/etr/progs/libhttpserver/test/readme", 200);
}

http_response Test::render_POST(const http_request& r)
{
	fstream filestr;
	filestr.open("test.txt", fstream::out | fstream::app);
	filestr << r.get_content() << endl;
	filestr.close();
	cout << "DOPO" << endl;
    vector<string> vv = r.get_path_pieces();
    for(int i = 0; i < vv.size(); i++)
    {
        cout << vv[i] << endl;
    }
	return http_string_response("OK",200);
}

http_response Test2::render_GET(const http_request& r)
{
	cout << "D2" << endl;
	return http_string_response("{\" var1 \" : \" "+r.get_arg("var1")+" \", \" var2 \" : \" "+r.get_arg("var2")+" \", \" var3 \" : \" "+r.get_arg("var3")+" \"}", 200);
}

http_response Test::render_PUT(const http_request& r)
{
	return http_string_response(r.get_content(), 200);
}

int main()
{
	webserver ws = create_webserver(9898);
	Test dt = Test();
	Test2 dt2 = Test2();
    ws.register_resource(string("base/{var1}/{var2}/drop_test/{var3}/tail"), &dt2, true);
    ws.register_resource(string("other/side"), &dt, true);
	boost::thread* t1 = new boost::thread(boost::bind(&webserver::start, ws, true));
	t1->join();
	return 0;
}
