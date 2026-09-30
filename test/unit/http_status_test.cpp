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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-102 Step 1: http::status — the validated numeric response status
// type seeding the header-time exchange decisions. Pins:
//   - from_code() accepts exactly 100..599 (both bounds inclusive) and
//     rejects anything outside, keeping the stored value inspectable;
//   - code() round-trips the stored value;
//   - the five RFC 9110 category predicates partition the valid range;
//   - the default-constructed value is invalid;
//   - value semantics: copyable and equality-by-code.

#include <cstdint>
#include <type_traits>

#include <httpserver/http/status.hpp>

#include "./littletest.hpp"

using httpserver::http::status;

static_assert(std::is_default_constructible_v<status>,
              "status is a default-constructible value type");
static_assert(std::is_copy_constructible_v<status>,
              "status is a copyable value type");
static_assert(std::is_copy_assignable_v<status>,
              "status is a copyable value type");
static_assert(std::is_nothrow_move_constructible_v<status>,
              "status moves without throwing");

LT_BEGIN_SUITE(http_status_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_status_suite)

LT_BEGIN_AUTO_TEST(http_status_suite, from_code_accepts_valid_range)
    const std::uint16_t accepted[] = {
        100, 101, 200, 201, 204, 301, 304, 404, 413, 418, 500, 501, 599,
    };
    for (const std::uint16_t code : accepted) {
        const status s = status::from_code(code);
        LT_CHECK(s.valid());
        LT_CHECK_EQ(s.code(), code);
    }
LT_END_AUTO_TEST(from_code_accepts_valid_range)

LT_BEGIN_AUTO_TEST(http_status_suite, from_code_rejects_out_of_range)
    const std::uint16_t rejected[] = {0, 1, 99, 600, 601, 999, 65535};
    for (const std::uint16_t code : rejected) {
        const status s = status::from_code(code);
        LT_CHECK(!s.valid());
        LT_CHECK_EQ(s.code(), code);
    }
LT_END_AUTO_TEST(from_code_rejects_out_of_range)

LT_BEGIN_AUTO_TEST(http_status_suite, default_value_is_invalid)
    const status s;
    LT_CHECK(!s.valid());
    LT_CHECK_EQ(s.code(), static_cast<std::uint16_t>(0));
LT_END_AUTO_TEST(default_value_is_invalid)

LT_BEGIN_AUTO_TEST(http_status_suite, category_predicates_partition_range)
    const status informational = status::from_code(100);
    LT_CHECK(informational.informational());
    LT_CHECK(!informational.success());
    LT_CHECK(!informational.redirection());
    LT_CHECK(!informational.client_error());
    LT_CHECK(!informational.server_error());

    const status success = status::from_code(204);
    LT_CHECK(!success.informational());
    LT_CHECK(success.success());
    LT_CHECK(!success.redirection());
    LT_CHECK(!success.client_error());
    LT_CHECK(!success.server_error());

    const status redirection = status::from_code(301);
    LT_CHECK(!redirection.informational());
    LT_CHECK(!redirection.success());
    LT_CHECK(redirection.redirection());
    LT_CHECK(!redirection.client_error());
    LT_CHECK(!redirection.server_error());

    const status client = status::from_code(404);
    LT_CHECK(!client.informational());
    LT_CHECK(!client.success());
    LT_CHECK(!client.redirection());
    LT_CHECK(client.client_error());
    LT_CHECK(!client.server_error());

    const status server = status::from_code(500);
    LT_CHECK(!server.informational());
    LT_CHECK(!server.success());
    LT_CHECK(!server.redirection());
    LT_CHECK(!server.client_error());
    LT_CHECK(server.server_error());

    const status invalid;
    LT_CHECK(!invalid.informational());
    LT_CHECK(!invalid.success());
    LT_CHECK(!invalid.redirection());
    LT_CHECK(!invalid.client_error());
    LT_CHECK(!invalid.server_error());
LT_END_AUTO_TEST(category_predicates_partition_range)

LT_BEGIN_AUTO_TEST(http_status_suite, equality_by_code)
    const status ok_a = status::from_code(200);
    const status ok_b = status::from_code(200);
    const status not_found = status::from_code(404);
    LT_CHECK(ok_a == ok_b);
    LT_CHECK(!(ok_a != ok_b));
    LT_CHECK(ok_a != not_found);
    LT_CHECK(!(ok_a == not_found));

    const status invalid_a;
    const status invalid_b;
    LT_CHECK(invalid_a == invalid_b);
    LT_CHECK(invalid_a != ok_a);
LT_END_AUTO_TEST(equality_by_code)

LT_BEGIN_AUTO_TEST(http_status_suite, copy_keeps_value)
    const status original = status::from_code(413);
    const status copy = original;
    LT_CHECK(copy == original);
    LT_CHECK_EQ(copy.code(), static_cast<std::uint16_t>(413));
    LT_CHECK(copy.client_error());
LT_END_AUTO_TEST(copy_keeps_value)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
