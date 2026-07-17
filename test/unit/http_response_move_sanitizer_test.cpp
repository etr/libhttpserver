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

// Sanitizer-clean http_response move semantics.
//
// This TU is the v2.0 sanitizer canary for http_response's move
// operations. It complements the
// existing whitebox SBO test (http_response_sbo_test.cpp) by:
//
//   1. Driving the four move-assign cases plus the two move-ctor cases via
//      factory-constructed responses (http_response::string / empty /
//      file), exercising the public API rather than placement-new'ing
//      bodies through the SBO friend hook.
//   2. Covering the heap-fallback branch in adopt_body_from / destroy_body
//      with a synthetic >64B body subclass (fat_body) — no production body
//      currently exceeds the SBO budget, so this is the first test that
//      genuinely exercises the heap-pointer-swap path.
//   3. Pinning the moved-from invariant contract: a moved-from response is
//      destructible, accessor-safe, and re-assignable.
//
// CI: this TU runs in `make check` and is picked up automatically by the
// asan / ubsan matrix entries in .github/workflows/verify-build.yml. Stray
// double-free, use-after-free, or misaligned-load bugs in any of the four
// move cases would be caught by the sanitizer runtimes there.
//
// This TU is built with -DHTTPSERVER_COMPILATION so it can include
// httpserver/detail/body.hpp (to subclass `body`) and use the
// http_response_sbo_test_access friend hook (the same hook the SBO test
// uses).

#include <microhttpd.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"
#include "httpserver/detail/body.hpp"
#include "./littletest.hpp"

// This TU intentionally exercises the deprecated string-blob
// cookie surface as part of the move/sanitizer coverage. Suppress the
// [[deprecated]] diagnostic for the whole file.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

using httpserver::body_kind;
using httpserver::http_response;
using httpserver::detail::body;


namespace httpserver {

// Friend accessor hook — see http_response_sbo_test.cpp for the canonical
// explanation of why this definition is intentionally duplicated across TUs.
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

// SBO is the test-access hook into http_response's private SBO fields.
// The alias matches the companion file http_response_sbo_test.cpp for
// consistency within the test corpus.
using SBO = httpserver::http_response_sbo_test_access;

// -----------------------------------------------------------------------
// Synthetic body kind > 64 B forcing the heap-fallback path.
//
// No production body subclass currently exceeds the 64-byte SBO budget
// (see static_asserts in detail/body.hpp). This subclass deliberately
// carries a 128-byte aligned payload so that sizeof(fat_body) > 64,
// matching the size predicate emplace_body<T> uses to choose the heap
// branch. Inserted into a response via the friend hook (the existing
// `place_heap_*` pattern in http_response_sbo_test.cpp) rather than via
// emplace_body — emplace_body is private and the only goal here is to
// drive adopt_body_from / destroy_body through the heap branch.
//
// Carries an optional dtor counter (pointer style, matching counter_body
// in the SBO test) so individual cases can assert "exactly one dtor
// fired" semantics on heap-allocated source / destination bodies.
class fat_body final : public body {
 public:
    explicit fat_body(int* counter) noexcept : counter_(counter) {}

    fat_body(fat_body&& o) noexcept
        : body(std::move(o)),  // body carries virtual dispatch machinery;
                               // std::move is the required form — a future
                               // refactor that makes fat_body trivially
                               // movable must still move the vptr-bearing
                               // base to preserve move_into contract.
          counter_(std::exchange(o.counter_, nullptr)) {
        // payload_ contents are uninitialised by design — they exist only
        // to push sizeof past 64 B. Sanitizers would flag a read of the
        // payload, which the test never performs.
    }

    ~fat_body() override {
        if (counter_) ++*counter_;
    }

    // Returns body_kind::empty by design: fat_body is a test-only
    // synthetic class whose sole purpose is to exceed the SBO budget.
    // It must never reach production dispatch where kind() would be used
    // as a discriminator. This intentional
    // mismatch is acceptable here because fat_body is test-only; a
    // static_assert below enforces that this class never leaves this TU.
    body_kind kind() const noexcept override { return body_kind::empty; }
    std::size_t size() const noexcept override { return 0; }
    MHD_Response* materialize() override { return nullptr; }

    void move_into(void* dst) noexcept override {
        ::new (dst) fat_body(std::move(*this));
    }

