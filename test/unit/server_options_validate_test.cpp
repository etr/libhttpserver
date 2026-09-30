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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-101 Step 1: the backend-neutral server configuration surface
// (PRD-V3N-REQ-014) and the pre-listen validation gate
// (PRD-V3N-REQ-016): documented bounds, listener set shape, timeout
// inventory, concurrency, budgets, and validation rules V1-V6 and V12
// of the plan's taxonomy. The TLS / provider / protocol combination
// rules (V7-V11) are covered in the step 2 tests.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include <httpserver/server/budgets.hpp>
#include <httpserver/server/options.hpp>

#include "./littletest.hpp"

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;

using outcome_code = httpserver::http::outcome_code;

// --- layout pins -----------------------------------------------------------

static_assert(std::is_same_v<std::underlying_type_t<srv::tls_provider>,
                             std::uint8_t>,
              "tls_provider underlying type must be std::uint8_t");
static_assert(std::is_same_v<std::underlying_type_t<srv::tls_profile>,
                             std::uint8_t>,
              "tls_profile underlying type must be std::uint8_t");
static_assert(srv::tls_options{}.provider == srv::tls_provider::none,
              "tls provider defaults to none (TLS-off)");
static_assert(srv::tls_options{}.profile == srv::tls_profile::none,
              "tls profile defaults to none");

// Default protocol set is HTTP/1.0 + HTTP/1.1 (the TLS-off baseline).
static_assert(srv::protocol_set{}.contains(http::protocol::http_1_0),
              "default protocol set contains HTTP/1.0");
static_assert(srv::protocol_set{}.contains(http::protocol::http_1_1),
              "default protocol set contains HTTP/1.1");
static_assert(!srv::protocol_set{}.contains(http::protocol::http_2),
              "default protocol set excludes HTTP/2");
static_assert(!srv::protocol_set{}.contains(http::protocol::http_3),
              "default protocol set excludes HTTP/3");

static_assert(srv::max_workers >= 1, "worker ceiling must be positive");
static_assert(srv::max_listeners >= 1, "listener ceiling must be positive");
static_assert(srv::max_timeout > std::chrono::milliseconds::zero(),
              "timeout ceiling must be positive");

