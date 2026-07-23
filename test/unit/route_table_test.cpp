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

// Unit tests for the bespoke segment-trie route storage and
// the LRU route cache. Exercises insert/find for exact, parameterized
// and prefix routes (trie), and cache key equality, LRU promotion,
// method-distinct keys (cache).

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./httpserver/detail/segment_trie.hpp"
#include "./httpserver/detail/route_cache.hpp"
#include "./httpserver/detail/route_entry.hpp"
#include "./httpserver/detail/route_table.hpp"
#include "./httpserver/detail/http_endpoint.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;
namespace htd = httpserver::detail;

// Sentinel makers to avoid crafting a real http_resource just to populate
// the handler field; we leave it null (a default-constructed shared_ptr)
// and ride on the sentinel id field in the test below. The handler field
// is irrelevant — the segment_trie / route_cache tests only care about the
// entry payload identity through pointer equality and the methods/is_prefix
// fields. (route_entry::handler was originally a
// std::variant<lambda_handler, shared_ptr<http_resource>>; the variant arm
// was collapsed to a bare shared_ptr<http_resource>.)
struct test_entry {
    int sentinel = 0;
    ht::method_set methods{};
    bool is_prefix = false;
};

// Minimal concrete http_resource for the route_table class-level tests
// below (register_v2_route needs a real shared_ptr<http_resource>
// payload, not the null-handler sentinel test_entry/make_entry use for
// the trie/cache primitive tests above).
class noop_route_resource : public ht::http_resource {
 public:
    ht::http_response render_get(const ht::http_request&) override {
        return ht::http_response::string("ok");
    }
};

static htd::route_entry make_entry(
        int /*sentinel*/,
        ht::method_set ms = ht::method_set{}.set(ht::http_method::get),
        bool is_prefix = false) {
    htd::route_entry e;
    e.methods = ms;
    e.is_prefix = is_prefix;
    // Leave handler default-constructed (null shared_ptr).
    return e;
}

LT_BEGIN_SUITE(route_table_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(route_table_suite)

// ----- Cycle B: exact path insertion / lookup ----------------------------

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_inserts_exact_path_and_finds_it)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/users/42", make_entry(1));
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(tree.find("/users/42", m));
    LT_CHECK(m.entry != nullptr);
    LT_CHECK(m.captures.empty());
    LT_CHECK(!tree.find("/users/43", m));
LT_END_AUTO_TEST(segment_trie_inserts_exact_path_and_finds_it)

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_root_path_finds_root)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/", make_entry(7));
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(tree.find("/", m));
    LT_CHECK(m.entry != nullptr);
LT_END_AUTO_TEST(segment_trie_root_path_finds_root)

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_no_partial_match)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/users", make_entry(2));
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(!tree.find("/users/42", m));      // exact miss (not a prefix)
    LT_CHECK(!tree.find("/userzzz", m));        // wrong segment
LT_END_AUTO_TEST(segment_trie_no_partial_match)

// ----- Cycle C: parameterized routes -------------------------------------

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_matches_parameterized_segment_and_captures)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/users/{id}/posts", make_entry(3));
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(tree.find("/users/42/posts", m));
    LT_CHECK_EQ(m.captures.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(m.captures[0].first, std::string("id"));
    LT_CHECK_EQ(m.captures[0].second, std::string("42"));
LT_END_AUTO_TEST(segment_trie_matches_parameterized_segment_and_captures)

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_parameterized_too_short_returns_null)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/users/{id}/posts", make_entry(3));
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(!tree.find("/users/42", m));
LT_END_AUTO_TEST(segment_trie_parameterized_too_short_returns_null)

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_exact_shadows_wildcard)
    // Both /users/me (exact) and /users/{id} (parameterized) registered.
    // /users/me must hit exact; /users/42 must hit parameterized.
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/users/me", make_entry(10));
    tree.insert("/users/{id}", make_entry(11));
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(tree.find("/users/me", m));
    LT_CHECK(m.captures.empty());
    LT_CHECK(tree.find("/users/42", m));
    LT_CHECK_EQ(m.captures.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(m.captures[0].second, std::string("42"));
LT_END_AUTO_TEST(segment_trie_exact_shadows_wildcard)

// ----- Cycle D: prefix routes --------------------------------------------

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_prefix_match_serves_subpaths_and_bare_path)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/static",
                make_entry(20, ht::method_set{}.set(ht::http_method::get),
                           /*is_prefix=*/true),
                /*is_prefix=*/true);
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(tree.find("/static", m));
    LT_CHECK(tree.find("/static/foo", m));
    LT_CHECK(tree.find("/static/foo/bar/baz", m));
    LT_CHECK(!tree.find("/staticx", m));        // sibling segment
    LT_CHECK(!tree.find("/", m));                // not a parent
