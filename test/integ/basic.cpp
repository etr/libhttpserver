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
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }
     shared_ptr<http_response> render_POST(const http_request& req) {
         return std::make_shared<string_response>(std::string(req.get_arg("arg1")) + std::string(req.get_arg("arg2")), 200, "text/plain");
     }
};

class large_post_resource_last_value : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }
     shared_ptr<http_response> render_POST(const http_request& req) {
         return std::make_shared<string_response>(std::string(req.get_arg("arg1").get_all_values().back()), 200, "text/plain");
     }
};

class large_post_resource_first_value : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }
     shared_ptr<http_response> render_POST(const http_request& req) {
         return std::make_shared<string_response>(std::string(req.get_arg("arg1").get_all_values().front()), 200, "text/plain");
     }
};

class arg_value_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }
     shared_ptr<http_response> render_POST(const http_request& req) {
         auto const arg_value = req.get_arg("arg").get_all_values();
         for (auto const & a : arg_value) {
            std::cerr << a << std::endl;
         }
         std::string all_values = std::accumulate(std::next(arg_value.begin()), arg_value.end(), std::string(arg_value[0]), [](std::string a, std::string_view in) {
            return std::move(a) + std::string(in);
         });
         return std::make_shared<string_response>(all_values, 200, "text/plain");
     }
};

class args_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return std::make_shared<string_response>(std::string(req.get_arg("arg")) + std::string(req.get_arg("arg2")), 200, "text/plain");
     }
};

class args_flat_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         auto args = req.get_args_flat();
         stringstream ss;
         for (const auto& [key, value] : args) {
             ss << key << "=" << value << ";";
         }
         return std::make_shared<string_response>(ss.str(), 200, "text/plain");
     }
};

class long_content_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>(lorem_ipsum, 200, "text/plain");
     }
};

class header_set_test_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         auto hrb = std::make_shared<string_response>("OK", 200, "text/plain");
         hrb->with_header("KEY", "VALUE");
         return hrb;
     }
};

class cookie_set_test_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         auto hrb = std::make_shared<string_response>("OK", 200, "text/plain");
         hrb->with_cookie("MyCookie", "CookieValue");
         return hrb;
     }
};

class cookie_reading_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return std::make_shared<string_response>(std::string(req.get_cookie("name")), 200, "text/plain");
     }
};

class header_reading_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return std::make_shared<string_response>(std::string(req.get_header("MyHeader")), 200, "text/plain");
     }
};

class full_args_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return std::make_shared<string_response>(std::string(req.get_args().at("arg")), 200, "text/plain");
     }
};

class querystring_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         return std::make_shared<string_response>(std::string(req.get_querystring()), 200, "text/plain");
     }
};

class path_pieces_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         stringstream ss;
         for (unsigned int i = 0; i < req.get_path_pieces().size(); i++) {
             ss << req.get_path_piece(i) << ",";
         }
         return std::make_shared<string_response>(ss.str(), 200, "text/plain");
     }
};

class complete_test_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }

     shared_ptr<http_response> render_POST(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }

     shared_ptr<http_response> render_PUT(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }

     shared_ptr<http_response> render_DELETE(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }

     shared_ptr<http_response> render_PATCH(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }

     shared_ptr<http_response> render_HEAD(const http_request&) {
         return std::make_shared<string_response>("", 200, "text/plain");
     }

     shared_ptr<http_response> render_OPTIONS(const http_request&) {
         auto resp = std::make_shared<string_response>("", 200, "text/plain");
         resp->with_header("Allow", "GET, POST, PUT, DELETE, HEAD, OPTIONS");
         return resp;
     }

     shared_ptr<http_response> render_TRACE(const http_request&) {
         return std::make_shared<string_response>("TRACE OK", 200, "message/http");
     }
};

class only_render_resource : public http_resource {
 public:
     shared_ptr<http_response> render(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }
};

class ok_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }
};

class nok_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("NOK", 200, "text/plain");
     }
};

class static_resource : public http_resource {
 public:
     explicit static_resource(std::string r) : resp(std::move(r)) {}

     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>(resp, 200, "text/plain");
     }

     std::string resp;
};

class no_response_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<http_response>();
     }
};

class empty_response_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<http_response>(nullptr);
     }
};

