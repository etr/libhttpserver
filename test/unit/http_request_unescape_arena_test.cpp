/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-072: arena-allocated unescape on the warm path.
//
// Two batches of tests in this TU:
//   1. zero-global-heap-allocation pins (headline). build_request_args
//      with a value larger than the std::string SSO threshold must NOT
//      touch the global heap on the warm path. Today the call site
//      copies the MHD-owned C string into a `std::string value(val_sv)`
//      and feeds that to the C-style unescaper -- a guaranteed
//      global-heap allocation when the value exceeds SSO. After the
//      fix, the unescape output is materialised in the per-connection
//      arena, so the warm-path count stays flat.
//
//      Mechanism: we install a global `operator new` counter (RAII
//      guard) around the warm cycle. The arena-backed pmr::string
//      destination uses the arena's polymorphic_allocator and does NOT
//      route through global new, so a clean implementation registers
//      zero increments during the measured window.
//
//   2. correctness + lifetime pins on the new arena-routed path:
//      - %2F decoded to '/'
//      - invalid hex passthrough ("%%", trailing "%")
//      - empty input
//      - the returned string_view aliases arena-backed storage and stays
//        valid across subsequent inserts (until the request completes).
//      - user-callback can grow the value (e.g. expander prepends a
//        prefix) and the result is observable in the args map.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory_resource>
#include <new>
#include <string>
#include <string_view>

// HTTPSERVER_COMPILATION supplied by test/Makefile.am AM_CPPFLAGS.
#include "httpserver/detail/http_request_impl.hpp"

#include "./littletest.hpp"

// Global heap-allocation counter. We override the four primary forms
// of `operator new` so any libc-heap call from the libhttpserver code
// path during the measured window is observed. The counter is gated
// by `g_count_new_enabled` so SUITE setup / instrumentation noise from
// outside the measured window is ignored.
//
// We deliberately count *all* sized + nothrow + aligned forms; any
// global heap allocation from build_request_args triggers an
// increment regardless of which overload the standard library picks.
namespace {
std::atomic<std::size_t> g_new_count{0};
std::atomic<bool> g_count_new_enabled{false};
}  // namespace

void* operator new(std::size_t n) {
    if (g_count_new_enabled.load(std::memory_order_relaxed)) {
        g_new_count.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) {
    if (g_count_new_enabled.load(std::memory_order_relaxed)) {
        g_new_count.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (g_count_new_enabled.load(std::memory_order_relaxed)) {
        g_new_count.fetch_add(1, std::memory_order_relaxed);
    }
    return std::malloc(n == 0 ? 1 : n);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    if (g_count_new_enabled.load(std::memory_order_relaxed)) {
        g_new_count.fetch_add(1, std::memory_order_relaxed);
    }
    return std::malloc(n == 0 ? 1 : n);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

// RAII guard that enables global-new counting on construction and
// disables it on destruction. Reset the counter on construction so
// each measured window starts from zero.
struct count_new_window {
    count_new_window() {
        g_new_count.store(0, std::memory_order_relaxed);
        g_count_new_enabled.store(true, std::memory_order_relaxed);
    }
    ~count_new_window() {
        g_count_new_enabled.store(false, std::memory_order_relaxed);
    }
    std::size_t count() const {
        return g_new_count.load(std::memory_order_relaxed);
    }
};

// Counting upstream resource. Wraps new_delete_resource and bumps a
// counter on every do_allocate. Used to assert that the warm-path
// build_request_args call does not spill out of the arena.
// (Same shape as assert_no_upstream_resource in http_request_arena_test.cpp.)
class assert_no_upstream_resource final : public std::pmr::memory_resource {
 public:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        ++upstream_alloc_count_;
        return std::pmr::new_delete_resource()->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }
    bool do_is_equal(const std::pmr::memory_resource& o) const noexcept override {
        return this == &o;
    }
    std::size_t upstream_alloc_count() const { return upstream_alloc_count_; }

 private:
    std::size_t upstream_alloc_count_ = 0;
};

// Test-only user unescaper: pass-through (does not mutate val). Used
// to exercise the user-unescaper code path without changing semantics.
void passthrough_unescaper(std::string& /*val*/) {
    // intentionally empty
}

// Test-only user unescaper: prepend "X-" so the output is longer than
// the input. Exercises the "user callback can grow" path.
void prepend_x_dash_unescaper(std::string& val) {
    val.insert(0, "X-");
}

// A value larger than std::string's SSO threshold (22 on libc++,
// 15 on libstdc++). Any std::string copy of this value forces a
// global-heap allocation. The "%2F" sequence is included so the
// default unescaper does real work (decodes to '/').
constexpr const char* kLongValueWithPct2F = "a%2Fbcdefghijklmnopqrstuvwxyz_padding_to_force_heap_allocation";  // NOLINT(whitespace/line_length)

// ---------------------------------------------------------------------------
// Helpers shared across the correctness tests (3-6).
//
// decode_via_arena: constructs a fresh arena-backed impl+accumulator,
// calls build_request_args once for (key, raw_value) with no unescaper,
// retrieves the stored value, deletes the impl, and returns the decoded
// std::string.  Each correctness test is a single LT_CHECK_EQ call.
// (code-simplifier-iter1-3)
// ---------------------------------------------------------------------------
using httpserver::detail::http_request_impl;
using httpserver::detail::arguments_accumulator;
using impl_alloc_t = std::pmr::polymorphic_allocator<http_request_impl>;

std::string decode_via_arena(const char* raw_value) {
    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(),
                                              std::pmr::new_delete_resource());
    impl_alloc_t alloc(&arena);
    auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);

    arguments_accumulator aa;
    aa.unescaper = nullptr;
    aa.arguments = &p->unescaped_args;

    http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k", raw_value);

    std::string result;
    auto it = p->unescaped_args.find(std::string_view("k"));
    if (it != p->unescaped_args.end() && !it->second.empty()) {
        result.assign(it->second[0].data(), it->second[0].size());
    }

    alloc.delete_object(p);
    return result;
}