LT_END_AUTO_TEST(segment_trie_prefix_match_serves_subpaths_and_bare_path)

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_more_specific_exact_shadows_prefix)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/static",
                make_entry(20, ht::method_set{}.set(ht::http_method::get),
                           /*is_prefix=*/true),
                /*is_prefix=*/true);
    tree.insert("/static/foo", make_entry(21));
    htd::segment_trie_match<htd::route_entry> m;

    LT_CHECK(tree.find("/static/foo", m));
    LT_CHECK(!m.is_prefix_match);                   // exact shadows prefix
    LT_CHECK(tree.find("/static/bar", m));
    LT_CHECK(m.is_prefix_match);
LT_END_AUTO_TEST(segment_trie_more_specific_exact_shadows_prefix)

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_matches_multiple_parameterized_segments)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/a/{x}/b/{y}/c", make_entry(30));
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(tree.find("/a/1/b/2/c", m));
    LT_CHECK_EQ(m.captures.size(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(m.captures[0].first, std::string("x"));
    LT_CHECK_EQ(m.captures[0].second, std::string("1"));
    LT_CHECK_EQ(m.captures[1].first, std::string("y"));
    LT_CHECK_EQ(m.captures[1].second, std::string("2"));
    // Too short: missing trailing /c
    LT_CHECK(!tree.find("/a/1/b/2", m));
LT_END_AUTO_TEST(segment_trie_matches_multiple_parameterized_segments)

LT_BEGIN_AUTO_TEST(route_table_suite, cache_duplicate_insert_replaces_in_place_and_keeps_size)
    htd::route_cache cache(4);
    htd::cache_value v1;
    v1.entry = make_entry(70);
    htd::cache_value v2;
    v2.entry = make_entry(71);
    // First insert: key A with value from entry 70.
    cache.insert({ht::http_method::get, "/dup"}, v1);
    LT_CHECK_EQ(cache.size(), static_cast<std::size_t>(1));
    // Second insert with same key, different value.
    cache.insert({ht::http_method::get, "/dup"}, v2);
    // Size must stay 1 — replace in place, no new list node.
    LT_CHECK_EQ(cache.size(), static_cast<std::size_t>(1));
    htd::cache_value out;
    LT_CHECK(cache.find({ht::http_method::get, "/dup"}, out));
LT_END_AUTO_TEST(cache_duplicate_insert_replaces_in_place_and_keeps_size)

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_remove_exact_path)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/users/42", make_entry(1));
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(tree.find("/users/42", m));
    LT_CHECK(tree.remove("/users/42", /*is_prefix=*/false));
    LT_CHECK(!tree.find("/users/42", m));
    // Removing twice is a no-op (returns false).
    LT_CHECK(!tree.remove("/users/42", /*is_prefix=*/false));
LT_END_AUTO_TEST(segment_trie_remove_exact_path)

// ----- Cycle E: route_cache ----------------------------------------------

LT_BEGIN_AUTO_TEST(route_table_suite, cache_inserts_and_finds)
    htd::route_cache cache(256);
    htd::cache_value v;
    v.entry = make_entry(50);
    cache.insert({ht::http_method::get, "/a"}, v);
    htd::cache_value out;
    LT_CHECK(cache.find({ht::http_method::get, "/a"}, out));
    LT_CHECK(!cache.find({ht::http_method::get, "/b"}, out));
LT_END_AUTO_TEST(cache_inserts_and_finds)

LT_BEGIN_AUTO_TEST(route_table_suite, cache_distinguishes_method)
    htd::route_cache cache(256);
    htd::cache_value v1{};
    v1.entry = make_entry(60);
    htd::cache_value v2{};
    v2.entry = make_entry(61);
    cache.insert({ht::http_method::get, "/a"}, v1);
    cache.insert({ht::http_method::post, "/a"}, v2);
    htd::cache_value out;
    LT_CHECK(cache.find({ht::http_method::get, "/a"}, out));
    LT_CHECK(cache.find({ht::http_method::post, "/a"}, out));
    LT_CHECK(!cache.find({ht::http_method::put, "/a"}, out));
LT_END_AUTO_TEST(cache_distinguishes_method)

LT_BEGIN_AUTO_TEST(route_table_suite, cache_hit_promotes_to_front_evicts_oldest)
    htd::route_cache cache(3);
    htd::cache_value v;
    cache.insert({ht::http_method::get, "/a"}, v);
    cache.insert({ht::http_method::get, "/b"}, v);
    cache.insert({ht::http_method::get, "/c"}, v);
    // Promote /a to the front.
    htd::cache_value out;
    LT_CHECK(cache.find({ht::http_method::get, "/a"}, out));
    // Insert /d -> evicts the oldest non-A entry, /b.
    cache.insert({ht::http_method::get, "/d"}, v);
    LT_CHECK(cache.find({ht::http_method::get, "/a"}, out));
    LT_CHECK(!cache.find({ht::http_method::get, "/b"}, out));
    LT_CHECK(cache.find({ht::http_method::get, "/c"}, out));
    LT_CHECK(cache.find({ht::http_method::get, "/d"}, out));
LT_END_AUTO_TEST(cache_hit_promotes_to_front_evicts_oldest)

LT_BEGIN_AUTO_TEST(route_table_suite, cache_clear_empties_storage)
    htd::route_cache cache(8);
    htd::cache_value v;
    cache.insert({ht::http_method::get, "/a"}, v);
    cache.insert({ht::http_method::get, "/b"}, v);
    cache.clear();
    htd::cache_value out;
    LT_CHECK(!cache.find({ht::http_method::get, "/a"}, out));
    LT_CHECK(!cache.find({ht::http_method::get, "/b"}, out));
LT_END_AUTO_TEST(cache_clear_empties_storage)

// find_by_view avoids constructing
// a cache_key on the warm-cache hot path. The view overload must return
// identical results to the key-based overload.
LT_BEGIN_AUTO_TEST(route_table_suite, cache_find_by_view_matches_find_by_key)
    htd::route_cache cache(8);
    htd::cache_value v;
    v.entry = make_entry(99);
    cache.insert({ht::http_method::get, "/view-path"}, v);

    htd::cache_value out_key;
    LT_CHECK(cache.find({ht::http_method::get, "/view-path"}, out_key));

    htd::cache_value out_view;
    std::string_view pv{"/view-path"};
    LT_CHECK(cache.find_by_view(ht::http_method::get, pv, out_view));

    // Miss: different path.
    std::string_view miss{"/other"};
    htd::cache_value out_miss;
    LT_CHECK(!cache.find_by_view(ht::http_method::get, miss, out_miss));

    // Miss: same path, different method.
    LT_CHECK(!cache.find_by_view(ht::http_method::post, pv, out_miss));
LT_END_AUTO_TEST(cache_find_by_view_matches_find_by_key)

// Security #6 (route_cache.hpp:94): max_entries=0 silently disables caching,
// which could mask DoS-mitigation behavior. Precondition check rejects it.
LT_BEGIN_AUTO_TEST(route_table_suite, cache_zero_max_entries_throws)
    bool threw = false;
    try {
        htd::route_cache cache(0);
        (void)cache;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(cache_zero_max_entries_throws)

// Minor #30: multiple parameterised segments coverage.
// (This test already exists at segment_trie_matches_multiple_parameterized_segments)

// Minor #30: removing a parameterised path does not clear sibling exact terminus.
LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_remove_parameterized_path)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/users/{id}", make_entry(40));
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(tree.find("/users/42", m));
    LT_CHECK(tree.remove("/users/{id}", /*is_prefix=*/false));
    LT_CHECK(!tree.find("/users/42", m));
    // Removing a non-existent parameterised path returns false.
    LT_CHECK(!tree.remove("/users/{id}", /*is_prefix=*/false));
LT_END_AUTO_TEST(segment_trie_remove_parameterized_path)

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_remove_prefix_path)
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/static",
                make_entry(41, ht::method_set{}.set(ht::http_method::get),
                           /*is_prefix=*/true),
                /*is_prefix=*/true);
    htd::segment_trie_match<htd::route_entry> m;
    LT_CHECK(tree.find("/static/css", m));
    LT_CHECK(tree.remove("/static", /*is_prefix=*/true));
    LT_CHECK(!tree.find("/static/css", m));
