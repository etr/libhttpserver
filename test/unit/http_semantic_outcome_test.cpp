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

// TASK-097 Step 1: compile-time and runtime contract for the v3
// semantic outcome vocabulary (httpserver::http::outcome_code /
// httpserver::http::outcome) and protocol value type
// (httpserver::http::protocol).
//
// Pins (PRD-V3N-REQ-037, DR-V3-001): outcome codes are libhttpserver-
// owned (no libmicrohttpd / MHD numeric values anywhere), the ok code
// is zero, outcome is default-constructible to ok, and protocol is a
// small closed libhttpserver enum with total to_string and exact-token
// parse.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <httpserver/http/outcome.hpp>
#include <httpserver/http/protocol.hpp>

#include "./littletest.hpp"

// --- outcome_code layout pins -------------------------------------------

static_assert(std::is_same_v<std::underlying_type_t<httpserver::http::outcome_code>,
                             std::uint8_t>,
              "outcome_code underlying type must be std::uint8_t");
static_assert(std::is_enum_v<httpserver::http::outcome_code>,
              "outcome_code must be an enum (scoped)");
static_assert(static_cast<std::uint8_t>(httpserver::http::outcome_code::ok) == 0,
              "outcome_code::ok must be zero");

// --- outcome type pins ---------------------------------------------------

static_assert(std::is_standard_layout_v<httpserver::http::outcome> ==
                  std::is_standard_layout_v<std::string>,
              "outcome is a value type with a std::string member");
static_assert(std::is_default_constructible_v<httpserver::http::outcome>,
              "outcome must be default constructible");

// Default outcome is ok with an empty diagnostic.
static_assert(httpserver::http::outcome{}.ok(),
              "default-constructed outcome must be ok");
static_assert(httpserver::http::outcome{}.code() ==
                  httpserver::http::outcome_code::ok,
              "default-constructed outcome code must be ok");

// --- protocol layout pins -------------------------------------------------

static_assert(std::is_same_v<std::underlying_type_t<httpserver::http::protocol>,
                             std::uint8_t>,
              "protocol underlying type must be std::uint8_t");
static_assert(std::is_enum_v<httpserver::http::protocol>,
              "protocol must be a scoped enum");

// to_string totality: the four in-range values and one out-of-range probe.
static_assert(httpserver::http::to_string(httpserver::http::protocol::http_1_0)
                  == "HTTP/1.0",
              "protocol::http_1_0 string form");
static_assert(httpserver::http::to_string(httpserver::http::protocol::http_1_1)
                  == "HTTP/1.1",
              "protocol::http_1_1 string form");
static_assert(httpserver::http::to_string(httpserver::http::protocol::http_2)
                  == "HTTP/2",
              "protocol::http_2 string form");
static_assert(httpserver::http::to_string(httpserver::http::protocol::http_3)
                  == "HTTP/3",
              "protocol::http_3 string form");
static_assert(httpserver::http::to_string(
                  static_cast<httpserver::http::protocol>(99)).empty(),
              "to_string(protocol) must be total: empty for out-of-range");

// parse exact tokens.
static_assert(httpserver::http::parse("HTTP/1.0") ==
                  std::optional(httpserver::http::protocol::http_1_0),
              "parse HTTP/1.0");
static_assert(httpserver::http::parse("HTTP/1.1") ==
                  std::optional(httpserver::http::protocol::http_1_1),
              "parse HTTP/1.1");
static_assert(httpserver::http::parse("HTTP/2") ==
                  std::optional(httpserver::http::protocol::http_2),
              "parse HTTP/2");
static_assert(httpserver::http::parse("HTTP/3") ==
                  std::optional(httpserver::http::protocol::http_3),
              "parse HTTP/3");
static_assert(!httpserver::http::parse("HTTP/9").has_value(),
              "parse must reject unknown protocol tokens");
static_assert(!httpserver::http::parse("http/1.1").has_value(),
              "parse is exact-token (case sensitive)");
static_assert(!httpserver::http::parse("").has_value(),
              "parse must reject the empty token");

LT_BEGIN_SUITE(http_semantic_outcome_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_semantic_outcome_suite)

LT_BEGIN_AUTO_TEST(http_semantic_outcome_suite, error_outcome_carries_code_and_message)
    const httpserver::http::outcome o{httpserver::http::outcome_code::limit_exceeded,
                                      "too many headers"};
    LT_CHECK(!o.ok());
    LT_CHECK(o.code() == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK_EQ(o.message(), std::string{"too many headers"});
LT_END_AUTO_TEST(error_outcome_carries_code_and_message)

LT_BEGIN_AUTO_TEST(http_semantic_outcome_suite, every_outcome_code_constructs)
    // The full taxonomy is constructible with a diagnostic and each
    // reports !ok except ok itself.
    const httpserver::http::outcome_code codes[] = {
        httpserver::http::outcome_code::invalid_argument,
        httpserver::http::outcome_code::invalid_state,
        httpserver::http::outcome_code::limit_exceeded,
        httpserver::http::outcome_code::protocol_error,
        httpserver::http::outcome_code::not_supported,
        httpserver::http::outcome_code::connection_closed,
        httpserver::http::outcome_code::cancelled,
        httpserver::http::outcome_code::timeout,
        httpserver::http::outcome_code::would_deadlock,
    };
    for (const auto c : codes) {
        const httpserver::http::outcome o{c, "diagnostic"};
        LT_CHECK(!o.ok());
        LT_CHECK(o.code() == c);
        LT_CHECK_EQ(o.message(), std::string{"diagnostic"});
    }
LT_END_AUTO_TEST(every_outcome_code_constructs)

LT_BEGIN_AUTO_TEST(http_semantic_outcome_suite, okay_instance_is_stable)
    const httpserver::http::outcome& ok1 = httpserver::http::outcome::okay();
    const httpserver::http::outcome& ok2 = httpserver::http::outcome::okay();
    LT_CHECK(&ok1 == &ok2);
    LT_CHECK(ok1.ok());
    LT_CHECK(ok1.message().empty());
    LT_CHECK(httpserver::http::outcome{}.message().empty());
LT_END_AUTO_TEST(okay_instance_is_stable)

LT_BEGIN_AUTO_TEST(http_semantic_outcome_suite, protocol_parse_and_to_string_roundtrip)
    for (const auto p : {httpserver::http::protocol::http_1_0,
                         httpserver::http::protocol::http_1_1,
                         httpserver::http::protocol::http_2,
                         httpserver::http::protocol::http_3}) {
        const auto parsed = httpserver::http::parse(httpserver::http::to_string(p));
        LT_CHECK(parsed.has_value());
        LT_CHECK(*parsed == p);
    }
LT_END_AUTO_TEST(protocol_parse_and_to_string_roundtrip)

LT_BEGIN_AUTO_TEST(http_semantic_outcome_suite, protocol_rejects_near_miss_tokens)
    LT_CHECK(!httpserver::http::parse("HTTP/1").has_value());
    LT_CHECK(!httpserver::http::parse("HTTP/1.2").has_value());
    LT_CHECK(!httpserver::http::parse("HTTP/10").has_value());
    LT_CHECK(!httpserver::http::parse("HTTP/2 ").has_value());
    LT_CHECK(!httpserver::http::parse(" HTTP/2").has_value());
LT_END_AUTO_TEST(protocol_rejects_near_miss_tokens)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
