/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// Contract under test:
//
// "r.add_hook(hook_phase::request_received, ...) throws
//  std::invalid_argument."
//
// Validates that http_resource::add_hook performs runtime phase
// validation: each overload binds to a single permitted phase and
// throws std::invalid_argument when the runtime phase tag does not
// match. The five permitted phases are the post-route-resolution ones
// (before_handler, handler_exception, after_handler, response_sent,
// request_completed); the other six phases are rejected as a category.
//
// Compile-time gate: the absence of add_hook overloads for the
// pre-route-resolution phase signatures is enforced naturally by C++
// overload resolution -- a `std::function<hook_action(
// request_received_ctx&)>` argument simply does not match any of the
// five overloads at all. This test exercises the runtime guard for
// callers who construct a matching-signature callable but pass the
// wrong phase tag (e.g. `add_hook(response_sent, before_handler_ctx
// hook)` -- valid signature, mismatched phase).
//
// No webserver, no MHD daemon, no curl -- exercises the validation
// path directly on a stand-alone http_resource subclass.

#include <functional>
#include <stdexcept>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_resource;

namespace {

class trivial_resource : public http_resource {
 public:
    httpserver::http_response render(const httpserver::http_request&) override {
        return httpserver::http_response::string("ok");
    }
};

}  // namespace

LT_BEGIN_SUITE(hooks_per_route_invalid_phase_throws_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_per_route_invalid_phase_throws_suite)

// Each of the six pre-route-resolution phases passed as a runtime tag
// to ANY of the five overloads must throw. We instantiate one of the
// five (before_handler) and feed it every wrong-phase tag in turn.

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   request_received_tag_throws)
    trivial_resource r;
    bool threw = false;
    try {
        r.add_hook(hook_phase::request_received,
            std::function<hook_action(httpserver::before_handler_ctx&)>(
                [](httpserver::before_handler_ctx&) {
                    return hook_action{};
                }));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(request_received_tag_throws)

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   connection_opened_tag_throws)
    trivial_resource r;
    bool threw = false;
    try {
        r.add_hook(hook_phase::connection_opened,
            std::function<hook_action(httpserver::before_handler_ctx&)>(
                [](httpserver::before_handler_ctx&) {
                    return hook_action{};
                }));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(connection_opened_tag_throws)

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   accept_decision_tag_throws)
    trivial_resource r;
    bool threw = false;
    try {
        r.add_hook(hook_phase::accept_decision,
            std::function<void(const httpserver::response_sent_ctx&)>(
                [](const httpserver::response_sent_ctx&) {}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(accept_decision_tag_throws)

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   body_chunk_tag_throws)
    trivial_resource r;
    bool threw = false;
    try {
        r.add_hook(hook_phase::body_chunk,
            std::function<hook_action(httpserver::after_handler_ctx&)>(
                [](httpserver::after_handler_ctx&) {
                    return hook_action{};
                }));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(body_chunk_tag_throws)

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   route_resolved_tag_throws)
    trivial_resource r;
    bool threw = false;
    try {
        r.add_hook(hook_phase::route_resolved,
            std::function<void(const httpserver::request_completed_ctx&)>(
                [](const httpserver::request_completed_ctx&) {}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(route_resolved_tag_throws)

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   connection_closed_tag_throws)
    trivial_resource r;
    bool threw = false;
    try {
        r.add_hook(hook_phase::connection_closed,
            std::function<hook_action(const httpserver::handler_exception_ctx&)>(
                [](const httpserver::handler_exception_ctx&) {
                    return hook_action{};
                }));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(connection_closed_tag_throws)

// Cross-mismatch within the five permitted phases is also a throw:
// passing `response_sent` to the before_handler overload should fail
// because each overload binds to exactly one phase.
LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   cross_mismatch_within_valid_phases_throws)
    trivial_resource r;
    bool threw = false;
    try {
        // before_handler overload, response_sent tag -- mismatch.
        r.add_hook(hook_phase::response_sent,
            std::function<hook_action(httpserver::before_handler_ctx&)>(
                [](httpserver::before_handler_ctx&) {
                    return hook_action{};
                }));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(cross_mismatch_within_valid_phases_throws)

// The five permitted phases must NOT throw on their matching overload.

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   before_handler_does_not_throw)
    trivial_resource r;
    bool threw = false;
    try {
        auto h = r.add_hook(hook_phase::before_handler,
            std::function<hook_action(httpserver::before_handler_ctx&)>(
                [](httpserver::before_handler_ctx&) {
                    return hook_action{};
                }));
        (void)h;
    } catch (...) {
        threw = true;
    }
    LT_CHECK_EQ(threw, false);
LT_END_AUTO_TEST(before_handler_does_not_throw)

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   handler_exception_does_not_throw)
    trivial_resource r;
    bool threw = false;
    try {
        auto h = r.add_hook(hook_phase::handler_exception,
            std::function<hook_action(const httpserver::handler_exception_ctx&)>(
                [](const httpserver::handler_exception_ctx&) {
                    return hook_action{};
                }));
        (void)h;
    } catch (...) {
        threw = true;
    }
    LT_CHECK_EQ(threw, false);
LT_END_AUTO_TEST(handler_exception_does_not_throw)

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   after_handler_does_not_throw)
    trivial_resource r;
    bool threw = false;
    try {
        auto h = r.add_hook(hook_phase::after_handler,
            std::function<hook_action(httpserver::after_handler_ctx&)>(
                [](httpserver::after_handler_ctx&) {
                    return hook_action{};
                }));
        (void)h;
    } catch (...) {
        threw = true;
    }
    LT_CHECK_EQ(threw, false);
LT_END_AUTO_TEST(after_handler_does_not_throw)

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   response_sent_does_not_throw)
    trivial_resource r;
    bool threw = false;
    try {
        auto h = r.add_hook(hook_phase::response_sent,
            std::function<void(const httpserver::response_sent_ctx&)>(
                [](const httpserver::response_sent_ctx&) {}));
        (void)h;
    } catch (...) {
        threw = true;
    }
    LT_CHECK_EQ(threw, false);
LT_END_AUTO_TEST(response_sent_does_not_throw)

LT_BEGIN_AUTO_TEST(hooks_per_route_invalid_phase_throws_suite,
                   request_completed_does_not_throw)
    trivial_resource r;
    bool threw = false;
    try {
        auto h = r.add_hook(hook_phase::request_completed,
            std::function<void(const httpserver::request_completed_ctx&)>(
                [](const httpserver::request_completed_ctx&) {}));
        (void)h;
    } catch (...) {
        threw = true;
    }
    LT_CHECK_EQ(threw, false);
LT_END_AUTO_TEST(request_completed_does_not_throw)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
