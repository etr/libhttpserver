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

// Unit tests for the internal detail::response_body hierarchy and the public
// body_kind enum. This TU is a build-tree test and is allowed
// to include both the public umbrella (for body_kind) and the private
// detail/response_body.hpp directly (for the subclasses) — header-hygiene from
// the consumer perspective is asserted separately by header_hygiene_*.

#include <fcntl.h>          // O_RDONLY
#include <microhttpd.h>
#include <sys/types.h>      // ssize_t
#include <unistd.h>         // pipe, close

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "./httpserver.hpp"                 // public umbrella → body_kind
#include "httpserver/detail/response_body.hpp"       // private hierarchy
#include "./littletest.hpp"

// -----------------------------------------------------------------------
// Step 1 — public body_kind enum: shape and enumerator presence.
// -----------------------------------------------------------------------
static_assert(std::is_enum_v<httpserver::body_kind>,
              "body_kind must be an enum");
static_assert(std::is_same_v<std::underlying_type_t<httpserver::body_kind>,
                             std::uint8_t>,
              "body_kind underlying type must be std::uint8_t");
static_assert(static_cast<int>(httpserver::body_kind::empty) == 0,
              "body_kind::empty must be the zero-initialised value");
// Reference each enumerator at compile time so a missing one breaks the build.
// Comparing against `empty` (=0) avoids -Wtype-limits on uint8_t-backed enums
// while still touching every name.
static_assert(httpserver::body_kind::string   != httpserver::body_kind::empty);
static_assert(httpserver::body_kind::file     != httpserver::body_kind::empty);
static_assert(httpserver::body_kind::iovec    != httpserver::body_kind::empty);
static_assert(httpserver::body_kind::pipe     != httpserver::body_kind::empty);
static_assert(httpserver::body_kind::deferred != httpserver::body_kind::empty);

// -----------------------------------------------------------------------
// Step 2 — abstract base contract.
// -----------------------------------------------------------------------
static_assert(std::is_abstract_v<httpserver::detail::response_body>,
              "detail::response_body must be abstract");
static_assert(std::has_virtual_destructor_v<httpserver::detail::response_body>,
              "detail::response_body must have a virtual destructor");

// -----------------------------------------------------------------------
// Step 3 — per-subclass SBO budget + base relationship.
// Mirrored asserts: identical lines also live in detail/response_body.hpp; placing
// them here gives a second failure site if the header drifts.
// -----------------------------------------------------------------------
static_assert(sizeof(httpserver::detail::empty_response_body) <= 64,
              "empty_response_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::string_response_body) <= 64,
              "string_response_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::file_response_body) <= 64,
              "file_response_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::iovec_response_body) <= 64,
              "iovec_response_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::pipe_response_body) <= 64,
              "pipe_response_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::deferred_response_body) <= 64,
              "deferred_response_body must fit in http_response SBO (DR-005)");
static_assert(alignof(httpserver::detail::deferred_response_body) <= 16,
              "deferred_response_body alignment must be <= 16 (DR-005)");

static_assert(std::is_base_of_v<httpserver::detail::response_body,
                                httpserver::detail::empty_response_body>);
static_assert(std::is_base_of_v<httpserver::detail::response_body,
                                httpserver::detail::string_response_body>);
static_assert(std::is_base_of_v<httpserver::detail::response_body,
                                httpserver::detail::file_response_body>);
static_assert(std::is_base_of_v<httpserver::detail::response_body,
                                httpserver::detail::iovec_response_body>);
static_assert(std::is_base_of_v<httpserver::detail::response_body,
                                httpserver::detail::pipe_response_body>);
static_assert(std::is_base_of_v<httpserver::detail::response_body,
                                httpserver::detail::deferred_response_body>);

LT_BEGIN_SUITE(body_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(body_suite)

// -----------------------------------------------------------------------
// empty_response_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, empty_body_kind_size_and_materialize)
    httpserver::detail::empty_response_body b;
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::empty));
    LT_CHECK_EQ(b.size(), 0u);
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(empty_body_kind_size_and_materialize)

