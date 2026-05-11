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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// TASK-028: v2.0 routing-semantics regression gate. Walks every routing
// pattern in the v1 test corpus (taxonomy enumerated in
// test/REGRESSION.md) through the public webserver registration surface
// and asserts the v2 3-tier table (probed via webserver_impl::lookup_v2)
// resolves each pattern to the right tier, the right captures, and the
// right method_set. Today the v2 table is shadow-populated alongside the
// v1 maps and is not yet on the dispatch path (TASK-036 cuts dispatch
// over). This TU is the only thing pinning v2-lookup semantics ahead of
// that cutover, so a failure here is a release-blocker per AR-003.
//
// Each LT_BEGIN_AUTO_TEST mirrors one row of the taxonomy table in
// REGRESSION.md. When you add a new routing pattern, add a test here
// AND a row to the table.

#include <memory>
#include <string>
#include <utility>

#include "./httpserver.hpp"
#include "./httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;

namespace {

// Stateless resource — every test that needs a class-derived handler
// uses this. The render_get body is irrelevant: lookup_v2 returns the
// route_entry without invoking the handler.
class noop_resource : public ht::http_resource {
 public:
    std::shared_ptr<ht::http_response> render_get(const ht::http_request&)
        override {
        return std::make_shared<ht::http_response>(
            ht::http_response::string("ok"));
    }
};

// Convenience: pull the impl handle off a non-started webserver.
ht::detail::webserver_impl& impl_of(ht::webserver& ws) {
    return *ht::webserver_test_access::impl(ws);
}

// Sentinel used by on_get_only_method_yields_get_only_method_set — we
// just need a lambda that compiles; the body is never called.
ht::http_response noop_handler(const ht::http_request&) {
    return ht::http_response(ht::http_response::string("ok"));
}

}  // namespace

LT_BEGIN_SUITE(routing_regression_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(routing_regression_suite)

// ---------------------------------------------------------------------
// Exact paths (taxonomy row: exact).
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   exact_path_hits_exact_tier_with_full_methods)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/exact", std::make_shared<noop_resource>());

    auto r = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/exact"));
    LT_CHECK(r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::exact);
    // register_path → method_set is "all methods set" (matches v1
    // semantics: class-derived resources serve every method the class
    // is configured to render).
    LT_CHECK(r.entry.methods == ht::method_set{}.set_all());
    LT_CHECK(!r.entry.is_prefix);
LT_END_AUTO_TEST(exact_path_hits_exact_tier_with_full_methods)

LT_BEGIN_AUTO_TEST(routing_regression_suite, exact_path_root_only)
    // Mirrors http_endpoint_root_only — root-only registration should be
    // exact-tier and only match the root, not arbitrary paths.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/", std::make_shared<noop_resource>());

    auto root = impl_of(ws).lookup_v2(ht::http_method::get,
                                      std::string("/"));
    LT_CHECK(root.found);
    LT_CHECK(root.tier == ht::detail::webserver_impl::tier_hit::exact);

    auto sub = impl_of(ws).lookup_v2(ht::http_method::get,
                                     std::string("/foo"));
    LT_CHECK(!sub.found);
    LT_CHECK(sub.tier == ht::detail::webserver_impl::tier_hit::none);
LT_END_AUTO_TEST(exact_path_root_only)

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   exact_path_normalization_aliases)
    // Mirrors basic_suite::duplicate_endpoints — v1 normalizes "OK",
    // "/OK", "/OK/", "OK/" to the same canonical key. The v2 lookup
    // path must agree.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/ok", std::make_shared<noop_resource>());

    // The radix tree's exact tier stores the canonical form. Both
    // /ok and /ok/ should resolve.
    auto a = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/ok"));
    LT_CHECK(a.found);
    auto b = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/ok/"));
    LT_CHECK(b.found);
LT_END_AUTO_TEST(exact_path_normalization_aliases)

