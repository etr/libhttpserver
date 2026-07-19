/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// Contract under test:
//
// "An after_handler hook returning hook_action::respond_with(r2) is
//  observed on the wire; the response from the resource is NOT."
//
// after_handler fires between dispatch_resource_handler (which populates
// conn->response with the resource's reply) and materialize_and_queue_response.
// A respond_with(...) action REPLACES conn->response so the resource's
// reply is discarded.

#include <curl/curl.h>

#include <functional>
#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./server_ready.hpp"

using httpserver::after_handler_ctx;
using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8240

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class resource_returns_handler_payload : public httpserver::http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("FROM_HANDLER").with_status(200);
    }
};

}  // namespace

LT_BEGIN_SUITE(hooks_after_handler_replaces_response_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_after_handler_replaces_response_suite)

LT_BEGIN_AUTO_TEST(hooks_after_handler_replaces_response_suite,
                   respond_with_replaces_handler_response)
    webserver ws{create_webserver(PORT)};

    auto h = ws.add_hook(hook_phase::after_handler,
        std::function<hook_action(after_handler_ctx&)>(
            [](after_handler_ctx&) {
                return hook_action::respond_with(
                    http_response::string("FROM_HOOK").with_status(418));
            }));

    auto resource = std::make_shared<resource_returns_handler_payload>();
    ws.register_path("/replace", resource);
    ws.start(false);
    httpserver_test::wait_for_server_ready(PORT);

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/replace";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;  // NOLINT(runtime/int) -- libcurl API takes long*
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    ws.stop();

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(code, 418L);
    LT_CHECK_EQ(body, std::string("FROM_HOOK"));
LT_END_AUTO_TEST(respond_with_replaces_handler_response)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
