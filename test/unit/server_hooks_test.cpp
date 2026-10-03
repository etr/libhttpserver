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

// TASK-118 step 2: the v3 lifecycle hook vocabulary and bus (plan D4).
// Seven request-scoped phases with the v2 names kept where the native
// point exists; four v2 phases (connection_opened, connection_closed,
// accept_decision, body_chunk) are migration-noted away. The bus keeps
// the v2 contracts: registration order within a phase, snapshot-copy
// firing (a hook may add or remove hooks mid-fire), short-circuit on
// the first respond_with, zero-cost-when-unused per-phase atomics, and
// runtime-safe add/remove.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/http/request_head.hpp>
#include <httpserver/server/hooks.hpp>

#include "./littletest.hpp"

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;

using srv::hook_action;
using srv::hook_phase;

// --- shape pins (DR-V3-001: semantic-exchange-only contexts) ---------------

static_assert(static_cast<std::size_t>(hook_phase::count_) == std::size_t{7},
              "seven request-scoped phases plus the sentinel");

static_assert(std::is_same_v<decltype(
                  std::declval<const srv::request_received_ctx&>().request),
              const http::request_head&>,
              "request_received observes the semantic head");

static_assert(std::is_same_v<decltype(
                  std::declval<const srv::route_resolved_ctx&>().matched),
              bool>,
              "route_resolved reports hit and miss alike");

static_assert(std::is_same_v<decltype(
                  std::declval<const srv::route_resolved_ctx&>().route.pattern),
              std::string_view>,
              "the route descriptor views the registered pattern text");
static_assert(std::is_same_v<decltype(
                  std::declval<const srv::route_resolved_ctx&>().route.methods),
              http::method_set>,
              "the route descriptor carries the Allow inputs");
static_assert(std::is_same_v<
                  decltype(std::declval<const srv::route_resolved_ctx&>()
                               .route.is_prefix),
              bool>,
              "the route descriptor names the prefix family");

static_assert(std::is_same_v<
                  decltype(std::declval<srv::after_handler_ctx&>().status),
              http::status>,
              "after_handler mutates the status");
static_assert(std::is_same_v<
                  decltype(std::declval<srv::after_handler_ctx&>().fields),
              http::fields>,
              "after_handler mutates the fields");

static_assert(std::is_same_v<
                  decltype(std::declval<const srv::request_completed_ctx&>()
                               .succeeded),
              bool>,
              "request_completed reports success");
static_assert(std::is_same_v<
                  decltype(std::declval<const srv::request_completed_ctx&>()
                               .end.code()),
              http::outcome_code>,
              "request_completed carries a typed end reason");

static_assert(!std::is_copy_constructible_v<srv::hook_bus>,
              "hook_bus is move-only (pimpl owns the storage)");
static_assert(std::is_move_constructible_v<srv::hook_bus>,
              "hook_bus is movable");
static_assert(!std::is_copy_constructible_v<srv::hook_handle>,
              "hook_handle is move-only");
static_assert(!std::is_copy_constructible_v<hook_action>,
              "hook_action is move-only");

// --- helpers -------------------------------------------------------------------
// LT_CHECK expands harness-local identifiers, so the checks stay in the
// test bodies and these helpers only compute values.

// A shared order log every hook appends its tag to.
using visit_log = std::shared_ptr<std::vector<std::string>>;

http::request_head sample_head() {
    http::request_head head;
    head.request_method = http::method::known(http::method_id::get);
    head.route_path = "/x";
    return head;
}

// Fires one phase with the matching ctx shape.
hook_action fire_received(srv::hook_bus& bus, const http::request_head& head) {
    srv::request_received_ctx ctx{head};
    return bus.fire<hook_phase::request_received>(ctx);
}

hook_action fire_resolved(srv::hook_bus& bus, const http::request_head& head) {
    srv::route_resolved_ctx ctx{head, true, srv::route_descriptor{}};
    return bus.fire<hook_phase::route_resolved>(ctx);
}

