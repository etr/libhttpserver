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

// TASK-025: lambda handler entry points on_get / on_post / on_put /
// on_delete / on_patch / on_options / on_head.
//
// Goal: stateless endpoints can be registered without subclassing
// http_resource. The seven on_* overloads accept
// std::function<http_response(const http_request&)>; multiple on_* calls
// on the same path COMPOSE (each adds a method bit to a single route
// entry); a duplicate (method, path) registration THROWS
// std::invalid_argument.
//
// This TU pins both the compile-time signature contract and the runtime
// behaviour (real curl round-trips against a running webserver),
// matching the TASK-024 test pattern.

#include <curl/curl.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "./httpserver.hpp"
#include "./httpserver/detail/route_entry.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::http_method;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::method_set;
using httpserver::webserver;

#define PORT 8190

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
        // strip leading spaces and trailing CRLF.
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
        // CURLOPT_NOBODY tells libcurl not to wait for a response body
        // (HEAD responses carry headers only). Without this, curl_easy_perform
        // hangs waiting for bytes that never arrive.
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

fetch_result fetch(const std::string& url) {
    return do_request(url, "GET");
}

}  // namespace

// ---- Compile-time signature contract -----------------------------------

// (1) Each on_* overload exists, takes (const string&, std::function<...>),
//     and returns void.
template <typename Fn>
using on_get_call_t = decltype(std::declval<webserver&>().on_get(
    std::declval<const std::string&>(), std::declval<Fn>()));

using lambda_sig = std::function<http_response(const http_request&)>;

static_assert(std::is_same_v<on_get_call_t<lambda_sig>, void>,
              "on_get(const string&, std::function<http_response(const http_request&)>) "
              "must exist and return void");

static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().on_post(
                      std::declval<const std::string&>(),
                      std::declval<lambda_sig>())),
                  void>,
              "on_post must exist and return void");
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().on_put(
                      std::declval<const std::string&>(),
                      std::declval<lambda_sig>())),
                  void>,
              "on_put must exist and return void");
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().on_delete(
                      std::declval<const std::string&>(),
                      std::declval<lambda_sig>())),
                  void>,
              "on_delete must exist and return void");
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().on_patch(
                      std::declval<const std::string&>(),
                      std::declval<lambda_sig>())),
                  void>,
              "on_patch must exist and return void");
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().on_options(
                      std::declval<const std::string&>(),
                      std::declval<lambda_sig>())),
                  void>,
              "on_options must exist and return void");
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().on_head(
                      std::declval<const std::string&>(),
                      std::declval<lambda_sig>())),
                  void>,
              "on_head must exist and return void");

// (2) The route_entry detail type pins the §4.7 shape: method_set + a
//     two-arm variant + a bool is_prefix flag.
static_assert(std::is_same_v<
                  decltype(httpserver::detail::route_entry::methods),
                  method_set>,
              "route_entry::methods must be method_set");

static_assert(std::is_same_v<
                  decltype(httpserver::detail::route_entry::handler),
                  std::variant<httpserver::detail::lambda_handler,
                               std::shared_ptr<http_resource>>>,
              "route_entry::handler must be variant<lambda_handler, "
              "shared_ptr<http_resource>>");

static_assert(std::is_same_v<
                  decltype(httpserver::detail::route_entry::is_prefix),
                  bool>,
              "route_entry::is_prefix must be bool");

// ---- Runtime behaviour tests -------------------------------------------

LT_BEGIN_SUITE(webserver_on_methods_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(webserver_on_methods_suite)

// PRD §3.4 acceptance: hello-world on_get returns 200 "hi".
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite, on_get_hello_world)
    webserver ws = create_webserver(PORT);
    ws.on_get("/", [](const http_request&) {
        return http_response::string("hi");
    });
    ws.start(false);

    fetch_result fr = fetch("localhost:8190/");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("hi"));

    ws.stop();
LT_END_AUTO_TEST(on_get_hello_world)

