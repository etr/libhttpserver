/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-047 acceptance criterion 2.
//
// "A `body_chunk` hook accumulates bytes.size() across firings; total
//  equals the request body length when the upload completes."
//
// We register a body_chunk hook that accumulates chunk.size() and records
// each (offset, size) pair. POST a deterministic 1.5 MB body to /sink and
// assert that the accumulated total equals body.size(), offsets are
// non-decreasing starting at zero, and the last firing's offset + size
// covers the whole body.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./curl_helpers.hpp"

using httpserver::body_chunk_ctx;
using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8201

namespace {

using httpserver_test::writefunc;

class sink_resource : public httpserver::http_resource {
 public:
    http_response render_post(const http_request&) override {
        return http_response::string("OK");
    }
};

struct chunk_event {
    std::uint64_t offset;
    std::size_t size;
    bool is_final;
};

}  // namespace

LT_BEGIN_SUITE(hooks_body_chunk_observes_progress_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_body_chunk_observes_progress_suite)

LT_BEGIN_AUTO_TEST(hooks_body_chunk_observes_progress_suite,
                   total_equals_body_size)
    webserver ws{create_webserver(PORT)};

    std::mutex mu;
    std::size_t total = 0;
    std::vector<chunk_event> events;

    auto h_body = ws.add_hook(hook_phase::body_chunk,
        std::function<hook_action(body_chunk_ctx&)>(
            [&](body_chunk_ctx& ctx) {
                std::lock_guard<std::mutex> g(mu);
                total += ctx.chunk.size();
                events.push_back({ctx.offset, ctx.chunk.size(), ctx.is_final});
                return hook_action::pass();
            }));

    auto resource = std::make_shared<sink_resource>();
    ws.register_path("/sink", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string body(1572864, 'A');   // 1.5 MB deterministic body

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/sink";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body.size()));  // NOLINT(runtime/int)
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    LT_CHECK_EQ(res, CURLE_OK);

    ws.stop();

    std::vector<chunk_event> snap;
    std::size_t total_snap = 0;
    {
        std::lock_guard<std::mutex> g(mu);
        snap = events;
        total_snap = total;
    }

    LT_CHECK_EQ(total_snap, body.size());
    LT_ASSERT(!snap.empty());
    // First offset is zero.
    LT_CHECK_EQ(snap.front().offset, static_cast<std::uint64_t>(0));
    // Offsets non-decreasing.
    for (std::size_t i = 1; i < snap.size(); ++i) {
        LT_CHECK(snap[i].offset >= snap[i - 1].offset);
    }
    // Last firing's offset + size covers the whole body.
    // (MHD does not always signal is_final in the upload-callback; we
    // relax to "the last entry's offset + size equals body.size()".)
    LT_CHECK_EQ(snap.back().offset + snap.back().size, body.size());
LT_END_AUTO_TEST(total_equals_body_size)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
