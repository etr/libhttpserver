/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-048 finding fix: verifies that the three v1 alias hooks are
// functional (not observation-only stubs) and that before_handler fires
// in finalize_answer (not inside dispatch_resource_handler), so auth and
// method-not-allowed are handled via the hook chain.
//
// Test rationale:
//
// method_not_allowed_handler registers a FUNCTIONAL before_handler hook.
// When method_not_allowed_handler is set AND a user before_handler hook is
// registered after construction (so alias is hook[0], user is hook[1]):
//
//   Current (stub) behaviour:
//     - Hook[0] = no-op stub (returns pass())
//     - Hook[1] = user hook (fires, increments counter)
//     - is_allowed check in dispatch_resource_handler returns false → 405
//     → user hook fires: counter == 1
//
//   Target (functional) behaviour:
//     - Hook[0] = functional method_not_allowed hook: checks is_allowed,
//       method not allowed → short-circuits with 405 response
//     - Hook[1] = user hook: NEVER fires (chain short-circuited)
//     → user hook fires: counter == 0
//
// The test asserts counter == 0, which only passes after the functional
// hook implementation.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::before_handler_ctx;
using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8206

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// POST-only resource: any GET triggers is_allowed=false.
class post_only_resource : public httpserver::http_resource {
 public:
    post_only_resource() {
        disallow_all();
        set_allowing(httpserver::http_method::post, true);
    }
    http_response render_post(const http_request&) override {
        return http_response::string("POST_OK");
    }
};

}  // namespace

LT_BEGIN_SUITE(hooks_alias_functional_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_alias_functional_suite)

// When method_not_allowed_handler is set (registering a before_handler
// alias as hook[0]) and a user before_handler hook is added after
// construction (hook[1]), a method-not-allowed request must short-circuit
// at hook[0] so hook[1] never fires.
LT_BEGIN_AUTO_TEST(hooks_alias_functional_suite,
                   method_not_allowed_alias_short_circuits_before_user_hook)
    std::atomic<int> user_hook_calls{0};
    // Install method_not_allowed_handler alias → hook[0] in before_handler chain.
    webserver ws{create_webserver(PORT)
        .method_not_allowed_handler([](const http_request&) {
            return http_response::string("CUSTOM_405").with_status(405);
        })};

    // User hook added after construction → hook[1].
    // If alias is functional (short-circuits), this must NOT fire.
    auto h = ws.add_hook(hook_phase::before_handler,
        std::function<hook_action(before_handler_ctx&)>(
            [&user_hook_calls](before_handler_ctx&) -> hook_action {
                user_hook_calls.fetch_add(1, std::memory_order_relaxed);
                return hook_action::pass();
            }));

    auto resource = std::make_shared<post_only_resource>();
    ws.register_path("/postonly", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    // GET on a POST-only resource → method not allowed.
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/postonly";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    ws.stop();

    // Alias fired and short-circuited → custom 405 body on the wire.
    LT_CHECK_EQ(http_code, 405L);
    LT_CHECK_EQ(resp_body, std::string("CUSTOM_405"));
    // User hook[1] must NOT have fired — alias hook[0] short-circuited.
    LT_CHECK_EQ(user_hook_calls.load(), 0);
LT_END_AUTO_TEST(method_not_allowed_alias_short_circuits_before_user_hook)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
