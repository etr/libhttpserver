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

#include <curl/curl.h>
#include <map>
#include <sstream>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/string_utilities.hpp"
#include "./littletest.hpp"

using std::string;
using std::map;
using std::shared_ptr;
using std::vector;
using std::stringstream;

using httpserver::http_resource;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::string_response;
using httpserver::file_response;
using httpserver::webserver;
using httpserver::create_webserver;

string lorem_ipsum(" , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat. , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat. , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat. , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat. , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat.");  // NOLINT

size_t writefunc(void *ptr, size_t size, size_t nmemb, string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

size_t headerfunc(void *ptr, size_t size, size_t nmemb, map<string, string>* ss) {
    string s_ptr(reinterpret_cast<char*>(ptr), size * nmemb);
    size_t pos = s_ptr.find(":");
    if (pos != string::npos) {
        (*ss)[s_ptr.substr(0, pos)] = s_ptr.substr(pos + 2, s_ptr.size() - pos - 4);
    }
    return size*nmemb;
}

class simple_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }
     shared_ptr<http_response> render_POST(const http_request& req) {
         return shared_ptr<string_response>(new string_response(std::string(req.get_arg("arg1")) + std::string(req.get_arg("arg2")), 200, "text/plain"));
     }
};

class args_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return shared_ptr<string_response>(new string_response(std::string(req.get_arg("arg")) + std::string(req.get_arg("arg2")), 200, "text/plain"));
     }
};

class long_content_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<string_response>(new string_response(lorem_ipsum, 200, "text/plain"));
     }
};

class header_set_test_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         shared_ptr<string_response> hrb(new string_response("OK", 200, "text/plain"));
         hrb->with_header("KEY", "VALUE");
         return hrb;
     }
};

class cookie_set_test_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         shared_ptr<string_response> hrb(new string_response("OK", 200, "text/plain"));
         hrb->with_cookie("MyCookie", "CookieValue");
         return hrb;
     }
};

class cookie_reading_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return shared_ptr<string_response>(new string_response(std::string(req.get_cookie("name")), 200, "text/plain"));
     }
};

class header_reading_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return shared_ptr<string_response>(new string_response(std::string(req.get_header("MyHeader")), 200, "text/plain"));
     }
};

class full_args_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return shared_ptr<string_response>(new string_response(std::string(req.get_args().at("arg")), 200, "text/plain"));
     }
};

class querystring_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return shared_ptr<string_response>(new string_response(std::string(req.get_querystring()), 200, "text/plain"));
     }
};

class path_pieces_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         stringstream ss;
         for (unsigned int i = 0; i < req.get_path_pieces().size(); i++) {
             ss << req.get_path_piece(i) << ",";
         }
         return shared_ptr<string_response>(new string_response(ss.str(), 200, "text/plain"));
     }
};

class complete_test_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }

     shared_ptr<http_response> render_POST(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }

     shared_ptr<http_response> render_PUT(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }

     shared_ptr<http_response> render_DELETE(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }

     shared_ptr<http_response> render_CONNECT(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }

     shared_ptr<http_response> render_PATCH(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }
};

class only_render_resource : public http_resource {
 public:
     shared_ptr<http_response> render(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }
};

class ok_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }
};

class nok_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<string_response>(new string_response("NOK", 200, "text/plain"));
     }
};

class no_response_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<http_response>(new http_response());
     }
};

class empty_response_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<http_response>(nullptr);
     }
};

class file_response_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<file_response>(new file_response("test_content", 200, "text/plain"));
     }
};

class file_response_resource_empty : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<file_response>(new file_response("test_content_empty", 200, "text/plain"));
     }
};

class file_response_resource_default_content_type : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<file_response>(new file_response("test_content", 200));
     }
};

class file_response_resource_missing : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<file_response>(new file_response("missing", 200));
     }
};

class file_response_resource_dir : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<file_response>(new file_response("integ", 200));
     }
};

class exception_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         throw std::domain_error("invalid");
     }
};

class error_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         throw "invalid";
     }
};

class print_request_resource : public http_resource {
 public:
     explicit print_request_resource(stringstream* ss) {
         this->ss = ss;
     }

     shared_ptr<http_response> render_GET(const http_request& req) {
         (*ss) << req;
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }

 private:
     stringstream* ss;
};

class print_response_resource : public http_resource {
 public:
     explicit print_response_resource(stringstream* ss) {
         this->ss = ss;
     }

     shared_ptr<http_response> render_GET(const http_request&) {
         shared_ptr<string_response> hresp(new string_response("OK", 200, "text/plain"));

         hresp->with_header("MyResponseHeader", "MyResponseHeaderValue");
         hresp->with_footer("MyResponseFooter", "MyResponseFooterValue");
         hresp->with_cookie("MyResponseCookie", "MyResponseCookieValue");

         (*ss) << *hresp;

         return hresp;
     }

 private:
     stringstream* ss;
};

