/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// TASK-026: generic webserver::route(method, path, handler) and
// webserver::route(method_set, path, handler).
//
// route() is the table-driven escape hatch for registering a lambda
// handler when the HTTP method is a runtime value (config-driven route
// tables, programmatic registration loops). The on_* family is preferred
// when the method is known statically. Both forms tunnel into the same
// internal registration path.
//
// This TU pins both the compile-time signature contract and the runtime
// behaviour (real curl round-trips against a running webserver).

#include <curl/curl.h>

#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::http_method;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::method_set;
using httpserver::webserver;

#define PORT 8230

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

struct fetch_result {
    long response_code;  // NOLINT(runtime/int) -- libcurl writes long
    std::string body;
    std::string allow_header;
};

// Header callback collects "Allow:" so the 405 path can be asserted.
size_t header_func(char* buffer, size_t size, size_t nitems, void* userdata) {
    fetch_result* fr = static_cast<fetch_result*>(userdata);
    std::string line(buffer, size * nitems);
    constexpr const char* kAllowPrefix = "Allow:";
    if (line.rfind(kAllowPrefix, 0) == 0) {
        std::string val = line.substr(std::string(kAllowPrefix).size());
        size_t start = val.find_first_not_of(" \t");
        size_t end = val.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            fr->allow_header = val.substr(start, end - start + 1);
        }
    }
    return size * nitems;
}

fetch_result do_request(const std::string& url, const std::string& method,
                        const std::string& body = "") {
    fetch_result fr{0, {}, {}};
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (method == "HEAD") {
        // CURLOPT_NOBODY: HEAD responses have no body; without this,
        // curl_easy_perform hangs waiting for bytes that never arrive.
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(body.size()));  // NOLINT(runtime/int)
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fr.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_func);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &fr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &fr.response_code);
    curl_easy_cleanup(curl);
    return fr;
}

}  // namespace

// ---- Compile-time signature contract -----------------------------------

using lambda_sig = std::function<http_response(const http_request&)>;

// route(http_method, const string&, std::function<...>) returning void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().route(
                      std::declval<http_method>(),
                      std::declval<const std::string&>(),
                      std::declval<lambda_sig>())),
                  void>,
              "route(http_method, const string&, std::function<...>) "
              "must exist and return void");

// route(method_set, const string&, std::function<...>) returning void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().route(
                      std::declval<method_set>(),
                      std::declval<const std::string&>(),
                      std::declval<lambda_sig>())),
                  void>,
              "route(method_set, const string&, std::function<...>) "
              "must exist and return void");

// ---- Runtime behaviour tests -------------------------------------------

LT_BEGIN_SUITE(webserver_route_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(webserver_route_suite)

// Single-method route(): GET serves a GET request.
LT_BEGIN_AUTO_TEST(webserver_route_suite, route_get_serves_get_request)
    webserver ws = create_webserver(PORT);
    ws.route(http_method::get, "/r", [](const http_request&) {
        return http_response::string("g");
    });
    ws.start(false);

    fetch_result fr = do_request("localhost:8230/r", "GET");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("g"));

    ws.stop();
LT_END_AUTO_TEST(route_get_serves_get_request)

// Single-method route(): POST serves a POST request.
LT_BEGIN_AUTO_TEST(webserver_route_suite, route_post_serves_post_request)
    webserver ws = create_webserver(PORT + 1);
    ws.route(http_method::post, "/r", [](const http_request&) {
        return http_response::string("p");
    });
    ws.start(false);

    fetch_result fr = do_request("localhost:8231/r", "POST", "");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("p"));

    ws.stop();
LT_END_AUTO_TEST(route_post_serves_post_request)

// route(get) only allows GET; other methods get 405 with Allow: GET.
LT_BEGIN_AUTO_TEST(webserver_route_suite, route_only_allows_registered_method)
    webserver ws = create_webserver(PORT + 2);
    ws.route(http_method::get, "/r", [](const http_request&) {
        return http_response::string("g");
    });
    ws.start(false);

    fetch_result post = do_request("localhost:8232/r", "POST", "");
    LT_CHECK_EQ(post.response_code, 405);
    LT_CHECK_EQ(post.allow_header, std::string("GET"));

    ws.stop();
