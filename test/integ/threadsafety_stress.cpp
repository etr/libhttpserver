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

// TASK-032: DR-008 thread-safety contract stress test.
//
// Sub-test A — concurrent_register_block_from_handlers_no_data_race
//   Drives the PUBLIC mutating surface (register_path, unregister_path,
//   block_ip, unblock_ip) from inside MHD handler threads against a
//   running webserver. 16 curl clients × N seconds at default port 0
//   (kernel-assigned). The TSan-clean rerun is the headline acceptance:
//   `make clean && CXXFLAGS=-fsanitize=thread … && make check` re-runs
//   this binary under TSan via the CI matrix entry `build-type: tsan`
//   in .github/workflows/verify-build.yml (no workflow edit required).
//
//   Wall-clock: 60 seconds by default (per the §9-testing-item-6
//   acceptance criterion: "at least 60 seconds"). Override locally with
//   HTTPSERVER_STRESS_SECONDS=N for fast iteration.
//
// Sub-test B — stop_from_handler_deadlocks_as_documented
//   The DR-008 negative case: stop() called from a handler thread
//   triggers libmicrohttpd to self-join → on this MHD version, an
//   abort with "Failed to join a thread."; on others, a silent
//   deadlock. The test forks a child process to contain the abort
//   so the parent test binary stays healthy. Either a non-zero child
//   exit within 5 s or a 5 s timeout (child SIGKILLed by parent)
//   counts as positive observation of the contract; a zero child exit
//   would be a regression. Skipped in CI; opt-in via
//   HTTPSERVER_RUN_STOP_FROM_HANDLER=1.
//
// **Manual TSan gate (documented):**
//   Rebuild with `CXXFLAGS="-fsanitize=thread -g -O1"
//   LDFLAGS="-fsanitize=thread"` and re-run this binary; expect no
//   "WARNING: ThreadSanitizer: data race" output. Same pattern as
//   route_table_concurrency.cpp (TASK-027).

#include <curl/curl.h>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;

namespace {

// Discard libcurl response bodies — we only care about completing the
// round-trip, not the body content.
size_t discard_write(char* /*ptr*/, size_t size, size_t nmemb,
                     void* /*userdata*/) {
    return size * nmemb;
}

// Counters bumped from handler threads. atomic<int> so the
// LT_CHECK(*_ops > 0) gates at the end are race-free reads.
struct OpCounters {
    std::atomic<int> register_ok{0};
    std::atomic<int> unregister_ok{0};
    std::atomic<int> block_ok{0};
    std::atomic<int> unblock_ok{0};
    std::atomic<int> handler_calls{0};
};

class noop_resource : public ht::http_resource {
 public:
    ht::http_response render_get(
        const ht::http_request&) override {
        return
            ht::http_response::string("ok");
    }
};

// Driver handler: each request decodes `op` and `i` from the query
// string and re-enters the public webserver API. Catches the documented
// `std::invalid_argument` on duplicate-registration races (per
// register_path's contract — not a data race).
ht::http_response driver_body(const ht::http_request& req,
                              ht::webserver* ws, OpCounters* counters) {
    counters->handler_calls.fetch_add(1, std::memory_order_relaxed);

    int op = 0;
    int i = 0;
    try {
        std::string op_s{req.get_arg("op")};
        std::string i_s{req.get_arg("i")};
        if (!op_s.empty()) op = std::stoi(op_s);
        if (!i_s.empty()) i = std::stoi(i_s);
    } catch (...) {
        // Malformed query — still return 200; we only need lock
        // exercise.
    }

    const int slot = i & 0x7;
    const std::string dyn_path =
        "/dyn/" + std::to_string(slot);
    const std::string ip =
        "198.51.100." + std::to_string(i & 0xff);

    switch (op & 0x3) {
        case 0:
            try {
                ws->register_path(dyn_path,
                                  std::make_shared<noop_resource>());
                counters->register_ok.fetch_add(
                    1, std::memory_order_relaxed);
            } catch (const std::invalid_argument&) {
                // Duplicate-registration race is contract, not a bug.
            }
            break;
        case 1:
            try {
                ws->unregister_path(dyn_path);
                counters->unregister_ok.fetch_add(
                    1, std::memory_order_relaxed);
            } catch (...) {
            }
            break;
        case 2:
            ws->block_ip(ip);
            counters->block_ok.fetch_add(1, std::memory_order_relaxed);
            break;
        case 3:
            ws->unblock_ip(ip);
            counters->unblock_ok.fetch_add(
                1, std::memory_order_relaxed);
            break;
    }

    return ht::http_response::string("ok");
}

// Stress duration: default 60 s (acceptance criterion), overridable
// via HTTPSERVER_STRESS_SECONDS for fast local iteration.
int stress_seconds() {
    if (const char* s = std::getenv("HTTPSERVER_STRESS_SECONDS")) {
        try {
            int v = std::stoi(s);
            if (v > 0) return v;
        } catch (...) {
        }
    }
    return 60;
}

}  // namespace