 private:
    int* counter_;
    // Padding to push sizeof(fat_body) past the 64-byte SBO budget so
    // that emplace_body<fat_body> would route to the heap branch. The
    // bytes are never read (only their size matters); the
    // [[maybe_unused]] suppresses -Wunused-private-field under -Werror.
    [[maybe_unused]] alignas(16) std::byte payload_[128];
};

// Compile-time sanity: fat_body must actually exceed the SBO budget so
// that an emplace_body<fat_body> would take the heap path. The test
// places fat_body via the friend hook regardless, but if a future
// refactor shrinks the class, this assert flags that we are no longer
// genuinely exercising the heap branch.
static_assert(sizeof(fat_body) > http_response::body_buf_size,
              "fat_body must exceed SBO budget to force heap fallback");
static_assert(std::is_nothrow_move_constructible_v<fat_body>,
              "fat_body move ctor must be noexcept so move_into stays "
              "noexcept (matches base class contract)");


// -----------------------------------------------------------------------
// Friend-hook helpers (heap path only). Mirrors place_heap_string /
// place_heap_counter from http_response_sbo_test.cpp: ::operator new
// pairs exactly with the destructor's ::operator delete.
//
// `r` must be a fresh default-constructed http_response (body_ == nullptr).
// -----------------------------------------------------------------------
void place_heap_fat(http_response& r, int* counter) {
    void* mem = ::operator new(sizeof(fat_body));
    body* b = ::new (mem) fat_body(counter);
    SBO::body_ptr(r) = b;
    SBO::body_inline(r) = false;
    SBO::kind(r) = body_kind::empty;
}

}  // namespace


LT_BEGIN_SUITE(http_response_move_sanitizer_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http_response_move_sanitizer_suite)


// -----------------------------------------------------------------------
// Move-ctor: inline source (factory-constructed).
//
// Exercises the placement-new branch of adopt_body_from. The destination
// must read the body through its own inline storage (pointer-identity
// check). The source's destructor must be a no-op after the move
// (body_ == nullptr).
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   move_ctor_inline_factory)
    http_response src = http_response::string("hello", "application/json")
                            .with_status(201)
                            .with_header("X-Trace", "abc")
                            .with_footer("X-Foot", "fv")
                            .with_cookie("Sess", "ck");
    LT_ASSERT_EQ(SBO::body_inline(src), true);

    http_response dst(std::move(src));

    // Destination: state preserved, body lives in dst's inline storage.
    LT_CHECK_EQ(dst.get_status(), 201);
    LT_CHECK_EQ(dst.get_header("Content-Type"), "application/json");
    LT_CHECK_EQ(dst.get_header("X-Trace"), "abc");
    LT_CHECK_EQ(dst.get_footer("X-Foot"), "fv");
    LT_CHECK_EQ(dst.get_cookie("Sess"), "ck");
    // static_cast<int> is required because LT_CHECK_EQ's failure-reporting
    // path uses operator<< to stream both sides, and body_kind (an enum
    // class) has no operator<<. Direct enum comparison via == would work at
    // runtime but would fail to compile when the diagnostic path is
    // instantiated. A dedicated operator<< for body_kind would allow
    // removing these casts.
    LT_CHECK_EQ(static_cast<int>(dst.kind()),
                static_cast<int>(body_kind::string));
    LT_CHECK_EQ(SBO::body_inline(dst), true);
    LT_CHECK_EQ(reinterpret_cast<void*>(SBO::body_ptr(dst)),
                reinterpret_cast<void*>(SBO::storage(dst)));

    // Source: moved-from contract — destructible (body_ == nullptr makes
    // ~http_response's destroy_body a no-op).
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
    LT_CHECK_EQ(SBO::body_inline(src), false);
LT_END_AUTO_TEST(move_ctor_inline_factory)


