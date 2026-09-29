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

// TASK-097 Step 2: compile-time and runtime contract for the v3
// semantic method type (httpserver::http::method / method_id).
//
// Pins (PRD-V3N-REQ-020, DR-V3-001): known-method table order, wire
// tokens, case-insensitive parse with uppercase normalization, RFC 9110
// token validation, extension-method round-trip through the public API,
// the invalid default state, and the absence of any numeric backend ID
// conversion (no v1-compat shim).

#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include <httpserver/http/method.hpp>

#include "./littletest.hpp"

// --- method_id layout pins ------------------------------------------------

static_assert(std::is_same_v<std::underlying_type_t<httpserver::http::method_id>,
                             std::uint8_t>,
              "method_id underlying type must be std::uint8_t");
static_assert(std::is_enum_v<httpserver::http::method_id>,
              "method_id must be a scoped enum");

// Table order pin: known methods in wire-canonical order, extension
// after them, unknown_ sentinel last.
static_assert(static_cast<std::uint8_t>(httpserver::http::method_id::patch) + 1u
                  == static_cast<std::uint8_t>(httpserver::http::method_id::extension),
              "extension must come immediately after the known methods");
static_assert(static_cast<std::uint8_t>(httpserver::http::method_id::extension) + 1u
                  == static_cast<std::uint8_t>(httpserver::http::method_id::unknown_),
              "unknown_ must come immediately after extension");

// --- method type pins ------------------------------------------------------

static_assert(std::is_default_constructible_v<httpserver::http::method>,
              "method must be default constructible");
static_assert(std::is_copy_constructible_v<httpserver::http::method>,
              "method must be copyable");
static_assert(std::is_move_constructible_v<httpserver::http::method>,
              "method must be movable");

// Default method is the invalid/unknown state.
static_assert(!httpserver::http::method{}.valid(),
              "default method must be invalid");
static_assert(httpserver::http::method{}.name().empty(),
              "default method must not carry a wire token");
static_assert(httpserver::http::method{}.id()
                  == httpserver::http::method_id::unknown_,
              "default method id must be unknown_");
static_assert(httpserver::http::method{}.name().empty()
                  == (httpserver::http::to_string(httpserver::http::method{})
                      .empty()),
              "to_string agrees with name() on the invalid state");

// to_string of known methods yields the uppercase wire token; "DELETE"
// is the token for the del enumerator (v2 keyword-avoidance precedent).
static_assert(httpserver::http::to_string(
                  httpserver::http::method::known(httpserver::http::method_id::get))
                  == "GET",
              "wire token for get");
static_assert(httpserver::http::to_string(
                  httpserver::http::method::known(httpserver::http::method_id::del))
                  == "DELETE",
              "del maps to the DELETE wire token");
static_assert(httpserver::http::to_string(
                  httpserver::http::method::known(httpserver::http::method_id::connect))
                  == "CONNECT",
              "wire token for connect");
static_assert(httpserver::http::to_string(
                  httpserver::http::method::known(httpserver::http::method_id::patch))
                  == "PATCH",
              "wire token for patch");

// parse of canonical uppercase tokens lands on the matching id.
static_assert(httpserver::http::method::parse("GET").has_value()
                  && httpserver::http::method::parse("GET")->id()
                         == httpserver::http::method_id::get,
              "parse GET");
static_assert(httpserver::http::method::parse("DELETE").has_value()
                  && httpserver::http::method::parse("DELETE")->id()
                         == httpserver::http::method_id::del,
              "parse DELETE");
static_assert(httpserver::http::method::parse("PATCH").has_value()
                  && httpserver::http::method::parse("PATCH")->id()
                         == httpserver::http::method_id::patch,
              "parse PATCH");
static_assert(!httpserver::http::method::parse("").has_value(),
              "empty token is not a method");
static_assert(!httpserver::http::method::parse("GET POST").has_value(),
              "tokens with separators are rejected");

// known + parse agreement for the full known table.
static_assert(httpserver::http::method::known(httpserver::http::method_id::get)
                  == *httpserver::http::method::parse("GET"),
              "known(get) == parse(GET)");
static_assert(httpserver::http::method::known(httpserver::http::method_id::head)
                  == *httpserver::http::method::parse("HEAD"),
              "known(head) == parse(HEAD)");
static_assert(httpserver::http::method::known(httpserver::http::method_id::post)
                  == *httpserver::http::method::parse("POST"),
              "known(post) == parse(POST)");
static_assert(httpserver::http::method::known(httpserver::http::method_id::put)
                  == *httpserver::http::method::parse("PUT"),
              "known(put) == parse(PUT)");
static_assert(httpserver::http::method::known(httpserver::http::method_id::del)
                  == *httpserver::http::method::parse("DELETE"),
              "known(del) == parse(DELETE)");
static_assert(httpserver::http::method::known(httpserver::http::method_id::options)
                  == *httpserver::http::method::parse("OPTIONS"),
              "known(options) == parse(OPTIONS)");
static_assert(httpserver::http::method::known(httpserver::http::method_id::trace)
                  == *httpserver::http::method::parse("TRACE"),
              "known(trace) == parse(TRACE)");

// No numeric backend IDs: the type must not convert to an integer or a
// bool (detection idiom, no_v1_compat_shim style). Presence of any
// implicit conversion operator would make these traits hold.
static_assert(!std::is_convertible_v<httpserver::http::method, int>,
              "method must not implicitly convert to a numeric id");
static_assert(!std::is_convertible_v<httpserver::http::method, bool>,
              "method must not implicitly convert to bool");
