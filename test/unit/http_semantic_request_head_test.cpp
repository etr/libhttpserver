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

// TASK-097 Step 4: request_head seed struct — raw target vs route path
// (PRD-V3N-REQ-019) plus method/protocol/fields membership.
//
// Pins: request_head is an aggregate holding the exact bytes of the
// request-target (raw_target, never rewritten by the library) next to
// the separately validated and normalized path used for route matching
// (route_path, produced only by validation/normalization). The two are
// distinct members: setting one never aliases or affects the other.

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <httpserver/http/request_head.hpp>

#include "./littletest.hpp"

// Aggregate layout pin: no user-declared constructors, so aggregate
// initialization works and the struct stays a plain value carrier.
static_assert(std::is_aggregate_v<httpserver::http::request_head>,
              "request_head must be an aggregate");
static_assert(std::is_default_constructible_v<httpserver::http::request_head>,
              "request_head must be default constructible");
static_assert(std::is_copy_constructible_v<httpserver::http::request_head>,
              "request_head must be copyable");
static_assert(std::is_move_constructible_v<httpserver::http::request_head>,
              "request_head must be movable");

LT_BEGIN_SUITE(http_semantic_request_head_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_semantic_request_head_suite)

LT_BEGIN_AUTO_TEST(http_semantic_request_head_suite, raw_target_and_route_path_differ_explicitly)
    // GET /a%2Fb?q#frag — raw_target keeps the received bytes verbatim
    // (percent-escape, query, fragment); route_path is what the
    // validation/normalization step produces for matching.
    const httpserver::http::request_head head{
        "/a%2Fb?q#frag",   // raw_target
        "/a/b",            // route_path (normalized)
        httpserver::http::method::known(httpserver::http::method_id::get),
        httpserver::http::protocol::http_1_1,
        {},
    };

    LT_CHECK_EQ(head.raw_target, std::string{"/a%2Fb?q#frag"});
    LT_CHECK_EQ(head.route_path, std::string{"/a/b"});
    LT_CHECK(head.raw_target != head.route_path);

    // The raw target is never rewritten: a route path without the
    // escape/query/fragment does not feed back into it.
    LT_CHECK(head.raw_target.find("%2F") != std::string::npos);
    LT_CHECK(head.route_path.find("%2F") == std::string::npos);
    LT_CHECK(head.route_path.find('?') == std::string::npos);
    LT_CHECK(head.route_path.find('#') == std::string::npos);
LT_END_AUTO_TEST(raw_target_and_route_path_differ_explicitly)

LT_BEGIN_AUTO_TEST(http_semantic_request_head_suite, members_never_alias)
    httpserver::http::request_head head{};
    head.raw_target = "/original";
    LT_CHECK(head.route_path.empty());

    head.route_path = "/normalized";
    // Setting route_path left raw_target untouched.
    LT_CHECK_EQ(head.raw_target, std::string{"/original"});

    head.raw_target = "/changed";
    LT_CHECK_EQ(head.route_path, std::string{"/normalized"});
LT_END_AUTO_TEST(members_never_alias)

LT_BEGIN_AUTO_TEST(http_semantic_request_head_suite, holds_method_protocol_and_fields)
    httpserver::http::request_head head{};
    head.request_method = httpserver::http::method::parse("POST").value();
    head.request_protocol = httpserver::http::protocol::http_2;
    head.head_fields.append("X-Trace", "t1");
    head.head_fields.append("x-trace", "t2");

    LT_CHECK(head.request_method.id() == httpserver::http::method_id::post);
    LT_CHECK(head.request_protocol == httpserver::http::protocol::http_2);
    LT_CHECK_EQ(head.head_fields.count("X-TRACE"), std::size_t{2});
    LT_CHECK(head.head_fields.first("x-trace")
             == std::optional(std::string_view{"t1"}));
LT_END_AUTO_TEST(holds_method_protocol_and_fields)

LT_BEGIN_AUTO_TEST(http_semantic_request_head_suite, copy_and_move_sanity)
    httpserver::http::request_head head{};
    head.raw_target = "/t?a=1";
    head.route_path = "/t";
    head.request_method = httpserver::http::method::known(
        httpserver::http::method_id::patch);
    head.request_protocol = httpserver::http::protocol::http_3;
    head.head_fields.append("Accept", "application/json");

    const httpserver::http::request_head copy = head;
    LT_CHECK(copy.raw_target == head.raw_target);
    LT_CHECK(copy.route_path == head.route_path);
    LT_CHECK(copy.request_method == head.request_method);
    LT_CHECK(copy.request_protocol == head.request_protocol);
    LT_CHECK(copy.head_fields == head.head_fields);

    httpserver::http::request_head moved = std::move(copy);
    LT_CHECK_EQ(moved.raw_target, std::string{"/t?a=1"});
    LT_CHECK_EQ(moved.route_path, std::string{"/t"});
    LT_CHECK(moved.request_method
             == httpserver::http::method::known(
                    httpserver::http::method_id::patch));
    LT_CHECK_EQ(moved.head_fields.count("accept"), std::size_t{1});
LT_END_AUTO_TEST(copy_and_move_sanity)

LT_BEGIN_AUTO_TEST(http_semantic_request_head_suite, default_head_is_valid_empty_state)
    const httpserver::http::request_head head{};
    LT_CHECK(head.raw_target.empty());
    LT_CHECK(head.route_path.empty());
    LT_CHECK(!head.request_method.valid());
    LT_CHECK(head.request_protocol == httpserver::http::protocol::http_1_0);
    LT_CHECK(head.head_fields.empty());
LT_END_AUTO_TEST(default_head_is_valid_empty_state)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
