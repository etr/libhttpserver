#include "littletest.hpp"
#include <curl/curl.h>
#include "httpserver.hpp"

using namespace httpserver;

class simple_resource : public http_resource
{
    public:
        virtual void render_GET(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
};

LT_BEGIN_SUITE(basic_suite)

    webserver* ws;
    simple_resource* res;

    void set_up()
    {
        ws = new webserver(create_webserver(8080));
        res = new simple_resource();
        ws->register_resource("base", res);
        ws->start(false);
    }

    void tier_down()
    {
        ws->stop();
        delete ws;
        delete res;
    }
LT_END_SUITE(basic_suite)

LT_BEGIN_AUTO_TEST(basic_suite, read_body)
    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);

    LT_ASSERT_EQ(res, 0);

LT_END_AUTO_TEST(read_body)


LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
