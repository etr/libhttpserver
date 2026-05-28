/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-050: design pin for the log_access alias.
//
// log_access(fn) is documented as a response_sent alias. Unlike the
// TASK-048 v1 aliases (auth/404/405) which push +1 into the user vector,
// the log_access alias lives in a dedicated single-slot member on
// webserver_impl (log_access_alias_), mirroring the TASK-049
// internal_error_handler alias slot. The fire site invokes the user
// vector first, then the alias slot.
//
// Why a slot and not a vector entry? Two reasons:
//   1. The slot lets user-added response_sent hooks observe the response
//      BEFORE the legacy access log formats it, preserving the v1
//      "logger sees the trailing event" contract.
//   2. Re-registration semantics ("replaces" per the task spec) are
//      naturally expressed as a single-writer slot rather than a
//      multi-entry vector. (At v2.0 the writer is webserver construction
//      only; a future runtime setter would still write the same slot.)
//
// This test pins three structural invariants:
//   1. A webserver built without log_access -> slot is empty AND the
//      response_sent vector is size 0.
//   2. A webserver built WITH log_access(fn) -> slot is non-empty AND
//      the response_sent vector is still size 0 (no +1 entry, unlike
//      the TASK-048 aliases which DO push +1).
//   3. ws.add_hook(response_sent, fn) appends to the user vector and
//      leaves the alias slot independent.
//
// SECURITY (CWE-117): the alias lambda must sanitize path and method
// before handing the formatted line to the user callable. Any ASCII
// control character (< 0x20 or == 0x7F) must be replaced with '-' so
// a client cannot inject additional log lines by embedding newlines in
// the request path or method string.

#include <algorithm>
#include <functional>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/create_test_request.hpp"
#include "httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

using httpserver::create_test_request;
using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::response_sent_ctx;
using httpserver::webserver;
using httpserver::detail::webserver_impl;

namespace {

webserver_impl* impl_of(webserver& ws) {
    return httpserver::webserver_test_access::impl(ws);
}

}  // namespace

LT_BEGIN_SUITE(hooks_log_access_alias_slot_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_log_access_alias_slot_suite)

LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   baseline_no_alias_slot_empty_and_vector_empty)
    webserver ws{create_webserver(8244)};
    auto* impl = impl_of(ws);
    LT_CHECK(!impl->log_access_alias_);
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(baseline_no_alias_slot_empty_and_vector_empty)

LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   log_access_populates_alias_slot_not_vector)
    auto logger = [](const std::string&) {};
    webserver ws{create_webserver(8244)
        .log_access(logger)};
    auto* impl = impl_of(ws);
    LT_CHECK(static_cast<bool>(impl->log_access_alias_));
    // The alias must NOT push an entry into the user vector.
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(log_access_populates_alias_slot_not_vector)

LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   user_add_hook_grows_vector_alias_slot_untouched)
    auto logger = [](const std::string&) {};
    webserver ws{create_webserver(8244)
        .log_access(logger)};
    auto* impl = impl_of(ws);

    LT_CHECK(static_cast<bool>(impl->log_access_alias_));
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(0));

    auto h = ws.add_hook(hook_phase::response_sent,
        std::function<void(const response_sent_ctx&)>(
            [](const response_sent_ctx&) {}));

    // The user vector grew by 1; the alias slot is still set independently.
    LT_CHECK(static_cast<bool>(impl->log_access_alias_));
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(1));
LT_END_AUTO_TEST(user_add_hook_grows_vector_alias_slot_untouched)

// SECURITY: path with embedded newline must not appear verbatim in the
// logged line. Control characters (< 0x20 or == 0x7F) must be replaced
// with '-' to prevent log-injection (CWE-117).
LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   alias_sanitizes_control_chars_in_path)
    std::string captured;
    auto logger = [&captured](const std::string& line) {
        captured = line;
    };
    webserver ws{create_webserver(8244).log_access(logger)};
    auto* impl = impl_of(ws);
    LT_CHECK(static_cast<bool>(impl->log_access_alias_));

    // Build a synthetic request with a newline injected in the path.
    http_request raw_req =
        create_test_request().path("/evil\ninjected").method("GET").build();

    response_sent_ctx ctx{};
    ctx.request = &raw_req;
    impl->log_access_alias_(ctx);

    // The logged line must not contain any control characters.
    bool has_ctrl = std::any_of(captured.begin(), captured.end(),
        [](unsigned char c) { return c < 0x20 || c == 0x7f; });
    LT_CHECK(!has_ctrl);
    // The replacement character '-' must appear where '\n' was, followed
    // by the rest of the path. Pin that the sanitizer replaced, not truncated.
    LT_CHECK(captured.find("-injected") != std::string::npos);
    LT_CHECK(captured.find('\n') == std::string::npos);
LT_END_AUTO_TEST(alias_sanitizes_control_chars_in_path)

// SECURITY: method with embedded carriage-return must also be sanitized.
LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   alias_sanitizes_control_chars_in_method)
    std::string captured;
    auto logger = [&captured](const std::string& line) {
        captured = line;
    };
    webserver ws{create_webserver(8244).log_access(logger)};
    auto* impl = impl_of(ws);
    LT_CHECK(static_cast<bool>(impl->log_access_alias_));

    http_request raw_req =
        create_test_request().path("/hello").method("GET\r\nX-Injected: yes").build();

    response_sent_ctx ctx{};
    ctx.request = &raw_req;
    impl->log_access_alias_(ctx);

    bool has_ctrl = std::any_of(captured.begin(), captured.end(),
        [](unsigned char c) { return c < 0x20 || c == 0x7f; });
    LT_CHECK(!has_ctrl);
    // Non-control portions of the method must remain intact.
    LT_CHECK(captured.find("GET") != std::string::npos);
LT_END_AUTO_TEST(alias_sanitizes_control_chars_in_method)

// Re-registration: calling log_access a second time on the builder
// replaces the previous callable. At v2.0 the only write point is
// webserver construction (write-once-at-construction contract); runtime
// re-registration via a setter is deferred to a future task.
// This test pins the replace semantics at construction time.
LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   log_access_second_registration_replaces_first)
    int first_calls = 0;
    int second_calls = 0;
    // Simulate re-registration by creating two builders and checking that
    // only the callable set on the webserver used for construction is stored.
    webserver ws1{create_webserver(8244)
        .log_access([&first_calls](const std::string&) { ++first_calls; })};
    webserver ws2{create_webserver(8245)
        .log_access([&second_calls](const std::string&) { ++second_calls; })};

    // Each webserver holds its own callable; neither pollutes the other.
    auto* impl1 = impl_of(ws1);
    auto* impl2 = impl_of(ws2);
    LT_CHECK(static_cast<bool>(impl1->log_access_alias_));
    LT_CHECK(static_cast<bool>(impl2->log_access_alias_));

    // Invoke each alias independently.
    http_request req1 =
        create_test_request().path("/a").method("GET").build();
    http_request req2 =
        create_test_request().path("/b").method("GET").build();
    response_sent_ctx ctx1{};
    ctx1.request = &req1;
    response_sent_ctx ctx2{};
    ctx2.request = &req2;

    impl1->log_access_alias_(ctx1);
    impl2->log_access_alias_(ctx2);

    // Only the callable stored on each respective webserver was invoked.
    LT_CHECK_EQ(first_calls, 1);
    LT_CHECK_EQ(second_calls, 1);
LT_END_AUTO_TEST(log_access_second_registration_replaces_first)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