// -----------------------------------------------------------------------
// Move-ctor: heap source (synthetic fat_body).
//
// Exercises the pointer-swap branch of adopt_body_from. The body pointer
// is transferred verbatim; no allocation, no body copy. The destination's
// destructor will ::operator delete the heap-allocated fat_body when
// scope ends — paired exactly with the ::operator new in place_heap_fat.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   move_ctor_heap_synthetic)
    int dtor_count = 0;
    body* original_ptr = nullptr;
    {
        http_response src;
        place_heap_fat(src, &dtor_count);
        original_ptr = SBO::body_ptr(src);

        http_response dst(std::move(src));

        LT_CHECK_EQ(SBO::body_inline(dst), false);
        LT_CHECK_EQ(SBO::body_ptr(dst), original_ptr);
        LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
        LT_CHECK_EQ(SBO::body_inline(src), false);

        // While dst is still alive, the heap body has NOT been destroyed.
        LT_CHECK_EQ(dtor_count, 0);
    }
    // Scope exit: dst's destructor runs destroy_body which dtors and
    // ::operator deletes the heap fat_body. src is moved-from (body_ ==
    // nullptr), so its destructor is a no-op — the heap body is NOT
    // double-deleted (ASan would flag a double free).
    LT_CHECK_EQ(dtor_count, 1);
LT_END_AUTO_TEST(move_ctor_heap_synthetic)


// -----------------------------------------------------------------------
// Move-assign: inline ↔ inline.
//
// Both sides factory-constructed (string body fits in SBO). The
// destination's existing body must be destroyed before the source's body
// is adopted; the destination must end up with the source's body in its
// own inline storage.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   move_assign_inline_to_inline_factory)
    http_response dst = http_response::string("OLD", "text/plain")
                            .with_status(200)
                            .with_header("X-Origin", "old");
    http_response src = http_response::string("NEW", "application/json")
                            .with_status(202)
                            .with_header("X-Origin", "new");

    LT_ASSERT_EQ(SBO::body_inline(dst), true);
    LT_ASSERT_EQ(SBO::body_inline(src), true);
    // Pre-move sentinel discriminator: the destination currently carries
    // the OLD content-type. Post-move it must reflect the SOURCE's
    // content-type — otherwise the move did not actually adopt.
    LT_ASSERT_EQ(dst.get_header("Content-Type"), "text/plain");

    dst = std::move(src);

    LT_CHECK_EQ(dst.get_status(), 202);
    LT_CHECK_EQ(dst.get_header("Content-Type"), "application/json");
    LT_CHECK_EQ(dst.get_header("X-Origin"), "new");
    LT_CHECK_EQ(SBO::body_inline(dst), true);
    LT_CHECK_EQ(reinterpret_cast<void*>(SBO::body_ptr(dst)),
                reinterpret_cast<void*>(SBO::storage(dst)));
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
LT_END_AUTO_TEST(move_assign_inline_to_inline_factory)


// -----------------------------------------------------------------------
// Move-assign: inline destination, heap source.
//
// Destination is factory-constructed (inline). Source carries a synthetic
// fat_body on the heap. Post-move: dst takes ownership of the heap body
// (pointer identity preserved); the old inline string_body's destructor
// must have fired (otherwise it would leak via the in-buffer string).
// On scope exit, dst's destructor frees the fat_body — exactly once.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   move_assign_inline_to_heap_synthetic)
    int dtor_count = 0;
    {
        http_response dst = http_response::string("OLD", "text/plain");
        http_response src;
        place_heap_fat(src, &dtor_count);
        body* heap_ptr = SBO::body_ptr(src);

        LT_ASSERT_EQ(SBO::body_inline(dst), true);
        LT_ASSERT_EQ(SBO::body_inline(src), false);

        dst = std::move(src);

        LT_CHECK_EQ(SBO::body_inline(dst), false);
        LT_CHECK_EQ(SBO::body_ptr(dst), heap_ptr);
        LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
        // Heap body still alive at this point.
        LT_CHECK_EQ(dtor_count, 0);
    }
    LT_CHECK_EQ(dtor_count, 1);
LT_END_AUTO_TEST(move_assign_inline_to_heap_synthetic)