// ---------------------------------------------------------------------
// Parameterized paths (taxonomy rows: single-param, multi-param,
// custom-regex param).
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   parameterized_single_segment_captures)
    // Mirrors basic_suite::regex_matching_arg via the on_get lambda
    // surface (TASK-025). The route is GET-only, so methods should
    // contain GET and NOT contain POST. The radix tier carries the
    // capture for {id}.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.on_get("/users/{id}", noop_handler);

    auto r = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/users/42"));
    LT_CHECK(r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::radix);
    LT_CHECK(r.entry.methods.contains(ht::http_method::get));
    LT_CHECK(!r.entry.methods.contains(ht::http_method::post));
    LT_CHECK_EQ(r.captured_params.size(),
                static_cast<std::size_t>(1));
    LT_CHECK_EQ(r.captured_params[0].first, std::string("id"));
    LT_CHECK_EQ(r.captured_params[0].second, std::string("42"));
LT_END_AUTO_TEST(parameterized_single_segment_captures)

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   parameterized_multiple_segments_capture_in_order)
    // Mirrors http_endpoint_suite::http_endpoint_multiple_params via
    // the v2 lookup. The two {name} segments come out in the order
    // they appear in the registered pattern.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/a/{x}/b/{y}/c",
                     std::make_shared<noop_resource>());

    auto r = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/a/1/b/2/c"));
    LT_CHECK(r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::radix);
    LT_CHECK_EQ(r.captured_params.size(),
                static_cast<std::size_t>(2));
    LT_CHECK_EQ(r.captured_params[0].first, std::string("x"));
    LT_CHECK_EQ(r.captured_params[0].second, std::string("1"));
    LT_CHECK_EQ(r.captured_params[1].first, std::string("y"));
    LT_CHECK_EQ(r.captured_params[1].second, std::string("2"));
LT_END_AUTO_TEST(parameterized_multiple_segments_capture_in_order)

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   parameterized_with_custom_regex_lands_in_radix_tier)
    // Mirrors basic_suite::regex_matching_arg_custom. v1 used the
    // http_endpoint compiled-regex map to enforce the per-segment
    // constraint ([0-9]+). The v2 radix tier treats the whole
    // `{name|constraint}` literal as a wildcard with name
    // "name|constraint" and does NOT enforce the per-segment regex —
    // every same-shape path falls into the same radix bucket.
    //
    // This is a documented v2 divergence (see test/REGRESSION.md, row
    // "custom-regex param"). The pin below locks the current v2
    // behavior so any future change to constraint handling shows up
    // as an explicit test edit, not silent drift.
    //
    // When the radix tier learns per-segment regex constraints (PRD
    // §3.7 future work, currently unscheduled), this test flips back
    // to the v1 semantics — assert `!miss.found` and the constraint
    // is honored.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/items/{id|([0-9]+)}",
                     std::make_shared<noop_resource>());

    auto hit = impl_of(ws).lookup_v2(ht::http_method::get,
                                     std::string("/items/42"));
    LT_CHECK(hit.found);
    LT_CHECK(hit.tier == ht::detail::webserver_impl::tier_hit::radix);

    // v2 current behavior: the radix tier matches regardless of the
    // ([0-9]+) constraint. Documented divergence from v1.
    //
    // FIXME(TASK-036-prereq): when the radix tier learns per-segment
    // regex constraints (PRD §3.7 future work, see REGRESSION.md
    // section 3 "must land before TASK-036"), flip the two assertions
    // below: `LT_CHECK(!non_numeric.found)` and remove the tier check.
    auto non_numeric = impl_of(ws).lookup_v2(ht::http_method::get,
                                             std::string("/items/abc"));
    LT_CHECK(non_numeric.found);
    LT_CHECK(non_numeric.tier ==
             ht::detail::webserver_impl::tier_hit::radix);
LT_END_AUTO_TEST(parameterized_with_custom_regex_lands_in_radix_tier)

// ---------------------------------------------------------------------
// Prefix paths (taxonomy row: prefix).
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   prefix_serves_bare_and_subpaths)
    // Mirrors basic_suite::family_endpoints. A prefix registration
    // serves both the bare path and arbitrary subpaths under it.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_prefix("/static", std::make_shared<noop_resource>());

    auto bare = impl_of(ws).lookup_v2(ht::http_method::get,
                                      std::string("/static"));
    LT_CHECK(bare.found);
    LT_CHECK(bare.tier == ht::detail::webserver_impl::tier_hit::radix);
    LT_CHECK(bare.entry.is_prefix);

    auto sub = impl_of(ws).lookup_v2(ht::http_method::get,
                                     std::string("/static/foo/bar"));
    LT_CHECK(sub.found);
    LT_CHECK(sub.tier == ht::detail::webserver_impl::tier_hit::radix);
    LT_CHECK(sub.entry.is_prefix);