LT_END_AUTO_TEST(segment_trie_remove_prefix_path)

LT_BEGIN_AUTO_TEST(route_table_suite, segment_trie_remove_one_terminus_does_not_clear_sibling)
    // /static has both an exact terminus and a prefix terminus.
    // Removing the exact one must not clear the prefix one.
    htd::segment_trie<htd::route_entry> tree;
    tree.insert("/static", make_entry(42),          /*is_prefix=*/false);
    tree.insert("/static",
                make_entry(43, ht::method_set{}.set(ht::http_method::get),
                           /*is_prefix=*/true),
                /*is_prefix=*/true);
    htd::segment_trie_match<htd::route_entry> m;
    // Remove the exact terminus.
    LT_CHECK(tree.remove("/static", /*is_prefix=*/false));
    // Subpath still served by prefix terminus.
    LT_CHECK(tree.find("/static/js", m));
    LT_CHECK(m.is_prefix_match);
LT_END_AUTO_TEST(segment_trie_remove_one_terminus_does_not_clear_sibling)

// find_by_view must promote the hit to the front of the LRU list,
// just like find(). After a find_by_view hit on /a, inserting /d into a
// capacity-3 cache must evict the true LRU entry (/b), not /a.
LT_BEGIN_AUTO_TEST(route_table_suite, cache_find_by_view_promotes_to_front)
    htd::route_cache cache(3);
    htd::cache_value v;
    cache.insert({ht::http_method::get, "/a"}, v);
    cache.insert({ht::http_method::get, "/b"}, v);
    cache.insert({ht::http_method::get, "/c"}, v);
    // Promote /a to front via find_by_view.
    htd::cache_value out;
    std::string_view pv_a{"/a"};
    LT_CHECK(cache.find_by_view(ht::http_method::get, pv_a, out));
    // Insert /d -> evicts oldest non-A entry (/b).
    cache.insert({ht::http_method::get, "/d"}, v);
    htd::cache_value probe;
    LT_CHECK(cache.find({ht::http_method::get, "/a"}, probe));
    LT_CHECK(!cache.find({ht::http_method::get, "/b"}, probe));
    LT_CHECK(cache.find({ht::http_method::get, "/c"}, probe));
    LT_CHECK(cache.find({ht::http_method::get, "/d"}, probe));
