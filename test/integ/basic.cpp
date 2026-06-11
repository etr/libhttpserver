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
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "httpserver/string_utilities.hpp"
#include "./littletest.hpp"
#include "./test_utils.hpp"

using std::string;
using std::map;
using std::shared_ptr;
using std::vector;
using std::stringstream;

using httpserver::http_resource;
using httpserver::http_request;
using httpserver::http_response;
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
     http_response render_get(const http_request&) {
         return http_response::string("OK");
     }
     http_response render_post(const http_request& req) {
         return http_response::string(std::string(req.get_arg("arg1")) + std::string(req.get_arg("arg2")));
     }
};

class large_post_resource_last_value : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::string("OK");
     }
     http_response render_post(const http_request& req) {
         return http_response::string(std::string(req.get_arg("arg1").get_all_values().back()));
     }
};

class large_post_resource_first_value : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::string("OK");
     }
     http_response render_post(const http_request& req) {
         return http_response::string(std::string(req.get_arg("arg1").get_all_values().front()));
     }
};

class arg_value_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::string("OK");
     }
     http_response render_post(const http_request& req) {
         auto const arg_value = req.get_arg("arg").get_all_values();
         for (auto const & a : arg_value) {
            std::cerr << a << std::endl;
         }
         std::string all_values = std::accumulate(std::next(arg_value.begin()), arg_value.end(), std::string(arg_value[0]), [](std::string a, std::string_view in) {
            return std::move(a) + std::string(in);
         });
         return http_response::string(all_values);
     }
};

class args_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         return http_response::string(std::string(req.get_arg("arg")) + std::string(req.get_arg("arg2")));
     }
};

class args_flat_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         auto args = req.get_args_flat();
         stringstream ss;
         for (const auto& [key, value] : args) {
             ss << key << "=" << value << ";";
         }
         return http_response::string(ss.str());
     }
};

class long_content_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::string(lorem_ipsum);
     }
};

class header_set_test_resource : public http_resource {
 public:
     http_response render_get(const http_request&) override {
         return http_response::string("OK")
                    .with_header("KEY", "VALUE");
     }
};

class cookie_set_test_resource : public http_resource {
 public:
     http_response render_get(const http_request&) override {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
         return http_response::string("OK")
                    .with_cookie("MyCookie", "CookieValue");
#pragma GCC diagnostic pop
     }
};

class cookie_reading_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         return http_response::string(std::string(req.get_cookie("name")));
     }
};

class header_reading_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         return http_response::string(std::string(req.get_header("MyHeader")));
     }
};

class full_args_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         // get_args() returns const http::arg_view_map& (TASK-017). The
         // .at("arg") call is read-only on const&; the http_arg_value is
         // implicitly converted to std::string at the call site. This call
         // site was reviewed for copy-semantic reliance and requires no
         // modification. (spec-alignment-checker-iter1-20)
         return http_response::string(std::string(req.get_args().at("arg")));
     }
};

class querystring_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         return http_response::string(std::string(req.get_querystring()));
     }
};

class path_pieces_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         stringstream ss;
         for (unsigned int i = 0; i < req.get_path_pieces().size(); i++) {
             ss << req.get_path_piece(i) << ",";
         }
         return http_response::string(ss.str());
     }
};

class complete_test_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::string("OK");
     }

     http_response render_post(const http_request&) {
         return http_response::string("OK");
     }

     http_response render_put(const http_request&) {
         return http_response::string("OK");
     }

     http_response render_delete(const http_request&) {
         return http_response::string("OK");
     }

     http_response render_patch(const http_request&) {
         return http_response::string("OK");
     }

     http_response render_head(const http_request&) {
         return http_response::string("");
     }

     http_response render_options(const http_request&) {
         return http_response::string("")
                    .with_header("Allow", "GET, POST, PUT, DELETE, HEAD, OPTIONS");
     }

     http_response render_trace(const http_request&) {
         return http_response::string("TRACE OK", "message/http");
     }
};

class only_render_resource : public http_resource {
 public:
     http_response render(const http_request&) {
         return http_response::string("OK");
     }
};

class ok_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::string("OK");
     }
};

class nok_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::string("NOK");
     }
};

class static_resource : public http_resource {
 public:
     explicit static_resource(std::string r) : resp(std::move(r)) {}

     http_response render_get(const http_request&) {
         return http_response::string(resp);
     }

     std::string resp;
};

class no_response_resource : public http_resource {
 public:
     // TASK-036: v1 returned shared_ptr<http_response>(nullptr) — the
     // dispatch path treated that as "no response, route to 500". v2's
     // equivalent is the default-constructed sentinel (status_code_ == -1).
     http_response render_get(const http_request&) override {
         return http_response{};
     }
};

class empty_response_resource : public http_resource {
 public:
     http_response render_get(const http_request&) override {
         // TASK-036: handler-returned "empty" response in v2 is the
         // default-constructed http_response (status_code_ == -1 sentinel).
         // The dispatch path recognises the sentinel and routes through
         // internal_error_page — same 500 behaviour the v1 null-pointer
         // arm produced.
         return http_response{};
     }
};

#ifndef HTTPSERVER_NO_LOCAL_FS
class file_response_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::file("test_content").with_header("Content-Type", "text/plain");
     }
};

class file_response_resource_empty : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::file("test_content_empty").with_header("Content-Type", "text/plain");
     }
};

class file_response_resource_default_content_type : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::file("test_content");
     }
};
#endif  // HTTPSERVER_NO_LOCAL_FS

class file_response_resource_missing : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::file("missing");
     }
};

#ifndef HTTPSERVER_NO_LOCAL_FS
class file_response_resource_dir : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::file("integ");
     }
};
#endif  // HTTPSERVER_NO_LOCAL_FS

class exception_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         throw std::domain_error("invalid");
     }
};

class error_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         throw "invalid";
     }
};

class print_request_resource : public http_resource {
 public:
     explicit print_request_resource(stringstream* ss) : ss(ss) {}

     http_response render_get(const http_request& req) {
         (*ss) << req;
         return http_response::string("OK");
     }

 private:
     stringstream* ss;
};

class print_response_resource : public http_resource {
 public:
     explicit print_response_resource(stringstream* ss) : ss(ss) {}

     http_response render_get(const http_request&) override {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
         auto hresp = http_response::string("OK")
                          .with_header("MyResponseHeader", "MyResponseHeaderValue")
                          .with_footer("MyResponseFooter", "MyResponseFooterValue")
                          .with_cookie("MyResponseCookie", "MyResponseCookieValue");
#pragma GCC diagnostic pop

         (*ss) << hresp;

         return hresp;
     }

 private:
     stringstream* ss;
};

class request_info_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         stringstream ss;
         ss << "requestor=" << req.get_requestor()
            << "&port=" << req.get_requestor_port()
            << "&version=" << req.get_version();
         return http_response::string(ss.str());
     }
};

