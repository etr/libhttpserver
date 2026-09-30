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

// TASK-101 Step 4: budget-bounded route registration with validated
// patterns (PRD-V3N-REQ-009). A registration is visible to every
// enabled HTTP version by construction: neither the registration API
// nor a stored entry carries a protocol dimension. Matching over
// requests arrives with the request-handling tasks; this suite pins
// the grammar, the typed admission failures, and the hierarchical
// routes budget.

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <httpserver/server/configuration.hpp>

#include "./littletest.hpp"

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;

using outcome_code = httpserver::http::outcome_code;

// --- layout pins -------------------------------------------------------------

static_assert(std::is_default_constructible_v<srv::route_pattern>,
              "route_pattern must be default constructible (parse out-param)");
static_assert(std::is_move_constructible_v<srv::route_registry>,
              "route_registry must be move constructible");
static_assert(!std::is_copy_constructible_v<srv::route_handler>,
              "route handlers are move-only (coroutine ABI)");
static_assert(std::is_invocable_r_v<httpserver::task<void>,
                                    srv::route_handler&,
                                    httpserver::exchange&>,
              "route_handler must be invocable as task<void>(exchange&)");

// --- helpers -------------------------------------------------------------------
// LT_CHECK expands harness-local identifiers, so the checks stay in the
// test bodies and these helpers only compute the verdict.

srv::route_handler make_handler() {
    return srv::route_handler(
        [](httpserver::exchange&) -> httpserver::task<void> { co_return; });
}

srv::resource_budget budget_with_routes(std::size_t capacity) {
    srv::budget_limits limits;
    limits.set(srv::resource::routes, capacity);
    return srv::resource_budget::root(limits);
}

bool creates(const srv::resource_budget& budget, srv::route_registry& out) {
    return srv::route_registry::create(budget, out).ok();
}

bool create_fails_with(const srv::resource_budget& budget,
                       outcome_code expected) {
    srv::route_registry out;
    const http::outcome result = srv::route_registry::create(budget, out);
    return result.code() == expected && !result.message().empty();
}

bool registers(srv::route_registry& registry, const http::method& m,
               std::string_view pattern) {
    const http::outcome result = registry.route(m, pattern, make_handler());
    return result.ok();
}

bool route_fails_with(srv::route_registry& registry, const http::method& m,
                      std::string_view pattern, outcome_code expected) {
    const http::outcome result = registry.route(m, pattern, make_handler());
    return result.code() == expected && !result.message().empty();
}

bool parses(std::string_view text, srv::route_pattern& out) {
    return srv::route_pattern::parse(text, out).ok();
}

bool rejects(std::string_view text) {
    srv::route_pattern out;
    const http::outcome result = srv::route_pattern::parse(text, out);
    // The out parameter is untouched on failure.
    return result.code() == outcome_code::invalid_argument
        && !result.message().empty() && out.text().empty()
        && out.segment_count() == 0 && out.parameter_count() == 0;
}

const http::method kGet = http::method::known(http::method_id::get);
const http::method kPost = http::method::known(http::method_id::post);

}  // namespace

