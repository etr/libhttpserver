/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-051 acceptance criterion 5:
//
// "TSan-clean under the existing tsan CI matrix entry; the route_table
//  -> resource -> server-wide lock order is exercised by
//  hooks_per_route_concurrent_registration (registration on resource R
//  from inside a handler on resource Q)."
//
// Stress test: resource Q's handler calls add_hook on a DIFFERENT
// resource R while Q's request is still in-flight (dispatch holding
// the route_table_mutex_ shared). This exercises the documented
// lock order route_table -> resource hook_table -> server-wide
// hook_table without ever holding two of those locks at the same
// time across an iteration step.
//
// The test runs many concurrent client requests for ~1-2 seconds so
// TSan has enough material to flag any racy interleaving. The
// per-request handle ownership pattern is intentionally simple: we
// retain the handles in a shared vector so the registrations stay
// alive; the per-route hook chain on R observes a steady stream of
// new slots as the test progresses.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_handle;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8245

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class r_resource : public http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("r");
    }
};

// Q's handler registers a fresh per-route hook on the OTHER resource R
// from inside the request thread. This is the inverted-order case the
// lock-order documentation in arch §5.6 is about.
class q_resource : public http_resource {
 public:
    q_resource(std::shared_ptr<http_resource> other,
               std::mutex* handles_mtx,
               std::vector<hook_handle>* handles)
        : other_(std::move(other)),
          handles_mtx_(handles_mtx),
          handles_(handles) {}

    http_response render_get(const http_request&) override {
        // Register a no-op per-route hook on the OTHER resource. This is
        // the operation that, if the lock order were wrong, would
        // deadlock or race with the request currently being dispatched.
        auto h = other_->add_hook(hook_phase::request_completed,
            std::function<void(const httpserver::request_completed_ctx&)>(
                [](const httpserver::request_completed_ctx&) {}));
        {
            std::lock_guard<std::mutex> g(*handles_mtx_);
            handles_->emplace_back(std::move(h));
        }
        return http_response::string("q");
    }

 private:
    std::shared_ptr<http_resource> other_;
    std::mutex* handles_mtx_;
    std::vector<hook_handle>* handles_;
};

}  // namespace

LT_BEGIN_SUITE(hooks_per_route_concurrent_registration_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_per_route_concurrent_registration_suite)

LT_BEGIN_AUTO_TEST(hooks_per_route_concurrent_registration_suite,
                   register_on_R_from_handler_on_Q)
    webserver ws{create_webserver(PORT)};

    auto r = std::make_shared<r_resource>();

    std::mutex handles_mtx;
    std::vector<hook_handle> handles;
    auto q = std::make_shared<q_resource>(r, &handles_mtx, &handles);

    ws.register_path("/q", q);
    ws.register_path("/r", r);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    constexpr int kThreads = 8;
    constexpr auto kRunDuration = std::chrono::milliseconds(800);
    const auto deadline = std::chrono::steady_clock::now() + kRunDuration;

    std::atomic<std::size_t> q_count{0};
    std::atomic<std::size_t> r_count{0};

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([deadline, &q_count, &r_count]() {
            CURL* curl = curl_easy_init();
            if (!curl) return;
            std::string url_q = "http://127.0.0.1:" +
                std::to_string(PORT) + "/q";
            std::string url_r = "http://127.0.0.1:" +
                std::to_string(PORT) + "/r";
            while (std::chrono::steady_clock::now() < deadline) {
                // Alternate between hitting q (which writes a new
                // per-route registration on R) and hitting r (which
                // reads R's hook table for the per-route response_sent
                // / request_completed chain).
                std::string body;
                curl_easy_reset(curl);
                curl_easy_setopt(curl, CURLOPT_URL, url_q.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
                if (curl_easy_perform(curl) == CURLE_OK) {
                    q_count.fetch_add(1, std::memory_order_relaxed);
                }
                body.clear();
                curl_easy_reset(curl);
                curl_easy_setopt(curl, CURLOPT_URL, url_r.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
                if (curl_easy_perform(curl) == CURLE_OK) {
                    r_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            curl_easy_cleanup(curl);
        });
    }
    for (auto& t : workers) t.join();
    ws.stop();

    // The headline is "no crash / no TSan flag"; volume gives TSan
    // material to inspect. A small >0 floor is enough to confirm the
    // request path actually ran -- on a heavily loaded host a few
    // requests might fail but the test should not crash.
    LT_CHECK(q_count.load() > 0);
    LT_CHECK(r_count.load() > 0);
LT_END_AUTO_TEST(register_on_R_from_handler_on_Q)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