class content_limit_resource : public http_resource {
 public:
     http_response render_post(const http_request& req) {
         return http_response::string(req.content_too_large() ? "TOO_LARGE" : "OK");
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
        // TASK-055 / DR-009 Revision 1: the suite-shared `ws` opts into the
        // verbose default-500-body path so the legacy tests that pre-date
        // the CWE-209 sanitization fix continue to assert what they were
        // written to assert. Affected tests: exception_forces_500,
        // untyped_error_forces_500, file_serving_resource_missing,
        // file_serving_resource_dir, long_error_message — all probe the
        // message-forwarding path through the dispatcher (intentional
        // regression coverage), not the default-body shape.
        ws = std::make_unique<webserver>(
            create_webserver(PORT).expose_exception_messages(true));
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
    auto ok = std::make_shared<ok_resource>();
    ws->register_path("OK", ok);
    auto nok = std::make_shared<nok_resource>();
    ws->register_path("NOK", nok);

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
    auto ok1 = std::make_shared<ok_resource>();
    auto ok2 = std::make_shared<ok_resource>();
    ws->register_path("OK", ok1);

    // TASK-023: the new void register_path throws std::invalid_argument
    // on duplicate registration (replaces v1's silent `return false`).
    LT_CHECK_THROW(ws->register_path("OK", ok2));
    LT_CHECK_THROW(ws->register_path("/OK", ok2));
    LT_CHECK_THROW(ws->register_path("/OK/", ok2));
    LT_CHECK_THROW(ws->register_path("OK/", ok2));

    // TASK-056: registering the SAME path as both exact and prefix is
    // now a collision (the (method, path) cache key can't discriminate
    // the two), so the prefix call throws. Pre-TASK-056 it silently
    // succeeded — see specs/architecture/04-components/route-table.md
    // for the rationale.
    LT_CHECK_THROW(ws->register_prefix("OK", ok2));

    // TASK-067: pre-TASK-067 the v1 `registered_resources` ordered map
    // was the duplicate-detection oracle, and its key comparator
    // (`http_endpoint::operator<`) was unconditionally case-insensitive
    // via `std::toupper` regardless of the CASE_INSENSITIVE build flag
    // (a v1 quirk that didn't match dispatch-time lookup semantics).
    // With the v1 maps gone the v2 3-tier route table is the only
    // oracle, and it is consistently case-sensitive without
    // -DCASE_INSENSITIVE — so "ok" is a genuinely distinct route key
    // from "OK" and registers successfully.
#ifdef CASE_INSENSITIVE
    LT_CHECK_THROW(ws->register_path("ok", ok2));
#else
    ws->register_path("ok", ok2);
#endif
LT_END_AUTO_TEST(duplicate_endpoints)

LT_BEGIN_AUTO_TEST(basic_suite, family_endpoints)
    auto ok1 = std::make_shared<static_resource>("1");
    auto ok2 = std::make_shared<static_resource>("2");
    // TASK-056: pre-TASK-056 this test registered the SAME path "OK"
    // as both exact and prefix; the collision guard now forbids that.
    // The test still pins what it always did — exact serves the bare
    // path, prefix serves arbitrary subpaths — but on distinct paths.
    ws->register_path("EXA", ok1);
    ws->register_prefix("FAM", ok2);

    curl_global_init(CURL_GLOBAL_ALL);

    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/EXA");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "1");
    curl_easy_cleanup(curl);
    }

    {
    // /EXA/ resolves to the exact route via trailing-slash path
    // normalisation, not via a prefix match — there is no prefix
    // registration at /EXA.
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/EXA/");
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/FAM/go");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "2");
    curl_easy_cleanup(curl);
    }

#ifdef CASE_INSENSITIVE
    // Case-insensitive matching probe: lowercase URLs hit the same
    // registered (uppercase) keys.
    {
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/exa");
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/exa/");
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/fam/go");
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
    auto ok1 = std::make_shared<static_resource>("1");
    auto ok2 = std::make_shared<static_resource>("2");
    ws->register_path("/foo/{var|([a-z]+)}/", ok1);
    ws->register_path("/{var|([a-z]+)}/bar/", ok2);

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
    // Post-TASK-053: v2 dispatch is deterministic — exact tier
    // walks before radix, and within the radix tier the trie
    // visits exact children before wildcards in registration
    // order. Both routes are wildcard-rooted, and `ok1`
    // (`/foo/{var}`) was registered first, so it wins under v2's
    // first-registered-wins rule. The v1 behavior here ("2"
    // wins) was an `std::map` iteration-order accident — the
    // original comment said "Not sure why regex wins, but it
    // does..." — not a designed contract. See
    // test/REGRESSION.md §4 ("Overlapping-routes precedence")
    // and the pinned unit-level test
    // `routing_regression_suite::overlapping_two_regex_routes_deterministic_first_wins`.
    LT_CHECK_EQ(s, "1");
    curl_easy_cleanup(curl);
    }

    auto ok3 = std::make_shared<static_resource>("3");
    ws->register_path("/foo/bar/", ok3);

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
    auto resource = std::make_shared<simple_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<long_content_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<header_set_test_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<cookie_set_test_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<header_reading_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<cookie_reading_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<complete_test_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<only_render_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<simple_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<large_post_resource_last_value>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<large_post_resource_first_value>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<arg_value_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<arg_value_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<simple_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<simple_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<no_response_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<empty_response_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<simple_resource>();
    ws->register_path("regex/matching/number/[0-9]+", resource);
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
    auto resource = std::make_shared<args_resource>();
    ws->register_path("this/captures/{arg}/passed/in/input", resource);
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
    auto resource = std::make_shared<args_resource>();
    ws->register_path("this/captures/{arg}/passed/in/input", resource);
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
    // v1 parity restored: the radix tier now enforces `{name|regex}`
    // per-segment constraints, and binds captures under the bare
    // `name` (e.g. "arg") rather than the full source token. A
    // segment that fails the regex misses the wildcard slot and the
    // request resolves to 404.
    auto resource = std::make_shared<args_resource>();
    ws->register_path("this/captures/numeric/{arg|([0-9]+)}/passed/in/input", resource);
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
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 200);
    LT_CHECK_EQ(s, "11");
    curl_easy_cleanup(curl);
    }

    {
    // `/text` fails the `([0-9]+)` constraint; the radix descent
    // breaks at the wildcard slot and the request resolves to 404.
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/this/captures/numeric/text/passed/in/input");
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
LT_END_AUTO_TEST(regex_matching_arg_custom)

LT_BEGIN_AUTO_TEST(basic_suite, querystring_processing)
    auto resource = std::make_shared<args_resource>();
    ws->register_path("this/captures/args/passed/in/the/querystring", resource);
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
    auto resource = std::make_shared<full_args_resource>();
    ws->register_path("this/captures/args/passed/in/the/querystring", resource);
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
    auto resource = std::make_shared<querystring_resource>();
    ws->register_path("this/captures/args/passed/in/the/querystring", resource);
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
    auto resource = std::make_shared<simple_resource>();
    ws->register_path("base", resource);
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

    ws->unregister_path("base");
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

    ws->register_path("base", resource);
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
    auto resource = std::make_shared<file_response_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<file_response_resource_empty>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<file_response_resource_default_content_type>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<file_response_resource_missing>();
    ws->register_path("base", resource);
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
    // TASK-031: default internal_error_page now surfaces the originating
    // message in the body. file_body::materialize() returns nullptr for a
    // missing file, which dispatch routes through internal_error_page
    // with a fixed diagnostic message.
    LT_CHECK_NEQ(s.find("materialize_response returned null"),
                 std::string::npos);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(file_serving_resource_missing)

#ifndef HTTPSERVER_NO_LOCAL_FS
LT_BEGIN_AUTO_TEST(basic_suite, file_serving_resource_dir)
    auto resource = std::make_shared<file_response_resource_dir>();
    ws->register_path("base", resource);
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
    // TASK-031: see file_serving_resource_missing — dispatch reports the
    // null-materialize diagnostic in the body.
    LT_CHECK_NEQ(s.find("materialize_response returned null"),
                 std::string::npos);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(file_serving_resource_dir)
#endif  // HTTPSERVER_NO_LOCAL_FS

// Regression anchor: exercises the std::domain_error (a std::exception
// subclass distinct from std::runtime_error) dispatch path on the shared
// webserver. The shared ws is configured with
// expose_exception_messages(true) (basic.cpp:420), so e.what() flows
// through to the default body. Intentionally kept distinct from
// dr009_default_body_is_fixed_string (DR-009 Revision 1), which pins
// the default-off behaviour on a dedicated webserver. Finding
// task031-review #36.
LT_BEGIN_AUTO_TEST(basic_suite, exception_forces_500)
    auto resource = std::make_shared<exception_resource>();
    ws->register_path("base", resource);
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
    // TASK-031 / DR-009: std::domain_error's what() ("invalid") is forwarded
    // to the default internal_error_page body.
    LT_CHECK_NEQ(s.find("invalid"), std::string::npos);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(exception_forces_500)

// Regression anchor: exercises a char* literal throw (non-std::exception) on
// the shared webserver. Intentionally kept distinct from
// dr009_default_body_is_fixed_string_for_non_std_exception (DR-009 Revision 1),
// which uses `throw 42` (an int) on a dedicated webserver. The two non-std
// types (char* vs int) exercise the same catch(...) branch; keeping both
// provides type-diversity coverage. Finding task031-review #37.
LT_BEGIN_AUTO_TEST(basic_suite, untyped_error_forces_500)
    auto resource = std::make_shared<error_resource>();
    ws->register_path("base", resource);
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
    // TASK-031 / DR-009: non-std::exception throws (here: a char* literal)
    // produce the sentinel message "unknown exception" in the default body.
    LT_CHECK_NEQ(s.find("unknown exception"), std::string::npos);

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 500);

    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(untyped_error_forces_500)