// ---------------------------------------------------------------------------
// Helper for the two headline alloc-pin tests (1) and (2).
//
// run_alloc_pin: constructs a fresh arena-backed impl+accumulator,
// runs `warmup_rounds` cold calls to prime caches and grow any
// thread_local buffers, then opens a count_new_window and runs one
// measured call, returning the observed global-allocation count.
// (code-simplifier-iter1-6)
// ---------------------------------------------------------------------------
std::size_t run_alloc_pin(httpserver::unescaper_ptr fn,
                          int warmup_rounds) {
    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(),
                                              std::pmr::new_delete_resource());
    impl_alloc_t alloc(&arena);
    auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);

    arguments_accumulator aa;
    aa.unescaper = fn;
    aa.arguments = &p->unescaped_args;

    for (int i = 0; i < warmup_rounds; ++i) {
        // Use a unique key per warmup call so each inserts into a fresh
        // map slot; this avoids accumulating a large vector under one key.
        const std::string warmup_key = "warmup" + std::to_string(i);
        http_request_impl::build_request_args(
            &aa, MHD_GET_ARGUMENT_KIND, warmup_key.c_str(),
            kLongValueWithPct2F);
    }

    std::size_t alloc_count = 0;
    {
        count_new_window window;
        http_request_impl::build_request_args(
            &aa, MHD_GET_ARGUMENT_KIND, "key", kLongValueWithPct2F);
        alloc_count = window.count();
    }

    alloc.delete_object(p);
    return alloc_count;
}

}  // namespace

// The suite has no shared fixture state: each test constructs its own
// arena+impl via the helpers above.  set_up/tear_down are present
// (required by the littletest template) but empty; the comments
// signal this is intentional, not an oversight.
// (code-simplifier-iter1-4)
LT_BEGIN_SUITE(http_request_unescape_arena_suite)
    void set_up() {}    // per-test setup is in the helpers above
    void tear_down() {} // per-test teardown is in the helpers above
LT_END_SUITE(http_request_unescape_arena_suite)

// (1) Headline pin -- default unescaper. With a value strictly longer
// than std::string's SSO threshold, the v1 code path copies into a
// std::string temporary that is guaranteed to allocate on the global
// heap. After the TASK-072 fix the unescape destination is materialised
// in the per-connection arena and no global-heap allocation occurs in
// the build_request_args call itself.
//
// One cold warmup call primes the impl-internal caches. The warm call
// must consume only arena memory.
LT_BEGIN_AUTO_TEST(http_request_unescape_arena_suite,
                   warm_path_zero_global_allocs_default_unescape)
    // 1 warmup: primes impl-internal caches; no thread_local to grow on
    // the default-unescaper path.
    LT_CHECK_EQ(run_alloc_pin(nullptr, 1), std::size_t{0});
LT_END_AUTO_TEST(warm_path_zero_global_allocs_default_unescape)

// (2) Headline pin -- user-registered unescaper. Same invariant must
// hold when the user-callback path is exercised. A per-thread scratch
// std::string (thread_value in unescape_in_arena) amortises its capacity
// across requests on the same thread.
//
// Two cold warmup calls are required: the first grows the thread_local
// buffer to the peak value length; the second confirms steady-state. The
// warm call after that must consume no further global heap.
LT_BEGIN_AUTO_TEST(http_request_unescape_arena_suite,
                   warm_path_zero_global_allocs_user_unescape)
    // 2 warmups: first grows thread_value capacity; second confirms it.
    LT_CHECK_EQ(run_alloc_pin(&passthrough_unescaper, 2), std::size_t{0});