// Cover the explicit empty_response_body(int flags) constructor path.
LT_BEGIN_AUTO_TEST(body_suite, empty_body_with_flags_materializes)
    // MHD_RF_HTTP_1_0_SERVER is a harmless non-zero flag value.
    httpserver::detail::empty_response_body b(static_cast<int>(MHD_RF_HTTP_1_0_SERVER));
    LT_CHECK_EQ(b.size(), 0u);
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(empty_body_with_flags_materializes)

// -----------------------------------------------------------------------
// string_response_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, string_body_kind_size_and_materialize)
    httpserver::detail::string_response_body b(std::string("hello"));
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::string));
    LT_CHECK_EQ(b.size(), 5u);
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(string_body_kind_size_and_materialize)

LT_BEGIN_AUTO_TEST(body_suite, string_body_empty_payload_is_zero_size)
    httpserver::detail::string_response_body b(std::string{});
    LT_CHECK_EQ(b.size(), 0u);
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(string_body_empty_payload_is_zero_size)

// -----------------------------------------------------------------------
// file_response_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, file_body_kind_and_materialize_existing_file)
    // test_content is a fixture shipped in test/ (one-line text file).
    httpserver::detail::file_response_body b("test_content");
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::file));
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    // Pin the size-caching side-effect: size() must reflect the on-disk size.
    LT_CHECK_GT(b.size(), 0u);
    MHD_destroy_response(r);
LT_END_AUTO_TEST(file_body_kind_and_materialize_existing_file)

// The file is opened and stat'd at construction so size() is accurate
// before materialize() is called,
// and materialize() uses fstat's st_size rather than lseek (no fd-position
// side-effect, no TOCTOU window on the size).
LT_BEGIN_AUTO_TEST(body_suite, file_body_size_known_before_materialize)
    // test_content is 21 bytes ("test content of file\n").
    // Update this assertion if test_content is intentionally changed.
    httpserver::detail::file_response_body b("test_content");
    // size() must be non-zero and correct at construction time — the file is
    // opened and fstat'd in the constructor, not in materialize().
    LT_CHECK_EQ(b.size(), static_cast<std::size_t>(21));
LT_END_AUTO_TEST(file_body_size_known_before_materialize)

LT_BEGIN_AUTO_TEST(body_suite, file_body_returns_null_on_missing_file)
    httpserver::detail::file_response_body b("/no/such/path/should/exist");
    // Constructor error: size() must be 0 and materialize() must return nullptr.
    LT_CHECK_EQ(b.size(), 0u);
    LT_CHECK_EQ(b.materialize(), static_cast<MHD_Response*>(nullptr));
LT_END_AUTO_TEST(file_body_returns_null_on_missing_file)

// file_response_body rejects non-regular files (S_ISREG check in constructor).
// Passing a directory path must leave fd_ = -1, so size() == 0 and
// materialize() returns nullptr — covering the !S_ISREG branch.
LT_BEGIN_AUTO_TEST(body_suite, file_body_returns_null_for_non_regular_file)
    httpserver::detail::file_response_body b("/tmp");
    LT_CHECK_EQ(b.size(), 0u);
    LT_CHECK_EQ(b.materialize(), static_cast<MHD_Response*>(nullptr));
LT_END_AUTO_TEST(file_body_returns_null_for_non_regular_file)

// reason: POSIX ::open/::close + errno EBADF inspection has no portable
// Windows equivalent. The file_response_body class is itself portable; only the
// anchor-fd test technique is POSIX-shaped. Gap tracked in test/PORTABILITY.md.
#ifndef _WIN32
// Parallel of pipe_body_destructor_closes_fd_when_not_materialized: the
// ownership contract states 'if materialize() is never called, ~file_response_body()
// must close fd_'. Verify via EBADF on a second ::close() of the same fd.
//
// Technique: open the file to get a known fd number, close it, then construct
// file_response_body (which re-opens and gets the same slot). After file_response_body goes out
// of scope without materialize(), the slot must be closed — EBADF confirms it.
//
// Caveat: this anchor-fd technique assumes nothing else in the process opens
// an fd between the close() in step 1 and the file_response_body construction in step
// 2, so the freed slot is still the lowest available one. That is true for
// this single-threaded test body, but could rarely flake under a heavily
// parallel `make check -j` if another thread/OS activity opens an fd in that
// narrow window.
LT_BEGIN_AUTO_TEST(body_suite, file_body_destructor_closes_fd_when_not_materialized)
    // Step 1: open the file to get a stable fd slot number, then close it.
    int anchor = ::open("test_content", O_RDONLY);
    LT_ASSERT_GTE(anchor, 0);
    ::close(anchor);  // release — file_response_body's open() will reclaim this slot

    // Step 2: construct file_response_body (it opens test_content, gets `anchor` slot).
    {
        httpserver::detail::file_response_body b("test_content");
        LT_ASSERT_GT(b.size(), 0u);  // open must have succeeded
        // Do NOT call materialize() — destructor must close fd_.
    }

    // Step 3: after ~file_response_body(), `anchor` must be EBADF.
    int second = ::close(anchor);
    LT_CHECK_EQ(second, -1);
    LT_CHECK_EQ(errno, EBADF);