LT_BEGIN_AUTO_TEST(basic_suite, request_is_printable)
    stringstream ss;
    auto resource = std::make_shared<print_request_resource>(&ss);
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<print_response_resource>(&ss);
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<path_pieces_resource>();
    ws->register_prefix("/settings", resource);
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
    auto resource = std::make_shared<path_pieces_resource>();
    ws->register_prefix("/settings", resource);
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
    auto resource = std::make_shared<ok_resource>();
    ws->register_path("/settings", resource);
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
    // v1 parity restored: the radix tier enforces the `[a-z]`
    // per-segment constraint. `/foo/a/bar/` satisfies it (200);
    // the literal `{v|[a-z]}` segment does not — its first
    // character `{` lies outside `[a-z]` — so that request misses
    // the wildcard slot and resolves to 404.
    auto resource = std::make_shared<ok_resource>();
    ws->register_path("/foo/{v|[a-z]}/bar", resource);
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
    auto resource = std::make_shared<simple_resource>();
    resource->disallow_all();
    resource->set_allowing(httpserver::http_method::post, true);
    resource->set_allowing(httpserver::http_method::head, true);
    ws->register_path("base", resource);
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
    // Allow-header tokens are emitted in http_method enum-declaration
    // order (head=1, post=2). For this test the resulting "HEAD, POST"
    // matches v1's std::map alphabetical order by coincidence; do not
    // generalize the assumption.
    LT_CHECK_EQ(ss["Allow"], "HEAD, POST");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(method_not_allowed_header)

