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

// Layout / POD-trait verification for `httpserver::iovec_entry`.
// This TU is allowed to include <sys/uio.h> and <microhttpd.h> directly —
// it is an internal test, not a header-hygiene sentinel. The library-side
// guarantee that downstream code does NOT see <sys/uio.h> via the umbrella
// is asserted separately by `header_hygiene_iovec_test.cpp`.

#include <microhttpd.h>
#include <sys/uio.h>

#include <cstddef>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// AC: trivially copyable + standard layout — required for the
// reinterpret_cast bridge to libmicrohttpd's MHD_IoVec / POSIX struct iovec.
static_assert(std::is_standard_layout_v<httpserver::iovec_entry>,
              "iovec_entry must be standard layout");
static_assert(std::is_trivially_copyable_v<httpserver::iovec_entry>,
              "iovec_entry must be trivially copyable");

// Member types as declared by the spec.
static_assert(std::is_same_v<decltype(httpserver::iovec_entry::base),
                             const void*>,
              "iovec_entry::base must be const void*");
static_assert(std::is_same_v<decltype(httpserver::iovec_entry::len),
                             std::size_t>,
              "iovec_entry::len must be std::size_t");

LT_BEGIN_SUITE(iovec_entry_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(iovec_entry_suite)

LT_BEGIN_AUTO_TEST(iovec_entry_suite, default_constructed_pod_holds_values)
    httpserver::iovec_entry e{};
    LT_CHECK_EQ(e.base, nullptr);
    LT_CHECK_EQ(e.len, 0u);
LT_END_AUTO_TEST(default_constructed_pod_holds_values)

LT_BEGIN_AUTO_TEST(iovec_entry_suite, brace_init_assigns_members)
    const char* payload = "hello";
    httpserver::iovec_entry e{payload, 5};
    LT_CHECK_EQ(e.base, static_cast<const void*>(payload));
    LT_CHECK_EQ(e.len, 5u);
LT_END_AUTO_TEST(brace_init_assigns_members)

// Reinterpret-cast bridge from a contiguous range of iovec_entry to
// POSIX struct iovec. This is the cast the library performs when feeding
// libmicrohttpd, and what TASK-010 will rely on when it lands the
// std::span<const iovec_entry> factory.
LT_BEGIN_AUTO_TEST(iovec_entry_suite, reinterpret_cast_to_struct_iovec_preserves_data)
    const char* a = "abc";
    const char* b = "wxyz";
    httpserver::iovec_entry entries[2] = {
        {a, 3},
        {b, 4},
    };
    const struct iovec* posix =
        reinterpret_cast<const struct iovec*>(&entries[0]);
    LT_CHECK_EQ(posix[0].iov_base, const_cast<void*>(static_cast<const void*>(a)));
    LT_CHECK_EQ(posix[0].iov_len, 3u);
    LT_CHECK_EQ(posix[1].iov_base, const_cast<void*>(static_cast<const void*>(b)));
    LT_CHECK_EQ(posix[1].iov_len, 4u);
LT_END_AUTO_TEST(reinterpret_cast_to_struct_iovec_preserves_data)

// Runtime bridge test for the actual production cast path: iovec_entry →
// MHD_IoVec. Mirrors the struct iovec test above but exercises the type
// used at dispatch time in iovec_response::get_raw_response().
LT_BEGIN_AUTO_TEST(iovec_entry_suite, reinterpret_cast_to_MHD_IoVec_preserves_data)
    const char* a = "hello";
    const char* b = "world";
    httpserver::iovec_entry entries[2] = {
        {a, 5},
        {b, 5},
    };
    const MHD_IoVec* mhd =
        reinterpret_cast<const MHD_IoVec*>(&entries[0]);
    LT_CHECK_EQ(mhd[0].iov_base, static_cast<const void*>(a));
    LT_CHECK_EQ(mhd[0].iov_len, 5u);
    LT_CHECK_EQ(mhd[1].iov_base, static_cast<const void*>(b));
    LT_CHECK_EQ(mhd[1].iov_len, 5u);
LT_END_AUTO_TEST(reinterpret_cast_to_MHD_IoVec_preserves_data)

// Verify trivially-copyable guarantee has observable runtime effect:
// a copy-constructed iovec_entry must preserve both members.
LT_BEGIN_AUTO_TEST(iovec_entry_suite, copy_constructed_iovec_entry_preserves_members)
    const char* payload = "data";
    httpserver::iovec_entry original{payload, 4};
    httpserver::iovec_entry copy = original;  // copy construction
    LT_CHECK_EQ(copy.base, static_cast<const void*>(payload));
    LT_CHECK_EQ(copy.len, 4u);
LT_END_AUTO_TEST(copy_constructed_iovec_entry_preserves_members)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