LT_END_AUTO_TEST(prefix_serves_bare_and_subpaths)

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   prefix_then_more_specific_exact_shadows)
    // Mirrors the "exact shadows prefix on the exact path, prefix
    // serves the rest" precedence relied on by family-style routes
    // with carve-outs.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    auto prefix_res = std::make_shared<noop_resource>();
    auto exact_res = std::make_shared<noop_resource>();
    ws.register_prefix("/static", prefix_res);
    ws.register_path("/static/index", exact_res);

    // /static/index — exact wins.
    auto idx = impl_of(ws).lookup_v2(ht::http_method::get,
                                     std::string("/static/index"));
    LT_CHECK(idx.found);
    LT_CHECK(idx.tier == ht::detail::webserver_impl::tier_hit::exact);
    LT_CHECK(!idx.entry.is_prefix);

    // /static/foo — radix tier serves it as a prefix subpath.
    auto foo = impl_of(ws).lookup_v2(ht::http_method::get,
                                     std::string("/static/foo"));
    LT_CHECK(foo.found);
    LT_CHECK(foo.tier == ht::detail::webserver_impl::tier_hit::radix);
    LT_CHECK(foo.entry.is_prefix);
LT_END_AUTO_TEST(prefix_then_more_specific_exact_shadows)

// ---------------------------------------------------------------------
// Pure regex paths (taxonomy row: regex).
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   regex_route_with_metachars_hits_regex_tier)
    // Mirrors basic_suite::regex_matching. A path with regex
    // metacharacters and no {name} params goes through the regex
    // tier under the default regex_checking=true.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/api/v[0-9]+",
                     std::make_shared<noop_resource>());

    auto r = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/api/v1"));
    LT_CHECK(r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::regex);
LT_END_AUTO_TEST(regex_route_with_metachars_hits_regex_tier)

// ---------------------------------------------------------------------
// Register/unregister cycles (taxonomy row: register/unregister).
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   register_then_unregister_then_lookup_misses)
    // Mirrors basic_suite::register_unregister via the v2 lookup.
    // After unregister_path, lookup_v2 must miss (tier_hit::none) so
    // future dispatch will 404.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/ephemeral", std::make_shared<noop_resource>());

    auto before = impl_of(ws).lookup_v2(ht::http_method::get,
                                        std::string("/ephemeral"));
    LT_CHECK(before.found);

    ws.unregister_path("/ephemeral");

    auto after = impl_of(ws).lookup_v2(ht::http_method::get,
                                       std::string("/ephemeral"));
    LT_CHECK(!after.found);
    LT_CHECK(after.tier ==
             ht::detail::webserver_impl::tier_hit::none);
LT_END_AUTO_TEST(register_then_unregister_then_lookup_misses)

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   prefix_register_then_unregister_then_lookup_misses)
    // Symmetric pin for the prefix half of register/unregister — the
    // v2 radix-tier prefix node must come out cleanly.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_prefix("/scratch", std::make_shared<noop_resource>());

    auto before = impl_of(ws).lookup_v2(
        ht::http_method::get, std::string("/scratch/anything"));
    LT_CHECK(before.found);

    ws.unregister_prefix("/scratch");

    auto after = impl_of(ws).lookup_v2(
        ht::http_method::get, std::string("/scratch/anything"));
    LT_CHECK(!after.found);
LT_END_AUTO_TEST(prefix_register_then_unregister_then_lookup_misses)

