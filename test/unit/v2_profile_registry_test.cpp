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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-096 Cycle 4: v2 profile registry guard.
//
// Every named profile in the parity fixture registry must start on an
// ephemeral port (port 0), serve its /__smoke GET with 200 "smoke-ok",
// and stop cleanly. This is the anti-rot gate for the profile registry
// the transcript runner consumes in TASK-097+ (TASK-105..107 drive the
// same profiles through raw segmented sockets).
//
// Feature-gated profiles (tls, auth_digest, websocket on off-builds)
// SKIP per the LT_SKIP_IF convention instead of failing.

#include "../parity/v2_fixture.hpp"

#include <curl/curl.h>

#include <cstdint>
#include <string>
#include <utility>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./integ/curl_helpers.hpp"
#include "./integ/server_ready.hpp"

namespace {

using httpserver_test::wait_for_server_ready;

struct smoke_result {
    long status = 0;
    std::string body;
};

smoke_result fetch_smoke(uint16_t port, bool https) {
    smoke_result out;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return out;
    const std::string url = (https ? "https://" : "http://") + std::string("127.0.0.1:") +
                            std::to_string(port) + "/__smoke";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &httpserver_test::writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out.body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    if (https) {
        // Test harness trusts the repo's self-signed test certificate.
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
    curl_easy_cleanup(curl);
    return out;
}

}  // namespace

LT_BEGIN_SUITE(v2_profile_registry_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(v2_profile_registry_suite)

// One smoke test per profile, generated so the LT_* macros stay inside
// a real test body. Feature-off builds SKIP via LT_SKIP_IF.
#define PARITY_SMOKE_TEST(test_name, profile_literal)                        \
    LT_BEGIN_AUTO_TEST(v2_profile_registry_suite, test_name)                 \
        parity::v2_fixture fx;                                               \
        LT_SKIP_IF(!fx.profile_available(profile_literal),                   \
                   "profile " profile_literal                                \
                   " unavailable in this build");                            \
        uint16_t port = fx.start(profile_literal);                           \
        LT_CHECK_NEQ(port, 0); /* port(0) -> ephemeral port */               \
        httpserver_test::wait_for_server_ready(static_cast<int>(port));      \
        smoke_result r = fetch_smoke(port,                                   \
            std::string(profile_literal) == "tls");                          \
        LT_CHECK_EQ(r.status, 200L);                                         \
        LT_CHECK_EQ(r.body, "smoke-ok");                                     \
        fx.stop();                                                           \
    LT_END_AUTO_TEST(test_name)

PARITY_SMOKE_TEST(profile_routing_basic, "routing_basic")
PARITY_SMOKE_TEST(profile_routing_hooks, "routing_hooks")
PARITY_SMOKE_TEST(profile_auth_basic, "auth_basic")
PARITY_SMOKE_TEST(profile_auth_digest, "auth_digest")
PARITY_SMOKE_TEST(profile_forms, "forms")
PARITY_SMOKE_TEST(profile_file_resp, "file_resp")
PARITY_SMOKE_TEST(profile_ip_controls, "ip_controls")
PARITY_SMOKE_TEST(profile_shoutcast, "shoutcast")
PARITY_SMOKE_TEST(profile_websocket, "websocket")
PARITY_SMOKE_TEST(profile_tls, "tls")

LT_BEGIN_AUTO_TEST(v2_profile_registry_suite, unknown_profile_throws)
    parity::v2_fixture fx;
    LT_CHECK_THROW(fx.start("no_such_profile"));
LT_END_AUTO_TEST(unknown_profile_throws)

LT_BEGIN_AUTO_TEST(v2_profile_registry_suite, availability_matches_features)
    parity::v2_fixture fx;
    auto f = httpserver::webserver::features();
    LT_CHECK_EQ(fx.profile_available("auth_basic"), f.basic_auth);
    LT_CHECK_EQ(fx.profile_available("auth_digest"), f.digest_auth);
    LT_CHECK_EQ(fx.profile_available("tls"), f.tls);
    LT_CHECK_EQ(fx.profile_available("websocket"), f.websocket);
    LT_CHECK(fx.has_profile("routing_basic"));
    LT_CHECK(!fx.has_profile("no_such_profile"));
LT_END_AUTO_TEST(availability_matches_features)

LT_BEGIN_AUTO_TEST(v2_profile_registry_suite, restart_same_profile)
    parity::v2_fixture fx;
    for (int i = 0; i < 2; ++i) {
        uint16_t port = fx.start("routing_basic");
        LT_CHECK_NEQ(port, 0);
        wait_for_server_ready(static_cast<int>(port));
        smoke_result r = fetch_smoke(port, false);
        LT_CHECK_EQ(r.status, 200L);
        fx.stop();
    }
LT_END_AUTO_TEST(restart_same_profile)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
