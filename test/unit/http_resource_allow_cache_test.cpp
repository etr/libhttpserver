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

// TASK-058 step 3: lazy Allow-header cache on http_resource.
//
// Goal: on the 405 (method-not-allowed) dispatch path, the Allow
// header value is rebuilt with detail::format_allow_header() on every
// request, allocating a fresh std::string each time.  Step 3 attaches
// a lazy cache to http_resource so subsequent 405s on the same
// resource return the previously-computed string by reference.
//
// Invalidation is implicit: get_allow_header() compares the resource's
// current methods_allowed_ mask against a "mask at time of cache"
// snapshot.  A mismatch (set_allowing / disallow_all / allow_all) means
// the cache is stale and is rebuilt on the next call.  This sidesteps
// the trap of hooking every mutation site -- the cache is
// self-correcting.
//
// Two pins:
//   1. Correctness -- the value matches what format_allow_header()
//      would have returned, AND mutating the mask invalidates the
//      cache so the next call returns an updated value.
//   2. Identity -- two consecutive calls without any mutation return
//      the same string contents AND the cache buffer pointer is
//      stable across calls (no reallocation per request -- the actual
//      hot-path win).
//
// Thread-safety is covered by the per-resource mutex inside
// get_allow_header(); a stress test for races is not in scope here
// (the warm-path bench drives the cache hard enough for tsan to see
// any latent race).

#include <memory>
#include <mutex>
#include <string>

#include "./httpserver.hpp"
#include "./httpserver/detail/method_utils.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;

namespace {

// Subclass that just exposes get_allow_header() unchanged.  The base
// class methods set_allowing / disallow_all / allow_all are public
// already so we can drive mutations directly through the resource.
class allow_cache_resource : public ht::http_resource {
 public:
    ht::http_response render_get(const ht::http_request&) override {
        return ht::http_response::string("ok");
    }
};

}  // namespace

LT_BEGIN_SUITE(http_resource_allow_cache_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http_resource_allow_cache_suite)

// ---------------------------------------------------------------------
// (1) Correctness: cached value matches format_allow_header() on a
// fresh (default-mask) resource.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_resource_allow_cache_suite,
                   default_mask_cached_value_matches_format_allow_header)
    allow_cache_resource r;
    const std::string expected =
        ht::detail::format_allow_header(r.get_allowed_methods());
    const std::string& cached = r.get_allow_header();
    LT_CHECK_EQ(cached, expected);
LT_END_AUTO_TEST(default_mask_cached_value_matches_format_allow_header)

// ---------------------------------------------------------------------
// (1b) Correctness: mutating the mask invalidates the cache.  The
// returned value after set_allowing(POST, false) must no longer name
// "POST".
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_resource_allow_cache_suite,
                   mask_mutation_invalidates_cache)
    allow_cache_resource r;
    // Prime the cache while POST is allowed.
    {
        const std::string& first = r.get_allow_header();
        LT_CHECK(first.find("POST") != std::string::npos);
    }
    // Mutate.
    r.set_allowing(ht::http_method::post, false);
    // The next read must see the new mask.
    const std::string& second = r.get_allow_header();
    LT_CHECK(second.find("POST") == std::string::npos);
    // And the contents must agree with a fresh format_allow_header() call.
    LT_CHECK_EQ(second,
                ht::detail::format_allow_header(r.get_allowed_methods()));
LT_END_AUTO_TEST(mask_mutation_invalidates_cache)

// ---------------------------------------------------------------------
// (1c) Correctness: invalidation also fires for disallow_all and
// allow_all (the two whole-mask APIs).
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_resource_allow_cache_suite,
                   disallow_all_then_allow_all_invalidates_cache)
    allow_cache_resource r;
    (void)r.get_allow_header();  // prime
    r.disallow_all();
    LT_CHECK_EQ(r.get_allow_header(), std::string{});
    r.allow_all();
    LT_CHECK_EQ(r.get_allow_header(),
                ht::detail::format_allow_header(r.get_allowed_methods()));
LT_END_AUTO_TEST(disallow_all_then_allow_all_invalidates_cache)

// ---------------------------------------------------------------------
// (2) Identity: two consecutive calls with no mutation return the
// same buffer.  This is what makes the cache an actual cache rather
// than a "recompute and return by reference" no-op.  The buffer
// pointer (data()) must match across calls; the size and contents
// must too.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_resource_allow_cache_suite,
                   consecutive_calls_return_same_buffer)
    allow_cache_resource r;
    const char* first_data = r.get_allow_header().data();
    const std::size_t first_size = r.get_allow_header().size();
    const char* second_data = r.get_allow_header().data();
    const std::size_t second_size = r.get_allow_header().size();
    LT_CHECK_EQ(first_data, second_data);
    LT_CHECK_EQ(first_size, second_size);
LT_END_AUTO_TEST(consecutive_calls_return_same_buffer)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
