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

// TASK-118 step 1: the ported route families (plan D1) -- method sets
// over the nine known slots, the prefix family, and resolve() with the
// v2 precedence shape (full matches before prefix matches; among full
// matches first registration order; among prefix matches most segments
// wins, ties by registration order). Extension methods register singly
// through route(); unknown_ is never registrable. The regex family and
// per-segment {name|regex} constraints are migration-noted away (see
// specs/architecture/v3/v2-parity-inventory.md).

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/server/configuration.hpp>

#include "./littletest.hpp"

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;

using outcome_code = httpserver::http::outcome_code;
using resolve_kind = srv::route_registry::resolve_result::resolve_kind;

const http::method kGet = http::method::known(http::method_id::get);
const http::method kHead = http::method::known(http::method_id::head);
const http::method kPost = http::method::known(http::method_id::post);
const http::method kPut = http::method::known(http::method_id::put);

srv::route_handler make_handler() {
    return srv::route_handler(
        [](httpserver::exchange&) -> httpserver::task<void> { co_return; });
}

srv::resource_budget budget_with_routes(std::size_t capacity) {
    srv::budget_limits limits;
    limits.set(srv::resource::routes, capacity);
    return srv::resource_budget::root(limits);
}

bool creates(std::size_t capacity, srv::route_registry& out) {
    const srv::resource_budget budget = budget_with_routes(capacity);
    return srv::route_registry::create(budget, out).ok();
}

bool route_ok(srv::route_registry& registry, const http::method& m,
              std::string_view pattern) {
    return registry.route(m, pattern, make_handler()).ok();
}

bool route_set_ok(srv::route_registry& registry, const http::method_set& set,
                  std::string_view pattern) {
    return registry.route(set, pattern, make_handler()).ok();
}

bool route_prefix_ok(srv::route_registry& registry,
                     const http::method_set& set, std::string_view pattern) {
    return registry.route_prefix(set, pattern, make_handler()).ok();
}

// The tail of every registration-failure probe: typed code plus a
// non-empty diagnostic, and the registry size unchanged.
bool failed_with(const http::outcome& result, outcome_code expected,
                 std::size_t size_before, std::size_t size_after) {
    return result.code() == expected && !result.message().empty()
        && size_before == size_after;
}

http::method_set set_of(const http::method& m) {
    http::method_set set;
    set.set(m.id());
    return set;
}

http::method_set set_of2(const http::method& a, const http::method& b) {
    http::method_set set;
    set.set(a.id());
    set.set(b.id());
    return set;
}

// The merged known methods of a resolve result rendered in method_id
// order, the Allow wire form.
std::string rendered(const srv::route_registry::resolve_result& r) {
    std::string out;
    for (std::size_t id = 0;
         id < static_cast<std::size_t>(http::method_id::extension); ++id) {
        if (!r.methods.contains(
                http::method::known(static_cast<http::method_id>(id)))) {
            continue;
        }
        if (!out.empty()) out.append(", ");
        out.append(http::method::known(static_cast<http::method_id>(id))
                       .name());
    }
    return out;
}

std::string captures_of(const srv::route_registry::resolve_result& r) {
    std::string out;
    for (const srv::route_captures& capture : r.captures) {
        if (!out.empty()) out.append(";");
        out.append(capture.name).append("=").append(capture.value);
    }
    return out;
}

// True when `m` resolves to a hit whose handler is the one most
// recently registered under `pattern` is irrelevant here -- the suites
// register one handler per pattern, so the pointer identity check is a
// bonus, not the contract.
bool resolves_hit(const srv::route_registry& registry, const http::method& m,
                  std::string_view path) {
    const srv::route_registry::resolve_result r = registry.resolve(m, path);
    return r.kind == resolve_kind::hit && r.handler != nullptr;
}

}  // namespace