LT_BEGIN_SUITE(threadsafety_stress_suite)
    void set_up() {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    void tear_down() {
        curl_global_cleanup();
    }
LT_END_SUITE(threadsafety_stress_suite)

LT_BEGIN_AUTO_TEST(threadsafety_stress_suite,
                   concurrent_register_block_from_handlers_no_data_race)
    OpCounters counters;

    // Port 0 lets the kernel pick a free port; read it back via
    // get_bound_port() to avoid hard-coded-port collisions when this
    // 60-s test runs alongside other integration tests under
    // `make -j check`.
    ht::webserver ws{
        ht::create_webserver(0)
            .start_method(ht::http::http_utils::INTERNAL_SELECT)
            .max_threads(8)};

    ws.on_get("/driver",
              [&ws, &counters](const ht::http_request& req) {
                  return driver_body(req, &ws, &counters);
              });

    ws.start(false);

    const uint16_t port = ws.get_bound_port();
    LT_CHECK_GT(port, 0);

    const std::string base =
        "http://127.0.0.1:" + std::to_string(port) + "/driver";

    std::atomic<bool> stop{false};
    constexpr int kClients = 16;
    std::vector<std::thread> clients;
    clients.reserve(kClients);

    for (int c = 0; c < kClients; ++c) {
        clients.emplace_back([&, c] {
            // Per-thread curl handle: curl_easy_* is per-handle
            // thread-safe; each thread owns its handle.
            CURL* curl = curl_easy_init();
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_write);
            // 198.51.100.0/24 is TEST-NET-2 (RFC 5737) — block/unblock
            // ops on these IPs cannot blacklist the loopback driver
            // traffic. Belt-and-braces: also bind curl to 127.0.0.1.
            curl_easy_setopt(curl, CURLOPT_INTERFACE, "127.0.0.1");
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);

            std::mt19937 rng(static_cast<uint32_t>(c) * 0x9e3779b9u);

            while (!stop.load(std::memory_order_relaxed)) {
                const int op = static_cast<int>(rng() & 0x3);
                const int i = static_cast<int>(rng() & 0xff);
                const std::string url =
                    base + "?op=" + std::to_string(op) +
                    "&i=" + std::to_string(i);
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_perform(curl);
            }
            curl_easy_cleanup(curl);
        });
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(stress_seconds());
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    stop.store(true, std::memory_order_relaxed);

    for (auto& t : clients) t.join();

    // Acceptance criterion: 60 s of concurrent register/lookup/block.
    // We don't pin exact counts — the gate is "all four mutating ops
    // executed at least once without deadlock or crash, and (under the
    // documented TSan rebuild) no data race fired".
    LT_CHECK_GT(counters.handler_calls.load(), 0);
    LT_CHECK_GT(counters.register_ok.load() +
                    counters.unregister_ok.load(),
                0);
    LT_CHECK_GT(counters.block_ok.load() +
                    counters.unblock_ok.load(),
                0);

    ws.stop();
LT_END_AUTO_TEST(concurrent_register_block_from_handlers_no_data_race)

