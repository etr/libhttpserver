/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

// TASK-009 unit test: SBO value-type layout for http_response.
//
// Verifies the type-trait acceptance criteria, the no-PIMPL exemption
// (PRD-HDR-REQ-004), and the four-case move cross-product (inline↔inline,
// inline↔heap, heap↔inline, heap↔heap) plus self-move-assignment safety.
// Compile-time `static_assert`s sit at TU scope so any future drift is
// caught on every build, even if no runtime test references them.
//
// This TU is built with -DHTTPSERVER_COMPILATION so it can reach the
// internal detail::body hierarchy directly — same exemption the body_test
// uses. From a consumer's perspective these layouts are opaque.
//
// All access to http_response's private SBO state goes through
// http_response_sbo_test_access, the single friend struct declared in
// http_response.hpp. The test does not widen any other API surface.

#include <microhttpd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"                  // public umbrella
#include "httpserver/detail/body.hpp"        // private hierarchy
#include "./littletest.hpp"

using httpserver::http_response;
using httpserver::body_kind;
using httpserver::detail::body;
using httpserver::detail::empty_body;
using httpserver::detail::string_body;

// -----------------------------------------------------------------------
// Compile-time AC enforcement.
// -----------------------------------------------------------------------
static_assert(std::is_nothrow_move_constructible_v<http_response>,
              "TASK-009 AC: move ctor must be noexcept");
static_assert(std::is_nothrow_move_assignable_v<http_response>,
              "TASK-009 AC: move assign must be noexcept (DR-005)");
static_assert(!std::is_copy_constructible_v<http_response>,
              "TASK-009 AC: responses are move-only");
static_assert(!std::is_copy_assignable_v<http_response>,
              "TASK-009 AC: responses are move-only");

// PRD-HDR-REQ-004 exemption: http_response is the explicit non-PIMPL
// value type. The body member is a raw detail::body* (NOT a
// unique_ptr<detail::body>), and there is no PIMPL impl_ pointer.
static_assert(!std::is_same_v<http_response::body_pointer_type,
                              std::unique_ptr<body>>,
              "PRD-HDR-REQ-004 exemption: http_response is not PIMPL");
static_assert(std::is_same_v<http_response::body_pointer_type, body*>,
              "TASK-009: body_pointer_type is detail::body*");

// SBO budget per DR-005.
static_assert(http_response::body_buf_size == 64,
              "DR-005: SBO buffer is 64 bytes");

// http_response carrying alignas(16) std::byte[64] must be aligned >= 16.
static_assert(alignof(http_response) >= 16,
              "alignas(16) on body_storage_ requires class alignment >= 16");

// `final` is deliberately NOT asserted here. TASK-013 picks it up after
// the v1 subclasses are removed.

namespace httpserver {

// Test-only friend: gives the SBO unit test direct access to the SBO
// state without leaking private members through accessors. Declared as a
// friend in http_response.hpp; defined here so it is an implementation
// detail of this TU and adds zero footprint to the production API.
struct http_response_sbo_test_access {
    static body*& body_ptr(http_response& r) noexcept { return r.body_; }
    static bool& body_inline(http_response& r) noexcept {
        return r.body_inline_;
    }
    static body_kind& kind(http_response& r) noexcept { return r.kind_; }
    static std::byte* storage(http_response& r) noexcept {
        return r.body_storage_;
    }
};

}  // namespace httpserver

namespace {

using SBO = httpserver::http_response_sbo_test_access;

// Place a string_body into r's inline storage and wire the response
// fields up. `r` must be empty (default-constructed).
void place_inline_string(http_response& r, std::string content) {
    ::new (SBO::storage(r)) string_body(std::move(content));
    SBO::body_ptr(r) = reinterpret_cast<body*>(SBO::storage(r));
    SBO::body_inline(r) = true;
    SBO::kind(r) = body_kind::string;
}

// Heap-allocate a string_body via ::operator new + placement-new so it
// matches the destructor's ::operator delete pairing.
void place_heap_string(http_response& r, std::string content) {
    void* mem = ::operator new(sizeof(string_body));
    body* b = ::new (mem) string_body(std::move(content));
    SBO::body_ptr(r) = b;
    SBO::body_inline(r) = false;
    SBO::kind(r) = body_kind::string;
}

// Counter-based body subclass used to verify dtor calls under both
// inline and heap paths. The class needs to fit in the 64-byte SBO
// budget (it does: one int*).
class counter_body final : public body {
 public:
    explicit counter_body(int* counter) noexcept : counter_(counter) {}