hook_action fire_before(srv::hook_bus& bus, const http::request_head& head) {
    srv::before_handler_ctx ctx{head, srv::route_descriptor{}};
    return bus.fire<hook_phase::before_handler>(ctx);
}

hook_action fire_exception(srv::hook_bus& bus, const http::request_head& head) {
    srv::handler_exception_ctx ctx{head, std::current_exception()};
    return bus.fire<hook_phase::handler_exception>(ctx);
}

hook_action fire_after(srv::hook_bus& bus, const http::request_head& head) {
    srv::after_handler_ctx ctx{head, http::status::from_code(200),
                               http::fields()};
    return bus.fire<hook_phase::after_handler>(ctx);
}

hook_action fire_sent(srv::hook_bus& bus, const http::request_head& head) {
    srv::response_sent_ctx ctx{head, 200};
    return bus.fire<hook_phase::response_sent>(ctx);
}

hook_action fire_completed(srv::hook_bus& bus, const http::request_head& head) {
    srv::request_completed_ctx ctx{head, true, http::outcome::okay()};
    return bus.fire<hook_phase::request_completed>(ctx);
}

std::string joined(const std::vector<std::string>& visits) {
    std::string out;
    for (const std::string& visit : visits) {
        if (!out.empty()) out.append(",");
        out.append(visit);
    }
    return out;
}

hook_action respond(std::uint16_t code, const char* bytes) {
    srv::hook_response response;
    response.status = http::status::from_code(code);
    const std::byte* raw = reinterpret_cast<const std::byte*>(bytes);
    response.body.assign(raw,
                         raw + std::char_traits<char>::length(bytes));
    return hook_action::respond_with(std::move(response));
}

}  // namespace