LT_BEGIN_SUITE(basic_suite)
    webserver* ws;

    void set_up() {
        ws = new webserver(create_webserver(8080));
        ws->start(false);
    }

    void tear_down() {
        ws->stop();
        delete ws;
    }
LT_END_SUITE(basic_suite)

LT_BEGIN_AUTO_TEST(basic_suite, server_runs)
    LT_CHECK_EQ(ws->is_running(), true);
LT_END_AUTO_TEST(server_runs)

LT_BEGIN_AUTO_TEST(basic_suite, two_endpoints)
    ok_resource ok;
    ws->register_resource("OK", &ok);
    nok_resource nok;
    ws->register_resource("NOK", &nok);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    {
        CURL *curl = curl_easy_init();
        CURLcode res;
        curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/OK");
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        curl_easy_cleanup(curl);
    }

    string t;
    {
        CURL *curl = curl_easy_init();
        CURLcode res;
        curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/NOK");
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(t, "NOK");
        curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(two_endpoints)

LT_BEGIN_AUTO_TEST(basic_suite, read_body)
    simple_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
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

LT_BEGIN_AUTO_TEST(basic_suite, read_long_body)
    long_content_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s.size(), lorem_ipsum.size());
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(read_long_body)

LT_BEGIN_AUTO_TEST(basic_suite, resource_setting_header)
    header_set_test_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    map<string, string> ss;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ss);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    LT_CHECK_EQ(ss["KEY"], "VALUE");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(resource_setting_header)

LT_BEGIN_AUTO_TEST(basic_suite, resource_setting_cookie)
    cookie_set_test_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);

    struct curl_slist *cookies;
    curl_easy_getinfo(curl, CURLINFO_COOKIELIST, &cookies);

    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    string read_cookie = "";

    read_cookie = cookies->data;
    curl_slist_free_all(cookies);

    vector<string> cookie_parts = httpserver::string_utilities::string_split(read_cookie, '\t', false);
    LT_CHECK_EQ(cookie_parts[5], "MyCookie");
    LT_CHECK_EQ(cookie_parts[6], "CookieValue");

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(resource_setting_cookie)

LT_BEGIN_AUTO_TEST(basic_suite, request_with_header)
    header_reading_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

    struct curl_slist *list = nullptr;
    list = curl_slist_append(list, "MyHeader: MyValue");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "MyValue");
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(request_with_header)

LT_BEGIN_AUTO_TEST(basic_suite, request_with_cookie)
    cookie_reading_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_COOKIE, "name=myname; present=yes;");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "myname");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(request_with_cookie)

LT_BEGIN_AUTO_TEST(basic_suite, complete)
    complete_test_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }
/*
    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CONNECT");
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }
*/

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, nullptr);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(complete)

LT_BEGIN_AUTO_TEST(basic_suite, only_render)
    only_render_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL* curl;
    CURLcode res;

    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

/*
    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CONNECT");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
*/

    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "NOT_EXISTENT");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "Method not Allowed");
    curl_easy_cleanup(curl);

    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, nullptr);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(only_render)

LT_BEGIN_AUTO_TEST(basic_suite, postprocessor)
    simple_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
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

LT_BEGIN_AUTO_TEST(basic_suite, empty_arg)
    simple_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(empty_arg)

LT_BEGIN_AUTO_TEST(basic_suite, no_response)
    no_response_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(no_response)

LT_BEGIN_AUTO_TEST(basic_suite, empty_response)
    empty_response_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(empty_response)

LT_BEGIN_AUTO_TEST(basic_suite, regex_matching)
    simple_resource resource;
    ws->register_resource("regex/matching/number/[0-9]+", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/regex/matching/number/10");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(regex_matching)

LT_BEGIN_AUTO_TEST(basic_suite, regex_matching_arg)
    args_resource resource;
    ws->register_resource("this/captures/{arg}/passed/in/input", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/this/captures/whatever/passed/in/input");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "whatever");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(regex_matching_arg)

LT_BEGIN_AUTO_TEST(basic_suite, regex_matching_arg_custom)
    args_resource resource;
    ws->register_resource("this/captures/numeric/{arg|([0-9]+)}/passed/in/input", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/this/captures/numeric/11/passed/in/input");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "11");
    curl_easy_cleanup(curl);
    }

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/this/captures/numeric/text/passed/in/input");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "Not Found");
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 404);
    curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(regex_matching_arg_custom)

LT_BEGIN_AUTO_TEST(basic_suite, querystring_processing)
    args_resource resource;
    ws->register_resource("this/captures/args/passed/in/the/querystring", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/this/captures/args/passed/in/the/querystring?arg=first&arg2=second");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "firstsecond");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(querystring_processing)

LT_BEGIN_AUTO_TEST(basic_suite, full_arguments_processing)
    full_args_resource resource;
    ws->register_resource("this/captures/args/passed/in/the/querystring", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/this/captures/args/passed/in/the/querystring?arg=argument");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "argument");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(full_arguments_processing)

LT_BEGIN_AUTO_TEST(basic_suite, querystring_query_processing)
    querystring_resource resource;
    ws->register_resource("this/captures/args/passed/in/the/querystring", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/this/captures/args/passed/in/the/querystring?arg1=value1&arg2=value2&arg3=value3");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "?arg1=value1&arg2=value2&arg3=value3");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(querystring_query_processing)

LT_BEGIN_AUTO_TEST(basic_suite, register_unregister)
    simple_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    {
    string s;
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
    }

    ws->unregister_resource("base");
    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 404);

    LT_CHECK_EQ(s, "Not Found");

    curl_easy_cleanup(curl);
    }

    ws->register_resource("base", &resource);
    {
    string s;
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
    }
LT_END_AUTO_TEST(register_unregister)

LT_BEGIN_AUTO_TEST(basic_suite, file_serving_resource)
    file_response_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "test content of file\n");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(file_serving_resource)

LT_BEGIN_AUTO_TEST(basic_suite, file_serving_resource_empty)
    file_response_resource_empty resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(file_serving_resource_empty)

LT_BEGIN_AUTO_TEST(basic_suite, file_serving_resource_default_content_type)
    file_response_resource_default_content_type resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    map<string, string> ss;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ss);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(ss["Content-Type"], "application/octet-stream");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(file_serving_resource_default_content_type)