LT_BEGIN_AUTO_TEST(threadsafety_stress_suite,
                   stop_from_handler_deadlocks_as_documented)
    // Gate: skip unless explicitly opted in. The deadlock case is by
    // design (DR-008); the test exists to PIN the documented behaviour
    // and is opt-in because reproducing the deadlock requires _Exit()
    // to escape the wedged process.
    const char* run = std::getenv("HTTPSERVER_RUN_STOP_FROM_HANDLER");
    if (run == nullptr || std::string(run) != "1") {
        std::cout << "[SKIP] stop_from_handler_deadlocks_as_documented"
                     " — set HTTPSERVER_RUN_STOP_FROM_HANDLER=1 to run\n";
        return;
    }

#ifdef _WIN32
    // fork()/waitpid() are POSIX-only; the wedge cannot be contained in a
    // child process on Windows. Skip — DR-008 is verified by the POSIX
    // lanes, and Windows is not a release-blocking target for this gate.
    std::cout << "[SKIP] stop_from_handler_deadlocks_as_documented"
                 " — fork()/waitpid() unavailable on Windows\n";
    return;
#else
    // Run the wedge in a forked child so the unsafe stop() call does
    // not kill the test binary. The expected observable on this MHD
    // version is fatal-abort: libmicrohttpd detects the self-join
    // attempt (pthread_join on the current thread returns EDEADLK)
    // and aborts with "Failed to join a thread." A silent deadlock
    // (process still alive after 5 s) is the alternative outcome
    // DR-008 documents — both qualify as "unsafe; do not do this."
    //
    // A `ready` result with a normal-zero exit would be a regression
    // against DR-008: it would mean stop() returned successfully from
    // a handler, contradicting the documented contract.
    const pid_t child = fork();
    LT_CHECK(child >= 0);
    if (child == 0) {
        // Child: trigger the contract violation. We do not care about
        // the child's stdout — silence it so the test log stays
        // readable.
        ::close(STDOUT_FILENO);
        ::close(STDERR_FILENO);

        ht::webserver ws{
            ht::create_webserver(0)
                .start_method(ht::http::http_utils::INTERNAL_SELECT)
                .max_threads(4)};

        ws.on_get("/wedge", [&ws](const ht::http_request&) {
            // Call stop() directly on the handler's MHD worker
            // thread → DR-008 unsafe path.
            ws.stop();
            // Below is unreachable. If reached, the contract is
            // broken — exit with a distinctive code (42) so the
            // parent can flag the regression.
            std::_Exit(42);
            return ht::http_response::string("unreachable");
        });

        ws.start(false);
        const uint16_t port = ws.get_bound_port();
        const std::string url = "http://127.0.0.1:" +
                                std::to_string(port) + "/wedge";

        CURL* curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_write);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        // If we ever reach here, stop()-from-handler did NOT abort
        // or deadlock as documented → regression. Exit 43 so the
        // parent flags it distinctly from the unreachable-after-
        // stop() case above.
        std::_Exit(43);
    }

    // Parent: bounded wait on the child. SIGKILL it after 5 s if it
    // is still running (the silent-deadlock branch of DR-008). Any
    // outcome OTHER than a zero exit is a positive observation of
    // the contract; a zero exit (or codes 42/43) is a regression.
    int status = 0;
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool reaped = false;
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t r = ::waitpid(child, &status, WNOHANG);
        if (r == child) {
            reaped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!reaped) {
        // Silent-deadlock branch: kill the child, then reap.
        ::kill(child, SIGKILL);
        ::waitpid(child, &status, 0);
        std::cout << "[OK] stop_from_handler — silent deadlock "
                     "reproduced (child still alive after 5s)\n";
    } else if (WIFSIGNALED(status)) {
        std::cout << "[OK] stop_from_handler — child aborted with "
                     "signal " << WTERMSIG(status)
                  << " (MHD self-join detection)\n";
    } else if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        // Codes 42/43 mark regressions seeded inside the child.
        // Zero exit means stop() returned cleanly from a handler.
        LT_CHECK(code != 0 && code != 42 && code != 43);
        std::cout << "[OK] stop_from_handler — child exited with "
                     "code " << code
                  << " (non-zero exit on contract violation)\n";
    }
#endif  // _WIN32
LT_END_AUTO_TEST(stop_from_handler_deadlocks_as_documented)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