LT_BEGIN_SUITE(server_routes_families_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(server_routes_families_suite)

// method_set is a bitset over the nine known slots: extension and
// unknown_ methods are never members, and any() tracks emptiness.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, method_set_membership)
    http::method_set empty;
    LT_CHECK(!empty.any());
    LT_CHECK(!empty.contains(kGet));
    LT_CHECK(!empty.contains(http::method{}));

    http::method_set get_head;
    get_head.set(http::method_id::get);
    get_head.set(http::method_id::head);
    LT_CHECK(get_head.any());
    LT_CHECK(get_head.contains(kGet));
    LT_CHECK(get_head.contains(kHead));
    LT_CHECK(!get_head.contains(kPost));

    // Extension and unknown_ slots are not representable.
    get_head.set(http::method_id::extension);
    get_head.set(http::method_id::unknown_);
    const http::method ext = http::method::extension("CUSTOM");
    LT_CHECK(!get_head.contains(ext));
    LT_CHECK(!get_head.contains(http::method{}));
    LT_CHECK(http::to_string(get_head) == "GET, HEAD");
LT_END_AUTO_TEST(method_set_membership)

// A one-bit method_set registration is equivalent to the legacy
// single-method route(): same admission, same hit, and a method
// mismatch reports the one-method Allow set.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, one_bit_set_matches_legacy)
    srv::route_registry legacy;
    LT_CHECK(creates(8, legacy));
    LT_CHECK(route_ok(legacy, kGet, "/legacy"));
    LT_CHECK(resolves_hit(legacy, kGet, "/legacy"));
    const srv::route_registry::resolve_result legacy_miss =
        legacy.resolve(kPost, "/legacy");
    LT_CHECK(legacy_miss.kind == resolve_kind::method_miss);
    LT_CHECK(legacy_miss.handler == nullptr);
    LT_CHECK(rendered(legacy_miss) == "GET");

    srv::route_registry via_set;
    LT_CHECK(creates(8, via_set));
    LT_CHECK(route_set_ok(via_set, set_of(kGet), "/legacy"));
    LT_CHECK(resolves_hit(via_set, kGet, "/legacy"));
    const srv::route_registry::resolve_result set_miss =
        via_set.resolve(kPost, "/legacy");
    LT_CHECK(set_miss.kind == resolve_kind::method_miss);
    LT_CHECK(rendered(set_miss) == "GET");
LT_END_AUTO_TEST(one_bit_set_matches_legacy)

// GET+HEAD serves both methods; a third method sees the merged Allow
// in method_id order.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, get_head_set_serves_both)
    srv::route_registry registry;
    LT_CHECK(creates(8, registry));
    LT_CHECK(route_set_ok(registry, set_of2(kGet, kHead), "/both"));
    LT_CHECK(resolves_hit(registry, kGet, "/both"));
    LT_CHECK(resolves_hit(registry, kHead, "/both"));
    const srv::route_registry::resolve_result miss =
        registry.resolve(kPost, "/both");
    LT_CHECK(miss.kind == resolve_kind::method_miss);
    LT_CHECK(rendered(miss) == "GET, HEAD");
LT_END_AUTO_TEST(get_head_set_serves_both)

// Duplicate rule: the same pattern with any overlapping method is
// invalid_state; disjoint method sets on one pattern coexist and the
// Allow merges.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, duplicate_and_merge_rules)
    srv::route_registry registry;
    LT_CHECK(creates(8, registry));
    LT_CHECK(route_ok(registry, kGet, "/a"));

    const std::size_t one = registry.size();
    LT_CHECK(failed_with(registry.route(kGet, "/a", make_handler()),
                         outcome_code::invalid_state, one, registry.size()));
    LT_CHECK(failed_with(registry.route(set_of2(kGet, kHead), "/a",
                                        make_handler()),
                         outcome_code::invalid_state, one, registry.size()));

    // Disjoint sets coexist.
    LT_CHECK(route_set_ok(registry, set_of(kPost), "/a"));
    LT_CHECK(registry.size() == std::size_t{2});
    LT_CHECK(resolves_hit(registry, kGet, "/a"));
    LT_CHECK(resolves_hit(registry, kPost, "/a"));
    const srv::route_registry::resolve_result miss =
        registry.resolve(kPut, "/a");
    LT_CHECK(miss.kind == resolve_kind::method_miss);
    LT_CHECK(rendered(miss) == "GET, POST");

    // An empty method_set is invalid_argument.
    const std::size_t two = registry.size();
    LT_CHECK(failed_with(registry.route(http::method_set{}, "/a",
                                        make_handler()),
                         outcome_code::invalid_argument, two,
                         registry.size()));
    // The unknown_ method stays unregistrable through both forms.
    LT_CHECK(failed_with(registry.route(http::method{}, "/a", make_handler()),
                         outcome_code::invalid_argument, two,
                         registry.size()));
LT_END_AUTO_TEST(duplicate_and_merge_rules)

// The prefix family: segments match a prefix of the request segments,
// equal length included; "/" is the documented catch-all.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, prefix_family_matching)
    srv::route_registry registry;
    LT_CHECK(creates(8, registry));
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/api"));
    LT_CHECK(resolves_hit(registry, kGet, "/api"));
    LT_CHECK(resolves_hit(registry, kGet, "/api/v1"));
    LT_CHECK(resolves_hit(registry, kGet, "/api/v1/users/42"));
    LT_CHECK(registry.resolve(kGet, "/apiv1").kind == resolve_kind::miss);

    const srv::route_registry::resolve_result hit =
        registry.resolve(kGet, "/api/v1");
    LT_CHECK(hit.kind == resolve_kind::hit);
    LT_CHECK(hit.is_prefix);
    LT_CHECK(hit.pattern_text == "/api");

    // A prefix registration may itself carry parameters.
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/shop/{cat}"));
    const srv::route_registry::resolve_result param_hit =
        registry.resolve(kGet, "/shop/garden/tools");
    LT_CHECK(param_hit.kind == resolve_kind::hit);
    LT_CHECK(param_hit.is_prefix);
    LT_CHECK(captures_of(param_hit) == "cat=garden");
LT_END_AUTO_TEST(prefix_family_matching)

// "/" prefix is the catch-all: it matches every route path.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, slash_catch_all)
    srv::route_registry registry;
    LT_CHECK(creates(8, registry));
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/"));
    LT_CHECK(resolves_hit(registry, kGet, "/"));
    LT_CHECK(resolves_hit(registry, kGet, "/anything/at/all"));
    const srv::route_registry::resolve_result hit =
        registry.resolve(kGet, "/x");
    LT_CHECK(hit.kind == resolve_kind::hit);
    LT_CHECK(hit.is_prefix);
    LT_CHECK(hit.pattern_text == "/");
LT_END_AUTO_TEST(slash_catch_all)

// Precedence: a full match (exact or parameterized) always beats a
// prefix match; among prefix matches the deeper pattern wins and ties
// fall back to registration order.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, family_precedence)
    srv::route_registry registry;
    LT_CHECK(creates(16, registry));
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/api"));
    LT_CHECK(route_ok(registry, kGet, "/api/exact"));
    const srv::route_registry::resolve_result exact =
        registry.resolve(kGet, "/api/exact");
    LT_CHECK(exact.kind == resolve_kind::hit);
    LT_CHECK(!exact.is_prefix);
    LT_CHECK(exact.pattern_text == "/api/exact");

    // A parameterized full match also beats a prefix.
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/shop"));
    LT_CHECK(route_ok(registry, kGet, "/shop/{id}"));
    const srv::route_registry::resolve_result param =
        registry.resolve(kGet, "/shop/42");
    LT_CHECK(param.kind == resolve_kind::hit);
    LT_CHECK(!param.is_prefix);
    LT_CHECK(param.pattern_text == "/shop/{id}");

    // Among prefixes: most segments wins.
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/deep"));
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/deep/a/b"));
    const srv::route_registry::resolve_result deep =
        registry.resolve(kGet, "/deep/a/b/c");
    LT_CHECK(deep.kind == resolve_kind::hit);
    LT_CHECK(deep.is_prefix);
    LT_CHECK(deep.pattern_text == "/deep/a/b");

    // Tie on segment count: registration order.
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/tie/{x}"));
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/tie/alt"));
    const srv::route_registry::resolve_result tie =
        registry.resolve(kGet, "/tie/alt/more");
    LT_CHECK(tie.kind == resolve_kind::hit);
    LT_CHECK(tie.pattern_text == "/tie/{x}");
LT_END_AUTO_TEST(family_precedence)

// The method-blind tier decision (the v2 posture): when a full-match
// pattern exists for the path, the outcome never falls through to a
// prefix that would accept the method -- the full tier owns the 405.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, full_tier_owns_405)
    srv::route_registry registry;
    LT_CHECK(creates(8, registry));
    LT_CHECK(route_ok(registry, kGet, "/users/42"));
    LT_CHECK(route_prefix_ok(registry, set_of2(kGet, kPost), "/users"));
    const srv::route_registry::resolve_result miss =
        registry.resolve(kPost, "/users/42");
    LT_CHECK(miss.kind == resolve_kind::method_miss);
    LT_CHECK(rendered(miss) == "GET");
    // The prefix still serves a path with no full registration.
    LT_CHECK(resolves_hit(registry, kPost, "/users/43"));
LT_END_AUTO_TEST(full_tier_owns_405)

// A parameterized and a literal pattern of the same depth both full-
// match; the first registration wins (migration note: v2 tiered exact
// above parameterized within the trie, v3 collapses both into the full
// tier in registration order).
LT_BEGIN_AUTO_TEST(server_routes_families_suite, same_depth_registration_order)
    srv::route_registry registry;
    LT_CHECK(creates(8, registry));
    LT_CHECK(route_ok(registry, kGet, "/files/{name}"));
    LT_CHECK(route_ok(registry, kGet, "/files/special"));
    const srv::route_registry::resolve_result hit =
        registry.resolve(kGet, "/files/special");
    LT_CHECK(hit.kind == resolve_kind::hit);
    LT_CHECK(hit.pattern_text == "/files/{name}");
LT_END_AUTO_TEST(same_depth_registration_order)

// Captures arrive in pattern order with their names.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, named_captures_in_order)
    srv::route_registry registry;
    LT_CHECK(creates(8, registry));
    LT_CHECK(route_ok(registry, kGet, "/params/{id}/name/{name}"));
    const srv::route_registry::resolve_result hit =
        registry.resolve(kGet, "/params/42/name/jane");
    LT_CHECK(hit.kind == resolve_kind::hit);
    LT_CHECK(!hit.is_prefix);
    LT_CHECK(hit.pattern_text == "/params/{id}/name/{name}");
    LT_CHECK(captures_of(hit) == "id=42;name=jane");
    LT_CHECK(hit.methods.contains(kGet));
LT_END_AUTO_TEST(named_captures_in_order)

// A path no family matches is a plain miss.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, plain_miss)
    srv::route_registry registry;
    LT_CHECK(creates(8, registry));
    LT_CHECK(route_ok(registry, kGet, "/hello"));
    LT_CHECK(route_prefix_ok(registry, set_of(kGet), "/api"));
    const srv::route_registry::resolve_result miss =
        registry.resolve(kGet, "/definitely/not/there");
    LT_CHECK(miss.kind == resolve_kind::miss);
    LT_CHECK(miss.handler == nullptr);
    LT_CHECK(miss.captures.empty());
LT_END_AUTO_TEST(plain_miss)

// Extension methods register singly and resolve by identity; a
// different method on the same path reports the extension token in
// the merged Allow set.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, extension_single_registration)
    srv::route_registry registry;
    LT_CHECK(creates(8, registry));
    const http::method purge = http::method::extension("PURGE");
    LT_CHECK(route_ok(registry, purge, "/cache"));
    LT_CHECK(resolves_hit(registry, purge, "/cache"));
    const http::method other = http::method::extension("BREW");
    const srv::route_registry::resolve_result miss =
        registry.resolve(other, "/cache");
    LT_CHECK(miss.kind == resolve_kind::method_miss);
    LT_CHECK(rendered(miss).empty());
    LT_CHECK(miss.extension_names.size() == std::size_t{1});
    LT_CHECK(miss.extension_names.front() == "PURGE");
LT_END_AUTO_TEST(extension_single_registration)

// -- allocation posture --------------------------------------------------
//
// The frozen assumption this suite pins (plan: the registry never
// re-canonicalizes immutable patterns per request): a miss over a
// populated registry allocates nothing beyond the request-path split,
// and a hit's allocations are bounded. The probe counts every plain
// global new; the measured window is the resolve() call alone, so the
// harness cannot pollute the count.

namespace alloc_probe {
inline std::atomic<int> allocations{0};
}

void* operator new(std::size_t n) {
    alloc_probe::allocations.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t n) { return operator new(n); }

void operator delete(void* p) noexcept { std::free(p); }

void operator delete[](void* p) noexcept { std::free(p); }

void operator delete(void* p, std::size_t) noexcept { std::free(p); }

void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

int allocations_now() {
    return alloc_probe::allocations.load(std::memory_order_relaxed);
}

}  // namespace

