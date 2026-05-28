/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-050 acceptance criterion 2.
//
// "An after_handler hook returning hook_action::pass() after calling
//  ctx.response->with_header('X-Foo', 'bar') produces a response on the
//  wire that carries the header."
//
// Pins the mutable-reference contract on after_handler_ctx::response.

#include <curl/curl.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::after_handler_ctx;
using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8241

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

size_t headerfunc(char* buffer, size_t size, size_t nitems, std::string* hdrs) {
    hdrs->append(buffer, size * nitems);
    return size * nitems;
}

class hello_resource : public httpserver::http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("hi");
    }
};

}  // namespace

LT_BEGIN_SUITE(hooks_after_handler_mutates_response_in_place_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_after_handler_mutates_response_in_place_suite)

LT_BEGIN_AUTO_TEST(hooks_after_handler_mutates_response_in_place_suite,
                   pass_with_header_mutation_lands_on_wire)
    webserver ws{create_webserver(PORT)};

    auto h = ws.add_hook(hook_phase::after_handler,
        std::function<hook_action(after_handler_ctx&)>(
            [](after_handler_ctx& ctx) {
                if (ctx.response != nullptr) {
                    ctx.response->with_header("X-Foo", "bar");
                }
                return hook_action::pass();
            }));

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/mut", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/mut";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string body, hdrs;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hdrs);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;  // NOLINT(runtime/int) -- libcurl API takes long*
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    ws.stop();

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(code, 200L);
    LT_CHECK_EQ(body, std::string("hi"));
    // Pin the in-place header mutation lands on the wire.
    LT_CHECK(hdrs.find("X-Foo: bar") != std::string::npos);
LT_END_AUTO_TEST(pass_with_header_mutation_lands_on_wire)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