// A lambda registered for GET only must 405 a POST and emit Allow: GET.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   lambda_route_dispatches_only_for_registered_method)
    webserver ws = create_webserver(PORT + 1);
    ws.on_get("/x", [](const http_request&) {
        return http_response::string("g");
    });
    ws.start(false);

    fetch_result get_result = do_request("localhost:8191/x", "GET");
    LT_CHECK_EQ(get_result.response_code, 200);
    LT_CHECK_EQ(get_result.body, std::string("g"));

    fetch_result post_result = do_request("localhost:8191/x", "POST", "ignored");
    LT_CHECK_EQ(post_result.response_code, 405);
    LT_CHECK_EQ(post_result.allow_header, std::string("GET"));

    ws.stop();
LT_END_AUTO_TEST(lambda_route_dispatches_only_for_registered_method)

// Multiple on_* on the same path compose: GET and POST both serve.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   on_get_and_on_post_same_path_serve_both)
    webserver ws = create_webserver(PORT + 2);
    ws.on_get("/y", [](const http_request&) {
        return http_response::string("g");
    });
    ws.on_post("/y", [](const http_request&) {
        return http_response::string("p");
    });
    ws.start(false);

    fetch_result get_result = do_request("localhost:8192/y", "GET");
    LT_CHECK_EQ(get_result.response_code, 200);
    LT_CHECK_EQ(get_result.body, std::string("g"));

    fetch_result post_result = do_request("localhost:8192/y", "POST", "");
    LT_CHECK_EQ(post_result.response_code, 200);
    LT_CHECK_EQ(post_result.body, std::string("p"));

    ws.stop();
LT_END_AUTO_TEST(on_get_and_on_post_same_path_serve_both)

// Each of the seven on_* overloads dispatches its own method.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   all_seven_on_methods_serve_their_method)
    webserver ws = create_webserver(PORT + 3);
    ws.on_get("/all", [](const http_request&) {
        return http_response::string("get");
    });
    ws.on_post("/all", [](const http_request&) {
        return http_response::string("post");
    });
    ws.on_put("/all", [](const http_request&) {
        return http_response::string("put");
    });
    ws.on_delete("/all", [](const http_request&) {
        return http_response::string("delete");
    });
    ws.on_patch("/all", [](const http_request&) {
        return http_response::string("patch");
    });
    ws.on_options("/all", [](const http_request&) {
        return http_response::string("options");
    });
    ws.on_head("/all", [](const http_request&) {
        return http_response::string("head");
    });
    ws.start(false);

    LT_CHECK_EQ(do_request("localhost:8193/all", "GET").body,
                std::string("get"));
    LT_CHECK_EQ(do_request("localhost:8193/all", "POST", "").body,
                std::string("post"));
    LT_CHECK_EQ(do_request("localhost:8193/all", "PUT", "").body,
                std::string("put"));
    LT_CHECK_EQ(do_request("localhost:8193/all", "DELETE").body,
                std::string("delete"));
    LT_CHECK_EQ(do_request("localhost:8193/all", "PATCH", "").body,
                std::string("patch"));
    LT_CHECK_EQ(do_request("localhost:8193/all", "OPTIONS").body,
                std::string("options"));
    // HEAD: curl strips body; just assert 200.
    LT_CHECK_EQ(do_request("localhost:8193/all", "HEAD").response_code, 200);

    ws.stop();
LT_END_AUTO_TEST(all_seven_on_methods_serve_their_method)