// ---------------------------------------------------------------------
// Method-mismatched semantics (taxonomy row: method-mismatched).
// The method_set bitmask is what dispatch uses for the 405 + Allow
// decision after TASK-036 cuts dispatch over.
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   on_get_only_method_yields_get_only_method_set)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.on_get("/g", noop_handler);

    // Looking up under GET: found, GET-only mask.
    auto r_get = impl_of(ws).lookup_v2(ht::http_method::get,
                                       std::string("/g"));
    LT_CHECK(r_get.found);
    LT_CHECK(r_get.entry.methods.contains(ht::http_method::get));
    LT_CHECK(!r_get.entry.methods.contains(ht::http_method::post));
    LT_CHECK(!r_get.entry.methods.contains(ht::http_method::put));

    // Looking up under POST: lookup_v2 doesn't filter on method (the
    // 405 decision lives at the dispatch site per the comment on
    // webserver_impl::lookup_v2). The entry still surfaces with its
    // GET-only mask so dispatch can compose Allow.
    auto r_post = impl_of(ws).lookup_v2(ht::http_method::post,
                                        std::string("/g"));
    LT_CHECK(r_post.found);
    LT_CHECK(r_post.entry.methods.contains(ht::http_method::get));
    LT_CHECK(!r_post.entry.methods.contains(ht::http_method::post));
LT_END_AUTO_TEST(on_get_only_method_yields_get_only_method_set)

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   on_get_then_on_post_same_path_merges_methods)
    // Mirrors webserver_on_methods_test merge-on-distinct-method
    // semantics — two on_* registrations on the same path compose
    // into one entry whose method_set carries both bits.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.on_get("/m", noop_handler);
    ws.on_post("/m", noop_handler);

    auto r = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/m"));
    LT_CHECK(r.found);
    LT_CHECK(r.entry.methods.contains(ht::http_method::get));
    LT_CHECK(r.entry.methods.contains(ht::http_method::post));
    LT_CHECK(!r.entry.methods.contains(ht::http_method::put));
LT_END_AUTO_TEST(on_get_then_on_post_same_path_merges_methods)

// ---------------------------------------------------------------------
// Overlapping endpoints (taxonomy row: overlap).
//
// v1's basic_suite::overlapping_endpoints documents "regex wins, not
// sure why" — an iteration-order accident over std::map. The v2 table
// gives a deterministic structural precedence (first-registered wins
// when two patterns of the same tier match). This test pins THAT v2
// behavior; the divergence-from-v1 rationale is documented in
// REGRESSION.md.
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   overlapping_two_regex_routes_deterministic_first_wins)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    auto first = std::make_shared<noop_resource>();
    auto second = std::make_shared<noop_resource>();
    ws.register_path("/foo/{var|([a-z]+)}/", first);
    ws.register_path("/{var|([a-z]+)}/bar/", second);

    auto r = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/foo/bar/"));
    LT_CHECK(r.found);
    // The v2 radix tree prefers exact children over wildcard children
    // at each node. /foo/{var|([a-z]+)}/ anchors segment 0 on the
    // literal "foo" (exact child), so the tree descends there first and
    // never tries the wildcard root branch required by the second
    // pattern. Structural precedence → first-registered wins here.
    auto* hp = std::get_if<std::shared_ptr<ht::http_resource>>(
        &r.entry.handler);
    LT_CHECK(hp != nullptr);
    LT_CHECK(*hp == first);
LT_END_AUTO_TEST(overlapping_two_regex_routes_deterministic_first_wins)

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   later_exact_registration_shadows_earlier_regex)
    // Mirrors the second half of basic_suite::overlapping_endpoints —
    // an exact-tier registration must beat a regex-tier registration
    // that also matches the path, because the lookup pipeline checks
    // exact before regex.
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    auto rx = std::make_shared<noop_resource>();
    auto exact = std::make_shared<noop_resource>();
    ws.register_path("/foo/{var|([a-z]+)}/", rx);
    ws.register_path("/foo/bar/", exact);

    auto r = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/foo/bar/"));
    LT_CHECK(r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::exact);
    auto* hp = std::get_if<std::shared_ptr<ht::http_resource>>(
        &r.entry.handler);
    LT_CHECK(hp != nullptr);
    LT_CHECK(*hp == exact);
LT_END_AUTO_TEST(later_exact_registration_shadows_earlier_regex)