LT_END_AUTO_TEST(file_body_destructor_closes_fd_when_not_materialized)
#endif  // !_WIN32

// -----------------------------------------------------------------------
// iovec_response_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, iovec_body_size_is_sum_of_entry_lengths)
    std::vector<httpserver::iovec_entry> entries = {
        {"abc", 3},
        {"defg", 4},
    };
    httpserver::detail::iovec_response_body b(std::move(entries));
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::iovec));
    LT_CHECK_EQ(b.size(), 7u);
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(iovec_body_size_is_sum_of_entry_lengths)

LT_BEGIN_AUTO_TEST(body_suite, iovec_body_empty_entries_materializes)
    httpserver::detail::iovec_response_body b(std::vector<httpserver::iovec_entry>{});
    LT_CHECK_EQ(b.size(), 0u);
    // Call materialize() and pin the observable result. MHD accepts a
    // zero-iovec call and returns a valid response on current libmicrohttpd.
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(iovec_body_empty_entries_materializes)

// -----------------------------------------------------------------------
// pipe_response_body
//
// reason: MSYS2/mingw does not expose POSIX ::pipe() — Windows pipes use
// _pipe() / CreatePipe() with different fd semantics. The pipe_response_body class
// itself is portable (it just owns and closes a fd) but the tests below
// need to *create* a pipe to exercise it, which is platform-specific. The
// Linux/macOS CI matrix exercises this code path; the Windows gap is
// tracked in test/PORTABILITY.md.
// -----------------------------------------------------------------------
#ifndef _WIN32
LT_BEGIN_AUTO_TEST(body_suite, pipe_body_kind_and_materialize)
    int fds[2];
    int rc = ::pipe(fds);
    LT_ASSERT_EQ(rc, 0);
    httpserver::detail::pipe_response_body b(fds[0]);  // takes ownership of read end
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::pipe));
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);  // MHD owns fds[0] from this point
    ::close(fds[1]);
LT_END_AUTO_TEST(pipe_body_kind_and_materialize)

LT_BEGIN_AUTO_TEST(body_suite, pipe_body_destructor_closes_fd_when_not_materialized)
    int fds[2];
    int rc = ::pipe(fds);
    LT_ASSERT_EQ(rc, 0);
    int read_fd = fds[0];
    {
        httpserver::detail::pipe_response_body b(read_fd);
        // Intentionally do NOT call materialize() — destructor must close fd.
    }
    // Second close on the now-closed fd must fail with EBADF.
    int second = ::close(read_fd);
    LT_CHECK_EQ(second, -1);
    LT_CHECK_EQ(errno, EBADF);
    ::close(fds[1]);
LT_END_AUTO_TEST(pipe_body_destructor_closes_fd_when_not_materialized)
#endif  // !_WIN32

// -----------------------------------------------------------------------
// deferred_response_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, deferred_body_kind_and_materialize)
    std::function<ssize_t(uint64_t, char*, std::size_t)> f =
        [](uint64_t, char*, std::size_t) -> ssize_t {
            return MHD_CONTENT_READER_END_OF_STREAM;
        };
    httpserver::detail::deferred_response_body b(std::move(f));
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::deferred));
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(deferred_body_kind_and_materialize)