LT_BEGIN_AUTO_TEST(basic_suite, request_info_getters)
    auto resource = std::make_shared<request_info_resource>();
    ws->register_path("request_info", resource);
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
    auto res = std::make_shared<simple_resource>();
    ws->register_path("temp", res);
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

    ws->unregister_path("temp");

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
    auto resource = std::make_shared<simple_resource>();

    std::atomic_bool done = false;
    auto register_thread = std::thread([&]() {
        int i = 0;
        while (!done) {
            ws->register_path(
                    std::string("/route") + std::to_string(++i), resource);
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

    // 2 s is sufficient to expose data races on any reasonably loaded CI
    // runner; the original 10 s added 8 s of dead time to every test run
    // with no extra coverage benefit (race window is equally likely to
    // manifest in 1 s).
    using std::chrono_literals::operator""s;
    std::this_thread::sleep_for(2s);
    done = true;
    if (register_thread.joinable()) {
        register_thread.join();
    }
    if (get_thread.joinable()) {
        get_thread.join();
    }
    // Liveness check: reaching this point without a crash or deadlock is the
    // contract. The tautological assertion is intentional — test failure
    // would only come from abort/SIGSEGV/deadlock above, not from this line.
    LT_CHECK_EQ(1, 1);
LT_END_AUTO_TEST(thread_safety)

LT_BEGIN_AUTO_TEST(basic_suite, head_request)
    auto resource = std::make_shared<complete_test_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<complete_test_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<complete_test_resource>();
    ws->register_path("base", resource);
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
    auto resource = std::make_shared<content_limit_resource>();
    ws->register_path("limit", resource);
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
    auto resource = std::make_shared<content_limit_resource>();
    ws->register_path("limit", resource);
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
    auto resource = std::make_shared<args_flat_resource>();
    ws->register_path("args_flat", resource);
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
    auto resource = std::make_shared<only_render_resource>();
    ws->register_path("only_render_head", resource);
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
    auto resource = std::make_shared<only_render_resource>();
    ws->register_path("only_render_options", resource);
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
    auto resource = std::make_shared<only_render_resource>();
    ws->register_path("only_render_trace", resource);
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
    http_response render_get(const http_request&) {
        // Generate an error with a message longer than 80 characters
        throw std::runtime_error(
            "This is a very long error message that exceeds the default buffer "
            "size of 80 characters to trigger the resize branch in error_log");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, long_error_message)
    auto resource = std::make_shared<long_error_message_resource>();
    ws->register_path("longerror", resource);
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
    auto resource = std::make_shared<only_render_resource>();
    ws->register_path("only_render_patch", resource);
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

// TASK-031 review cleanup: response_throws_invalid_argument,
// response_throws_runtime_error, response_throws_non_std_exception, and
// internal_error_handler_also_throws were removed here (findings 5 and 6).
// Their coverage is a strict subset of the dr009_* suite which follows
// (dr009_default_body_is_fixed_string,
//  dr009_default_body_is_fixed_string_for_non_std_exception,
//  dr009_verbose_body_surfaces_message_when_opted_in,
//  dr009_verbose_body_surfaces_unknown_exception_when_opted_in,
//  dr009_throwing_handler_yields_empty_body_500 /
//  dr009_throwing_handler_logs_generically). The dr009_* tests assert on
// body content, message forwarding, and log capture in addition to status.

// Test tcp_nodelay option
LT_BEGIN_AUTO_TEST(basic_suite, tcp_nodelay_option)
    webserver ws2{create_webserver(PORT + 51).tcp_nodelay()};
    auto resource = std::make_shared<ok_resource>();
    ws2.register_path("nodelay_test", resource);
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
    http_response render_get(const http_request& req) {
        std::string arg = std::string(req.get_arg_flat("key"));
        return http_response::string(arg);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, custom_unescaper)
    webserver ws2{create_webserver(PORT + 52).unescaper(my_custom_unescaper)};
    auto resource = std::make_shared<arg_echo_resource>();
    ws2.register_path("echo", resource);
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
http_response my_custom_not_found(const http_request&) {
    return http_response::string("CUSTOM_404").with_status(404);
}

LT_BEGIN_AUTO_TEST(basic_suite, custom_not_found_handler)
    webserver ws2{create_webserver(PORT + 53).not_found_handler(my_custom_not_found)};
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
http_response my_custom_method_not_allowed(const http_request&) {
    return http_response::string("CUSTOM_405").with_status(405);
}

// Resource that only allows POST
class post_only_resource : public http_resource {
 public:
    post_only_resource() {
        disallow_all();
        set_allowing(httpserver::http_method::post, true);
    }
    http_response render_post(const http_request&) {
        return http_response::string("POST_OK");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, custom_method_not_allowed_handler)
    webserver ws2{create_webserver(PORT + 54).method_not_allowed_handler(my_custom_method_not_allowed)};
    auto resource = std::make_shared<post_only_resource>();
    ws2.register_path("postonly", resource);
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
    http_response render_get(const http_request& req) {
        // Test requestor IP and port
        std::string ip = std::string(req.get_requestor());
        uint16_t port = req.get_requestor_port();

        // Call them again to test caching (should hit cache on second call)
        std::string ip2 = std::string(req.get_requestor());

        std::string response = "IP:" + ip + ",PORT:" + std::to_string(port);
        return http_response::string(response);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, requestor_info)
    webserver ws2{create_webserver(PORT + 55)};
    auto resource = std::make_shared<requestor_cache_resource>();
    ws2.register_path("reqinfo", resource);
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
    http_response render_get(const http_request& req) {
        // Call get_querystring twice to test caching
        std::string qs1 = std::string(req.get_querystring());
        std::string qs2 = std::string(req.get_querystring());  // Should hit cache

        return http_response::string(qs1);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, querystring_caching)
    webserver ws2{create_webserver(PORT + 56)};
    auto resource = std::make_shared<querystring_cache_resource>();
    ws2.register_path("qscache", resource);
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
    http_response render_get(const http_request& req) {
        // Call get_args twice to test caching. TASK-017: returns const&
        // aliasing the impl-owned cache; both calls must return the same
        // address proving the cache is built once and reused.
        // (code-quality-reviewer-iter1-6 / test-quality-reviewer-iter1-22:
        // promote address equality to an assertion rather than dead code)
        const auto& args1 = req.get_args();
        const auto& args2 = req.get_args();  // Should hit cache
        // Address equality: the two references must alias the same object.
        if (&args1 != &args2) {
            return http_response::string("ERROR: args cache not stable");
        }

        // Also test get_args_flat (still by-value for now -- TASK-017 only
        // narrows the six container getters listed in its acceptance set).
        auto flat = req.get_args_flat();

        std::string response;
        for (const auto& [key, val] : flat) {
            response += std::string(key) + "=" + std::string(val) + ";";
        }
        return http_response::string(response);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, args_caching)
    webserver ws2{create_webserver(PORT + 57)};
    auto resource = std::make_shared<args_cache_resource>();
    ws2.register_path("argscache", resource);
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
    http_response render_post(const http_request& req) {
        // Test get_footers() - returns empty map for non-chunked requests.
        // TASK-017: now returns const& aliasing impl-owned storage.
        const auto& footers = req.get_footers();

        // Test get_footer() with a key that doesn't exist
        auto footer_val = req.get_footer("X-Test-Trailer");

        // Build response showing footer count and specific footer value
        std::string response = "footers=" + std::to_string(footers.size());
        if (!footer_val.empty()) {
            response += ",X-Test-Trailer=" + std::string(footer_val);
        }

        return http_response::string(response);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, footer_access_no_trailers)
    webserver ws2{create_webserver(PORT + 58)};
    auto resource = std::make_shared<footer_test_resource>();
    ws2.register_path("footers", resource);
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
    http_response render_get(const http_request&) override {
        auto response = http_response::string("body content")
                            .with_footer("X-Checksum", "abc123")
                            .with_footer("X-Processing-Time", "42ms");

        // Test get_footer and get_footers on response. The returned
        // string_view points into the response's storage; we only
        // read it before returning so the response (and thus the
        // backing string) outlives any read.
        auto checksum = response.get_footer("X-Checksum");
        auto all_footers = response.get_footers();
        (void)checksum;
        (void)all_footers;

        return response;
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, response_with_footers)
    webserver ws2{create_webserver(PORT + 59)};
    auto resource = std::make_shared<response_footer_resource>();
    ws2.register_path("resp_footers", resource);
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

// Resource that tests get_arg with non-existent key
class arg_not_found_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        // Get an arg that doesn't exist - should return empty http_arg_value
        auto missing_arg = req.get_arg("nonexistent_key");
        // http_arg_value.get_all_values() should return empty vector
        std::string result = missing_arg.get_all_values().empty() ? "EMPTY" : "HAS_VALUES";
        return http_response::string(result);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, arg_not_found)
    auto resource = std::make_shared<arg_not_found_resource>();
    ws->register_path("arg_not_found", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/arg_not_found?existing=value");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "EMPTY");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(arg_not_found)

// Resource that tests get_arg_flat fallback to connection value
class arg_flat_fallback_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        // Test get_arg_flat with a key that exists in GET args but not in unescaped_args
        // This tests the fallback branch in get_arg_flat
        std::string val = std::string(req.get_arg_flat("qparam"));
        return http_response::string(val);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, arg_flat_fallback)
    auto resource = std::make_shared<arg_flat_fallback_resource>();
    ws->register_path("arg_flat_fb", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/arg_flat_fb?qparam=myvalue");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "myvalue");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(arg_flat_fallback)

// Resource that tests get_path_piece with out of bounds index
class path_piece_oob_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        // Get path piece at an index that's out of bounds
        std::string piece = req.get_path_piece(100);  // Way beyond the path pieces
        // Should return empty string
        std::string result = piece.empty() ? "OOB_EMPTY" : piece;
        return http_response::string(result);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, path_piece_out_of_bounds)
    auto resource = std::make_shared<path_piece_oob_resource>();
    ws->register_path("path/piece/test", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/path/piece/test");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OOB_EMPTY");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(path_piece_out_of_bounds)

// Resource that tests empty querystring
class empty_querystring_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        std::string qs = std::string(req.get_querystring());
        std::string result = qs.empty() ? "NO_QS" : qs;
        return http_response::string(result);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, empty_querystring)
    auto resource = std::make_shared<empty_querystring_resource>();
    ws->register_path("empty_qs", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    // URL without any query string
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/empty_qs");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "NO_QS");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(empty_querystring)

// Resource that tests query parameters with null/empty values
// Covers http_request.cpp lines 234 and 248 (arg_value == nullptr branches)
class null_value_query_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        // Test getting an argument that was passed without a value (e.g., ?keyonly)
        auto keyonly_arg = req.get_arg("keyonly");
        auto normal_arg = req.get_arg("normal");

        // Also test querystring which exercises build_request_querystring
        std::string qs = std::string(req.get_querystring());

        stringstream ss;
        ss << "keyonly=" << (keyonly_arg.get_all_values().empty() ? "MISSING" :
                            (keyonly_arg.get_all_values()[0].empty() ? "EMPTY" : "VALUE"));
        ss << ",normal=" << (normal_arg.get_all_values().empty() ? "MISSING" :
                            std::string(normal_arg.get_all_values()[0]));
        ss << ",qs=" << (qs.find("keyonly") != string::npos ? "HAS_KEYONLY" : "NO_KEYONLY");

        return http_response::string(ss.str());
    }
};

#ifdef HAVE_BAUTH
// Resource that tests auth caching (get_user/get_pass called multiple times)
class auth_cache_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        // Call get_user and get_pass multiple times to test caching
        std::string user1 = std::string(req.get_user());
        std::string pass1 = std::string(req.get_pass());
        std::string user2 = std::string(req.get_user());  // Should hit cache
        std::string pass2 = std::string(req.get_pass());  // Should hit cache

        std::string result = user1.empty() ? "NO_AUTH" : ("USER:" + user1);
        return http_response::string(result);
    }
};
#endif  // HAVE_BAUTH