// ---------------------------------------------------------------------
// Single-resource mode (taxonomy row: single-resource).
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   single_resource_mode_serves_any_subpath)
    // Mirrors basic_suite::single_resource_mode — under single_resource
    // mode, a single register_prefix("/") catches everything.
    ht::webserver ws{ht::create_webserver(8080)
        .single_resource()
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    auto only = std::make_shared<noop_resource>();
    ws.register_prefix("/", only);

    auto root = impl_of(ws).lookup_v2(ht::http_method::get,
                                      std::string("/"));
    LT_CHECK(root.found);

    auto deep = impl_of(ws).lookup_v2(
        ht::http_method::get, std::string("/anything/at/all"));
    LT_CHECK(deep.found);
    auto* hp = std::get_if<std::shared_ptr<ht::http_resource>>(
        &deep.entry.handler);
    LT_CHECK(hp != nullptr);
    LT_CHECK(*hp == only);
LT_END_AUTO_TEST(single_resource_mode_serves_any_subpath)

// ---------------------------------------------------------------------
// Regex-checking off (taxonomy row: regex-disabled).
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   no_regex_checking_treats_metachars_as_literal)
    // With regex_checking(false), a path containing regex metacharacters
    // is registered as a literal exact path. The classifier must NOT
    // route it to the regex tier.
    ht::webserver ws{ht::create_webserver(8080)
        .regex_checking(false)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/api/v[0-9]+",
                     std::make_shared<noop_resource>());

    // Literal match: hits exact tier.
    auto lit = impl_of(ws).lookup_v2(ht::http_method::get,
                                     std::string("/api/v[0-9]+"));
    LT_CHECK(lit.found);
    LT_CHECK(lit.tier == ht::detail::webserver_impl::tier_hit::exact);

    // Regex-style probe: misses, because the route is no longer
    // interpreted as a regex.
    auto miss = impl_of(ws).lookup_v2(ht::http_method::get,
                                      std::string("/api/v1"));
    LT_CHECK(!miss.found);
LT_END_AUTO_TEST(no_regex_checking_treats_metachars_as_literal)

// ---------------------------------------------------------------------
// Baseline miss (taxonomy row: unregistered).
//
// Verifies that lookup_v2 returns found=false and tier_hit::none for a
// path that was never registered through any surface. Guards against a
// hypothetical bug where a default-constructed entry is treated as
// found.
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite, unregistered_path_yields_miss)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    // Register an unrelated path so the table is not completely empty,
    // reducing the chance this is a vacuous pass.
    ws.register_path("/other", std::make_shared<noop_resource>());

    auto r = impl_of(ws).lookup_v2(ht::http_method::get,
                                   std::string("/never-registered"));
    LT_CHECK(!r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::none);
LT_END_AUTO_TEST(unregistered_path_yields_miss)

// ---------------------------------------------------------------------
// Cache tier (taxonomy row: cache).
//
// (1) warm-cache: second lookup on the same path hits the LRU cache tier.
// (2) cache-invalidation: after unregister_path the stale cache entry
//     must not survive — lookup must miss.
// ---------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   second_lookup_same_path_hits_cache_tier)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/cached", std::make_shared<noop_resource>());

    // First lookup: populates the cache.
    auto cold = impl_of(ws).lookup_v2(ht::http_method::get,
                                      std::string("/cached"));
    LT_CHECK(cold.found);
    // First lookup resolves via a tier other than cache.
    LT_CHECK(cold.tier != ht::detail::webserver_impl::tier_hit::cache);

    // Second lookup for the same path: must come from the LRU cache.
    auto warm = impl_of(ws).lookup_v2(ht::http_method::get,
                                      std::string("/cached"));
    LT_CHECK(warm.found);
    LT_CHECK(warm.tier == ht::detail::webserver_impl::tier_hit::cache);
LT_END_AUTO_TEST(second_lookup_same_path_hits_cache_tier)

LT_BEGIN_AUTO_TEST(routing_regression_suite,
                   unregister_invalidates_cache_entry)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/volatile", std::make_shared<noop_resource>());

    // Warm the cache.
    auto before = impl_of(ws).lookup_v2(ht::http_method::get,
                                        std::string("/volatile"));
    LT_CHECK(before.found);

    // Remove the registration — must also purge the cached entry.
    ws.unregister_path("/volatile");

    // A stale cache entry would return found=true with tier_hit::cache;
    // correct behavior is a miss.
    auto after = impl_of(ws).lookup_v2(ht::http_method::get,
                                       std::string("/volatile"));
    LT_CHECK(!after.found);
    LT_CHECK(after.tier == ht::detail::webserver_impl::tier_hit::none);
LT_END_AUTO_TEST(unregister_invalidates_cache_entry)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