LT_BEGIN_AUTO_TEST(body_suite, deferred_body_trampoline_invokes_stored_callable)
    bool called = false;
    httpserver::detail::deferred_response_body b(
        [&](uint64_t pos, char* buf, std::size_t max) -> ssize_t {
            called = true;
            (void)pos;
            if (max >= 2) {
                buf[0] = 'h';
                buf[1] = 'i';
                return 2;
            }
            return 0;
        });
    char out[16] = {};
    ssize_t n = httpserver::detail::deferred_response_body::trampoline(
        &b, 0, out, sizeof(out));
    LT_CHECK_EQ(called, true);
    LT_CHECK_EQ(n, static_cast<ssize_t>(2));
    LT_CHECK_EQ(out[0], 'h');
    LT_CHECK_EQ(out[1], 'i');
LT_END_AUTO_TEST(deferred_body_trampoline_invokes_stored_callable)

// The trampoline must not invoke an empty/null
// std::function — it should return MHD_CONTENT_READER_END_WITH_ERROR instead
// of throwing std::bad_function_call (which would terminate in MHD's IO thread).
LT_BEGIN_AUTO_TEST(body_suite, deferred_body_trampoline_null_cls_returns_error)
    // cls == nullptr: trampoline must guard against null self pointer.
    char out[16] = {};
    ssize_t n = httpserver::detail::deferred_response_body::trampoline(
        nullptr, 0, out, sizeof(out));
    LT_CHECK_EQ(n, static_cast<ssize_t>(MHD_CONTENT_READER_END_WITH_ERROR));
LT_END_AUTO_TEST(deferred_body_trampoline_null_cls_returns_error)

// Companion to the null-cls case above: pins the other half of the
// `!self || !self->producer_` guard in deferred_response_body::trampoline
// (src/detail/response_body.cpp). A move-constructed-from deferred_response_body has a
// non-null `self` but an empty producer_, so trampoline must still return
// MHD_CONTENT_READER_END_WITH_ERROR rather than invoking an empty
// std::function (which would throw std::bad_function_call and terminate
// in MHD's IO thread).
//
// The capture below is deliberately oversized (well past std::function's
// small-object-optimization threshold on every mainstream stdlib) so the
// lambda is heap-allocated by std::function. This is required for the
// test to be meaningful: empirically (verified on libc++/Apple Clang),
// std::function's move constructor for a *small*, SBO-eligible captureless
// lambda may leave the source still holding a live (copied) target rather
// than becoming empty, since the standard only guarantees the moved-from
// std::function is left in a valid, unspecified state -- not that it is
// empty. A heap-allocated target must have its owning pointer transferred
// (and the source's pointer nulled) on move, so the source reliably
// becomes empty in that case.
LT_BEGIN_AUTO_TEST(body_suite, deferred_body_trampoline_moved_from_producer_returns_error)
    std::string oversized_capture(256, 'z');  // forces heap allocation in std::function
    httpserver::detail::deferred_response_body b(
        [oversized_capture](uint64_t, char*, std::size_t) -> ssize_t {
            (void)oversized_capture;
            return MHD_CONTENT_READER_END_OF_STREAM;
        });
    httpserver::detail::deferred_response_body moved_to(std::move(b));
    (void)moved_to;

    char out[16] = {};
    ssize_t n = httpserver::detail::deferred_response_body::trampoline(
        &b, 0, out, sizeof(out));
    LT_CHECK_EQ(n, static_cast<ssize_t>(MHD_CONTENT_READER_END_WITH_ERROR));
LT_END_AUTO_TEST(deferred_body_trampoline_moved_from_producer_returns_error)

LT_BEGIN_AUTO_TEST(body_suite, deferred_body_destructor_releases_callable)
    auto sentinel = std::make_shared<int>(42);
    std::weak_ptr<int> w = sentinel;
    {
        httpserver::detail::deferred_response_body b(
            [s = std::move(sentinel)](uint64_t, char*, std::size_t) -> ssize_t {
                (void)s;
                return MHD_CONTENT_READER_END_OF_STREAM;
            });
        LT_CHECK_EQ(w.expired(), false);
    }
    LT_CHECK_EQ(w.expired(), true);
LT_END_AUTO_TEST(deferred_body_destructor_releases_callable)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