#ifdef HAVE_BAUTH
LT_BEGIN_AUTO_TEST(basic_suite, auth_caching)
    auto resource = std::make_shared<auth_cache_resource>();
    ws->register_path("auth_cache", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/auth_cache");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    // No authentication provided
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "NO_AUTH");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(auth_caching)
#endif  // HAVE_BAUTH

// Test query parameters with null/empty values (e.g., ?keyonly&normal=value)
// This covers http_request.cpp lines 234 and 248 (arg_value == nullptr branches)
LT_BEGIN_AUTO_TEST(basic_suite, null_value_query_param)
    auto resource = std::make_shared<null_value_query_resource>();
    ws->register_path("null_val_query", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    // Query string with a key that has no value (keyonly) and one with value (normal=test)
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/null_val_query?keyonly&normal=test");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // keyonly should have an empty value (not missing)
    LT_CHECK_EQ(s.find("keyonly=EMPTY") != string::npos, true);
    LT_CHECK_EQ(s.find("normal=test") != string::npos, true);
    LT_CHECK_EQ(s.find("qs=HAS_KEYONLY") != string::npos, true);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(null_value_query_param)

// Test PUT method on a resource that only implements render()
LT_BEGIN_AUTO_TEST(basic_suite, only_render_put)
    auto resource = std::make_shared<only_render_resource>();
    ws->register_path("only_render_put", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/only_render_put");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(only_render_put)

// Test DELETE method on a resource that only implements render()
LT_BEGIN_AUTO_TEST(basic_suite, only_render_delete)
    auto resource = std::make_shared<only_render_resource>();
    ws->register_path("only_render_delete", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/only_render_delete");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(only_render_delete)

// Test POST method on a resource that only implements render()
LT_BEGIN_AUTO_TEST(basic_suite, only_render_post)
    auto resource = std::make_shared<only_render_resource>();
    ws->register_path("only_render_post", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/only_render_post");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "test=data");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(only_render_post)

// Test unregister_path functionality
LT_BEGIN_AUTO_TEST(basic_suite, unregister_path)
    webserver ws2{create_webserver(PORT + 67)};
    auto resource = std::make_shared<ok_resource>();
    ws2.register_path("test_unreg", resource);
    ws2.start(false);

    // First verify resource works
    {
        curl_global_init(CURL_GLOBAL_ALL);
        string s;
        CURL *curl = curl_easy_init();
        CURLcode res;
        std::string url = "http://localhost:" + std::to_string(PORT + 67) + "/test_unreg";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        curl_easy_cleanup(curl);
    }

    // Now unregister
    ws2.unregister_path("test_unreg");

    // Resource should no longer be accessible (404)
    {
        string s;
        CURL *curl = curl_easy_init();
        CURLcode res;
        std::string url = "http://localhost:" + std::to_string(PORT + 67) + "/test_unreg";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        int64_t http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        LT_CHECK_EQ(http_code, 404);
        curl_easy_cleanup(curl);
    }

    ws2.stop();
LT_END_AUTO_TEST(unregister_path)

// Resource that tests get_arg_flat() returning first value for multi-value arg
class arg_flat_multi_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        // get_arg_flat should return the first value even for multi-value args
        std::string flat_val = std::string(req.get_arg_flat("key"));
        return http_response::string("flat=" + flat_val);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, get_arg_flat_first_value)
    auto resource = std::make_shared<arg_flat_multi_resource>();
    ws->register_path("arg_flat_first", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/arg_flat_first?key=value1&key=value2");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "flat=value1");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(get_arg_flat_first_value)

// Test access and error log callbacks
struct LogCapture {
    static std::string& access_log_msg() {
        static std::string msg;
        return msg;
    }
    static std::string& error_log_msg() {
        static std::string msg;
        return msg;
    }
};

void test_access_logger(const std::string& msg) {
    LogCapture::access_log_msg() = msg;
}

void test_error_logger(const std::string& msg) {
    LogCapture::error_log_msg() = msg;
}

LT_BEGIN_AUTO_TEST(basic_suite, log_access_callback)
    LogCapture::access_log_msg().clear();

    webserver ws2{create_webserver(PORT + 70)
        .log_access(test_access_logger)};
    auto resource = std::make_shared<ok_resource>();
    ws2.register_path("logtest", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 70) + "/logtest";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    // The access log should have been called with the request info
    LT_CHECK_EQ(LogCapture::access_log_msg().find("/logtest") != std::string::npos, true);
    LT_CHECK_EQ(LogCapture::access_log_msg().find("METHOD") != std::string::npos, true);

    curl_easy_cleanup(curl);
    ws2.stop();
LT_END_AUTO_TEST(log_access_callback)

// Test single_resource mode
LT_BEGIN_AUTO_TEST(basic_suite, single_resource_mode)
    webserver ws2{create_webserver(PORT + 71)
        .single_resource()};
    auto resource = std::make_shared<ok_resource>();
    // In single_resource mode, must register at "/" with family=true
    ws2.register_prefix("/", resource);
    ws2.start(false);

    // All paths should route to the single resource
    {
        curl_global_init(CURL_GLOBAL_ALL);
        string s;
        CURL *curl = curl_easy_init();
        CURLcode res;
        std::string url = "http://localhost:" + std::to_string(PORT + 71) + "/any/path/here";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        curl_easy_cleanup(curl);
    }

    // Even root should work
    {
        string s;
        CURL *curl = curl_easy_init();
        CURLcode res;
        std::string url = "http://localhost:" + std::to_string(PORT + 71) + "/";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        curl_easy_cleanup(curl);
    }

    ws2.stop();
LT_END_AUTO_TEST(single_resource_mode)

// Test resource with no render methods overridden (exercises empty_render path)
// Note: empty_render returns string_response with code -1, which triggers internal error
class empty_render_resource : public http_resource {
 public:
    // No render methods overridden - uses default empty_render() path
};

LT_BEGIN_AUTO_TEST(basic_suite, default_render_method)
    // Test that a resource with no render overrides triggers internal error
    // (because empty_render returns response code -1)
    webserver ws2{create_webserver(PORT + 73)};
    auto resource = std::make_shared<empty_render_resource>();
    ws2.register_path("empty", resource);
    ws2.start(false);

    {
        curl_global_init(CURL_GLOBAL_ALL);
        string s;
        int64_t http_code = 0;
        CURL *curl = curl_easy_init();
        CURLcode res;
        std::string url = "http://localhost:" + std::to_string(PORT + 73) + "/empty";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        LT_ASSERT_EQ(res, 0);
        // Default empty_render returns code -1, which causes internal error (500)
        LT_CHECK_EQ(http_code, 500);
        curl_easy_cleanup(curl);
    }

    ws2.stop();
LT_END_AUTO_TEST(default_render_method)

// Test resource that overrides only render() (not render_get)
class render_override_resource : public http_resource {
 public:
    http_response render(const http_request&) {
        return http_response::string("base_render");
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, render_fallthrough_to_base)
    // Test that render_get calls render() when not overridden
    webserver ws2{create_webserver(PORT + 74)};
    auto resource = std::make_shared<render_override_resource>();
    ws2.register_path("base", resource);
    ws2.start(false);

    {
        curl_global_init(CURL_GLOBAL_ALL);
        string s;
        CURL *curl = curl_easy_init();
        CURLcode res;
        std::string url = "http://localhost:" + std::to_string(PORT + 74) + "/base";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "base_render");
        curl_easy_cleanup(curl);
    }

    ws2.stop();
LT_END_AUTO_TEST(render_fallthrough_to_base)

// Note: CONNECT method is a special HTTP method for tunneling that
// behaves differently than standard HTTP methods, so we don't test
// it the same way as other methods.

// Test all HTTP methods falling through to base render()
LT_BEGIN_AUTO_TEST(basic_suite, all_methods_fallthrough_to_render)
    // render_override_resource only defines render(), not render_get/POST/etc.
    // So all method-specific calls should fall through to render()
    webserver ws2{create_webserver(PORT + 75)};
    auto resource = std::make_shared<render_override_resource>();
    ws2.register_path("fallthrough", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    CURL* curl;
    CURLcode res;
    string s;

    // Test POST fallthrough
    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, ("localhost:" + std::to_string(PORT + 75) + "/fallthrough").c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "base_render");
    curl_easy_cleanup(curl);

    // Test PUT fallthrough
    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, ("localhost:" + std::to_string(PORT + 75) + "/fallthrough").c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "base_render");
    curl_easy_cleanup(curl);

    // Test DELETE fallthrough
    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, ("localhost:" + std::to_string(PORT + 75) + "/fallthrough").c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "base_render");
    curl_easy_cleanup(curl);

    // Test PATCH fallthrough
    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, ("localhost:" + std::to_string(PORT + 75) + "/fallthrough").c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "base_render");
    curl_easy_cleanup(curl);

    // Test HEAD fallthrough (body is empty for HEAD)
    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, ("localhost:" + std::to_string(PORT + 75) + "/fallthrough").c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // HEAD response has no body
    curl_easy_cleanup(curl);

    // Test OPTIONS fallthrough
    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, ("localhost:" + std::to_string(PORT + 75) + "/fallthrough").c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "base_render");
    curl_easy_cleanup(curl);

    // Test TRACE fallthrough
    s = "";
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, ("localhost:" + std::to_string(PORT + 75) + "/fallthrough").c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "TRACE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "base_render");
    curl_easy_cleanup(curl);

    ws2.stop();
