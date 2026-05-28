/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-051 acceptance criterion 4:
//
// "hooks_per_route_resource_destroyed_first: a hook_handle whose
//  resource was destroyed has remove() as a no-op (no crash, no UAF --
//  verified under ASan)."
//
// Exercises the weak_ptr-expiry path in hook_handle::remove(): we
// drop every shared_ptr<http_resource> reference, then call
// handle.remove() / let the handle's destructor fire. The handle
// must NOT dereference freed memory.
//
// No webserver, no MHD daemon, no curl -- the test runs purely on a
// stand-alone shared_ptr<http_resource>, which is also what ASan can
// reliably observe (an MHD-driven path would trip a 1000 unrelated
// allocator events between the registration and the destruction).

#include <functional>
#include <memory>
#include <utility>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::hook_action;
using httpserver::hook_handle;
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

LT_BEGIN_SUITE(hooks_per_route_resource_destroyed_first_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_per_route_resource_destroyed_first_suite)

// Path 1: explicit remove() after the resource is gone -- must no-op,
// must not crash, must not UAF (ASan).
LT_BEGIN_AUTO_TEST(hooks_per_route_resource_destroyed_first_suite,
                   explicit_remove_after_destroy_is_noop)
    hook_handle h;
    {
        auto r = std::make_shared<trivial_resource>();
        h = r->add_hook(hook_phase::response_sent,
            std::function<void(const httpserver::response_sent_ctx&)>(
                [](const httpserver::response_sent_ctx&) {}));
        // r drops out of scope -> shared_ptr<resource_hook_table>
        // inside r is destroyed -> handle's weak_ptr expires.
    }
    // The resource is gone. remove() must be a clean no-op.
    h.remove();
    // A second remove() (idempotent path) must also be a no-op.
    h.remove();
    // Reaching this point means no crash / no UAF (validated under ASan).
LT_END_AUTO_TEST(explicit_remove_after_destroy_is_noop)

// Path 2: RAII destruction of the handle after the resource is gone.
// The destructor must not crash or UAF.
LT_BEGIN_AUTO_TEST(hooks_per_route_resource_destroyed_first_suite,
                   raii_destruction_after_destroy_is_noop)
    {
        hook_handle h;
        {
            auto r = std::make_shared<trivial_resource>();
            h = r->add_hook(hook_phase::request_completed,
                std::function<void(const httpserver::request_completed_ctx&)>(
                    [](const httpserver::request_completed_ctx&) {}));
            // r drops out of scope here.
        }
        // ~hook_handle() fires at the close of THIS scope, after the
        // resource is destroyed. Must not crash / UAF.
    }
    // Reaching this point means no crash / no UAF (validated under ASan).
LT_END_AUTO_TEST(raii_destruction_after_destroy_is_noop)

// Path 3: handle moved THEN the source resource destroyed.
LT_BEGIN_AUTO_TEST(hooks_per_route_resource_destroyed_first_suite,
                   move_then_destroy_then_remove_is_noop)
    hook_handle h2;
    {
        auto r = std::make_shared<trivial_resource>();
        auto h1 = r->add_hook(hook_phase::after_handler,
            std::function<hook_action(httpserver::after_handler_ctx&)>(
                [](httpserver::after_handler_ctx&) { return hook_action{}; }));
        h2 = std::move(h1);
        // h1 is disarmed.
    }
    h2.remove();
    // Reaching this point means no crash / no UAF (validated under ASan).
LT_END_AUTO_TEST(move_then_destroy_then_remove_is_noop)

// Path 4: registration on a resource that is then DROPPED inside a
// scope -- assertion: no crash. The handle outlives the resource.
LT_BEGIN_AUTO_TEST(hooks_per_route_resource_destroyed_first_suite,
                   handle_outlives_resource)
    hook_handle h;
    {
        auto r = std::make_shared<trivial_resource>();
        h = r->add_hook(hook_phase::before_handler,
            std::function<hook_action(httpserver::before_handler_ctx&)>(
                [](httpserver::before_handler_ctx&) { return hook_action{}; }));
        // No remove() call here; r drops and the resource's hook_table
        // ref drops with it. handle now points at a defunct table via
        // its weak_ptr.
    }
    // Reaching this line means the resource was destroyed cleanly even
    // though the handle is still armed. ~hook_handle() will fire at end
    // of this test and must safely no-op (validated under ASan).
LT_END_AUTO_TEST(handle_outlives_resource)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