LT_END_AUTO_TEST(cache_find_by_view_promotes_to_front)

// ----- Class-level route_table tests --------------------------------------
//
// Everything above drives the underlying primitives (segment_trie /
// route_cache) directly. The route_table class itself -- its
// register_v2_route / lookup_v2 orchestration, the table-before-cache
// lock discipline documented on lock_for_write(), and the duplicate/
// collision-rejection logic -- was previously only exercised indirectly
// through webserver_impl's lookup_v2/invalidate_route_cache forwarders
// (routing_regression_test.cpp, route_table_concurrency.cpp). These
// tests construct httpserver::detail::route_table directly.

LT_BEGIN_AUTO_TEST(route_table_suite,
                   route_table_register_exact_then_lookup_v2_hits)
    htd::route_table rt;
    htd::http_endpoint idx("/exact/path", /*family=*/false,
                           /*registration=*/true, /*use_regex=*/false);
    rt.register_v2_route(idx, std::make_shared<noop_route_resource>(),
                         /*family=*/false);

    auto result = rt.lookup_v2(ht::http_method::get, "/exact/path");
    LT_CHECK(result.found);
    LT_CHECK(result.tier == htd::route_table::tier_hit::exact);
    LT_CHECK(result.captured_params.empty());
LT_END_AUTO_TEST(route_table_register_exact_then_lookup_v2_hits)