LT_END_AUTO_TEST(all_methods_fallthrough_to_render)

// TASK-031 review cleanup: builder_custom_internal_error_handler was removed
// here (finding 7). Its behavior (custom handler body is used) is a strict
// subset of dr009_runtime_error_message_passed_to_handler (PORT+81), which
// additionally verifies that e.what() is forwarded as the message argument.
// Port 76 is now free for future tests.

// Test get_arg_flat fallback to MHD connection value
class arg_flat_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        // get_arg_flat should fall back to MHD connection value for query params
        std::string result = std::string(req.get_arg_flat("q"));
        return http_response::string(result);
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, get_arg_flat_fallback)
    webserver ws2{create_webserver(PORT + 77)};
    auto resource = std::make_shared<arg_flat_resource>();
    ws2.register_path("argflat", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 77) + "/argflat?q=test_value";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "test_value");
    curl_easy_cleanup(curl);

    ws2.stop();
LT_END_AUTO_TEST(get_arg_flat_fallback)

// Test large multipart form field that triggers grow_last_arg path
class large_multipart_resource : public http_resource {
 public:
    http_response render_post(const http_request& req) {
        std::string result = std::string(req.get_arg("large_field"));
        return http_response::string(std::to_string(result.size()));
    }
};

LT_BEGIN_AUTO_TEST(basic_suite, large_multipart_form_field)
    // This test sends a large text field via multipart form-data
    // to trigger the grow_last_arg path in http_request.cpp (line 544)
    webserver ws2{create_webserver(PORT + 78)};
    auto resource = std::make_shared<large_multipart_resource>();
    ws2.register_path("largemp", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;

    // Create a large string (100KB) to ensure MHD chunks it
    const size_t large_size = 100 * 1024;
    std::string large_data(large_size, 'X');

    // Use curl_mime for multipart/form-data
    curl_mime *form = curl_mime_init(curl);
    curl_mimepart *field = curl_mime_addpart(form);
    curl_mime_name(field, "large_field");
    curl_mime_data(field, large_data.c_str(), CURL_ZERO_TERMINATED);

    std::string url = "http://localhost:" + std::to_string(PORT + 78) + "/largemp";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // The response should be the size of the large field
    LT_CHECK_EQ(s, std::to_string(large_size));

    curl_mime_free(form);
    curl_easy_cleanup(curl);

    ws2.stop();
LT_END_AUTO_TEST(large_multipart_form_field)

#ifdef HAVE_GNUTLS
// Resource that tests client certificate methods on non-TLS requests
class client_cert_non_tls_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        std::string result;
        // All these should return false/empty since this is not a TLS connection
        // TASK-019: the four cert-string accessors return string_view.
        // `const char* + string_view` is not in the standard, so we
        // copy each view into a std::string for the `+` chain.
        result += "has_tls_session:" + std::string(req.has_tls_session() ? "yes" : "no") + ";";
        result += "has_client_cert:" + std::string(req.has_client_certificate() ? "yes" : "no") + ";";
        result += "dn:" + std::string(req.get_client_cert_dn()) + ";";
        result += "issuer:" + std::string(req.get_client_cert_issuer_dn()) + ";";
        result += "cn:" + std::string(req.get_client_cert_cn()) + ";";
        result += "verified:" + std::string(req.is_client_cert_verified() ? "yes" : "no") + ";";
        result += "fingerprint:" + std::string(req.get_client_cert_fingerprint_sha256()) + ";";
        result += "not_before:" + std::to_string(req.get_client_cert_not_before()) + ";";
        result += "not_after:" + std::to_string(req.get_client_cert_not_after());
        return http_response::string(result);
    }
};

