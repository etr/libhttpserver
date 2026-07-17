/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// Design pin for the log_access alias.
//
// log_access(fn) is documented as a response_sent alias. Unlike the
// v1 aliases (auth/404/405) which push +1 into the user vector,
// the log_access alias lives in a dedicated single-slot member on
// webserver_impl (log_access_alias_), mirroring the
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
//      the v1 aliases which DO push +1).
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
#include <string_view>

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

// NOTE: a former user_add_hook_grows_vector_alias_slot_untouched test
// duplicated steps (a) and (b) of
// log_access_alias_is_immutable_after_construction below; that test is
// now the single owner of the full add/remove/invoke alias contract.

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

// Alias slots are construction-time-only; add_hook() and
// remove() must not reseat them (see hook_handle.hpp).
LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   log_access_alias_is_immutable_after_construction)
    int direct_calls = 0;
    int user_hook_calls = 0;
    webserver ws{create_webserver(8244)
        .log_access([&direct_calls](const std::string&) { ++direct_calls; })};
    auto* impl = impl_of(ws);

    // (a) Construction wires the slot exactly once.
    LT_CHECK(static_cast<bool>(impl->log_access_alias_));
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(0));

    // (b) Adding a user response_sent hook grows the vector but leaves
    // the alias slot untouched -- the public way to add observation at
    // response_sent post-construction is add_hook(), which routes into
    // the user vector and never reseats the alias slot.
    auto h = ws.add_hook(hook_phase::response_sent,
        std::function<void(const response_sent_ctx&)>(
            [&user_hook_calls](const response_sent_ctx&) {
                ++user_hook_calls;
            }));
    LT_CHECK(static_cast<bool>(impl->log_access_alias_));
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(1));

    // (c) Removing the user hook leaves the alias slot untouched -- the
    // slot is not under user control via the hook bus.
    h.remove();
    LT_CHECK(static_cast<bool>(impl->log_access_alias_));
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(0));

    // (d) Direct invocation still reaches the construction-time callable.
    http_request req =
        create_test_request().path("/a").method("GET").build();
    response_sent_ctx ctx{};
    ctx.request = &req;
    impl->log_access_alias_(ctx);
    LT_CHECK_EQ(direct_calls, 1);
    LT_CHECK_EQ(user_hook_calls, 0);
LT_END_AUTO_TEST(log_access_alias_is_immutable_after_construction)

LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   handler_exception_alias_is_immutable_after_construction)
    // Mirror of the log_access pin above for handler_exception_alias_.
    // Construction wires the slot; user add_hook() at handler_exception
    // grows hooks_handler_exception_ but does not displace or clear the
    // alias slot. Removing the user hook leaves the alias slot intact.
    int handler_calls = 0;
    webserver ws{create_webserver(8244)
        .internal_error_handler(
            [&handler_calls](const httpserver::http_request&, std::string_view)
                -> httpserver::http_response {
                ++handler_calls;
                return httpserver::http_response::string("oops");
            })};
    auto* impl = impl_of(ws);

    // (a) Construction wires the alias slot exactly once.
    LT_CHECK(static_cast<bool>(impl->handler_exception_alias_));
    LT_CHECK_EQ(impl->hooks_handler_exception_.size(),
                static_cast<std::size_t>(0));

    // (b) User add_hook(handler_exception, ...) grows the vector; alias
    // slot remains set independently.
    auto h = ws.add_hook(hook_phase::handler_exception,
        std::function<httpserver::hook_action(
                const httpserver::handler_exception_ctx&)>(
            [](const httpserver::handler_exception_ctx&) {
                return httpserver::hook_action::pass();
            }));
    LT_CHECK(static_cast<bool>(impl->handler_exception_alias_));
    LT_CHECK_EQ(impl->hooks_handler_exception_.size(),
                static_cast<std::size_t>(1));

    // (c) Removing the user hook does not clear the alias slot -- the
    // slot is not under user control via the hook bus.
    h.remove();
    LT_CHECK(static_cast<bool>(impl->handler_exception_alias_));
    LT_CHECK_EQ(impl->hooks_handler_exception_.size(),
                static_cast<std::size_t>(0));

    // (d) Direct invocation still reaches the construction-time callable.
    http_request req =
        create_test_request().path("/a").method("GET").build();
    httpserver::handler_exception_ctx ctx{};
    ctx.request = &req;
    impl->handler_exception_alias_(ctx);
    LT_CHECK_EQ(handler_calls, 1);
LT_END_AUTO_TEST(handler_exception_alias_is_immutable_after_construction)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