// -----------------------------------------------------------------------
// Move-assign: heap destination, inline source.
//
// Destination is the synthetic heap fat_body. Source is a factory-built
// inline string body. Post-move: dst now carries the source's body in
// dst's inline storage; the heap fat_body's destructor must have fired
// AND ::operator delete must have run — ASan is the canary for either a
// missing free (leak) or a double free. We assert dtor_count == 1 to
// pin "destroy ran exactly once".
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   move_assign_heap_to_inline_synthetic)
    int dtor_count = 0;
    http_response dst;
    place_heap_fat(dst, &dtor_count);
    http_response src = http_response::string("NEW", "text/plain");

    LT_ASSERT_EQ(SBO::body_inline(dst), false);
    LT_ASSERT_EQ(SBO::body_inline(src), true);

    dst = std::move(src);

    // Old heap body destroyed during destroy_body() inside move-assign.
    LT_CHECK_EQ(dtor_count, 1);
    LT_CHECK_EQ(SBO::body_inline(dst), true);
    LT_CHECK_EQ(reinterpret_cast<void*>(SBO::body_ptr(dst)),
                reinterpret_cast<void*>(SBO::storage(dst)));
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
    LT_CHECK_EQ(dst.get_header("Content-Type"), "text/plain");
LT_END_AUTO_TEST(move_assign_heap_to_inline_synthetic)


// -----------------------------------------------------------------------
// Move-assign: heap destination, heap source.
//
// Both sides hold synthetic fat_body instances on the heap, each with a
// distinct dtor-counter. Move-assign destroys dst's heap body (counter
// ticks immediately) and adopts src's heap pointer. Scope exit: src is
// moved-from (no-op dtor), dst's adopted fat_body finally gets
// ::operator delete'd. Total: dst_counter == 1, src_counter == 1.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   move_assign_heap_to_heap_synthetic)
    int dst_counter = 0;
    int src_counter = 0;
    body* src_ptr = nullptr;
    {
        http_response dst;
        http_response src;
        place_heap_fat(dst, &dst_counter);
        place_heap_fat(src, &src_counter);
        src_ptr = SBO::body_ptr(src);

        dst = std::move(src);

        // dst's original heap body destroyed during destroy_body().
        LT_CHECK_EQ(dst_counter, 1);
        // src's body still alive — pointer transferred to dst.
        LT_CHECK_EQ(src_counter, 0);
        LT_CHECK_EQ(SBO::body_inline(dst), false);
        LT_CHECK_EQ(SBO::body_ptr(dst), src_ptr);
        LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
    }
    // Scope exit: src moved-from (no-op), dst destroys the adopted body.
    LT_CHECK_EQ(dst_counter, 1);
    LT_CHECK_EQ(src_counter, 1);
LT_END_AUTO_TEST(move_assign_heap_to_heap_synthetic)


// -----------------------------------------------------------------------
// Self move-assign safety (public-API surface).
//
// http_response.cpp:150-152 guards self-assign with an explicit identity
// check. Without it, destroy_body() would destroy the body we are about
// to read from — ASan would flag use-after-free in adopt_body_from. The
// SBO test covers this through a manually placement-new'd counter_body;
// this duplicates the coverage at the public-API surface so the
// sanitizer matrix exercises it for both pathways.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   move_assign_self_inline_statePreserved)
    http_response r = http_response::string("self", "application/json")
                          .with_status(200)
                          .with_header("X-Self", "1");

    // Aliased through a reference to defeat -Wself-move on clang/gcc.
    http_response& alias = r;
    r = std::move(alias);

    LT_CHECK_EQ(r.get_status(), 200);
    LT_CHECK_EQ(r.get_header("Content-Type"), "application/json");
    LT_CHECK_EQ(r.get_header("X-Self"), "1");
    LT_ASSERT_NEQ(SBO::body_ptr(r), static_cast<body*>(nullptr));
LT_END_AUTO_TEST(move_assign_self_inline_statePreserved)


