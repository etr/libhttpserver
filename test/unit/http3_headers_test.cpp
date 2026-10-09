/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string>
#include <vector>
#include <httpserver/detail/http3_request_head.hpp>
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace http = httpserver::http;
std::vector<hd::qpack_field> head_fields() {
    return {{":method", "POST"}, {":scheme", "https"}, {":path", "/items/../hello?x=%2f"}, {":authority", "example.test"},
            {"x-repeat", "a"},   {"x-repeat", "b"},    {"content-length", "0004"},         {"content-length", "4"}};
}
LT_BEGIN_SUITE(http3_headers_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(http3_headers_suite)
LT_BEGIN_AUTO_TEST(http3_headers_suite, preserves_semantic_head_and_checked_lengths)
    auto fields = head_fields();
    http::request_head head;
    LT_ASSERT(hd::http3_convert_request(fields, head));
    LT_CHECK(head.request_protocol == http::protocol::http_3);
    LT_CHECK_EQ(head.raw_target, "/items/../hello?x=%2f");
    LT_CHECK_EQ(head.route_path, "/hello");
    LT_CHECK_EQ(head.head_fields.all("x-repeat").size(), 2u);
    LT_CHECK_EQ(*head.head_fields.first("host"), "example.test");
    std::optional<std::uint64_t> n;
    LT_CHECK(hd::http3_content_length(head.head_fields, n));
    LT_CHECK_EQ(*n, 4u);
LT_END_AUTO_TEST(preserves_semantic_head_and_checked_lengths)
LT_BEGIN_AUTO_TEST(http3_headers_suite, rejects_malformed_pseudo_fields_and_regular_fields)
    for (const auto& field : std::vector<hd::qpack_field>{{"X-UPPER", "x"},
                                                          {"connection", "close"},
                                                          {"transfer-encoding", "chunked"},
                                                          {"te", "gzip"},
                                                          {"host", "other.test"},
                                                          {"x-control", "a\r\nb"},
                                                          {"x-space", " leading"},
                                                          {"content-length", "5"},
                                                          {"content-length", "18446744073709551616"}}) {
        auto fields = head_fields();
        fields.push_back(field);
        http::request_head head;
        LT_CHECK(!hd::http3_convert_request(fields, head));
    }
    for (unsigned index : {0u, 1u, 2u}) {
        auto fields = head_fields();
        fields.erase(fields.begin() + index);
        http::request_head head;
        LT_CHECK(!hd::http3_convert_request(fields, head));
    }
LT_END_AUTO_TEST(rejects_malformed_pseudo_fields_and_regular_fields)
LT_BEGIN_AUTO_TEST(http3_headers_suite, rejects_connect_with_otherwise_valid_pseudo_fields)
    for (const auto& fields : std::vector<std::vector<hd::qpack_field>>{
             {{":method", "CONNECT"}, {":authority", "example.test:443"}},
             {{":method", "CONNECT"}, {":protocol", "websocket"}, {":scheme", "https"}, {":authority", "example.test"}, {":path", "/hello"}, {"x-regular", "yes"}}}) {
        http::request_head head;
        LT_CHECK(!hd::http3_convert_request(fields, head));
    }
LT_END_AUTO_TEST(rejects_connect_with_otherwise_valid_pseudo_fields)
LT_BEGIN_AUTO_TEST(http3_headers_suite, rejects_duplicate_unknown_and_late_pseudo_fields)
    for (const auto& field : std::vector<hd::qpack_field>{{":method", "POST"}, {":unknown", "x"}, {":protocol", "websocket"}}) {
        auto fields = head_fields();
        fields.insert(fields.begin() + 4, field);
        http::request_head head;
        LT_CHECK(!hd::http3_convert_request(fields, head));
    }
    auto fields = head_fields();
    fields.push_back({":path", "/late"});
    http::request_head head;
    LT_CHECK(!hd::http3_convert_request(fields, head));
LT_END_AUTO_TEST(rejects_duplicate_unknown_and_late_pseudo_fields)
LT_BEGIN_AUTO_TEST(http3_headers_suite, trailers_preserve_order_and_reject_framing_and_routing)
    http::fields trailers;
    LT_ASSERT(hd::http3_convert_trailers(std::vector<hd::qpack_field>{{"x-end", "a"}, {"x-end", "b"}}, trailers));
    LT_CHECK_EQ(trailers.all("x-end").size(), 2u);
    for (auto name : {":path", "content-length", "host", "te", "trailer", "connection"}) {
        http::fields out;
        LT_CHECK(!hd::http3_convert_trailers(std::vector<hd::qpack_field>{{name, "x"}}, out));
    }
LT_END_AUTO_TEST(trailers_preserve_order_and_reject_framing_and_routing)
LT_BEGIN_AUTO_TEST(http3_headers_suite, extended_connect_has_canonical_route_and_effective_authority)
    std::vector<hd::qpack_field> fields{{":method", "CONNECT"}, {":protocol", "websocket"}, {":scheme", "https"},
        {":authority", "example.test"}, {":path", "/items/../chat?q=1"}, {"sec-websocket-version", "13"}};
    http::request_head head; hd::http3_connect_metadata connect;
    LT_ASSERT(hd::http3_convert_request(fields, head, connect));
    LT_CHECK(connect.protocol == "websocket"); LT_CHECK_EQ(connect.scheme, "https");
    LT_CHECK(head.request_method.id() == http::method_id::connect);
    LT_CHECK_EQ(head.route_path, "/chat"); LT_CHECK_EQ(head.raw_target, "/items/../chat?q=1");
    LT_CHECK_EQ(*head.head_fields.first("host"), "example.test"); LT_CHECK(!head.head_fields.first(":protocol"));
LT_END_AUTO_TEST(extended_connect_has_canonical_route_and_effective_authority)
LT_BEGIN_AUTO_TEST(http3_headers_suite, metadata_overload_rejects_incomplete_and_invalid_extended_connect)
    const std::vector<hd::qpack_field> base{{":method", "CONNECT"}, {":protocol", "websocket"}, {":scheme", "https"},
        {":authority", "example.test"}, {":path", "/chat"}};
    for (unsigned index : {2u, 3u, 4u}) {
        auto fields = base; fields.erase(fields.begin() + index);
        http::request_head head; hd::http3_connect_metadata connect;
        LT_CHECK(!hd::http3_convert_request(fields, head, connect));
    }
    for (const auto& mutation : std::vector<hd::qpack_field>{{":protocol", "bad protocol"}, {":protocol", ""}, {":scheme", "1http"},
            {":authority", "bad host"}, {":path", "*"}, {":path", "relative"}, {":path", "/bad%gg"}}) {
        auto fields = base;
        for (auto& field : fields) if (field.name == mutation.name) field.value = mutation.value;
        http::request_head head; hd::http3_connect_metadata connect;
        LT_CHECK(!hd::http3_convert_request(fields, head, connect));
    }
LT_END_AUTO_TEST(metadata_overload_rejects_incomplete_and_invalid_extended_connect)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
