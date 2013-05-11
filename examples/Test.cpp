#include "Test.hpp"
#include <signal.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <stdio.h>
using namespace std;

webserver* ws_ptr;

/*
void signal_callback_handler(int signum)
{
    cout << "bye!" << endl;
    ws_ptr->stop();
}
*/
Test::Test() : http_resource() 
{
}

Test2::Test2() : http_resource() 
{
}

void Test::render_GET(const http_request& r, http_response** res)
{
/*	cout << r.get_version() << endl;
	cout << r.get_requestor() << endl;
	cout << r.get_requestor_port() << endl;
	cout << "PROVA: " << r.get_arg("prova") << endl;
	cout << "ALTRO: " << r.get_arg("altro") << endl;
    cout << "THUMB: " << r.get_arg("thumbId") << endl;
    cout << "COOKIE: " << r.get_cookie("auth") << endl;
    std::map<std::string, std::string, header_comparator> head;
    r.get_headers(head);
    for(std::map<std::string, std::string, header_comparator>::const_iterator it = head.begin(); it != head.end(); ++it)
        cout << (*it).first <<  "-> " << (*it).second << endl;
	string pp = r.get_arg("prova"); */

/*
    cout << r.get_querystring() << endl;
    *res = new http_file_response("/home/etr/progs/libhttpserver/test/noimg.png", 200, "image/png");
*/

    std::vector<std::string> topics;
    topics.push_back("prova");
    *res = new long_polling_receive_response("", 200, "", topics, false, 10, "keepalive\n");
}

void Test::render_POST(const http_request& r, http_response** res)
{
/*	fstream filestr;
	filestr.open("test.txt", fstream::out | fstream::app);
	filestr << r.get_content() << endl;
	filestr.close();
	cout << "DOPO" << endl;
    vector<string> vv = r.get_path_pieces();
    for(int i = 0; i < vv.size(); i++)
    {
        cout << vv[i] << endl;
    }
	return http_string_response("OK",200);*/

    /*
	http_string_response* s = new http_string_response("OK",100);
    s->set_header(http_utils::http_header_location, "B");
    s->set_cookie("Ciccio", "Puppo");
    s->set_cookie("Peppe", "Puppo");
    cout << s->get_cookie("Ciccio") << endl;
    *res = s;
    */

    *res = new long_polling_send_response("hi!!!!\n", "prova");
}

void Test2::render_GET(const http_request& r, http_response** res)
{
	cout << "D2" << endl;
    typedef std::map<std::string, std::string,
            httpserver::http::header_comparator> c_type;
    c_type c;
    r.get_cookies(c);
    for(c_type::const_iterator it = c.begin(); it != c.end(); ++it)
        cout << (*it).first << " -> " << (*it).second << endl;
	*res = new http_string_response("{\" var1 \" : \" "+r.get_arg("var1")+" \", \" var2 \" : \" "+r.get_arg("var2")+" \", \" var3 \" : \" "+r.get_arg("var3")+" \"}", 200);
}

void Test::render_PUT(const http_request& r, http_response** res)
{
	*res = new http_string_response(r.get_content(), 200);
}

int main()
{
//    signal(SIGINT, &signal_callback_handler);
	webserver ws = create_webserver(8080).max_threads(5);
    ws_ptr = &ws;
	Test dt = Test();
	Test2 dt2 = Test2();
    ws.register_resource(string("base/{var1}/{var2}/drop_test/{var3}/tail"), &dt2, true);
    ws.register_resource(string("other/side/{thumbId|[0-9]*}"), &dt, true);
    ws.register_resource(string("another/{thumbId|[0-9]*}"), &dt, true);
    ws.register_resource(string("edge/thumbnail"), &dt, true);
    ws.start(true);
	return 0;
}