#ifndef HTTPSERVER_NO_LOCAL_FS
class file_response_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<file_response>("test_content", 200, "text/plain");
     }
};

class file_response_resource_empty : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<file_response>("test_content_empty", 200, "text/plain");
     }
};

class file_response_resource_default_content_type : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<file_response>("test_content", 200);
     }
};
#endif  // HTTPSERVER_NO_LOCAL_FS

class file_response_resource_missing : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<file_response>("missing", 200);
     }
};

#ifndef HTTPSERVER_NO_LOCAL_FS
class file_response_resource_dir : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<file_response>("integ", 200);
     }
};
#endif  // HTTPSERVER_NO_LOCAL_FS

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
     explicit print_request_resource(stringstream* ss) : ss(ss) {}

     shared_ptr<http_response> render_GET(const http_request& req) {
         (*ss) << req;
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }

 private:
     stringstream* ss;
};

class print_response_resource : public http_resource {
 public:
     explicit print_response_resource(stringstream* ss) : ss(ss) {}

     shared_ptr<http_response> render_GET(const http_request&) {
         auto hresp = std::make_shared<string_response>("OK", 200, "text/plain");

         hresp->with_header("MyResponseHeader", "MyResponseHeaderValue");
         hresp->with_footer("MyResponseFooter", "MyResponseFooterValue");
         hresp->with_cookie("MyResponseCookie", "MyResponseCookieValue");

         (*ss) << *hresp;

         return hresp;
     }

 private:
     stringstream* ss;
};

class request_info_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         stringstream ss;
         ss << "requestor=" << req.get_requestor()
            << "&port=" << req.get_requestor_port()
            << "&version=" << req.get_version();
         return std::make_shared<string_response>(ss.str(), 200, "text/plain");
     }
};

class content_limit_resource : public http_resource {
 public:
     shared_ptr<http_response> render_POST(const http_request& req) {
         return std::make_shared<string_response>(
             req.content_too_large() ? "TOO_LARGE" : "OK", 200, "text/plain");
     }
};

#ifdef HTTPSERVER_PORT
#define PORT HTTPSERVER_PORT
#else
#define PORT 8080
#endif  // PORT

#define STR2(p) #p
#define STR(p) STR2(p)
#define PORT_STRING STR(PORT)


LT_BEGIN_SUITE(basic_suite)
    std::unique_ptr<webserver> ws;

    void set_up() {
        ws = std::make_unique<webserver>(create_webserver(PORT));
        ws->start(false);
    }

    void tear_down() {
        ws->stop();
    }
LT_END_SUITE(basic_suite)

LT_BEGIN_AUTO_TEST(basic_suite, server_runs)
    LT_CHECK_EQ(ws->is_running(), true);
LT_END_AUTO_TEST(server_runs)

LT_BEGIN_AUTO_TEST(basic_suite, two_endpoints)
    ok_resource ok;
    LT_ASSERT_EQ(true, ws->register_resource("OK", &ok));
    nok_resource nok;
    LT_ASSERT_EQ(true, ws->register_resource("NOK", &nok));

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    {
        CURL *curl = curl_easy_init();
        CURLcode res;
        curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/OK");
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
        curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/NOK");
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(t, "NOK");
        curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(two_endpoints)

LT_BEGIN_AUTO_TEST(basic_suite, duplicate_endpoints)
    ok_resource ok1, ok2;
    LT_CHECK_EQ(true, ws->register_resource("OK", &ok1));

    // All of these collide and the registration fails
    LT_CHECK_EQ(false, ws->register_resource("OK", &ok2));
    LT_CHECK_EQ(false, ws->register_resource("/OK", &ok2));
    LT_CHECK_EQ(false, ws->register_resource("/OK/", &ok2));
    LT_CHECK_EQ(false, ws->register_resource("OK/", &ok2));

    // Check how family interacts.
    LT_CHECK_EQ(true, ws->register_resource("OK", &ok2, true));

    // Check that switched case does the right thing, whatever that is here.
#ifdef CASE_INSENSITIVE
    LT_CHECK_EQ(false, ws->register_resource("ok", &ok2));
#else
    // TODO(etr): this should be true.
    // However, http_endpoint::operator< is always case-insensitive
    LT_CHECK_EQ(false, ws->register_resource("ok", &ok2));
#endif
LT_END_AUTO_TEST(duplicate_endpoints)

LT_BEGIN_AUTO_TEST(basic_suite, family_endpoints)
    static_resource ok1("1"), ok2("2");
    LT_CHECK_EQ(true, ws->register_resource("OK", &ok1));
    LT_CHECK_EQ(true, ws->register_resource("OK", &ok2, true));

    curl_global_init(CURL_GLOBAL_ALL);

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/OK");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "1");
    curl_easy_cleanup(curl);
    }

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/OK/");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "1");
    curl_easy_cleanup(curl);
    }

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/OK/go");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "2");
    curl_easy_cleanup(curl);
    }