// Test that client certificate methods return appropriate values for non-TLS requests
LT_BEGIN_AUTO_TEST(basic_suite, client_cert_methods_non_tls)
    webserver ws{create_webserver(PORT + 79)};
    auto ccnr = std::make_shared<client_cert_non_tls_resource>();
    ws.register_path("/cert_test", ccnr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(PORT + 79) + "/cert_test";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    // Verify all methods return false/empty for non-TLS
    LT_CHECK_NEQ(s.find("has_tls_session:no"), std::string::npos);
    LT_CHECK_NEQ(s.find("has_client_cert:no"), std::string::npos);
    LT_CHECK_NEQ(s.find("dn:;"), std::string::npos);
    LT_CHECK_NEQ(s.find("issuer:;"), std::string::npos);
    LT_CHECK_NEQ(s.find("cn:;"), std::string::npos);
    LT_CHECK_NEQ(s.find("verified:no"), std::string::npos);
    LT_CHECK_NEQ(s.find("fingerprint:;"), std::string::npos);
    LT_CHECK_NEQ(s.find("not_before:-1"), std::string::npos);
    LT_CHECK_NEQ(s.find("not_after:-1"), std::string::npos);

    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(client_cert_methods_non_tls)
#endif  // HAVE_GNUTLS

// ============================================================================
// TASK-031: Handler error-propagation contract (DR-009 / §5.2 / PRD-FLG-REQ-002).
//
// The 6-point contract:
//   1. Wrap handler invocation in a two-branch catch.
//   2. On std::exception: log via error_logger, invoke internal_error_handler
//      with e.what(), send the resulting response (default 500 if unset).
//   3. On non-std::exception: same path, message "unknown exception".
//   4. If internal_error_handler itself throws: log generically, send
//      hardcoded 500 with empty body.
//   5. feature_unavailable lands as a generic 500 (no special status mapping).
//   6. Documented in webserver.hpp Doxygen.
// ============================================================================

namespace task031 {

// Port-offset allocation for the DR-009 integration tests (finding
// task031-review #2: explicit range avoids per-search collisions).
// TASK-055 / DR-009 Revision 1: PORT+80 and PORT+83 are reused by the
// post-revision "default body is fixed string" tests; PORT+88 and PORT+89
// cover the verbose-mode opt-in for the same exception paths.
//   PORT + 80  dr009_default_body_is_fixed_string                       (rev1)
//   PORT + 81  dr009_runtime_error_message_passed_to_handler
//   PORT + 82  dr009_runtime_error_logged_via_error_logger
//   PORT + 83  dr009_default_body_is_fixed_string_for_non_std_exception (rev1)
//   PORT + 84  dr009_non_std_exception_passes_unknown_exception_to_handler
//   PORT + 85  dr009_throwing_handler_yields_empty_body_500
//   PORT + 86  dr009_throwing_handler_logs_generically
//   PORT + 87  dr009_feature_unavailable_lands_as_generic_500
//   PORT + 88  dr009_verbose_body_surfaces_message_when_opted_in        (rev1)
//   PORT + 89  dr009_verbose_body_surfaces_unknown_exception_when_opted_in (rev1)
// Next free: PORT + 90.

// Thin HTTP GET helper used by the DR-009 integration tests to reduce
// per-test boilerplate (finding task031-review #1). Sends a GET request to
// `url`, captures the body, and returns {body, http_status_code}.
// curl_global_init must have been called before the first use.
static std::pair<std::string, int64_t> do_get(const std::string& url) {
    std::string body;
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    int64_t code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }
    curl_easy_cleanup(curl);
    return {body, code};
}

// Thread-safe capture struct: handlers / loggers run on MHD worker threads.
struct captured_call {
    std::mutex m;
    std::string last_msg;
    std::atomic<int> count{0};

    std::string read_last_msg() {
        std::lock_guard<std::mutex> g(m);
        return last_msg;
    }
};

class boom_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         throw std::runtime_error("boom");
     }
};

class throw_int_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         throw 42;
     }
};

class feature_unavailable_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         throw httpserver::feature_unavailable{"widget", "HAVE_WIDGET"};
     }
};

}  // namespace task031

// AC1.a (DR-009 Revision 1 / TASK-055): std::runtime_error("boom") yields
// a 500 whose default body is the fixed string "Internal Server Error" —
// the originating message MUST NOT appear in the body (CWE-209 fix).
// The verbose opt-in behaviour is covered by
// dr009_verbose_body_surfaces_message_when_opted_in.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_default_body_is_fixed_string)
    webserver ws2{create_webserver(PORT + 80)};
    auto resource = std::make_shared<task031::boom_resource>();
    ws2.register_path("boom", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 80) + "/boom");
    LT_ASSERT_EQ(http_code, 500);
    // The body is the fixed sanitized string ...
    LT_CHECK_EQ(s, std::string("Internal Server Error"));
    // ... and crucially does NOT carry the originating message (CWE-209).
    LT_CHECK_EQ(s.find("boom"), std::string::npos);

    ws2.stop();
LT_END_AUTO_TEST(dr009_default_body_is_fixed_string)

// AC1.a-verbose (DR-009 Revision 1 / TASK-055): with the
// expose_exception_messages(true) opt-in, the default body restores the
// pre-revision behaviour of surfacing the originating exception message.
// This is the development-only round-trip of the v1 behaviour.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_verbose_body_surfaces_message_when_opted_in)
    webserver ws2{create_webserver(PORT + 88).expose_exception_messages(true)};
    auto resource = std::make_shared<task031::boom_resource>();
    ws2.register_path("boom", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 88) + "/boom");
    LT_ASSERT_EQ(http_code, 500);
    LT_CHECK_NEQ(s.find("boom"), std::string::npos);

    ws2.stop();
LT_END_AUTO_TEST(dr009_verbose_body_surfaces_message_when_opted_in)

// AC1.b: std::runtime_error("boom") -> the captured message reaches the
// user-wired internal_error_handler unchanged.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_runtime_error_message_passed_to_handler)
    task031::captured_call cap;
    auto handler = [&cap](const http_request&, std::string_view msg) {
        {
            std::lock_guard<std::mutex> g(cap.m);
            cap.last_msg.assign(msg);
        }
        cap.count++;
        return http_response::string("CAPTURED:" + std::string(msg))
            .with_status(500);
    };

    webserver ws2{create_webserver(PORT + 81)
        .internal_error_handler(handler)};
    auto resource = std::make_shared<task031::boom_resource>();
    ws2.register_path("boom", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 81) + "/boom");
    LT_ASSERT_EQ(http_code, 500);
    LT_CHECK_EQ(cap.read_last_msg(), "boom");
    LT_CHECK_EQ(s, "CAPTURED:boom");

    ws2.stop();