LT_BEGIN_AUTO_TEST(route_table_suite,
                   route_table_register_parameterized_then_lookup_v2_captures)
    htd::route_table rt;
    htd::http_endpoint idx("/users/{id}", /*family=*/false,
                           /*registration=*/true, /*use_regex=*/false);
    rt.register_v2_route(idx, std::make_shared<noop_route_resource>(),
                         /*family=*/false);

    auto result = rt.lookup_v2(ht::http_method::get, "/users/42");
    LT_CHECK(result.found);
    LT_CHECK(result.tier == htd::route_table::tier_hit::radix);
    LT_CHECK_EQ(result.captured_params.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(result.captured_params[0].first, std::string("id"));
    LT_CHECK_EQ(result.captured_params[0].second, std::string("42"));
LT_END_AUTO_TEST(route_table_register_parameterized_then_lookup_v2_captures)

LT_BEGIN_AUTO_TEST(route_table_suite, route_table_lookup_v2_miss_reports_none)
    htd::route_table rt;
    auto result = rt.lookup_v2(ht::http_method::get, "/never/registered");
    LT_CHECK(!result.found);
    LT_CHECK(result.tier == htd::route_table::tier_hit::none);
LT_END_AUTO_TEST(route_table_lookup_v2_miss_reports_none)

// register_v2_route's documented atomicity contract: a same-kind
// duplicate registration throws BEFORE any mutation, and the throw
// happens on the SAME call (reject_duplicate_v2_entry_ runs inside the
// single unique_lock window register_v2_route takes internally).
LT_BEGIN_AUTO_TEST(route_table_suite,
                   route_table_register_duplicate_exact_throws)
    htd::route_table rt;
    htd::http_endpoint idx("/dup", /*family=*/false,
                           /*registration=*/true, /*use_regex=*/false);
    rt.register_v2_route(idx, std::make_shared<noop_route_resource>(),
                         /*family=*/false);

    bool threw = false;
    try {
        rt.register_v2_route(idx, std::make_shared<noop_route_resource>(),
                             /*family=*/false);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);

    // The original registration must survive the rejected duplicate.
    auto result = rt.lookup_v2(ht::http_method::get, "/dup");
    LT_CHECK(result.found);
LT_END_AUTO_TEST(route_table_register_duplicate_exact_throws)

// The (method, path) cache key cannot discriminate an exact terminus
// from a prefix terminus at the same canonical path, so registering a
// prefix (family=true) at a path already holding an exact route (or
// vice versa) must throw -- reject_terminus_collision, invoked from
// register_v2_route before any mutation.
LT_BEGIN_AUTO_TEST(route_table_suite,
                   route_table_register_prefix_over_exact_throws_collision)
    htd::route_table rt;
    htd::http_endpoint exact_idx("/static", /*family=*/false,
                                 /*registration=*/true, /*use_regex=*/false);
    rt.register_v2_route(exact_idx, std::make_shared<noop_route_resource>(),
                         /*family=*/false);

    htd::http_endpoint prefix_idx("/static", /*family=*/true,
                                  /*registration=*/true, /*use_regex=*/false);
    bool threw = false;
    try {
        rt.register_v2_route(prefix_idx, std::make_shared<noop_route_resource>(),
                             /*family=*/true);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(route_table_register_prefix_over_exact_throws_collision)

// lock_for_write() + the "locked" primitives (find_v2_entry_by_path_ /
// upsert_v2_table_entry_locked_) pin the class's documented
// caller-orchestrated sequence: webserver_impl's on_*/route path holds
// this lock across "probe -> shim-create -> commit -> upsert" so the
// two are atomic against concurrent registration/dispatch. This test
// drives that sequence directly and then -- critically -- checks that
// lookup_v2 (a fully independent shared-lock acquisition) succeeds once
// the scoped lock is released. If lock_for_write()'s unique_lock were
// ever leaked past its scope, this would deadlock instead of returning.
LT_BEGIN_AUTO_TEST(route_table_suite,
                   route_table_lock_for_write_probe_then_upsert_then_lookup)
    htd::route_table rt;
    htd::http_endpoint idx("/orchestrated", /*family=*/false,
                           /*registration=*/true, /*use_regex=*/false);
    {
        auto lock = rt.lock_for_write();
        const htd::route_entry* existing = rt.find_v2_entry_by_path_(idx);
        LT_CHECK(existing == nullptr);

        auto shim = std::make_shared<noop_route_resource>();
        rt.upsert_v2_table_entry_locked_(
            idx, ht::method_set{}.set(ht::http_method::get), shim,
            /*fresh=*/true);
    }  // lock released here.

    auto result = rt.lookup_v2(ht::http_method::get, "/orchestrated");
    LT_CHECK(result.found);
    LT_CHECK(result.tier == htd::route_table::tier_hit::exact);
LT_END_AUTO_TEST(route_table_lock_for_write_probe_then_upsert_then_lookup)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