LT_BEGIN_SUITE(server_routes_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(server_routes_suite)

// The pattern grammar accepts the root, literal paths, and parameter
// segments with [A-Za-z0-9_]+ names.
LT_BEGIN_AUTO_TEST(server_routes_suite, pattern_grammar_accepts)
    const char* const accepted[] = {
        "/", "/users", "/users/42", "/a/b/c", "/users/{id}", "/a/{b}/c",
        "/a/{b}/{c}", "/files/{name_1}", "/a.b~c", "/v1.0/status",
        "/a#b", "/{9x}",
    };
    for (const char* text : accepted) {
        srv::route_pattern pattern;
        LT_CHECK(parses(text, pattern));
    }
LT_END_AUTO_TEST(pattern_grammar_accepts)

// The pattern grammar rejects malformed patterns and leaves the out
// parameter untouched.
LT_BEGIN_AUTO_TEST(server_routes_suite, pattern_grammar_rejects)
    const char* const rejected[] = {
        "", "users", "users/", "/users/", "//", "/a//b", "/a/{}",
        "/a/{b}{c}", "/{a/b}", "/a/{b}/", "/a/{bad-name}", "/a/{b}/{b}",
        "/a b", "/a/{b}c", "/caf\xc3\xa9", "/a?b", "/a[b]", "/a,b",
    };
    for (const char* text : rejected) {
        LT_CHECK(rejects(text));
    }
LT_END_AUTO_TEST(pattern_grammar_rejects)

// Segment and parameter counts describe the canonical text.
LT_BEGIN_AUTO_TEST(server_routes_suite, pattern_counts)
    srv::route_pattern root;
    LT_CHECK(parses("/", root));
    LT_CHECK(root.text() == "/");
    LT_CHECK_EQ(root.segment_count(), std::size_t{0});
    LT_CHECK_EQ(root.parameter_count(), std::size_t{0});

    srv::route_pattern literal;
    LT_CHECK(parses("/users/42", literal));
    LT_CHECK(literal.text() == "/users/42");
    LT_CHECK_EQ(literal.segment_count(), std::size_t{2});
    LT_CHECK_EQ(literal.parameter_count(), std::size_t{0});

    srv::route_pattern parameterized;
    LT_CHECK(parses("/users/{id}/posts/{post_id}", parameterized));
    LT_CHECK_EQ(parameterized.segment_count(), std::size_t{4});
    LT_CHECK_EQ(parameterized.parameter_count(), std::size_t{2});
LT_END_AUTO_TEST(pattern_counts)

// Creating a registry requires a budget with routes capacity.
LT_BEGIN_AUTO_TEST(server_routes_suite, registry_requires_capacity)
    srv::resource_budget empty_budget;
    LT_CHECK(create_fails_with(empty_budget, outcome_code::invalid_argument));

    srv::budget_limits zero;
    zero.set(srv::resource::routes, 0);
    const srv::resource_budget no_routes = srv::resource_budget::root(zero);
    LT_CHECK(create_fails_with(no_routes, outcome_code::invalid_argument));

    const srv::resource_budget budget = budget_with_routes(8);
    srv::route_registry registry;
    LT_CHECK(creates(budget, registry));
    LT_CHECK_EQ(registry.budget().capacity(srv::resource::routes),
                std::size_t{8});
    LT_CHECK_EQ(registry.size(), std::size_t{0});

    // A default-constructed registry is empty and refuses registration.
    srv::route_registry detached;
    LT_CHECK_EQ(detached.size(), std::size_t{0});
    LT_CHECK(route_fails_with(detached, kGet, "/a",
                              outcome_code::invalid_state));
LT_END_AUTO_TEST(registry_requires_capacity)

// Method, pattern, handler, and duplicate admission rules.
LT_BEGIN_AUTO_TEST(server_routes_suite, route_admission_rules)
    const srv::resource_budget budget = budget_with_routes(8);
    srv::route_registry registry;
    LT_CHECK(creates(budget, registry));

    // The invalid unknown_ method is rejected.
    srv::route_pattern parsed;
    LT_CHECK(registry.route(http::method{}, "/", make_handler()).code()
                 == outcome_code::invalid_argument);
    // Malformed patterns are rejected with the parse diagnostic.
    LT_CHECK(route_fails_with(registry, kGet, "no-slash",
                              outcome_code::invalid_argument));
    // An empty handler is rejected.
    srv::route_handler no_handler;
    LT_CHECK(registry.route(kGet, "/a", std::move(no_handler)).code()
                 == outcome_code::invalid_argument);
    LT_CHECK_EQ(registry.size(), std::size_t{0});

    LT_CHECK(registers(registry, kGet, "/a"));
    // Duplicate (method, canonical pattern) is invalid_state.
    LT_CHECK(route_fails_with(registry, kGet, "/a",
                              outcome_code::invalid_state));
    // Same pattern, different method is fine.
    LT_CHECK(registers(registry, kPost, "/a"));
    // Same shape, different parameter name is a different pattern.
    LT_CHECK(registers(registry, kGet, "/users/{id}"));
    LT_CHECK(registers(registry, kGet, "/users/{name}"));
    LT_CHECK_EQ(registry.size(), std::size_t{4});
LT_END_AUTO_TEST(route_admission_rules)

// The routes budget bounds the registry.
LT_BEGIN_AUTO_TEST(server_routes_suite, registry_capacity_enforced)
    const srv::resource_budget budget = budget_with_routes(3);
    srv::route_registry registry;
    LT_CHECK(creates(budget, registry));
    LT_CHECK(registers(registry, kGet, "/one"));
    LT_CHECK(registers(registry, kGet, "/two"));
    LT_CHECK(registers(registry, kGet, "/three"));
    LT_CHECK(route_fails_with(registry, kGet, "/four",
                              outcome_code::limit_exceeded));
    // The refused registration consumed nothing.
    LT_CHECK_EQ(registry.size(), std::size_t{3});
    // The refused pattern is still registrable elsewhere: the budget
    // node itself is exhausted, so a fresh registry on the same node
    // sees the same refusal.
    srv::route_registry second;
    LT_CHECK(creates(budget, second));
    LT_CHECK(route_fails_with(second, kGet, "/four",
                              outcome_code::limit_exceeded));
LT_END_AUTO_TEST(registry_capacity_enforced)

// A registry over a child budget is additionally bounded by the parent
// scope's remaining routes capacity.
LT_BEGIN_AUTO_TEST(server_routes_suite, registry_bounded_by_parent)
    srv::budget_limits limits;
    limits.set(srv::resource::routes, 2);
    const srv::resource_budget parent = srv::resource_budget::root(limits);
    srv::resource_budget child;
    LT_CHECK(parent.child(limits, child).ok());

    // One routes unit is committed directly against the parent scope.
    srv::reservation external;
    LT_CHECK(parent.reserve(srv::resource::routes, 1, external).ok());

    srv::route_registry registry;
    LT_CHECK(creates(child, registry));
    LT_CHECK(registers(registry, kGet, "/only"));
    LT_CHECK(route_fails_with(registry, kGet, "/second",
                              outcome_code::limit_exceeded));
    LT_CHECK_EQ(registry.size(), std::size_t{1});
    external.release();
    LT_CHECK(registers(registry, kGet, "/second"));
LT_END_AUTO_TEST(registry_bounded_by_parent)

// registered() probes exact-segment visibility; a miss on method,
// pattern shape, or parameter name.
LT_BEGIN_AUTO_TEST(server_routes_suite, registered_probe)
    srv::route_registry detached;
    srv::route_pattern nowhere;
    LT_CHECK(parses("/a", nowhere));
    LT_CHECK(!detached.registered(kGet, nowhere));

    const srv::resource_budget budget = budget_with_routes(8);
    srv::route_registry registry;
    LT_CHECK(creates(budget, registry));
    LT_CHECK(registers(registry, kGet, "/a"));
    LT_CHECK(registers(registry, kGet, "/users/{id}"));

    srv::route_pattern a;
    LT_CHECK(parses("/a", a));
    srv::route_pattern users_by_id;
    LT_CHECK(parses("/users/{id}", users_by_id));
    srv::route_pattern b;
    LT_CHECK(parses("/b", b));
    srv::route_pattern users;
    LT_CHECK(parses("/users", users));

    LT_CHECK(registry.registered(kGet, a));
    LT_CHECK(registry.registered(kGet, users_by_id));
    LT_CHECK(!registry.registered(kPost, a));
    LT_CHECK(!registry.registered(kGet, b));
    LT_CHECK(!registry.registered(kGet, users));
    LT_CHECK_EQ(registry.size(), std::size_t{2});
LT_END_AUTO_TEST(registered_probe)

// REQ-009, structural pin: registration is version-agnostic. Neither
// route() nor registered() nor a stored entry consults a protocol
// value; the compile of this suite is the proof that the API has no
// protocol dimension, and the registry accepts routes while the server
// configuration's protocol set is entirely untouched here.
LT_BEGIN_AUTO_TEST(server_routes_suite, registration_has_no_protocol_axis)
    const srv::resource_budget budget = budget_with_routes(2);
    srv::route_registry registry;
    LT_CHECK(creates(budget, registry));
    LT_CHECK(registers(registry, kGet, "/shared"));
    srv::route_pattern shared;
    LT_CHECK(parses("/shared", shared));
    // Visible through the method+pattern key alone.
    LT_CHECK(registry.registered(kGet, shared));
    LT_CHECK_EQ(registry.size(), std::size_t{1});
LT_END_AUTO_TEST(registration_has_no_protocol_axis)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