// Every timeout field defaults to a positive value within the ceiling.
#define SRV_TIMEOUT_PIN(field)                                                \
    static_assert(srv::timeout_options{}.field                                \
                          > std::chrono::milliseconds::zero(),                \
                  "default " #field " timeout must be positive");             \
    static_assert(srv::timeout_options{}.field <= srv::max_timeout,           \
                  "default " #field " timeout must be within the ceiling")

SRV_TIMEOUT_PIN(handshake);
SRV_TIMEOUT_PIN(header);
SRV_TIMEOUT_PIN(body_idle);
SRV_TIMEOUT_PIN(suspension);
SRV_TIMEOUT_PIN(write_idle);
SRV_TIMEOUT_PIN(ws_close);
SRV_TIMEOUT_PIN(drain);

#undef SRV_TIMEOUT_PIN

// Default and ceiling capacities are positive and ordered for every
// resource kind, and the count covers every enumerator.
static_assert([] {
    for (std::size_t i = 0; i < srv::resource_count; ++i) {
        const auto kind = static_cast<srv::resource>(i);
        if (srv::default_capacity(kind) == 0) return false;
        if (srv::default_capacity(kind) > srv::max_capacity(kind)) return false;
    }
    return true;
}(), "default capacities must be positive and within the ceilings");
static_assert(static_cast<std::size_t>(srv::resource::routes) + 1
                  == srv::resource_count,
              "resource_count must cover every enumerator");
static_assert(srv::budget_limits{}.get(srv::resource::routes)
                  == srv::default_capacity(srv::resource::routes),
              "budget_limits defaults must match default_capacity");

// --- validation helpers -----------------------------------------------------
// LT_CHECK expands harness-local identifiers, so the checks stay in the
// test bodies and these helpers only compute the verdict.

srv::server_options with_listener(std::string address, std::uint16_t port,
                                  bool tls) {
    srv::server_options options;
    srv::listener_options listener;
    listener.address = std::move(address);
    listener.port = port;
    listener.tls = tls;
    options.add_listener(std::move(listener));
    return options;
}

bool validates_ok(const srv::server_options& options) {
    return options.validate().ok();
}

bool fails_with(const srv::server_options& options, outcome_code expected) {
    const http::outcome result = options.validate();
    return result.code() == expected && !result.message().empty();
}

}  // namespace

LT_BEGIN_SUITE(server_options_validate_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(server_options_validate_suite)

// The documented defaults plus one any-address listener are valid.
LT_BEGIN_AUTO_TEST(server_options_validate_suite, defaults_with_one_listener)
    LT_CHECK(validates_ok(with_listener("*", 0, false)));
    LT_CHECK(validates_ok(with_listener("", 0, false)));
    LT_CHECK(validates_ok(with_listener("127.0.0.1", 8080, false)));
LT_END_AUTO_TEST(defaults_with_one_listener)

// V1: at least one listener is required.
LT_BEGIN_AUTO_TEST(server_options_validate_suite, no_listener_fails)
    const srv::server_options options;
    LT_CHECK(fails_with(options, outcome_code::invalid_argument));
LT_END_AUTO_TEST(no_listener_fails)

// V2: listener addresses must be "", "*", or a numeric IP literal.
LT_BEGIN_AUTO_TEST(server_options_validate_suite, listener_address_validated)
    const char* const rejected[] = {
        "localhost", "example.com", "1.2.3", "1.2.3.4.5", "256.1.1.1",
        "01.2.3.4", "1..2.3", "1.2.3.4.", ".1.2.3.4", "1:2:3:4:5:6:7:8:9",
        ":::", "1:::2", "fe80::1%eth0", "gg::1", "1.2.3.4 ", " 1.2.3.4",
        "1.2.3.4:80", "-1", "::ffff:192.0.2.128:80",
    };
    for (const char* address : rejected) {
        LT_CHECK(fails_with(with_listener(address, 8080, false),
                            outcome_code::invalid_argument));
    }
    const char* const accepted[] = {
        "", "*", "0.0.0.0", "127.0.0.1", "192.168.0.12", "255.255.255.255",
        "::", "::1", "2001:db8::1", "1:2:3:4:5:6:7:8",
        "::ffff:192.0.2.128", "2001:0DB8:0000:0000:0000:0000:0000:0001",
    };
    for (const char* address : accepted) {
        LT_CHECK(validates_ok(with_listener(address, 8080, false)));
    }
LT_END_AUTO_TEST(listener_address_validated)

// V3: identical (address, port, tls) listeners collide.
LT_BEGIN_AUTO_TEST(server_options_validate_suite, duplicate_listener_fails)
    srv::server_options options;
    srv::listener_options first;
    first.address = "127.0.0.1";
    first.port = 8080;
    options.add_listener(first);
    LT_CHECK(validates_ok(options));

    options.add_listener(first);
    LT_CHECK(fails_with(options, outcome_code::invalid_argument));

    srv::server_options distinct;
    distinct.add_listener(first);
    srv::listener_options other_port = first;
    other_port.port = 8081;
    distinct.add_listener(other_port);
    srv::listener_options other_tls = first;
    other_tls.tls = true;
    distinct.add_listener(other_tls);
    srv::listener_options other_address = first;
    other_address.address = "127.0.0.2";
    distinct.add_listener(other_address);
    LT_CHECK(validates_ok(distinct));
LT_END_AUTO_TEST(duplicate_listener_fails)

// V4: 0 workers means auto and is valid; only the ceiling is rejected.
LT_BEGIN_AUTO_TEST(server_options_validate_suite, worker_bounds)
    srv::server_options options = with_listener("*", 0, false);
    options.concurrency().workers = srv::max_workers;
    LT_CHECK(validates_ok(options));
    options.concurrency().workers = srv::max_workers + 1;
    LT_CHECK(fails_with(options, outcome_code::invalid_argument));
    options.concurrency().workers = 0;
    LT_CHECK(validates_ok(options));
LT_END_AUTO_TEST(worker_bounds)

// V5: every timeout must be positive and within the documented ceiling.
LT_BEGIN_AUTO_TEST(server_options_validate_suite, timeout_bounds)
    srv::server_options options = with_listener("*", 0, false);
    srv::timeout_options& timeouts = options.timeouts();
    std::chrono::milliseconds* const fields[] = {
        &timeouts.handshake, &timeouts.header, &timeouts.body_idle,
        &timeouts.suspension, &timeouts.write_idle, &timeouts.ws_close,
        &timeouts.drain,
    };
    for (std::chrono::milliseconds* field : fields) {
        *field = std::chrono::milliseconds(0);
        LT_CHECK(fails_with(options, outcome_code::invalid_argument));
        *field = std::chrono::milliseconds(-1);
        LT_CHECK(fails_with(options, outcome_code::invalid_argument));
        *field = srv::max_timeout + std::chrono::milliseconds(1);
        LT_CHECK(fails_with(options, outcome_code::invalid_argument));
        *field = srv::max_timeout;
        LT_CHECK(validates_ok(options));
    }
LT_END_AUTO_TEST(timeout_bounds)

// V6: every budget capacity must be positive and within its ceiling.
LT_BEGIN_AUTO_TEST(server_options_validate_suite, budget_capacity_bounds)
    srv::server_options options = with_listener("*", 0, false);
    srv::budget_limits& limits = options.budgets();
    for (std::size_t i = 0; i < srv::resource_count; ++i) {
        const srv::resource kind = static_cast<srv::resource>(i);
        limits.set(kind, 0);
        LT_CHECK(fails_with(options, outcome_code::invalid_argument));
        limits.set(kind, srv::max_capacity(kind));
        LT_CHECK(validates_ok(options));
        limits.set(kind, srv::max_capacity(kind) + 1);
        LT_CHECK(fails_with(options, outcome_code::invalid_argument));
        limits.set(kind, srv::default_capacity(kind));
    }
    LT_CHECK(validates_ok(options));
LT_END_AUTO_TEST(budget_capacity_bounds)

// V12: listener count is capped.
LT_BEGIN_AUTO_TEST(server_options_validate_suite, listener_cap)
    srv::server_options options;
    for (std::size_t i = 0; i < srv::max_listeners; ++i) {
        srv::listener_options listener;
        listener.address = "127.0.0.1";
        listener.port = static_cast<std::uint16_t>(10000 + i);
        options.add_listener(listener);
    }
    LT_CHECK_EQ(options.listener_count(), srv::max_listeners);
    LT_CHECK(validates_ok(options));

    srv::listener_options extra;
    extra.address = "127.0.0.1";
    extra.port = static_cast<std::uint16_t>(10000 + srv::max_listeners);
    options.add_listener(extra);
    LT_CHECK(fails_with(options, outcome_code::invalid_argument));
LT_END_AUTO_TEST(listener_cap)

// add_listener defers every judgment to validate().
LT_BEGIN_AUTO_TEST(server_options_validate_suite, add_listener_defers)
    srv::server_options options;
    srv::listener_options listener;
    listener.address = "not-an-address";
    options.add_listener(listener);
    LT_CHECK_EQ(options.listener_count(), std::size_t{1});
    LT_CHECK_EQ(options.listener(0).port, std::uint16_t{0});
    LT_CHECK(!options.listener(0).tls);
    LT_CHECK(fails_with(options, outcome_code::invalid_argument));
LT_END_AUTO_TEST(add_listener_defers)

// validate() is pure: repeatable, and callable through a const view.
LT_BEGIN_AUTO_TEST(server_options_validate_suite, validation_is_pure)
    const srv::server_options ok_options = with_listener("*", 0, false);
    const http::outcome first = ok_options.validate();
    const http::outcome second = ok_options.validate();
    LT_CHECK(first.ok());
    LT_CHECK(second.ok());

    srv::server_options bad = ok_options;
    bad.concurrency().workers = srv::max_workers + 1;
    const http::outcome bad_first = bad.validate();
    const http::outcome bad_second = bad.validate();
    LT_CHECK(bad_first.code() == outcome_code::invalid_argument);
    LT_CHECK(bad_second.code() == outcome_code::invalid_argument);
    LT_CHECK(bad_first.message() == bad_second.message());

    const srv::server_options& view = bad;
    LT_CHECK(view.validate().code() == outcome_code::invalid_argument);
LT_END_AUTO_TEST(validation_is_pure)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
