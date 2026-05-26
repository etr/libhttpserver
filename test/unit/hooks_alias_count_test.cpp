/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-048 acceptance criterion 4.
//
// "A test verifies the alias is observably an alias: registering
//  auth_handler(fn) then querying the before_handler hook count
//  reports +1."
//
// Implementation: each of the three v1 error/auth setters is, when
// non-null, an alias for an internally-registered hook at the matching
// phase. The internal registration is observable through the
// HTTPSERVER_COMPILATION-gated webserver_test_access friend bridge.
//
//   - auth_handler(fn)              ->  +1 on hook_phase::before_handler
//   - method_not_allowed_handler(h) ->  +1 on hook_phase::before_handler
//   - not_found_handler(h)          ->  +1 on hook_phase::route_resolved
//
// Aliases are only installed when the user-supplied callable is non-null.
// A webserver constructed without any of these aliases set has zero
// hooks at both phases.

#include <functional>
#include <memory>

#include "./httpserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;
using httpserver::detail::webserver_impl;

namespace {

std::size_t before_handler_count(webserver& ws) {
    auto* impl = httpserver::webserver_test_access::impl(ws);
    return impl->hooks_before_handler_.size();
}

std::size_t route_resolved_count(webserver& ws) {
    auto* impl = httpserver::webserver_test_access::impl(ws);
    return impl->hooks_route_resolved_.size();
}

}  // namespace

LT_BEGIN_SUITE(hooks_alias_count_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_alias_count_suite)

LT_BEGIN_AUTO_TEST(hooks_alias_count_suite, baseline_has_no_alias_hooks)
    webserver ws{create_webserver(8205)};
    LT_CHECK_EQ(before_handler_count(ws), static_cast<std::size_t>(0));
    LT_CHECK_EQ(route_resolved_count(ws), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(baseline_has_no_alias_hooks)

LT_BEGIN_AUTO_TEST(hooks_alias_count_suite, auth_handler_registers_one_before_handler)
    webserver ws{create_webserver(8205)
        .auth_handler([](const http_request&)
                          -> std::shared_ptr<http_response> {
            return nullptr;
        })};
    // Alias installed: +1 on before_handler.
    LT_CHECK_EQ(before_handler_count(ws), static_cast<std::size_t>(1));
    LT_CHECK_EQ(route_resolved_count(ws), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(auth_handler_registers_one_before_handler)

LT_BEGIN_AUTO_TEST(hooks_alias_count_suite,
                   method_not_allowed_handler_registers_one_before_handler)
    webserver ws{create_webserver(8205)
        .method_not_allowed_handler([](const http_request&) {
            return http_response::string("405");
        })};
    LT_CHECK_EQ(before_handler_count(ws), static_cast<std::size_t>(1));
    LT_CHECK_EQ(route_resolved_count(ws), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(method_not_allowed_handler_registers_one_before_handler)

LT_BEGIN_AUTO_TEST(hooks_alias_count_suite,
                   not_found_handler_registers_one_route_resolved)
    webserver ws{create_webserver(8205)
        .not_found_handler([](const http_request&) {
            return http_response::string("404");
        })};
    LT_CHECK_EQ(before_handler_count(ws), static_cast<std::size_t>(0));
    LT_CHECK_EQ(route_resolved_count(ws), static_cast<std::size_t>(1));
LT_END_AUTO_TEST(not_found_handler_registers_one_route_resolved)

LT_BEGIN_AUTO_TEST(hooks_alias_count_suite, all_three_aliases_stack)
    webserver ws{create_webserver(8205)
        .auth_handler([](const http_request&)
                          -> std::shared_ptr<http_response> {
            return nullptr;
        })
        .method_not_allowed_handler([](const http_request&) {
            return http_response::string("405");
        })
        .not_found_handler([](const http_request&) {
            return http_response::string("404");
        })};
    LT_CHECK_EQ(before_handler_count(ws), static_cast<std::size_t>(2));
    LT_CHECK_EQ(route_resolved_count(ws), static_cast<std::size_t>(1));
LT_END_AUTO_TEST(all_three_aliases_stack)

LT_BEGIN_AUTO_TEST(hooks_alias_count_suite, user_add_hook_stacks_on_top_of_alias)
    webserver ws{create_webserver(8205)
        .auth_handler([](const http_request&)
                          -> std::shared_ptr<http_response> {
            return nullptr;
        })};
    // Alias installed: +1.
    LT_CHECK_EQ(before_handler_count(ws), static_cast<std::size_t>(1));

    auto h = ws.add_hook(hook_phase::before_handler,
        std::function<httpserver::hook_action(
            httpserver::before_handler_ctx&)>(
            [](httpserver::before_handler_ctx&) {
                return httpserver::hook_action{};
            }));
    // User hook stacks: count is now 2.
    LT_CHECK_EQ(before_handler_count(ws), static_cast<std::size_t>(2));
LT_END_AUTO_TEST(user_add_hook_stacks_on_top_of_alias)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