LT_BEGIN_SUITE(server_hooks_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(server_hooks_suite)

// pass() and respond_with() shape the action: is_pass, carried status,
// and the consumed-once take.
LT_BEGIN_AUTO_TEST(server_hooks_suite, action_shape)
    const hook_action pass = hook_action::pass();
    LT_CHECK(pass.is_pass());

    hook_action replied = respond(403, "no");
    LT_CHECK(!replied.is_pass());
    const srv::hook_response taken = std::move(replied).take_response();
    LT_CHECK_EQ(taken.status.code(), std::uint16_t{403});
    LT_CHECK_EQ(taken.body.size(), std::size_t{2});
    LT_CHECK(taken.fields.empty());
LT_END_AUTO_TEST(action_shape)

// Hooks of one phase fire in registration order, once per fire call;
// only the fired phase's hooks run.
LT_BEGIN_AUTO_TEST(server_hooks_suite, registration_order_once_per_phase)
    srv::hook_bus bus;
    const visit_log visits = std::make_shared<std::vector<std::string>>();
    srv::hook_handle a = bus.add<hook_phase::before_handler>(
        [visits](srv::before_handler_ctx&) -> hook_action {
            visits->push_back("a");
            return hook_action::pass();
        });
    srv::hook_handle b = bus.add<hook_phase::before_handler>(
        [visits](srv::before_handler_ctx&) -> hook_action {
            visits->push_back("b");
            return hook_action::pass();
        });
    srv::hook_handle other = bus.add<hook_phase::response_sent>(
        [visits](srv::response_sent_ctx&) -> hook_action {
            visits->push_back("sent");
            return hook_action::pass();
        });
    (void)other;

    const http::request_head head = sample_head();
    const hook_action outcome = fire_before(bus, head);
    LT_CHECK(outcome.is_pass());
    LT_CHECK(joined(*visits) == "a,b");
    fire_before(bus, head);
    LT_CHECK(joined(*visits) == "a,b,a,b");
    LT_CHECK(bus.any_hooks(hook_phase::before_handler));
    LT_CHECK(bus.any_hooks(hook_phase::response_sent));
    LT_CHECK(!bus.any_hooks(hook_phase::after_handler));
    LT_CHECK(a.armed() && b.armed());
LT_END_AUTO_TEST(registration_order_once_per_phase)

// A respond_with short-circuits: later hooks of the phase do not run
// and fire returns the action.
LT_BEGIN_AUTO_TEST(server_hooks_suite, short_circuit_skips_remaining)
    srv::hook_bus bus;
    const visit_log visits = std::make_shared<std::vector<std::string>>();
    srv::hook_handle gate = bus.add<hook_phase::request_received>(
        [visits](srv::request_received_ctx&) -> hook_action {
            visits->push_back("gate");
            return respond(413, "too large");
        });
    srv::hook_handle after = bus.add<hook_phase::request_received>(
        [visits](srv::request_received_ctx&) -> hook_action {
            visits->push_back("skipped");
            return hook_action::pass();
        });
    (void)gate;

    hook_action outcome = fire_received(bus, sample_head());
    LT_CHECK(!outcome.is_pass());
    const srv::hook_response taken = std::move(outcome).take_response();
    LT_CHECK_EQ(taken.status.code(), std::uint16_t{413});
    LT_CHECK(joined(*visits) == "gate");
    LT_CHECK(after.armed());
LT_END_AUTO_TEST(short_circuit_skips_remaining)

// remove() and the handle destructor erase the registration; detach()
// makes it permanent.
LT_BEGIN_AUTO_TEST(server_hooks_suite, handle_lifetime)
    srv::hook_bus bus;
    const visit_log visits = std::make_shared<std::vector<std::string>>();
    srv::hook_handle removed = bus.add<hook_phase::after_handler>(
        [visits](srv::after_handler_ctx&) -> hook_action {
            visits->push_back("removed");
            return hook_action::pass();
        });
    srv::hook_handle detached = bus.add<hook_phase::after_handler>(
        [visits](srv::after_handler_ctx&) -> hook_action {
            visits->push_back("detached");
            return hook_action::pass();
        });

    fire_after(bus, sample_head());
    LT_CHECK(joined(*visits) == "removed,detached");

    removed.remove();
    LT_CHECK(!removed.armed());
    // Idempotent.
    removed.remove();
    fire_after(bus, sample_head());
    LT_CHECK(joined(*visits) == "removed,detached,detached");

    detached.detach();
    LT_CHECK(!detached.armed());
    {
        srv::hook_handle scoped = bus.add<hook_phase::after_handler>(
            [visits](srv::after_handler_ctx&) -> hook_action {
                visits->push_back("scoped");
                return hook_action::pass();
            });
        // Destructor erases the registration.
    }
    fire_after(bus, sample_head());
    LT_CHECK(joined(*visits)
             == "removed,detached,detached,detached");
LT_END_AUTO_TEST(handle_lifetime)

// any_hooks() tracks emptiness per phase.
LT_BEGIN_AUTO_TEST(server_hooks_suite, any_hooks_transitions)
    srv::hook_bus bus;
    LT_CHECK(!bus.any_hooks(hook_phase::response_sent));
    srv::hook_handle one = bus.add<hook_phase::response_sent>(
        [](srv::response_sent_ctx&) -> hook_action {
            return hook_action::pass();
        });
    LT_CHECK(bus.any_hooks(hook_phase::response_sent));
    srv::hook_handle two = bus.add<hook_phase::response_sent>(
        [](srv::response_sent_ctx&) -> hook_action {
            return hook_action::pass();
        });
    one.remove();
    LT_CHECK(bus.any_hooks(hook_phase::response_sent));
    two.remove();
    LT_CHECK(!bus.any_hooks(hook_phase::response_sent));
LT_END_AUTO_TEST(any_hooks_transitions)

// Firing runs over a snapshot: a hook that registers another hook
// mid-fire does not run it in the same fire, only on the next one.
LT_BEGIN_AUTO_TEST(server_hooks_suite, snapshot_during_firing)
    srv::hook_bus bus;
    const visit_log visits = std::make_shared<std::vector<std::string>>();
    bool seeded_late = false;
    srv::hook_handle seeded;
    seeded = bus.add<hook_phase::route_resolved>(
        [&bus, visits, &seeded_late](srv::route_resolved_ctx&) -> hook_action {
            visits->push_back("seed");
            if (!seeded_late) {
                seeded_late = true;
                bus.add<hook_phase::route_resolved>(
                    [visits](srv::route_resolved_ctx&) -> hook_action {
                        visits->push_back("late");
                        return hook_action::pass();
                    }).detach();
            }
            return hook_action::pass();
        });

    fire_resolved(bus, sample_head());
    LT_CHECK(joined(*visits) == "seed");
    fire_resolved(bus, sample_head());
    LT_CHECK(joined(*visits) == "seed,seed,late");
    seeded.remove();
    fire_resolved(bus, sample_head());
    LT_CHECK(joined(*visits) == "seed,seed,late,late");
LT_END_AUTO_TEST(snapshot_during_firing)

// Firing a phase with no hooks is a pass without allocation.
LT_BEGIN_AUTO_TEST(server_hooks_suite, empty_fire_is_pass)
    srv::hook_bus bus;
    for (const hook_phase phase : {
             hook_phase::request_received, hook_phase::route_resolved,
             hook_phase::before_handler, hook_phase::handler_exception,
             hook_phase::after_handler, hook_phase::response_sent,
             hook_phase::request_completed,
         }) {
        LT_CHECK(!bus.any_hooks(phase));
    }
    LT_CHECK(fire_received(bus, sample_head()).is_pass());
    LT_CHECK(fire_resolved(bus, sample_head()).is_pass());
    LT_CHECK(fire_before(bus, sample_head()).is_pass());
    LT_CHECK(fire_exception(bus, sample_head()).is_pass());
    LT_CHECK(fire_after(bus, sample_head()).is_pass());
    LT_CHECK(fire_sent(bus, sample_head()).is_pass());
    LT_CHECK(fire_completed(bus, sample_head()).is_pass());
LT_END_AUTO_TEST(empty_fire_is_pass)

// The bus is movable; registrations survive the move.
LT_BEGIN_AUTO_TEST(server_hooks_suite, bus_moves_with_registrations)
    const visit_log visits = std::make_shared<std::vector<std::string>>();
    srv::hook_bus moved;
    srv::hook_handle kept;
    {
        srv::hook_bus bus;
        kept = bus.add<hook_phase::response_sent>(
            [visits](srv::response_sent_ctx&) -> hook_action {
                visits->push_back("sent");
                return hook_action::pass();
            });
        moved = std::move(bus);
    }
    fire_sent(moved, sample_head());
    LT_CHECK(joined(*visits) == "sent");
    LT_CHECK(moved.any_hooks(hook_phase::response_sent));
LT_END_AUTO_TEST(bus_moves_with_registrations)

// add() and fire() race safely: concurrent registration while a fire
// is in flight neither corrupts the bus nor joins the running snapshot.
LT_BEGIN_AUTO_TEST(server_hooks_suite, concurrent_add_while_firing)
    srv::hook_bus bus;
    const visit_log visits = std::make_shared<std::vector<std::string>>();
    srv::hook_handle core = bus.add<hook_phase::request_completed>(
        [visits](srv::request_completed_ctx&) -> hook_action {
            visits->push_back("core");
            return hook_action::pass();
        });
    (void)core;
    std::thread racer([&bus] {
        for (int i = 0; i < 64; ++i) {
            bus.add<hook_phase::request_completed>(
                [](srv::request_completed_ctx&) -> hook_action {
                    return hook_action::pass();
                }).detach();
        }
    });
    for (int i = 0; i < 64; ++i) {
        fire_completed(bus, sample_head());
    }
    racer.join();
    LT_CHECK(visits->size() == std::size_t{64});
LT_END_AUTO_TEST(concurrent_add_while_firing)

// A handle outliving its bus is a silent no-op (weak ownership).
LT_BEGIN_AUTO_TEST(server_hooks_suite, handle_outlives_bus)
    srv::hook_handle stranded;
    {
        srv::hook_bus bus;
        stranded = bus.add<hook_phase::before_handler>(
            [](srv::before_handler_ctx&) -> hook_action {
                return hook_action::pass();
            });
    }
    stranded.remove();  // must not crash
    LT_CHECK(!stranded.armed());
LT_END_AUTO_TEST(handle_outlives_bus)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