LT_END_AUTO_TEST(route_only_allows_registered_method)

// THE acceptance criterion: load (method, path) pairs from a vector at
// runtime and register each via route(); both routes serve correctly.
LT_BEGIN_AUTO_TEST(webserver_route_suite, route_runtime_vector_dispatch)
    webserver ws = create_webserver(PORT + 3);

    std::vector<std::pair<http_method, std::string>> table = {
        {http_method::get,  "/a"},
        {http_method::post, "/b"},
    };
    for (const auto& entry : table) {
        const std::string body = entry.second + "-served";
        ws.route(entry.first, entry.second,
                 [body](const http_request&) {
                     return http_response::string(body);
                 });
    }
    ws.start(false);

    fetch_result a = do_request("localhost:8233/a", "GET");
    LT_CHECK_EQ(a.response_code, 200);
    LT_CHECK_EQ(a.body, std::string("/a-served"));

    fetch_result b = do_request("localhost:8233/b", "POST", "");
    LT_CHECK_EQ(b.response_code, 200);
    LT_CHECK_EQ(b.body, std::string("/b-served"));

    ws.stop();
LT_END_AUTO_TEST(route_runtime_vector_dispatch)

// route(method_set{get, head}, ...): both methods serve.
LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   route_method_set_get_and_head_serve_both)
    webserver ws = create_webserver(PORT + 4);
    ws.route(method_set{}.set(http_method::get).set(http_method::head),
             "/c", [](const http_request&) {
                 return http_response::string("c");
             });
    ws.start(false);

    fetch_result g = do_request("localhost:8234/c", "GET");
    LT_CHECK_EQ(g.response_code, 200);
    LT_CHECK_EQ(g.body, std::string("c"));

    fetch_result h = do_request("localhost:8234/c", "HEAD");
    LT_CHECK_EQ(h.response_code, 200);

    ws.stop();
LT_END_AUTO_TEST(route_method_set_get_and_head_serve_both)

// route(method_set{get, head}, ...): a method NOT in the set returns 405
// and the Allow header lists exactly GET, HEAD (in enum order).
LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   route_method_set_serves_only_set_bits)
    webserver ws = create_webserver(PORT + 5);
    ws.route(method_set{}.set(http_method::get).set(http_method::head),
             "/c", [](const http_request&) {
                 return http_response::string("c");
             });
    ws.start(false);

    fetch_result post = do_request("localhost:8235/c", "POST", "");
    LT_CHECK_EQ(post.response_code, 405);
    // Enum order is HEAD before GET? No: get=0, head=1, so 'get' fires
    // first in the enum sweep, then 'head'. Allow lists in that order.
    // (Matches TASK-021 / TASK-025 contract.)
    LT_CHECK_EQ(post.allow_header, std::string("GET, HEAD"));

    ws.stop();
LT_END_AUTO_TEST(route_method_set_serves_only_set_bits)