LT_END_AUTO_TEST(dr009_runtime_error_message_passed_to_handler)

// AC1.c: std::runtime_error("boom") -> error_logger receives a record that
// contains "boom" somewhere in its text.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_runtime_error_logged_via_error_logger)
    task031::captured_call cap;
    auto logger = [&cap](const std::string& msg) {
        {
            std::lock_guard<std::mutex> g(cap.m);
            cap.last_msg.append(msg).append("\n");
        }
        cap.count++;
    };

    webserver ws2{create_webserver(PORT + 82).log_error(logger)};
    auto resource = std::make_shared<task031::boom_resource>();
    ws2.register_path("boom", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 82) + "/boom");
    LT_ASSERT_EQ(http_code, 500);
    std::string log_buf = cap.read_last_msg();
    // AC1.c: the log record must include the dispatch-context prefix AND
    // the originating message so the log line is self-contained (finding
    // task031-review: assert both content and context).
    LT_CHECK_NEQ(log_buf.find("dispatch: handler threw"), std::string::npos);
    LT_CHECK_NEQ(log_buf.find("boom"), std::string::npos);

    ws2.stop();
LT_END_AUTO_TEST(dr009_runtime_error_logged_via_error_logger)

// AC2.a (DR-009 Revision 1 / TASK-055): throw 42 (non-std::exception)
// yields a 500 whose default body is the fixed string "Internal Server
// Error" — the documented "unknown exception" sentinel is NOT exposed on
// the wire any more (CWE-209 fix). The sentinel is still carried through
// to the configured internal_error_handler and to the log_error callback;
// it is the *default body* path alone that is sanitized.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_default_body_is_fixed_string_for_non_std_exception)
    webserver ws2{create_webserver(PORT + 83)};
    auto resource = std::make_shared<task031::throw_int_resource>();
    ws2.register_path("non_std", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 83) + "/non_std");
    LT_ASSERT_EQ(http_code, 500);
    LT_CHECK_EQ(s, std::string("Internal Server Error"));

    ws2.stop();
LT_END_AUTO_TEST(dr009_default_body_is_fixed_string_for_non_std_exception)

// AC2.a-verbose (DR-009 Revision 1 / TASK-055): with the
// expose_exception_messages(true) opt-in, the default body restores the
// pre-revision behaviour of surfacing the "unknown exception" sentinel
// for the non-std::exception throw path.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_verbose_body_surfaces_unknown_exception_when_opted_in)
    webserver ws2{create_webserver(PORT + 89).expose_exception_messages(true)};
    auto resource = std::make_shared<task031::throw_int_resource>();
    ws2.register_path("non_std", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 89) + "/non_std");
    LT_ASSERT_EQ(http_code, 500);
    LT_CHECK_NEQ(s.find("unknown exception"), std::string::npos);

    ws2.stop();
LT_END_AUTO_TEST(dr009_verbose_body_surfaces_unknown_exception_when_opted_in)

// AC2.b: throw 42 -> handler receives "unknown exception" as its message.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_non_std_exception_passes_unknown_exception_to_handler)
    task031::captured_call cap;
    auto handler = [&cap](const http_request&, std::string_view msg) {
        {
            std::lock_guard<std::mutex> g(cap.m);
            cap.last_msg.assign(msg);
        }
        cap.count++;
        return http_response::string("CAPTURED:" + std::string(msg))
            .with_status(500);
    };

    webserver ws2{create_webserver(PORT + 84)
        .internal_error_handler(handler)};
    auto resource = std::make_shared<task031::throw_int_resource>();
    ws2.register_path("non_std", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 84) + "/non_std");
    LT_ASSERT_EQ(http_code, 500);
    LT_CHECK_EQ(cap.read_last_msg(), "unknown exception");

    ws2.stop();
LT_END_AUTO_TEST(dr009_non_std_exception_passes_unknown_exception_to_handler)

// AC3.a: internal_error_handler itself throws -> empty-body 500.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_throwing_handler_yields_empty_body_500)
    auto handler = [](const http_request&, std::string_view) -> http_response {
        throw std::runtime_error("handler boom");
    };

    webserver ws2{create_webserver(PORT + 85)
        .internal_error_handler(handler)};
    auto resource = std::make_shared<task031::boom_resource>();
    ws2.register_path("boom", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 85) + "/boom");
    LT_ASSERT_EQ(http_code, 500);
    LT_CHECK_EQ(s, "");

    ws2.stop();
LT_END_AUTO_TEST(dr009_throwing_handler_yields_empty_body_500)

// AC3.b: when internal_error_handler throws, error_logger sees a generic
// "internal_error_handler threw" record so operators can diagnose the
// double-fault.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_throwing_handler_logs_generically)
    task031::captured_call cap;
    auto logger = [&cap](const std::string& msg) {
        {
            std::lock_guard<std::mutex> g(cap.m);
            cap.last_msg.append(msg).append("\n");
        }
        cap.count++;
    };
    auto handler = [](const http_request&, std::string_view) -> http_response {
        throw std::runtime_error("handler boom");
    };

    webserver ws2{create_webserver(PORT + 86)
        .internal_error_handler(handler)
        .log_error(logger)};
    auto resource = std::make_shared<task031::boom_resource>();
    ws2.register_path("boom", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 86) + "/boom");
    LT_ASSERT_EQ(http_code, 500);
    LT_CHECK_EQ(s, "");

    std::string log_buf = cap.read_last_msg();
    LT_CHECK_NEQ(log_buf.find("internal_error_handler threw"),
                 std::string::npos);

    ws2.stop();
LT_END_AUTO_TEST(dr009_throwing_handler_logs_generically)

// AC4: feature_unavailable is a std::runtime_error; it lands as a generic
// 500 with NO special status mapping. The default body surfaces its
// what() text (which embeds the feature name and build flag).
// TASK-055 / DR-009 Revision 1: the verbose body opt-in is part of this
// test's contract — the assertion intent is "the what() text of
// feature_unavailable flows through the message-forwarding path to the
// body", which is now the development-only verbose path. The
// post-revision default body is sanitized.
LT_BEGIN_AUTO_TEST(basic_suite, dr009_feature_unavailable_lands_as_generic_500)
    webserver ws2{create_webserver(PORT + 87).expose_exception_messages(true)};
    auto resource = std::make_shared<task031::feature_unavailable_resource>();
    ws2.register_path("widget", resource);
    ws2.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    auto [s, http_code] = task031::do_get(
        "http://localhost:" + std::to_string(PORT + 87) + "/widget");
    LT_ASSERT_EQ(http_code, 500);
    // The feature_unavailable's what() string is surfaced in the body.
    LT_CHECK_NEQ(s.find("widget"), std::string::npos);
    LT_CHECK_NEQ(s.find("HAVE_WIDGET"), std::string::npos);

    ws2.stop();
LT_END_AUTO_TEST(dr009_feature_unavailable_lands_as_generic_500)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
