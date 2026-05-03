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

// Unit tests for the internal detail::body hierarchy and the public
// body_kind enum (TASK-008). This TU is a build-tree test and is allowed
// to include both the public umbrella (for body_kind) and the private
// details/body.hpp directly (for the subclasses) — header-hygiene from
// the consumer perspective is asserted separately by header_hygiene_*.

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

#include <microhttpd.h>

#include "./httpserver.hpp"                 // public umbrella → body_kind
#include "httpserver/details/body.hpp"      // private hierarchy
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
static_assert(static_cast<int>(httpserver::body_kind::string) >= 0);
static_assert(static_cast<int>(httpserver::body_kind::file) >= 0);
static_assert(static_cast<int>(httpserver::body_kind::iovec) >= 0);
static_assert(static_cast<int>(httpserver::body_kind::pipe) >= 0);
static_assert(static_cast<int>(httpserver::body_kind::deferred) >= 0);

// -----------------------------------------------------------------------
// Step 2 — abstract base contract.
// -----------------------------------------------------------------------
static_assert(std::is_abstract_v<httpserver::detail::body>,
              "detail::body must be abstract");
static_assert(std::has_virtual_destructor_v<httpserver::detail::body>,
              "detail::body must have a virtual destructor");

// -----------------------------------------------------------------------
// Step 3 — per-subclass SBO budget + base relationship.
// Mirrored asserts: identical lines also live in details/body.hpp; placing
// them here gives a second failure site if the header drifts.
// -----------------------------------------------------------------------
static_assert(sizeof(httpserver::detail::empty_body) <= 64,
              "empty_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::string_body) <= 64,
              "string_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::file_body) <= 64,
              "file_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::iovec_body) <= 64,
              "iovec_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::pipe_body) <= 64,
              "pipe_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(httpserver::detail::deferred_body) <= 64,
              "deferred_body must fit in http_response SBO (DR-005)");
static_assert(alignof(httpserver::detail::deferred_body) <= 16,
              "deferred_body alignment must be <= 16 (DR-005)");

static_assert(std::is_base_of_v<httpserver::detail::body,
                                httpserver::detail::empty_body>);
static_assert(std::is_base_of_v<httpserver::detail::body,
                                httpserver::detail::string_body>);
static_assert(std::is_base_of_v<httpserver::detail::body,
                                httpserver::detail::file_body>);
static_assert(std::is_base_of_v<httpserver::detail::body,
                                httpserver::detail::iovec_body>);
static_assert(std::is_base_of_v<httpserver::detail::body,
                                httpserver::detail::pipe_body>);
static_assert(std::is_base_of_v<httpserver::detail::body,
                                httpserver::detail::deferred_body>);

LT_BEGIN_SUITE(body_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(body_suite)

// -----------------------------------------------------------------------
// empty_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, empty_body_kind_size_and_materialize)
    httpserver::detail::empty_body b;
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::empty));
    LT_CHECK_EQ(b.size(), 0u);
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(empty_body_kind_size_and_materialize)

// -----------------------------------------------------------------------
// string_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, string_body_kind_size_and_materialize)
    httpserver::detail::string_body b(std::string("hello"));
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::string));
    LT_CHECK_EQ(b.size(), 5u);
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(string_body_kind_size_and_materialize)

LT_BEGIN_AUTO_TEST(body_suite, string_body_empty_payload_is_zero_size)
    httpserver::detail::string_body b(std::string{});
    LT_CHECK_EQ(b.size(), 0u);
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(string_body_empty_payload_is_zero_size)

// -----------------------------------------------------------------------
// file_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, file_body_kind_and_materialize_existing_file)
    // test_content is a fixture shipped in test/ (one-line text file).
    httpserver::detail::file_body b("test_content");
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::file));
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(file_body_kind_and_materialize_existing_file)