// A miss over a populated registry: the only allocation is the
// request-path split itself (one segment -> exactly one growth step,
// implementation-independent); no per-entry probe allocates.
LT_BEGIN_AUTO_TEST(server_routes_families_suite, miss_path_allocation_free)
    srv::route_registry registry;
    LT_CHECK(creates(16, registry));
    for (int i = 0; i < 8; ++i) {
        LT_CHECK(route_ok(registry, kGet, "/r" + std::to_string(i)));
    }
    const int before = allocations_now();
    const srv::route_registry::resolve_result miss =
        registry.resolve(kGet, "/missing");
    const int delta = allocations_now() - before;
    LT_CHECK(miss.kind == resolve_kind::miss);
    LT_CHECK_EQ(delta, 1);
LT_END_AUTO_TEST(miss_path_allocation_free)

// A hit with unobservable merged methods (the default): the full tier
// stops at its first hit, so a later same-shape extension single is
// never probed and never merges its token. The two allocations are
// the request-path split and the resolve_result's pattern-text copy
// (a 31-byte pattern, past the small-string optimization, so the copy
// is visible in the count).
LT_BEGIN_AUTO_TEST(server_routes_families_suite, hit_path_allocations_bounded)
    srv::route_registry registry;
    LT_CHECK(creates(16, registry));
    const std::string long_pattern =
        "/" + std::string(30, 'x');
    LT_CHECK(route_ok(registry, kGet, long_pattern));
    const http::method purge = http::method::extension("PURGE");
    LT_CHECK(route_ok(registry, purge, long_pattern));
    const int before = allocations_now();
    const srv::route_registry::resolve_result hit =
        registry.resolve(kGet, long_pattern);
    const int delta = allocations_now() - before;
    LT_CHECK(hit.kind == resolve_kind::hit);
    LT_CHECK_EQ(delta, 2);