    counter_body(counter_body&& o) noexcept
        : body(std::move(o)),
          counter_(std::exchange(o.counter_, nullptr)) {}

    ~counter_body() override {
        if (counter_) ++*counter_;
    }

    body_kind kind() const noexcept override { return body_kind::empty; }
    std::size_t size() const noexcept override { return 0; }
    MHD_Response* materialize() override { return nullptr; }

    void move_into(void* dst) noexcept override {
        ::new (dst) counter_body(std::move(*this));
    }

 private:
    int* counter_;
};

void place_inline_counter(http_response& r, int* counter) {
    ::new (SBO::storage(r)) counter_body(counter);
    SBO::body_ptr(r) = reinterpret_cast<body*>(SBO::storage(r));
    SBO::body_inline(r) = true;
    SBO::kind(r) = body_kind::empty;
}

void place_heap_counter(http_response& r, int* counter) {
    void* mem = ::operator new(sizeof(counter_body));
    body* b = ::new (mem) counter_body(counter);
    SBO::body_ptr(r) = b;
    SBO::body_inline(r) = false;
    SBO::kind(r) = body_kind::empty;
}

}  // namespace

LT_BEGIN_SUITE(http_response_sbo_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http_response_sbo_suite)

// -----------------------------------------------------------------------
// Move-construction: inline source.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_sbo_suite, move_ctor_inline_source)
    http_response src;
    place_inline_string(src, "hello");

    http_response dst(std::move(src));

    LT_CHECK_EQ(SBO::body_inline(dst), true);
    LT_ASSERT_NEQ(SBO::body_ptr(dst), static_cast<body*>(nullptr));
    LT_CHECK_EQ(static_cast<int>(SBO::kind(dst)),
                static_cast<int>(body_kind::string));
    // dst's body must point INTO dst's inline buffer, not into src's.
    LT_CHECK_EQ(reinterpret_cast<void*>(SBO::body_ptr(dst)),
                reinterpret_cast<void*>(SBO::storage(dst)));
    // src must be torn down so its destructor is a no-op.
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
    LT_CHECK_EQ(SBO::body_inline(src), false);
LT_END_AUTO_TEST(move_ctor_inline_source)

// -----------------------------------------------------------------------
// Move-construction: heap source. Pointer ownership transfers; no
// allocation/deallocation of the body itself happens during the move.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_sbo_suite, move_ctor_heap_source)
    http_response src;
    place_heap_string(src, "world");
    body* original_ptr = SBO::body_ptr(src);

    http_response dst(std::move(src));

    LT_CHECK_EQ(SBO::body_inline(dst), false);
    LT_CHECK_EQ(SBO::body_ptr(dst), original_ptr);
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
LT_END_AUTO_TEST(move_ctor_heap_source)

// -----------------------------------------------------------------------
// Move-assignment 4-case cross product.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_sbo_suite, move_assign_inline_to_inline)
    http_response dst;
    http_response src;
    place_inline_string(dst, "old");
    place_inline_string(src, "new");

    dst = std::move(src);

    LT_CHECK_EQ(SBO::body_inline(dst), true);
    LT_ASSERT_NEQ(SBO::body_ptr(dst), static_cast<body*>(nullptr));
    LT_CHECK_EQ(reinterpret_cast<void*>(SBO::body_ptr(dst)),
                reinterpret_cast<void*>(SBO::storage(dst)));
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
LT_END_AUTO_TEST(move_assign_inline_to_inline)

