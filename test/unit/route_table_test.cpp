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

// TASK-027: unit tests for the bespoke segment-trie route storage and
// the LRU route cache. Exercises insert/find for exact, parameterized
// and prefix routes (trie), and cache key equality, LRU promotion,
// method-distinct keys (cache).

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./httpserver/detail/radix_tree.hpp"
#include "./httpserver/detail/route_cache.hpp"
#include "./httpserver/detail/route_entry.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;
namespace htd = httpserver::detail;

// Sentinel makers to avoid crafting a real http_resource just to populate
// the variant; we leave the lambda_handler default-constructed (empty
// std::function) and ride on the sentinel id field in the test below.
// The variant arm itself is irrelevant — the radix_tree / route_cache
// tests only care about the entry payload identity through pointer
// equality and the methods/is_prefix fields.
struct test_entry {
    int sentinel = 0;
    ht::method_set methods{};
    bool is_prefix = false;
};

static htd::route_entry make_entry(
        int /*sentinel*/,
        ht::method_set ms = ht::method_set{}.set(ht::http_method::get),
        bool is_prefix = false) {
    htd::route_entry e;
    e.methods = ms;
    e.is_prefix = is_prefix;
    // Leave variant alternative #0 (lambda_handler) default-constructed.
    return e;
}

LT_BEGIN_SUITE(route_table_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(route_table_suite)

// ----- Cycle B: exact path insertion / lookup ----------------------------

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_inserts_exact_path_and_finds_it)
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/users/42", make_entry(1));
    htd::radix_match<htd::route_entry> m;
    LT_CHECK(tree.find("/users/42", m));
    LT_CHECK(m.entry != nullptr);
    LT_CHECK(m.captures.empty());
    LT_CHECK(!tree.find("/users/43", m));
LT_END_AUTO_TEST(radix_tree_inserts_exact_path_and_finds_it)

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_root_path_finds_root)
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/", make_entry(7));
    htd::radix_match<htd::route_entry> m;
    LT_CHECK(tree.find("/", m));
    LT_CHECK(m.entry != nullptr);
LT_END_AUTO_TEST(radix_tree_root_path_finds_root)

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_no_partial_match)
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/users", make_entry(2));
    htd::radix_match<htd::route_entry> m;
    LT_CHECK(!tree.find("/users/42", m));      // exact miss (not a prefix)
    LT_CHECK(!tree.find("/userzzz", m));        // wrong segment
LT_END_AUTO_TEST(radix_tree_no_partial_match)

// ----- Cycle C: parameterized routes -------------------------------------

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_matches_parameterized_segment_and_captures)
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/users/{id}/posts", make_entry(3));
    htd::radix_match<htd::route_entry> m;
    LT_CHECK(tree.find("/users/42/posts", m));
    LT_CHECK_EQ(m.captures.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(m.captures[0].first, std::string("id"));
    LT_CHECK_EQ(m.captures[0].second, std::string("42"));
LT_END_AUTO_TEST(radix_tree_matches_parameterized_segment_and_captures)

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_parameterized_too_short_returns_null)
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/users/{id}/posts", make_entry(3));
    htd::radix_match<htd::route_entry> m;
    LT_CHECK(!tree.find("/users/42", m));
LT_END_AUTO_TEST(radix_tree_parameterized_too_short_returns_null)

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_exact_shadows_wildcard)
    // Both /users/me (exact) and /users/{id} (parameterized) registered.
    // /users/me must hit exact; /users/42 must hit parameterized.
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/users/me", make_entry(10));
    tree.insert("/users/{id}", make_entry(11));
    htd::radix_match<htd::route_entry> m;
    LT_CHECK(tree.find("/users/me", m));
    LT_CHECK(m.captures.empty());
    LT_CHECK(tree.find("/users/42", m));
    LT_CHECK_EQ(m.captures.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(m.captures[0].second, std::string("42"));
LT_END_AUTO_TEST(radix_tree_exact_shadows_wildcard)

// ----- Cycle D: prefix routes --------------------------------------------

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_prefix_match_serves_subpaths_and_bare_path)
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/static",
                make_entry(20, ht::method_set{}.set(ht::http_method::get),
                           /*is_prefix=*/true),
                /*is_prefix=*/true);
    htd::radix_match<htd::route_entry> m;
    LT_CHECK(tree.find("/static", m));
    LT_CHECK(tree.find("/static/foo", m));
    LT_CHECK(tree.find("/static/foo/bar/baz", m));
    LT_CHECK(!tree.find("/staticx", m));        // sibling segment
    LT_CHECK(!tree.find("/", m));                // not a parent
LT_END_AUTO_TEST(radix_tree_prefix_match_serves_subpaths_and_bare_path)

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_more_specific_exact_shadows_prefix)
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/static",
                make_entry(20, ht::method_set{}.set(ht::http_method::get),
                           /*is_prefix=*/true),
                /*is_prefix=*/true);
    tree.insert("/static/foo", make_entry(21));
    htd::radix_match<htd::route_entry> m;

    LT_CHECK(tree.find("/static/foo", m));
    LT_CHECK(!m.is_prefix_match);                   // exact shadows prefix
    LT_CHECK(tree.find("/static/bar", m));
    LT_CHECK(m.is_prefix_match);
LT_END_AUTO_TEST(radix_tree_more_specific_exact_shadows_prefix)

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_matches_multiple_parameterized_segments)
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/a/{x}/b/{y}/c", make_entry(30));
    htd::radix_match<htd::route_entry> m;
    LT_CHECK(tree.find("/a/1/b/2/c", m));
    LT_CHECK_EQ(m.captures.size(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(m.captures[0].first, std::string("x"));
    LT_CHECK_EQ(m.captures[0].second, std::string("1"));
    LT_CHECK_EQ(m.captures[1].first, std::string("y"));
    LT_CHECK_EQ(m.captures[1].second, std::string("2"));
    // Too short: missing trailing /c
    LT_CHECK(!tree.find("/a/1/b/2", m));
LT_END_AUTO_TEST(radix_tree_matches_multiple_parameterized_segments)

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

LT_BEGIN_AUTO_TEST(route_table_suite, radix_tree_remove_exact_path)
    htd::radix_tree<htd::route_entry> tree;
    tree.insert("/users/42", make_entry(1));
    htd::radix_match<htd::route_entry> m;
    LT_CHECK(tree.find("/users/42", m));
    LT_CHECK(tree.remove("/users/42", /*is_prefix=*/false));
    LT_CHECK(!tree.find("/users/42", m));
    // Removing twice is a no-op (returns false).
    LT_CHECK(!tree.remove("/users/42", /*is_prefix=*/false));
LT_END_AUTO_TEST(radix_tree_remove_exact_path)

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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