static_assert(!std::is_convertible_v<httpserver::http::method_id, int>,
              "method_id is scoped: no implicit conversion to int");

LT_BEGIN_SUITE(http_semantic_method_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_semantic_method_suite)

LT_BEGIN_AUTO_TEST(http_semantic_method_suite, parse_is_case_insensitive)
    const auto m = httpserver::http::method::parse("get");
    LT_CHECK(m.has_value());
    LT_CHECK(m->id() == httpserver::http::method_id::get);
    LT_CHECK_EQ(m->name(), std::string_view{"GET"});
    LT_CHECK(*m == *httpserver::http::method::parse("GET"));
    LT_CHECK(*m == *httpserver::http::method::parse("GeT"));
LT_END_AUTO_TEST(parse_is_case_insensitive)

LT_BEGIN_AUTO_TEST(http_semantic_method_suite, extension_roundtrips_through_public_api)
    const auto m = httpserver::http::method::parse("PROPFIND");
    LT_CHECK(m.has_value());
    LT_CHECK(m->is_extension());
    LT_CHECK(m->valid());
    LT_CHECK_EQ(m->name(), std::string_view{"PROPFIND"});
    // Round-trip: parse(name()) yields the same method.
    const auto again = httpserver::http::method::parse(m->name());
    LT_CHECK(again.has_value());
    LT_CHECK(*again == *m);
LT_END_AUTO_TEST(extension_roundtrips_through_public_api)

LT_BEGIN_AUTO_TEST(http_semantic_method_suite, extension_factory_normalizes_case)
    const auto m = httpserver::http::method::extension("mkcalendar");
    LT_CHECK(m.is_extension());
    LT_CHECK_EQ(m.name(), std::string_view{"MKCALENDAR"});
    const auto again = httpserver::http::method::parse("MKCALENDAR");
    LT_CHECK(again.has_value());
    LT_CHECK(*again == m);
LT_END_AUTO_TEST(extension_factory_normalizes_case)

LT_BEGIN_AUTO_TEST(http_semantic_method_suite, invalid_extension_names_rejected)
    LT_CHECK(!httpserver::http::method::parse("bad token").has_value());
    LT_CHECK(!httpserver::http::method::parse("GET/1").has_value());
    LT_CHECK(!httpserver::http::method::parse("(X)").has_value());
    LT_CHECK(!httpserver::http::method::parse("WITH\tTAB").has_value());
    LT_CHECK(!httpserver::http::method::parse("WITH\nNL").has_value());
    LT_CHECK(!httpserver::http::method::parse("Q\"UOTED").has_value());
    LT_CHECK(!httpserver::http::method::parse("COMMA,X").has_value());
LT_END_AUTO_TEST(invalid_extension_names_rejected)

LT_BEGIN_AUTO_TEST(http_semantic_method_suite, extension_never_equals_known)
    const auto propfind = httpserver::http::method::extension("PROPFIND");
    const auto get = httpserver::http::method::parse("GET");
    LT_CHECK(get.has_value());
    LT_CHECK(!(propfind == *get));
    LT_CHECK(propfind != *get);
    LT_CHECK(propfind.id() == httpserver::http::method_id::extension);
    LT_CHECK(get->id() == httpserver::http::method_id::get);
LT_END_AUTO_TEST(extension_never_equals_known)

LT_BEGIN_AUTO_TEST(http_semantic_method_suite, default_method_is_invalid)
    httpserver::http::method m;
    LT_CHECK(!m.valid());
    LT_CHECK(m.is_extension() == false);
    LT_CHECK(m.name().empty());
    // Default methods compare equal to each other, and to nothing else.
    LT_CHECK(m == httpserver::http::method{});
    const auto get = httpserver::http::method::parse("GET");
    LT_CHECK(get.has_value());
    LT_CHECK(m != *get);
    // to_string is total: empty for the invalid state.
    LT_CHECK(httpserver::http::to_string(m).empty());
LT_END_AUTO_TEST(default_method_is_invalid)

LT_BEGIN_AUTO_TEST(http_semantic_method_suite, extensions_with_same_name_are_equal)
    LT_CHECK(httpserver::http::method::extension("REPORT")
             == httpserver::http::method::extension("report"));
    LT_CHECK(httpserver::http::method::extension("REPORT")
             != httpserver::http::method::extension("VERSION-CONTROL"));
LT_END_AUTO_TEST(extensions_with_same_name_are_equal)

LT_BEGIN_AUTO_TEST(http_semantic_method_suite, full_known_table_parse_roundtrip)
    const std::pair<httpserver::http::method_id, std::string_view> table[] = {
        {httpserver::http::method_id::get, "GET"},
        {httpserver::http::method_id::head, "HEAD"},
        {httpserver::http::method_id::post, "POST"},
        {httpserver::http::method_id::put, "PUT"},
        {httpserver::http::method_id::del, "DELETE"},
        {httpserver::http::method_id::connect, "CONNECT"},
        {httpserver::http::method_id::options, "OPTIONS"},
        {httpserver::http::method_id::trace, "TRACE"},
        {httpserver::http::method_id::patch, "PATCH"},
    };
    for (const auto& [id, token] : table) {
        const auto m = httpserver::http::method::parse(token);
        LT_CHECK(m.has_value());
        LT_CHECK(!m->is_extension());
        LT_CHECK(m->valid());
        LT_CHECK(m->id() == id);
        LT_CHECK_EQ(m->name(), token);
        LT_CHECK(httpserver::http::method::known(id) == *m);
        LT_CHECK_EQ(httpserver::http::to_string(*m), token);
    }
LT_END_AUTO_TEST(full_known_table_parse_roundtrip)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