LT_BEGIN_AUTO_TEST(basic_suite, file_serving_resource_missing)
    file_response_resource_missing resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "Internal Error");

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(file_serving_resource_missing)

LT_BEGIN_AUTO_TEST(basic_suite, file_serving_resource_dir)
    file_response_resource_dir resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "Internal Error");

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(file_serving_resource_dir)

LT_BEGIN_AUTO_TEST(basic_suite, exception_forces_500)
    exception_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "Internal Error");

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(exception_forces_500)

LT_BEGIN_AUTO_TEST(basic_suite, untyped_error_forces_500)
    error_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "Internal Error");

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(untyped_error_forces_500)

LT_BEGIN_AUTO_TEST(basic_suite, request_is_printable)
    stringstream ss;
    print_request_resource resource(&ss);
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

    struct curl_slist *list = nullptr;
    list = curl_slist_append(nullptr, "MyHeader: MyValue");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    res = curl_easy_perform(curl);
    curl_slist_free_all(list);

    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    string actual = ss.str();
    LT_CHECK_EQ(actual.find("GET Request") != string::npos, true);
    LT_CHECK_EQ(actual.find("Headers [") != string::npos, true);
    LT_CHECK_EQ(actual.find("Host") != string::npos, true);
    LT_CHECK_EQ(actual.find("Accept:\"*/*\"") != string::npos, true);
    LT_CHECK_EQ(actual.find("MyHeader:\"MyValue\"") != string::npos, true);
    LT_CHECK_EQ(actual.find("Version [ HTTP/1.1 ]") != string::npos, true);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(request_is_printable)

LT_BEGIN_AUTO_TEST(basic_suite, response_is_printable)
    stringstream ss;
    print_response_resource resource(&ss);
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

    struct curl_slist *list = nullptr;
    list = curl_slist_append(nullptr, "MyHeader: MyValue");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    res = curl_easy_perform(curl);
    curl_slist_free_all(list);

    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    string actual = ss.str();
    LT_CHECK_EQ(actual.find("Response [response_code:200]") != string::npos, true);
    LT_CHECK_EQ(actual.find("Headers [Content-Type:\"text/plain\" MyResponseHeader:\"MyResponseHeaderValue\" ]") != string::npos, true);
    LT_CHECK_EQ(actual.find("Footers [MyResponseFooter:\"MyResponseFooterValue\" ]") != string::npos, true);
    LT_CHECK_EQ(actual.find("Cookies [MyResponseCookie:\"MyResponseCookieValue\" ]") != string::npos, true);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(response_is_printable)

LT_BEGIN_AUTO_TEST(basic_suite, long_path_pieces)
    path_pieces_resource resource;
    ws->register_resource("/settings", &resource, true);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/settings/somestringthatisreallylong/with_really_a_lot_of_content/and_underscores_and_looooooooooooooooooong_stuff");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "settings,somestringthatisreallylong,with_really_a_lot_of_content,and_underscores_and_looooooooooooooooooong_stuff,");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(long_path_pieces)

LT_BEGIN_AUTO_TEST(basic_suite, url_with_regex_like_pieces)
    path_pieces_resource resource;
    ws->register_resource("/settings", &resource, true);
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/settings/{}");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "settings,{},");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(url_with_regex_like_pieces)

LT_BEGIN_AUTO_TEST(basic_suite, method_not_allowed_header)
    simple_resource resource;
    resource.disallow_all();
    resource.set_allowing("POST", true);
    resource.set_allowing("HEAD", true);
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    map<string, string> ss;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ss);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 405);
    // elements in http_resource::method_state are sorted (std::map)
    LT_CHECK_EQ(ss["Allow"], "HEAD, POST");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(method_not_allowed_header)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