LT_END_AUTO_TEST(hit_path_allocations_bounded)

// The observability rule of the full-tier scan: without descriptor
// observers the tier stops at its first hit (the base match()
// semantics, and out.methods carries only what was merged up to and
// including the hit); with observers (route_resolved/before_handler
// hooks consult route_descriptor.methods) the scan continues so the
// merged methods stay complete.
LT_BEGIN_AUTO_TEST(server_routes_families_suite,
                   resolve_observability_controls_method_merge)
    srv::route_registry registry;
    LT_CHECK(creates(16, registry));
    LT_CHECK(route_ok(registry, kGet, "/a"));
    LT_CHECK(route_set_ok(registry, set_of(kPost), "/a"));

    const srv::route_registry::resolve_result first_hit =
        registry.resolve(kGet, "/a");
    LT_CHECK(first_hit.kind == resolve_kind::hit);
    LT_CHECK(first_hit.methods.contains(kGet));
    LT_CHECK(!first_hit.methods.contains(kPost));

    const srv::route_registry::resolve_result merged =
        registry.resolve(kGet, "/a", true);
    LT_CHECK(merged.kind == resolve_kind::hit);
    LT_CHECK(merged.methods.contains(kGet));
    LT_CHECK(merged.methods.contains(kPost));

    // A method miss keeps the full merge either way (the Allow set).
    const srv::route_registry::resolve_result miss =
        registry.resolve(kPut, "/a");
    LT_CHECK(miss.kind == resolve_kind::method_miss);
    LT_CHECK(rendered(miss) == "GET, POST");
LT_END_AUTO_TEST(resolve_observability_controls_method_merge)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