// route(method_set{}, ...) -- empty set -- throws std::invalid_argument
// (silent no-op would surprise table-driven users whose runtime config
// produced an empty set due to a bug).
LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   route_method_set_empty_throws_invalid_argument)
    webserver ws = create_webserver(PORT + 6);
    bool threw = false;
    try {
        ws.route(method_set{}, "/e", [](const http_request&) {
            return http_response::string("x");
        });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(route_method_set_empty_throws_invalid_argument)

// route(http_method::count_, ...) -- the sentinel last enumerator --
// throws std::invalid_argument. The public route() accepts a runtime
// http_method, so this guard is now load-bearing.
LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   route_with_count_sentinel_throws_invalid_argument)
    webserver ws = create_webserver(PORT + 7);
    bool threw = false;
    try {
        ws.route(http_method::count_, "/s", [](const http_request&) {
            return http_response::string("x");
        });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(route_with_count_sentinel_throws_invalid_argument)

// Duplicate (method, path) registration via route() throws (delegates to
// the on_* conflict path).
LT_BEGIN_AUTO_TEST(webserver_route_suite, route_duplicate_method_path_throws)
    webserver ws = create_webserver(PORT + 8);
    ws.route(http_method::get, "/d", [](const http_request&) {
        return http_response::string("first");
    });

    bool threw = false;
    try {
        ws.route(http_method::get, "/d", [](const http_request&) {
            return http_response::string("second");
        });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(route_duplicate_method_path_throws)

// Cross-overload conflict: route(get, "/x", ...) then on_get("/x", ...)
// must throw. Same shared registration path means the conflict is
// detected from either entry point.
LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   route_then_on_get_same_path_throws)
    webserver ws = create_webserver(PORT + 9);
    ws.route(http_method::get, "/x", [](const http_request&) {
        return http_response::string("r");
    });

    bool threw = false;
    try {
        ws.on_get("/x", [](const http_request&) {
            return http_response::string("o");
        });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(route_then_on_get_same_path_throws)

// Reverse direction: on_get then route(get) must also throw.
LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   on_get_then_route_get_same_path_throws)
    webserver ws = create_webserver(PORT + 10);
    ws.on_get("/x", [](const http_request&) {
        return http_response::string("o");
    });

    bool threw = false;
    try {
        ws.route(http_method::get, "/x", [](const http_request&) {
            return http_response::string("r");
        });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(on_get_then_route_get_same_path_throws)

// Atomicity: route(method_set{get, post}, "/p", ...) when GET is already
// taken on /p must throw AND leave POST unregistered (POST /p -> 405).
// Pins all-or-nothing semantics for the method_set overload.
LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   route_method_set_partial_overlap_with_existing_throws_atomic)
    webserver ws = create_webserver(PORT + 11);
    ws.on_get("/p", [](const http_request&) {
        return http_response::string("g");
    });

    bool threw = false;
    try {
        ws.route(method_set{}.set(http_method::get).set(http_method::post),
                 "/p", [](const http_request&) {
                     return http_response::string("either");
                 });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);

    // Now start the server and verify POST /p returns 405 -- POST was
    // NOT silently registered before the throw.
    ws.start(false);
    fetch_result post = do_request("localhost:8241/p", "POST", "");
    LT_CHECK_EQ(post.response_code, 405);
    // GET is still wired up (unaffected by the failed registration).
    fetch_result get = do_request("localhost:8241/p", "GET");
    LT_CHECK_EQ(get.response_code, 200);
    LT_CHECK_EQ(get.body, std::string("g"));
    ws.stop();
LT_END_AUTO_TEST(route_method_set_partial_overlap_with_existing_throws_atomic)

// route(method_set, "/users/{id}", ...) goes through the regex tier and
// binds {id}.
LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   route_method_set_parameterized_path_binds_arg)
    webserver ws = create_webserver(PORT + 12);
    ws.route(method_set{}.set(http_method::get).set(http_method::head),
             "/users/{id}", [](const http_request& req) {
                 std::string body = "id=";
                 body.append(req.get_arg("id"));
                 return http_response::string(body);
             });
    ws.start(false);

    fetch_result fr = do_request("localhost:8242/users/42", "GET");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("id=42"));

    ws.stop();
LT_END_AUTO_TEST(route_method_set_parameterized_path_binds_arg)

// Empty (default-constructed) std::function throws on either overload.
LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   route_empty_handler_throws_invalid_argument_single_method)
    webserver ws = create_webserver(PORT + 13);
    bool threw = false;
    try {
        ws.route(http_method::get, "/",
                 std::function<http_response(const http_request&)>{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(route_empty_handler_throws_invalid_argument_single_method)

LT_BEGIN_AUTO_TEST(webserver_route_suite,
                   route_empty_handler_throws_invalid_argument_method_set)
    webserver ws = create_webserver(PORT + 14);
    bool threw = false;
    try {
        ws.route(method_set{}.set(http_method::get), "/",
                 std::function<http_response(const http_request&)>{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(route_empty_handler_throws_invalid_argument_method_set)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