LT_BEGIN_AUTO_TEST(http_response_sbo_suite, move_assign_inline_to_heap)
    http_response dst;
    http_response src;
    place_inline_string(dst, "old-inline");
    place_heap_string(src, "new-heap");
    body* heap_ptr = SBO::body_ptr(src);

    dst = std::move(src);

    LT_CHECK_EQ(SBO::body_inline(dst), false);
    LT_CHECK_EQ(SBO::body_ptr(dst), heap_ptr);
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
LT_END_AUTO_TEST(move_assign_inline_to_heap)

LT_BEGIN_AUTO_TEST(http_response_sbo_suite, move_assign_heap_to_inline)
    http_response dst;
    http_response src;
    place_heap_string(dst, "old-heap");
    place_inline_string(src, "new-inline");

    dst = std::move(src);

    LT_CHECK_EQ(SBO::body_inline(dst), true);
    LT_CHECK_EQ(reinterpret_cast<void*>(SBO::body_ptr(dst)),
                reinterpret_cast<void*>(SBO::storage(dst)));
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
LT_END_AUTO_TEST(move_assign_heap_to_inline)

LT_BEGIN_AUTO_TEST(http_response_sbo_suite, move_assign_heap_to_heap)
    http_response dst;
    http_response src;
    place_heap_string(dst, "old-heap");
    place_heap_string(src, "new-heap");
    body* new_ptr = SBO::body_ptr(src);

    dst = std::move(src);

    LT_CHECK_EQ(SBO::body_inline(dst), false);
    LT_CHECK_EQ(SBO::body_ptr(dst), new_ptr);
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
LT_END_AUTO_TEST(move_assign_heap_to_heap)

// -----------------------------------------------------------------------
// Destructor: inline body's dtor runs but no `delete` is invoked. ASan
// would catch a stray free on a non-heap pointer.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_sbo_suite,
                   destructor_inline_calls_dtor_no_delete)
    int dtor_count = 0;
    {
        http_response r;
        place_inline_counter(r, &dtor_count);
    }
    LT_CHECK_EQ(dtor_count, 1);
LT_END_AUTO_TEST(destructor_inline_calls_dtor_no_delete)

// -----------------------------------------------------------------------
// Destructor: heap body's dtor runs and the body memory is freed.
// ASan/UBSan are the canary for a missing free or a double free.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_sbo_suite,
                   destructor_heap_calls_dtor_and_delete)
    int dtor_count = 0;
    {
        http_response r;
        place_heap_counter(r, &dtor_count);
    }
    LT_CHECK_EQ(dtor_count, 1);
LT_END_AUTO_TEST(destructor_heap_calls_dtor_and_delete)

// -----------------------------------------------------------------------
// Self-move-assign safety: the standard move-assign defect.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_sbo_suite, self_move_assign_safe)
    int dtor_count = 0;
    http_response r;
    place_inline_counter(r, &dtor_count);

    // Aliased through a reference to defeat -Wself-move on clang/gcc.
    http_response& alias = r;
    r = std::move(alias);

    // Body must still be valid; dtor must not have fired yet.
    LT_CHECK_EQ(dtor_count, 0);
    LT_ASSERT_NEQ(SBO::body_ptr(r), static_cast<body*>(nullptr));
LT_END_AUTO_TEST(self_move_assign_safe)

// -----------------------------------------------------------------------
// Header/footer/cookie fields move with the rest of the response.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_sbo_suite, headers_move_with_response)
    http_response src(201, "application/json");
    src.with_header("X-Trace", "abc123");
    src.with_footer("X-Footer", "fv");
    src.with_cookie("Sess", "ck");

    http_response dst(std::move(src));

    LT_CHECK_EQ(dst.get_response_code(), 201);
    LT_CHECK_EQ(dst.get_header("X-Trace"), "abc123");
    LT_CHECK_EQ(dst.get_footer("X-Footer"), "fv");
    LT_CHECK_EQ(dst.get_cookie("Sess"), "ck");
LT_END_AUTO_TEST(headers_move_with_response)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