#ifdef CASE_INSENSITIVE
    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/OK");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "1");
    curl_easy_cleanup(curl);
    }

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/OK/");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "1");
    curl_easy_cleanup(curl);
    }

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/OK/go");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "2");
    curl_easy_cleanup(curl);
    }
#endif
LT_END_AUTO_TEST(family_endpoints)

LT_BEGIN_AUTO_TEST(basic_suite, overlapping_endpoints)
    // Setup two different resources that can both match the same URL.
    static_resource ok1("1"), ok2("2");
    LT_CHECK_EQ(true, ws->register_resource("/foo/{var|([a-z]+)}/", &ok1));
    LT_CHECK_EQ(true, ws->register_resource("/{var|([a-z]+)}/bar/", &ok2));

    curl_global_init(CURL_GLOBAL_ALL);

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/foo/bar/");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "2");   // Not sure why regex wins, but it does...
    curl_easy_cleanup(curl);
    }

    static_resource ok3("3");
    LT_CHECK_EQ(true, ws->register_resource("/foo/bar/", &ok3));

    {
    // Check that an exact, non-RE match overrides both patterns.
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/foo/bar/");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "3");
    curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(overlapping_endpoints)

LT_BEGIN_AUTO_TEST(basic_suite, read_body)
    simple_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    map<string, string> ss;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }
/*
    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CONNECT");
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
    }
*/

    {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL* curl;
    CURLcode res;

    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "NOT_EXISTENT");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "Method not Allowed");
    curl_easy_cleanup(curl);

    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg1=lib&arg2=httpserver");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "libhttpserver");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(postprocessor)

LT_BEGIN_AUTO_TEST(basic_suite, postprocessor_large_field_last_field)
    large_post_resource_last_value resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;

    const int size = 20000;
    const char value = 'A';
    const char* prefix = "arg1=BB&arg1=";

    // Calculate the total length of the string
    int totalLength = std::strlen(prefix) + size;

    // Allocate memory for the char array
    char* cString = new char[totalLength + 1];  // +1 for the null terminator

    // Copy the prefix
    int offset = std::snprintf(cString, totalLength + 1, "%s", prefix);

    // Append 20,000 times the character 'A' to the string
    for (int i = 0; i < size; ++i) {
        cString[offset++] = value;
    }

    // Append the suffix
    cString[offset] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, cString);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, std::string(20000, 'A'));

    curl_easy_cleanup(curl);
    delete[] cString;
LT_END_AUTO_TEST(postprocessor_large_field_last_field)

LT_BEGIN_AUTO_TEST(basic_suite, postprocessor_large_field_first_field)
    large_post_resource_first_value resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;

    const int size = 20000;
    const char value = 'A';
    const char* prefix = "arg1=";
    const char* middle = "&arg1=";
    const char* suffix = "BB";

    // Calculate the total length of the string
    int totalLength = std::strlen(prefix) + size + std::strlen(middle) + std::strlen(suffix);

    // Allocate memory for the char array
    char* cString = new char[totalLength + 1];  // +1 for the null terminator

    // Copy the prefix
    int offset = std::snprintf(cString, totalLength + 1, "%s", prefix);

    // Append 20,000 times the character 'A' to the string
    for (int i = 0; i < size; ++i) {
        cString[offset++] = value;
    }

    // Append the middle part
    offset += std::snprintf(cString + offset, totalLength + 1 - offset, "%s", middle);

    // Append the suffix
    std::snprintf(cString + offset, totalLength + 1 - offset, "%s", suffix);

    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, cString);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, std::string(20000, 'A'));

    curl_easy_cleanup(curl);
    delete[] cString;
LT_END_AUTO_TEST(postprocessor_large_field_first_field)

