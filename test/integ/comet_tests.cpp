/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#if defined(__MINGW32__) || defined(__CYGWIN32__)
#define _WINDOWS
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x600
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include "littletest.hpp"
#include <curl/curl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "httpserver.hpp"
#include <unistd.h>
#include <signal.h>

#define MY_OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

using namespace std;
using namespace httpserver;

struct closure_type
{
    std::string s;
    int counter;
};

size_t writefunc_listener(void *ptr, size_t size, size_t nmemb, closure_type* cls)
{
    char* new_content = (char*) ptr;
    if (cls->counter > 0)
    {
        return 0;
    }

    cls->counter++;
    std::string prefix = "RECEIVED: ";
    cls->s.insert(0, prefix);
    cls->s.append(new_content, size*nmemb);
    return size*nmemb;
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, std::string* s)
{
    s->append((char*) ptr, size*nmemb);
    return size*nmemb;
}

class polling_resource : public httpserver::http_resource
{
    public:
        const httpserver::http_response render_GET(const httpserver::http_request& req)
        {
            std::string topic = "interesting_topic";
            if (req.get_arg("action") == "receive")
            {
                std::vector<std::string> topics;
                topics.push_back(topic);
                return httpserver::http_response_builder("interesting listener").long_polling_receive_response(topics);
            }
            else
            {
                return httpserver::http_response_builder("interesting message").long_polling_send_response(topic);
            }
        }
};

LT_BEGIN_SUITE(authentication_suite)
    void set_up()
    {
    }

    void tear_down()
    {
    }
LT_END_SUITE(authentication_suite)

void* listener(void* par)
{
    curl_global_init(CURL_GLOBAL_ALL);

    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base?action=receive");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc_listener);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (closure_type*) par);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return 0x0;
}

LT_BEGIN_AUTO_TEST(authentication_suite, comet)
    webserver* ws = new webserver(create_webserver(8080).comet());

    polling_resource* pr = new polling_resource();
    ws->register_resource("base", pr);
    ws->start(false);

    closure_type cls;
    cls.counter = 0;

    pthread_t tid;
    pthread_create(&tid, NULL, listener, (void *) &cls);

    sleep(1);

    {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base?action=send");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "interesting message");
    curl_easy_cleanup(curl);
    }

    {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base?action=send");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "interesting message");
    curl_easy_cleanup(curl);
    }

    pthread_join(tid, NULL);

    LT_CHECK_EQ(cls.s, "RECEIVED: interesting message");
    //not stopping to avoid errors with pending connections
LT_END_AUTO_TEST(comet)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