LT_END_AUTO_TEST(warm_path_zero_global_allocs_user_unescape)

// (3) Correctness: "%2F" decodes to '/' on the default-unescaper path.
LT_BEGIN_AUTO_TEST(http_request_unescape_arena_suite,
                   unescape_pct2f_decodes_to_slash)
    LT_CHECK_EQ(decode_via_arena("a%2Fb"), std::string("a/b"));
LT_END_AUTO_TEST(unescape_pct2f_decodes_to_slash)

// (4) Invalid hex passthrough: "%%" stays as "%%" (consistent with
// http_unescape's fall-through behavior on non-hex digits after '%').
LT_BEGIN_AUTO_TEST(http_request_unescape_arena_suite,
                   unescape_double_percent_passthrough)
    LT_CHECK_EQ(decode_via_arena("a%%b"), std::string("a%%b"));
LT_END_AUTO_TEST(unescape_double_percent_passthrough)

// (5) Trailing percent: "abc%" stays as "abc%".
LT_BEGIN_AUTO_TEST(http_request_unescape_arena_suite,
                   unescape_trailing_percent_passthrough)
    LT_CHECK_EQ(decode_via_arena("abc%"), std::string("abc%"));
LT_END_AUTO_TEST(unescape_trailing_percent_passthrough)

// (6) Empty value: produces an empty arg without crashing.
LT_BEGIN_AUTO_TEST(http_request_unescape_arena_suite,
                   unescape_empty_value)
    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(),
                                              std::pmr::new_delete_resource());
    impl_alloc_t alloc(&arena);
    auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);

    arguments_accumulator aa;
    aa.unescaper = nullptr;
    aa.arguments = &p->unescaped_args;

    http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k", "");

    auto it = p->unescaped_args.find(std::string_view("k"));
    LT_CHECK(it != p->unescaped_args.end());
    LT_CHECK_EQ(it->second.size(), std::size_t{1});
    LT_CHECK_EQ(it->second[0].size(), std::size_t{0});

    alloc.delete_object(p);
LT_END_AUTO_TEST(unescape_empty_value)

// (7) Lifetime pin: a string_view obtained from unescaped_args after
// one build_request_args call must remain valid after a subsequent call
// inserts another arg, until the request completes. This pins the
// TASK-018 lifetime contract on the arena-routed path.
LT_BEGIN_AUTO_TEST(http_request_unescape_arena_suite,
                   unescape_view_outlives_subsequent_inserts)
    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(),
                                              std::pmr::new_delete_resource());
    impl_alloc_t alloc(&arena);
    auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);

    arguments_accumulator aa;
    aa.unescaper = nullptr;
    aa.arguments = &p->unescaped_args;

    http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k1", "a%2Fb");

    // Capture a view of the stored arg value.
    auto it1 = p->unescaped_args.find(std::string_view("k1"));
    LT_CHECK(it1 != p->unescaped_args.end());
    std::string_view sv(it1->second[0].data(), it1->second[0].size());

    // Insert several more args to force the map to grow (additional
    // arena allocations for the new pmr::string keys + vectors).
    http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k2", "v2%2F");
    http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k3", "another_value_here");
    http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k4", "yet_another_one");

    // The original view must still read "a/b".
    LT_CHECK_EQ(std::string(sv), std::string("a/b"));

    alloc.delete_object(p);
LT_END_AUTO_TEST(unescape_view_outlives_subsequent_inserts)

// (8) User-callback can grow the value (HTML-entity-style expander).
// Correctness pin only: allocation count is not asserted because the
// arena-backed sink may legitimately grow past the input size.
LT_BEGIN_AUTO_TEST(http_request_unescape_arena_suite,
                   unescape_user_callback_can_grow_value)
    alignas(std::max_align_t) std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena(buf.data(), buf.size(),
                                              std::pmr::new_delete_resource());
    impl_alloc_t alloc(&arena);
    auto* p = alloc.new_object<http_request_impl>(nullptr, nullptr, alloc);

    arguments_accumulator aa;
    aa.unescaper = &prepend_x_dash_unescaper;
    aa.arguments = &p->unescaped_args;

    http_request_impl::build_request_args(
        &aa, MHD_GET_ARGUMENT_KIND, "k", "original");

    auto it = p->unescaped_args.find(std::string_view("k"));
    LT_CHECK(it != p->unescaped_args.end());
    LT_CHECK_EQ(std::string(it->second[0].data(), it->second[0].size()),
                std::string("X-original"));

    alloc.delete_object(p);
LT_END_AUTO_TEST(unescape_user_callback_can_grow_value)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