// Conflict: a second on_get on the same path throws.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   duplicate_on_get_same_path_throws_invalid_argument)
    webserver ws = create_webserver(PORT + 4);
    ws.on_get("/z", [](const http_request&) {
        return http_response::string("first");
    });

    bool threw = false;
    try {
        ws.on_get("/z", [](const http_request&) {
            return http_response::string("second");
        });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(duplicate_on_get_same_path_throws_invalid_argument)

// Conflict-after-merge: register GET, then POST, then GET again.
// The third call must throw even though the intervening POST registration
// succeeded, because GET is already covered for /w.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   on_get_then_on_get_after_on_post_still_throws)
    webserver ws = create_webserver(PORT + 5);
    ws.on_get("/w", [](const http_request&) {
        return http_response::string("g1");
    });
    ws.on_post("/w", [](const http_request&) {
        return http_response::string("p");
    });

    bool threw = false;
    try {
        ws.on_get("/w", [](const http_request&) {
            return http_response::string("g2");
        });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(on_get_then_on_get_after_on_post_still_throws)

// Parameterized path goes through the regex tier; arg is bound from URL.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   on_get_parameterized_path_binds_arg)
    webserver ws = create_webserver(PORT + 6);
    ws.on_get("/users/{id}", [](const http_request& req) {
        std::string body = "id=";
        body.append(req.get_arg("id"));
        return http_response::string(body);
    });
    ws.start(false);

    fetch_result fr = fetch("localhost:8196/users/42");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("id=42"));

    ws.stop();
LT_END_AUTO_TEST(on_get_parameterized_path_binds_arg)

// Lambda routes and class routes cannot share a path: registering a
// class resource at a path already owned by a lambda must throw.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   on_get_then_register_path_with_class_throws)
    class my_resource : public http_resource {};

    webserver ws = create_webserver(PORT + 7);
    ws.on_get("/m", [](const http_request&) {
        return http_response::string("lambda");
    });

    bool threw = false;
    try {
        ws.register_path("/m", std::make_shared<my_resource>());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(on_get_then_register_path_with_class_throws)

// Guard: on_get with an empty (default-constructed) std::function throws
// std::invalid_argument. The `if (!handler)` branch on line 332 of
// webserver.cpp must fire before any route-table mutation.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   on_get_empty_handler_throws_invalid_argument)
    webserver ws = create_webserver(PORT + 8);
    bool threw = false;
    try {
        ws.on_get("/", std::function<http_response(const http_request&)>{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(on_get_empty_handler_throws_invalid_argument)

// Guard: on_get on a single_resource webserver with a path other than ""
// or "/" throws std::invalid_argument.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   on_get_single_resource_non_root_path_throws)
    webserver ws = create_webserver(PORT + 9).single_resource();
    bool threw = false;
    try {
        ws.on_get("/some/path", [](const http_request&) {
            return http_response::string("x");
        });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(on_get_single_resource_non_root_path_throws)

// Mutual-exclusion: registering a class-based resource first, then
// calling on_get on the same path, must also throw std::invalid_argument.
// This is the reverse of on_get_then_register_path_with_class_throws.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   register_path_then_on_get_same_path_throws)
    class my_resource : public http_resource {};

    webserver ws = create_webserver(PORT + 10);
    ws.register_path("/n", std::make_shared<my_resource>());

    bool threw = false;
    try {
        ws.on_get("/n", [](const http_request&) {
            return http_response::string("lambda");
        });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(register_path_then_on_get_same_path_throws)

// Verify each on_* method routes correctly: individual per-method tests
// so a failure in one method does not mask failures in others.
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite, on_get_dispatches_get)
    webserver ws = create_webserver(PORT + 11);
    ws.on_get("/all7", [](const http_request&) {
        return http_response::string("get");
    });
    ws.start(false);
    LT_CHECK_EQ(do_request("localhost:8201/all7", "GET").body,
                std::string("get"));
    ws.stop();
LT_END_AUTO_TEST(on_get_dispatches_get)

LT_BEGIN_AUTO_TEST(webserver_on_methods_suite, on_post_dispatches_post)
    webserver ws = create_webserver(PORT + 12);
    ws.on_post("/all7", [](const http_request&) {
        return http_response::string("post");
    });
    ws.start(false);
    LT_CHECK_EQ(do_request("localhost:8202/all7", "POST", "").body,
                std::string("post"));
    ws.stop();
LT_END_AUTO_TEST(on_post_dispatches_post)

LT_BEGIN_AUTO_TEST(webserver_on_methods_suite, on_put_dispatches_put)
    webserver ws = create_webserver(PORT + 13);
    ws.on_put("/all7", [](const http_request&) {
        return http_response::string("put");
    });
    ws.start(false);
    LT_CHECK_EQ(do_request("localhost:8203/all7", "PUT", "").body,
                std::string("put"));
    ws.stop();
LT_END_AUTO_TEST(on_put_dispatches_put)

LT_BEGIN_AUTO_TEST(webserver_on_methods_suite, on_delete_dispatches_delete)
    webserver ws = create_webserver(PORT + 14);
    ws.on_delete("/all7", [](const http_request&) {
        return http_response::string("delete");
    });
    ws.start(false);
    LT_CHECK_EQ(do_request("localhost:8204/all7", "DELETE").body,
                std::string("delete"));
    ws.stop();
LT_END_AUTO_TEST(on_delete_dispatches_delete)

LT_BEGIN_AUTO_TEST(webserver_on_methods_suite, on_patch_dispatches_patch)
    webserver ws = create_webserver(PORT + 15);
    ws.on_patch("/all7", [](const http_request&) {
        return http_response::string("patch");
    });
    ws.start(false);
    LT_CHECK_EQ(do_request("localhost:8205/all7", "PATCH", "").body,
                std::string("patch"));
    ws.stop();
LT_END_AUTO_TEST(on_patch_dispatches_patch)

LT_BEGIN_AUTO_TEST(webserver_on_methods_suite, on_options_dispatches_options)
    webserver ws = create_webserver(PORT + 16);
    ws.on_options("/all7", [](const http_request&) {
        return http_response::string("options");
    });
    ws.start(false);
    LT_CHECK_EQ(do_request("localhost:8206/all7", "OPTIONS").body,
                std::string("options"));
    ws.stop();
LT_END_AUTO_TEST(on_options_dispatches_options)

LT_BEGIN_AUTO_TEST(webserver_on_methods_suite, on_head_dispatches_head)
    webserver ws = create_webserver(PORT + 17);
    ws.on_head("/all7", [](const http_request&) {
        return http_response::string("head");
    });
    ws.start(false);
    // HEAD: curl strips body; just assert 200.
    LT_CHECK_EQ(do_request("localhost:8207/all7", "HEAD").response_code, 200);
    ws.stop();
LT_END_AUTO_TEST(on_head_dispatches_head)

// Compose two on_* calls on a true regex-tier path (regex metacharacters,
// no {name} params). The second call (on_post, fresh==false) must update
// the existing regex-tier entry's methods bitmask without recompiling the
// regex or double-inserting, and both methods must be served.
//
// This test guards the refactored classify_route_tier helper and the
// fresh-gated update path (finding code-simplifier-iter2-1, -2).
LT_BEGIN_AUTO_TEST(webserver_on_methods_suite,
                   on_get_and_on_post_compose_on_true_regex_path)
    webserver ws = create_webserver(PORT + 18);
    // /api/v[0-9]+ contains regex metacharacters but no {name} params.
    // With regex_checking=true (default) this is a "true regex" path:
    // the compiled pattern matches /api/v1 but not the literal string
    // /api/v[0-9]+ itself, so the route lands in the regex tier.
    ws.on_get("/api/v[0-9]+", [](const http_request&) {
        return http_response::string("get");
    });
    // Second registration on the same path -- this is the fresh==false
    // update path in on_methods_ that previously recompiled std::regex.
    ws.on_post("/api/v[0-9]+", [](const http_request&) {
        return http_response::string("post");
    });
    ws.start(false);

    fetch_result get_result = do_request("localhost:8208/api/v1", "GET");
    LT_CHECK_EQ(get_result.response_code, 200);
    LT_CHECK_EQ(get_result.body, std::string("get"));

    fetch_result post_result = do_request("localhost:8208/api/v2", "POST", "");
    LT_CHECK_EQ(post_result.response_code, 200);
    LT_CHECK_EQ(post_result.body, std::string("post"));

    // DELETE is not registered; must 405.
    fetch_result del_result = do_request("localhost:8208/api/v3", "DELETE");
    LT_CHECK_EQ(del_result.response_code, 405);

    ws.stop();
LT_END_AUTO_TEST(on_get_and_on_post_compose_on_true_regex_path)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