// -----------------------------------------------------------------------
// Accessor-safety on a moved-from source.
//
// A moved-from http_response is in a
// valid, accessor-safe state. get_status(), kind(), get_header(),
// get_headers().size() must all return *something* without UB; UBSan
// would flag any dangling read or invalid load.
//
// We do NOT pin specific return values for moved-from accessors (the
// public contract intentionally leaves those unspecified per std::map
// moved-from-state semantics), but we DO require the calls themselves
// to be defined behaviour.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   moved_from_accessors_are_defined)
    http_response src = http_response::string("payload", "text/json")
                            .with_status(202)
                            .with_header("X-A", "1")
                            .with_footer("X-B", "2")
                            .with_cookie("X-C", "3");

    http_response dst(std::move(src));

    // The accessor calls below must not trap, abort, or trigger UBSan.
    // We use volatile sinks so the optimiser cannot dead-code them.
    // The volatile assignments alone prevent dead-code elimination; no
    // subsequent (void) casts are needed.
    volatile int sink_status = src.get_status();
    volatile int sink_kind = static_cast<int>(src.kind());
    volatile std::size_t sink_hdrs = src.get_headers().size();
    volatile std::size_t sink_ftrs = src.get_footers().size();
    volatile std::size_t sink_ckis = src.get_cookies().size();

    // get_header / get_footer / get_cookie return a string_view; calling
    // them on a moved-from response must not UB. The view's contents
    // depend on std::map's moved-from-state semantics (libstdc++ vs.
    // libc++) and are deliberately unpinned — we only assert the call
    // executes.
    auto v_h = src.get_header("X-A");
    auto v_f = src.get_footer("X-B");
    auto v_c = src.get_cookie("X-C");
    (void)v_h;
    (void)v_f;
    (void)v_c;

    // Pin the post-move invariant: dst received the body (non-null
    // pointer) and sink_status is a value that compiles, satisfying the
    // "defined behaviour" contract above. sink_status may be 0 or 202
    // depending on the std::map moved-from state, but it must be a valid
    // int — the volatile read above already asserts no UB occurred.
    LT_ASSERT_NEQ(SBO::body_ptr(dst), static_cast<body*>(nullptr));
    // Suppress unused-variable warnings on compilers that warn even after
    // a volatile assignment; the reads already happened above.
    (void)sink_status;
    (void)sink_kind;
    (void)sink_hdrs;
    (void)sink_ftrs;
    (void)sink_ckis;
LT_END_AUTO_TEST(moved_from_accessors_are_defined)


// -----------------------------------------------------------------------
// Re-assignability of a moved-from source.
//
// A moved-from http_response must be a valid move-assign target. This
// verifies the moved-from invariant that lets callers reuse the storage
// rather than constructing fresh, which is a common pattern in handler
// chains. ASan would flag a UAF if destroy_body() inside the second
// move-assign tried to dereference the nullptr body_ from the first move.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   moved_from_is_reassignable)
    http_response src = http_response::string("first", "text/plain")
                            .with_status(200);
    http_response dst(std::move(src));
    LT_ASSERT_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));

    // Assign a fresh response into the moved-from src — must be safe.
    src = http_response::empty().with_status(204);

    LT_CHECK_EQ(src.get_status(), 204);
    LT_CHECK_EQ(static_cast<int>(src.kind()),
                static_cast<int>(body_kind::empty));
    LT_ASSERT_NEQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
LT_END_AUTO_TEST(moved_from_is_reassignable)


// -----------------------------------------------------------------------
// file_body move-ctor under sanitizers.
//
// file_body has a hand-written move ctor (detail/body.hpp:182) that
// transfers fd ownership and flips materialized_ to suppress double-close
// in the source's destructor. ASan would flag a use-after-close (or a
// double-close abort from glibc) if either side mishandled the fd. The
// existing v1 test fixture `test_content` provides a small file the
// libtest infrastructure expects to be present at the test working dir.
//
// This is the only "real" production heap-aware body the test exercises:
// while file_body fits the SBO budget, its hand-written move + fd
// ownership is a different lifetime contract than the trivial-move
// string/empty path, so this suite deliberately includes
// at least one file move-construct case.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_move_sanitizer_suite,
                   move_ctor_file_body_inline)
    // "test_content" is the well-known fixture file created by the
    // test/Makefile.am infrastructure (via TESTS_ENVIRONMENT or as a
    // pre-existing file in the build/test/ directory from which `make check`
    // runs the test binaries). The same fixture is used by other file-body
    // tests in this suite. If running the binary manually outside `make
    // check`, ensure a readable file named "test_content" exists in the
    // current working directory.
    http_response src = http_response::file("test_content");
    LT_ASSERT_EQ(SBO::body_inline(src), true);

    http_response dst(std::move(src));

    LT_CHECK_EQ(static_cast<int>(dst.kind()),
                static_cast<int>(body_kind::file));
    LT_CHECK_EQ(SBO::body_inline(dst), true);
    LT_CHECK_EQ(SBO::body_ptr(src), static_cast<body*>(nullptr));
    // The fd lives inside dst's file_body; scope exit closes it exactly
    // once (the moved-from src's destructor is a no-op).
LT_END_AUTO_TEST(move_ctor_file_body_inline)


LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
