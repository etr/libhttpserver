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

// Unit tests for iovec_response: constructor variants, response code,
// content-type forwarding, and move semantics. These tests exercise the
// class without starting the MHD daemon, so they do not call
// get_raw_response().

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// Security: iovec_response must NOT be copy-constructible or copy-assignable.
// The owning constructor stores void* pointers into owned_buffers_ strings
// inside entries_. A defaulted copy would shallow-copy entries_ while
// deep-copying owned_buffers_ (new addresses), leaving entries_ dangling after
// the source is destroyed (CWE-416 use-after-free). Deleting copy forces
// callers onto move-only semantics, which is safe because std::vector move
// transfers the heap block, keeping string addresses stable.
static_assert(!std::is_copy_constructible_v<httpserver::iovec_response>,
              "iovec_response must not be copy-constructible (UAF risk on owning path)");
static_assert(!std::is_copy_assignable_v<httpserver::iovec_response>,
              "iovec_response must not be copy-assignable (UAF risk on owning path)");

// Move semantics must still work.
static_assert(std::is_move_constructible_v<httpserver::iovec_response>,
              "iovec_response must be move-constructible");
static_assert(std::is_move_assignable_v<httpserver::iovec_response>,
              "iovec_response must be move-assignable");

LT_BEGIN_SUITE(iovec_response_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(iovec_response_suite)

// Owning constructor: accepts std::vector<std::string>.
LT_BEGIN_AUTO_TEST(iovec_response_suite, owning_constructor_sets_response_code)
    std::vector<std::string> parts = {"hello", " world"};
    httpserver::iovec_response resp(parts, 200, "text/plain");
    LT_CHECK_EQ(resp.get_response_code(), 200);
LT_END_AUTO_TEST(owning_constructor_sets_response_code)

// Verify content-type forwarding for the owning constructor.
LT_BEGIN_AUTO_TEST(iovec_response_suite, owning_constructor_forwards_content_type)
    std::vector<std::string> parts = {"hello"};
    httpserver::iovec_response resp(parts, 200, "application/json");
    LT_CHECK_EQ(resp.get_header("Content-Type"), "application/json");
LT_END_AUTO_TEST(owning_constructor_forwards_content_type)

// Move constructor: source parts are consumed; response code is correct.
// This is the intended usage pattern in the dispatch path (shared_ptr +
// std::move). After the move, the moved-from vector is empty.
LT_BEGIN_AUTO_TEST(iovec_response_suite, owning_constructor_move_leaves_source_empty)
    std::vector<std::string> parts = {"hello", " world"};
    httpserver::iovec_response resp(std::move(parts), 201, "application/json");
    LT_CHECK_EQ(resp.get_response_code(), 201);
    LT_CHECK_EQ(parts.empty(), true);
LT_END_AUTO_TEST(owning_constructor_move_leaves_source_empty)

// Non-owning constructor: accepts std::vector<iovec_entry> (caller-owned
// buffers). This is TASK-004's genuine zero-copy path: the caller holds the
// data and passes pointer+length pairs directly.
LT_BEGIN_AUTO_TEST(iovec_response_suite, non_owning_constructor_sets_response_code)
    const char* buf1 = "hello";
    const char* buf2 = " world";
    std::vector<httpserver::iovec_entry> entries = {
        {buf1, 5},
        {buf2, 6},
    };
    httpserver::iovec_response resp(entries, 200, "text/plain");
    LT_CHECK_EQ(resp.get_response_code(), 200);
LT_END_AUTO_TEST(non_owning_constructor_sets_response_code)

// Verify content-type forwarding for the non-owning constructor.
LT_BEGIN_AUTO_TEST(iovec_response_suite, non_owning_constructor_forwards_content_type)
    const char* buf = "hello";
    std::vector<httpserver::iovec_entry> entries = {{buf, 5}};
    httpserver::iovec_response resp(entries, 200, "text/html");
    LT_CHECK_EQ(resp.get_header("Content-Type"), "text/html");
LT_END_AUTO_TEST(non_owning_constructor_forwards_content_type)

LT_BEGIN_AUTO_TEST(iovec_response_suite, non_owning_constructor_custom_code)
    const char* buf = "not found";
    std::vector<httpserver::iovec_entry> entries = {{buf, 9}};
    httpserver::iovec_response resp(entries, 404, "text/plain");
    LT_CHECK_EQ(resp.get_response_code(), 404);
LT_END_AUTO_TEST(non_owning_constructor_custom_code)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
