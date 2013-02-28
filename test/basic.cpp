#include "littletest.hpp"
#include <curl/curl.h>
#include <string>
#include <map>
#include "httpserver.hpp"

using namespace httpserver;
using namespace std;

size_t writefunc(void *ptr, size_t size, size_t nmemb, std::string *s)
{
    s->append((char*) ptr, size*nmemb);
    return size*nmemb;
}

size_t headerfunc(void *ptr, size_t size, size_t nmemb, map<string, string>* ss)
{
    string s_ptr((char*)ptr, size*nmemb);
    size_t pos = s_ptr.find(":");
    if(pos != string::npos)
        (*ss)[s_ptr.substr(0, pos)] = 
            s_ptr.substr(pos + 2, s_ptr.size() - pos - 4);
    return size*nmemb;
}

class simple_resource : public http_resource<simple_resource>
{
    public:
        void render_GET(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_POST(const http_request& req, http_response** res)
        {
            *res = new http_string_response(
                    req.get_arg("arg1")+req.get_arg("arg2"), 200, "text/plain"
            );
        }
};

class header_test_resource : public http_resource<header_test_resource>
{
    public:
        void render_GET(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
            (*res)->set_header("KEY", "VALUE");
        }
};

class complete_test_resource : public http_resource<complete_test_resource>
{
    public:
        void render_GET(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_POST(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_PUT(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_DELETE(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_CONNECT(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
};

class only_render_resource : public http_resource<only_render_resource>
{
    public:
        void render(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
};

LT_BEGIN_SUITE(basic_suite)

    webserver* ws;

    void set_up()
    {
        ws = new webserver(create_webserver(8080));
        ws->start(false);
    }

    void tear_down()
    {
        ws->stop();
        delete ws;
    }
LT_END_SUITE(basic_suite)

LT_BEGIN_AUTO_TEST(basic_suite, read_body)
    simple_resource* resource = new simple_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(read_body)

LT_BEGIN_AUTO_TEST(basic_suite, read_header)
    header_test_resource* resource = new header_test_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    map<string, string> ss;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, &ss);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    LT_CHECK_EQ(ss["KEY"], "VALUE");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(read_header)

LT_BEGIN_AUTO_TEST(basic_suite, complete)
    complete_test_resource* resource = new complete_test_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CONNECT");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(complete)

LT_BEGIN_AUTO_TEST(basic_suite, only_render)
    only_render_resource* resource = new only_render_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CONNECT");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

LT_END_AUTO_TEST(only_render)

LT_BEGIN_AUTO_TEST(basic_suite, postprocessor)
    simple_resource* resource = new simple_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg1=lib&arg2=httpserver");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "libhttpserver");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(postprocessor)


LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