LT_BEGIN_AUTO_TEST(basic_suite, same_key_different_value)
    arg_value_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    // The curl default content type triggers the file processing
    // logic in the webserver. However, since there is no actual
    // file, the arg handling should be the same.
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg=inertia&arg=isaproperty&arg=ofmatter");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "inertiaisapropertyofmatter");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(same_key_different_value)

LT_BEGIN_AUTO_TEST(basic_suite, same_key_different_value_plain_content)
    arg_value_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base?arg=beep&arg=boop&arg=hello&arg=what");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg=beep&arg=boop&arg=hello&arg=what");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    struct curl_slist *list = NULL;
    list = curl_slist_append(list, "content-type: text/plain");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    res = curl_easy_perform(curl);
    curl_slist_free_all(list);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "beepboophellowhat");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(same_key_different_value_plain_content)

LT_BEGIN_AUTO_TEST(basic_suite, empty_arg)
    simple_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(empty_arg)

LT_BEGIN_AUTO_TEST(basic_suite, empty_arg_value_at_end)
    // Test for issue #268: POST body keys without values at the end
    // are not processed when using application/x-www-form-urlencoded
    simple_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    // Test case 1: arg2 has empty value at end (the bug case)
    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg1=val1&arg2=");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // arg1="val1", arg2="" -> response should be "val1"
    LT_CHECK_EQ(s, "val1");
    curl_easy_cleanup(curl);
    }

    // Test case 2: only arg1 with empty value
    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg1=");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // arg1="" -> response should be ""
    LT_CHECK_EQ(s, "");
    curl_easy_cleanup(curl);
    }

    // Test case 3: both args with empty values
    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg1=&arg2=");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // arg1="", arg2="" -> response should be ""
    LT_CHECK_EQ(s, "");
    curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(empty_arg_value_at_end)