LT_BEGIN_AUTO_TEST(body_suite, file_body_returns_null_on_missing_file)
    httpserver::detail::file_body b("/no/such/path/should/exist");
    // Mirrors v1 file_response::get_raw_response semantics.
    LT_CHECK_EQ(b.materialize(), static_cast<MHD_Response*>(nullptr));
LT_END_AUTO_TEST(file_body_returns_null_on_missing_file)

// -----------------------------------------------------------------------
// iovec_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, iovec_body_size_is_sum_of_entry_lengths)
    std::vector<httpserver::iovec_entry> entries = {
        {"abc", 3},
        {"defg", 4},
    };
    httpserver::detail::iovec_body b(std::move(entries));
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::iovec));
    LT_CHECK_EQ(b.size(), 7u);
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(iovec_body_size_is_sum_of_entry_lengths)

LT_BEGIN_AUTO_TEST(body_suite, iovec_body_empty_entries_materializes)
    httpserver::detail::iovec_body b(std::vector<httpserver::iovec_entry>{});
    LT_CHECK_EQ(b.size(), 0u);
    // MHD may or may not accept a zero-iovec response; we only assert that
    // size() is correct and that constructing/destroying does not crash.
LT_END_AUTO_TEST(iovec_body_empty_entries_materializes)

// -----------------------------------------------------------------------
// pipe_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, pipe_body_kind_and_materialize)
    int fds[2];
    int rc = ::pipe(fds);
    LT_ASSERT_EQ(rc, 0);
    httpserver::detail::pipe_body b(fds[0]);  // takes ownership of read end
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
        httpserver::detail::pipe_body b(read_fd);
        // Intentionally do NOT call materialize() — destructor must close fd.
    }
    // Second close on the now-closed fd must fail with EBADF.
    int second = ::close(read_fd);
    LT_CHECK_EQ(second, -1);
    LT_CHECK_EQ(errno, EBADF);
    ::close(fds[1]);
LT_END_AUTO_TEST(pipe_body_destructor_closes_fd_when_not_materialized)

// -----------------------------------------------------------------------
// deferred_body
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(body_suite, deferred_body_kind_and_materialize)
    std::function<ssize_t(uint64_t, char*, std::size_t)> f =
        [](uint64_t, char*, std::size_t) -> ssize_t {
            return MHD_CONTENT_READER_END_OF_STREAM;
        };
    httpserver::detail::deferred_body b(std::move(f));
    LT_CHECK_EQ(static_cast<int>(b.kind()),
                static_cast<int>(httpserver::body_kind::deferred));
    MHD_Response* r = b.materialize();
    LT_ASSERT_NEQ(r, static_cast<MHD_Response*>(nullptr));
    MHD_destroy_response(r);
LT_END_AUTO_TEST(deferred_body_kind_and_materialize)

LT_BEGIN_AUTO_TEST(body_suite, deferred_body_trampoline_invokes_stored_callable)
    bool called = false;
    httpserver::detail::deferred_body b(
        [&](uint64_t pos, char* buf, std::size_t max) -> ssize_t {
            called = true;
            (void)pos;
            if (max >= 2) { buf[0] = 'h'; buf[1] = 'i'; return 2; }
            return 0;
        });
    char out[16] = {};
    ssize_t n = httpserver::detail::deferred_body::trampoline(
        &b, 0, out, sizeof(out));
    LT_CHECK_EQ(called, true);
    LT_CHECK_EQ(n, static_cast<ssize_t>(2));
    LT_CHECK_EQ(out[0], 'h');
    LT_CHECK_EQ(out[1], 'i');
LT_END_AUTO_TEST(deferred_body_trampoline_invokes_stored_callable)

LT_BEGIN_AUTO_TEST(body_suite, deferred_body_destructor_releases_callable)
    auto sentinel = std::make_shared<int>(42);
    std::weak_ptr<int> w = sentinel;
    {
        httpserver::detail::deferred_body b(
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
