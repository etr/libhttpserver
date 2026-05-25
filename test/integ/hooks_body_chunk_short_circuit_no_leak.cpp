/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-047 acceptance criterion 3.
//
// "A `body_chunk` short-circuit drops the post-processor without leaking
//  (verified under ASan in the existing `build-type: asan` CI matrix
//  entry)."
//
// We POST application/x-www-form-urlencoded data to /upload-form so MHD
// allocates a post-processor in requests_answer_first_step. A body_chunk
// hook then returns respond_with(403) on the very first firing,
// short-circuiting the upload. The short-circuit branch must call
// MHD_destroy_post_processor(mr->pp) — otherwise ASan flags a leak of
// the post-processor's 32 KB buffer.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::body_chunk_ctx;
using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8202

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

struct counters {
    std::atomic<bool> handler_called{false};
};

class upload_form_resource : public httpserver::http_resource {
 public:
    explicit upload_form_resource(counters* c) : c_(c) {}
    http_response render_post(const http_request&) override {
        c_->handler_called.store(true, std::memory_order_relaxed);
        return http_response::string("OK");
    }
 private:
    counters* c_;
};

}  // namespace

LT_BEGIN_SUITE(hooks_body_chunk_short_circuit_no_leak_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_body_chunk_short_circuit_no_leak_suite)

LT_BEGIN_AUTO_TEST(hooks_body_chunk_short_circuit_no_leak_suite,
                   form_urlencoded_short_circuit_destroys_pp)
    counters c;
    // post_process_enabled defaults to true; left as-is so the
    // form-urlencoded content-type triggers MHD_create_post_processor.
    webserver ws{create_webserver(PORT)};

    std::atomic<std::size_t> body_chunk_count{0};
    auto h_body = ws.add_hook(hook_phase::body_chunk,
        std::function<hook_action(body_chunk_ctx&)>(
            [&body_chunk_count](body_chunk_ctx&) {
                if (body_chunk_count.fetch_add(1, std::memory_order_relaxed) == 0) {
                    return hook_action::respond_with(
                        http_response::empty().with_status(403));
                }
                return hook_action::pass();
            }));

    auto resource = std::make_shared<upload_form_resource>(&c);
    ws.register_path("/upload-form", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string body = "key=";
    body.append(5000, 'X');

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/upload-form";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body.size()));
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers,
                                "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(http_code, 403L);
    LT_CHECK_EQ(resp_body, std::string{});
    LT_CHECK_EQ(c.handler_called.load(), false);

    ws.stop();
LT_END_AUTO_TEST(form_urlencoded_short_circuit_destroys_pp)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