LT_BEGIN_AUTO_TEST(basic_suite, no_response)
    no_response_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("regex/matching/number/[0-9]+", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/regex/matching/number/10");
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
    LT_ASSERT_EQ(true, ws->register_resource("this/captures/{arg}/passed/in/input", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/this/captures/whatever/passed/in/input");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "whatever");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(regex_matching_arg)

LT_BEGIN_AUTO_TEST(basic_suite, regex_matching_arg_with_url_pars)
    args_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("this/captures/{arg}/passed/in/input", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/this/captures/whatever/passed/in/input?arg2=second_argument");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "whateversecond_argument");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(regex_matching_arg_with_url_pars)

LT_BEGIN_AUTO_TEST(basic_suite, regex_matching_arg_custom)
    args_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("this/captures/numeric/{arg|([0-9]+)}/passed/in/input", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/this/captures/numeric/11/passed/in/input");
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/this/captures/numeric/text/passed/in/input");
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
    LT_ASSERT_EQ(true, ws->register_resource("this/captures/args/passed/in/the/querystring", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/this/captures/args/passed/in/the/querystring?arg=first&arg2=second");
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
    LT_ASSERT_EQ(true, ws->register_resource("this/captures/args/passed/in/the/querystring", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/this/captures/args/passed/in/the/querystring?arg=argument");
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
    LT_ASSERT_EQ(true, ws->register_resource("this/captures/args/passed/in/the/querystring", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/this/captures/args/passed/in/the/querystring?arg1=value1&arg2=value2&arg3=value3");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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

    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(register_unregister)

#ifndef HTTPSERVER_NO_LOCAL_FS
LT_BEGIN_AUTO_TEST(basic_suite, file_serving_resource)
    file_response_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    map<string, string> ss;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ss);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(ss["Content-Type"], "application/octet-stream");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(file_serving_resource_default_content_type)
#endif  // HTTPSERVER_NO_LOCAL_FS

LT_BEGIN_AUTO_TEST(basic_suite, file_serving_resource_missing)
    file_response_resource_missing resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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

#ifndef HTTPSERVER_NO_LOCAL_FS
LT_BEGIN_AUTO_TEST(basic_suite, file_serving_resource_dir)
    file_response_resource_dir resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
#endif  // HTTPSERVER_NO_LOCAL_FS

LT_BEGIN_AUTO_TEST(basic_suite, exception_forces_500)
    exception_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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
    LT_ASSERT_EQ(true, ws->register_resource("/settings", &resource, true));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/settings/somestringthatisreallylong/with_really_a_lot_of_content/and_underscores_and_looooooooooooooooooong_stuff");
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
    LT_ASSERT_EQ(true, ws->register_resource("/settings", &resource, true));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/settings/{}");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "settings,{},");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(url_with_regex_like_pieces)

LT_BEGIN_AUTO_TEST(basic_suite, non_family_url_with_regex_like_pieces)
    ok_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("/settings", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/settings/{}");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE , &http_code);
    LT_ASSERT_EQ(http_code, 404);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(non_family_url_with_regex_like_pieces)

LT_BEGIN_AUTO_TEST(basic_suite, regex_url_exact_match)
    ok_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("/foo/{v|[a-z]}/bar", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/foo/a/bar/");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE , &http_code);
    LT_ASSERT_EQ(http_code, 200);

    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/foo/{v|[a-z]}/bar/");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE , &http_code);
    LT_ASSERT_EQ(http_code, 404);
    curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(regex_url_exact_match)

LT_BEGIN_AUTO_TEST(basic_suite, method_not_allowed_header)
    simple_resource resource;
    resource.disallow_all();
    resource.set_allowing("POST", true);
    resource.set_allowing("HEAD", true);
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    map<string, string> ss;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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

LT_BEGIN_AUTO_TEST(basic_suite, request_info_getters)
    request_info_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("request_info", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/request_info");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_NEQ(s.find("127.0.0.1"), string::npos);
    LT_CHECK_NEQ(s.find("HTTP/1.1"), string::npos);
    LT_CHECK_NEQ(s.find("port="), string::npos);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(request_info_getters)

LT_BEGIN_AUTO_TEST(basic_suite, unregister_then_404)
    simple_resource res;
    LT_ASSERT_EQ(true, ws->register_resource("temp", &res));
    curl_global_init(CURL_GLOBAL_ALL);

    {
        string s;
        CURL *curl = curl_easy_init();
        CURLcode result;
        curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/temp");
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        result = curl_easy_perform(curl);
        LT_ASSERT_EQ(result, 0);
        int64_t http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        LT_CHECK_EQ(http_code, 200);
        LT_CHECK_EQ(s, "OK");
        curl_easy_cleanup(curl);
    }

    ws->unregister_resource("temp");

    {
        string s;
        CURL *curl = curl_easy_init();
        CURLcode result;
        curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/temp");
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        result = curl_easy_perform(curl);
        LT_ASSERT_EQ(result, 0);
        int64_t http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        LT_CHECK_EQ(http_code, 404);
        curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(unregister_then_404)

LT_BEGIN_AUTO_TEST(basic_suite, thread_safety)
    simple_resource resource;

    std::atomic_bool done = false;
    auto register_thread = std::thread([&]() {
        int i = 0;
        while (!done) {
            ws->register_resource(
                    std::string("/route") + std::to_string(++i), &resource);
        }
    });

    auto get_thread = std::thread([&](){
        while (!done) {
            CURL *curl = curl_easy_init();
            std::string s;
            std::string url = "localhost:" PORT_STRING "/route" + std::to_string(rand() % 10000000);  // NOLINT(runtime/threadsafe_fn)
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);
        }
    });

    using std::chrono_literals::operator""s;
    std::this_thread::sleep_for(10s);
    done = true;
    if (register_thread.joinable()) {
        register_thread.join();
    }
    if (get_thread.joinable()) {
        get_thread.join();
    }
    LT_CHECK_EQ(1, 1);
LT_END_AUTO_TEST(thread_safety)

LT_BEGIN_AUTO_TEST(basic_suite, head_request)
    complete_test_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(head_request)

LT_BEGIN_AUTO_TEST(basic_suite, options_request)
    complete_test_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    map<string, string> ss;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ss);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(ss["Allow"], "GET, POST, PUT, DELETE, HEAD, OPTIONS");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(options_request)

LT_BEGIN_AUTO_TEST(basic_suite, trace_request)
    complete_test_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "TRACE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "TRACE OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(trace_request)

LT_BEGIN_SUITE(content_limit_suite)
    std::unique_ptr<webserver> ws;
    int content_limit_port;
    string content_limit_url;

    void set_up() {
        content_limit_port = PORT + 10;
        content_limit_url = "localhost:" + std::to_string(content_limit_port) + "/limit";
        ws = std::make_unique<webserver>(create_webserver(content_limit_port).content_size_limit(100));
        ws->start(false);
    }

    void tear_down() {
        ws->stop();
    }
LT_END_SUITE(content_limit_suite)

LT_BEGIN_AUTO_TEST(content_limit_suite, content_exceeds_limit)
    content_limit_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("limit", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;

    std::string large_data(200, 'X');

    curl_easy_setopt(curl, CURLOPT_URL, content_limit_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, large_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, large_data.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "TOO_LARGE");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(content_exceeds_limit)

LT_BEGIN_AUTO_TEST(content_limit_suite, content_within_limit)
    content_limit_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("limit", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;

    std::string small_data(50, 'X');

    curl_easy_setopt(curl, CURLOPT_URL, content_limit_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, small_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, small_data.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(content_within_limit)

LT_BEGIN_AUTO_TEST(basic_suite, get_args_flat)
    args_flat_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("args_flat", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/args_flat?foo=bar&baz=qux");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_NEQ(s.find("foo=bar"), string::npos);
    LT_CHECK_NEQ(s.find("baz=qux"), string::npos);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(get_args_flat)

LT_BEGIN_AUTO_TEST(basic_suite, only_render_head)
    only_render_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("only_render_head", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/only_render_head");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(only_render_head)

LT_BEGIN_AUTO_TEST(basic_suite, only_render_options)
    only_render_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("only_render_options", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/only_render_options");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(only_render_options)

LT_BEGIN_AUTO_TEST(basic_suite, only_render_trace)
    only_render_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("only_render_trace", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/only_render_trace");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "TRACE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(only_render_trace)

// Test for long error log message (triggers resize branch)
class long_error_message_resource : public http_resource {
 public:
    shared_ptr<http_response> render_GET(const http_request&) {
        // Generate an error with a message longer than 80 characters
        throw std::runtime_error(
            "This is a very long error message that exceeds the default buffer "
            "size of 80 characters to trigger the resize branch in error_log");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, long_error_message)
    long_error_message_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("longerror", &resource));
    curl_global_init(CURL_GLOBAL_ALL);

    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/longerror");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(long_error_message)

// Test PATCH request on a resource that only implements render()
LT_BEGIN_AUTO_TEST(basic_suite, only_render_patch)
    only_render_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("only_render_patch", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/only_render_patch");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(only_render_patch)

// Custom response class that throws std::invalid_argument in get_raw_response
class invalid_argument_response : public http_response {
 public:
    invalid_argument_response() : http_response(200, "text/plain") {}
    MHD_Response* get_raw_response() override {
        throw std::invalid_argument("Resource not found");
    }
};

// Resource that returns invalid_argument_response
class invalid_arg_resource : public http_resource {
 public:
    shared_ptr<http_response> render_GET(const http_request&) {
        return std::make_shared<invalid_argument_response>();
    }
};

// Custom response class that throws std::runtime_error in get_raw_response
class runtime_error_response : public http_response {
 public:
    runtime_error_response() : http_response(200, "text/plain") {}
    MHD_Response* get_raw_response() override {
        throw std::runtime_error("Internal error in response");
    }
};

// Resource that returns runtime_error_response
class runtime_error_resource : public http_resource {
 public:
    shared_ptr<http_response> render_GET(const http_request&) {
        return std::make_shared<runtime_error_response>();
    }
};

// Custom response class that throws non-std exception in get_raw_response
class non_std_exception_response : public http_response {
 public:
    non_std_exception_response() : http_response(200, "text/plain") {}
    MHD_Response* get_raw_response() override {
        throw 42;  // Throws an int, not a std::exception
    }
};

// Resource that returns non_std_exception_response
class non_std_exception_resource : public http_resource {
 public:
    shared_ptr<http_response> render_GET(const http_request&) {
        return std::make_shared<non_std_exception_response>();
    }
};

// Test response throwing std::invalid_argument -> should get 404
LT_BEGIN_AUTO_TEST(basic_suite, response_throws_invalid_argument)
    invalid_arg_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("invalid_arg", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/invalid_arg");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 404);  // invalid_argument -> not found

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(response_throws_invalid_argument)

// Test response throwing std::runtime_error -> should get 500
LT_BEGIN_AUTO_TEST(basic_suite, response_throws_runtime_error)
    runtime_error_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("runtime_err", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/runtime_err");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);  // runtime_error -> internal server error

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(response_throws_runtime_error)

// Test response throwing non-std exception -> should get 500
LT_BEGIN_AUTO_TEST(basic_suite, response_throws_non_std_exception)
    non_std_exception_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("non_std_exc", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/non_std_exc");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);  // non-std exception -> internal server error

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(response_throws_non_std_exception)

// Custom internal error handler that also throws an exception
// This tests the outer catch block (lines 826-829 in webserver.cpp)
shared_ptr<http_response> throwing_internal_error_handler(const http_request&) {
    throw std::runtime_error("Internal error handler also throws");
}

// Test case: resource throws exception AND internal error handler throws
// This triggers the outer catch block which uses force_our=true
LT_BEGIN_AUTO_TEST(basic_suite, internal_error_handler_also_throws)
    // Create a separate webserver with throwing internal error handler
    webserver ws2 = create_webserver(PORT + 50)
        .internal_error_resource(throwing_internal_error_handler);
    runtime_error_resource resource;  // Resource that throws in get_raw_response
    LT_ASSERT_EQ(true, ws2.register_resource("error_cascade", &resource));
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 50) + "/error_cascade";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    // When internal error handler throws, we fall back to the built-in error page
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(internal_error_handler_also_throws)

// Test tcp_nodelay option
LT_BEGIN_AUTO_TEST(basic_suite, tcp_nodelay_option)
    webserver ws2 = create_webserver(PORT + 51).tcp_nodelay();
    ok_resource resource;
    LT_ASSERT_EQ(true, ws2.register_resource("nodelay_test", &resource));
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 51) + "/nodelay_test";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(tcp_nodelay_option)

// Custom unescaper function to test the unescaper branch
void my_custom_unescaper(std::string& s) {
    // Simple unescaper that just converts '+' to space
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') s[i] = ' ';
    }
}

// Resource that returns the query string argument
class arg_echo_resource : public http_resource {
 public:
    shared_ptr<http_response> render_GET(const http_request& req) {
        std::string arg = std::string(req.get_arg_flat("key"));
        return std::make_shared<string_response>(arg, 200, "text/plain");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, custom_unescaper)
    webserver ws2 = create_webserver(PORT + 52).unescaper(my_custom_unescaper);
    arg_echo_resource resource;
    LT_ASSERT_EQ(true, ws2.register_resource("echo", &resource));
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 52) + "/echo?key=hello+world";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "hello world");

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(custom_unescaper)

// Custom not_found handler
shared_ptr<http_response> my_custom_not_found(const http_request&) {
    return std::make_shared<string_response>("CUSTOM_404", 404, "text/plain");
}

LT_BEGIN_AUTO_TEST(basic_suite, custom_not_found_handler)
    webserver ws2 = create_webserver(PORT + 53).not_found_resource(my_custom_not_found);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 53) + "/nonexistent";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "CUSTOM_404");

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(custom_not_found_handler)

// Custom method_not_allowed handler
shared_ptr<http_response> my_custom_method_not_allowed(const http_request&) {
    return std::make_shared<string_response>("CUSTOM_405", 405, "text/plain");
}

// Resource that only allows POST
class post_only_resource : public http_resource {
 public:
    post_only_resource() {
        disallow_all();
        set_allowing("POST", true);
    }
    shared_ptr<http_response> render_POST(const http_request&) {
        return std::make_shared<string_response>("POST_OK", 200, "text/plain");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, custom_method_not_allowed_handler)
    webserver ws2 = create_webserver(PORT + 54).method_not_allowed_resource(my_custom_method_not_allowed);
    post_only_resource resource;
    LT_ASSERT_EQ(true, ws2.register_resource("postonly", &resource));
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 54) + "/postonly";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);  // GET on a POST-only resource
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "CUSTOM_405");

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(custom_method_not_allowed_handler)

// Resource that tests requestor info caching
class requestor_cache_resource : public http_resource {
 public:
    shared_ptr<http_response> render_GET(const http_request& req) {
        // Test requestor IP and port
        std::string ip = std::string(req.get_requestor());
        uint16_t port = req.get_requestor_port();

        // Call them again to test caching (should hit cache on second call)
        std::string ip2 = std::string(req.get_requestor());

        std::string response = "IP:" + ip + ",PORT:" + std::to_string(port);
        return std::make_shared<string_response>(response, 200, "text/plain");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, requestor_info)
    webserver ws2 = create_webserver(PORT + 55);
    requestor_cache_resource resource;
    LT_ASSERT_EQ(true, ws2.register_resource("reqinfo", &resource));
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 55) + "/reqinfo";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // Response should contain IP and PORT
    LT_CHECK_EQ(s.find("IP:127.0.0.1") != string::npos, true);
    LT_CHECK_EQ(s.find("PORT:") != string::npos, true);

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(requestor_info)

// Resource that tests querystring caching
class querystring_cache_resource : public http_resource {
 public:
    shared_ptr<http_response> render_GET(const http_request& req) {
        // Call get_querystring twice to test caching
        std::string qs1 = std::string(req.get_querystring());
        std::string qs2 = std::string(req.get_querystring());  // Should hit cache

        return std::make_shared<string_response>(qs1, 200, "text/plain");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, querystring_caching)
    webserver ws2 = create_webserver(PORT + 56);
    querystring_cache_resource resource;
    LT_ASSERT_EQ(true, ws2.register_resource("qscache", &resource));
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 56) + "/qscache?foo=bar&baz=qux";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // Check querystring contains the parameters
    LT_CHECK_EQ(s.find("foo") != string::npos, true);
    LT_CHECK_EQ(s.find("bar") != string::npos, true);

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(querystring_caching)

// Resource that tests args caching
class args_cache_resource : public http_resource {
 public:
    shared_ptr<http_response> render_GET(const http_request& req) {
        // Call get_args twice to test caching
        auto args1 = req.get_args();
        auto args2 = req.get_args();  // Should hit cache

        // Also test get_args_flat
        auto flat = req.get_args_flat();

        std::string response;
        for (const auto& [key, val] : flat) {
            response += std::string(key) + "=" + std::string(val) + ";";
        }
        return std::make_shared<string_response>(response, 200, "text/plain");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, args_caching)
    webserver ws2 = create_webserver(PORT + 57);
    args_cache_resource resource;
    LT_ASSERT_EQ(true, ws2.register_resource("argscache", &resource));
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 57) + "/argscache?key1=val1&key2=val2";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s.find("key1=val1") != string::npos, true);
    LT_CHECK_EQ(s.find("key2=val2") != string::npos, true);

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(args_caching)

// Resource that tests footer/trailer access
class footer_test_resource : public http_resource {
 public:
    shared_ptr<http_response> render_POST(const http_request& req) {
        // Test get_footers() - returns empty map for non-chunked requests
        auto footers = req.get_footers();

        // Test get_footer() with a key that doesn't exist
        auto footer_val = req.get_footer("X-Test-Trailer");

        // Build response showing footer count and specific footer value
        std::string response = "footers=" + std::to_string(footers.size());
        if (!footer_val.empty()) {
            response += ",X-Test-Trailer=" + std::string(footer_val);
        }

        return std::make_shared<string_response>(response, 200, "text/plain");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, footer_access_no_trailers)
    webserver ws2 = create_webserver(PORT + 58);
    footer_test_resource resource;
    LT_ASSERT_EQ(true, ws2.register_resource("footers", &resource));
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 58) + "/footers";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "test=data");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // Without trailers, footers should be empty
    LT_CHECK_EQ(s, "footers=0");

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(footer_access_no_trailers)

// Resource that returns a response with footers (trailers)
class response_footer_resource : public http_resource {
 public:
    shared_ptr<http_response> render_GET(const http_request&) {
        auto response = std::make_shared<string_response>("body content", 200, "text/plain");
        // Add a footer to the response
        response->with_footer("X-Checksum", "abc123");
        response->with_footer("X-Processing-Time", "42ms");

        // Test get_footer and get_footers on response
        auto checksum = response->get_footer("X-Checksum");
        auto all_footers = response->get_footers();

        return response;
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, response_with_footers)
    webserver ws2 = create_webserver(PORT + 59);
    response_footer_resource resource;
    LT_ASSERT_EQ(true, ws2.register_resource("resp_footers", &resource));
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 59) + "/resp_footers";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "body content");

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(response_with_footers)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
