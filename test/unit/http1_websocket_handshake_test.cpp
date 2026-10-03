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

#include <string>
#include <httpserver/detail/http1_websocket_handshake.hpp>
#include "./littletest.hpp"
namespace h = httpserver;
namespace {
h::http::request_head valid() {
    h::http::request_head r;
    r.request_protocol = h::http::protocol::http_1_1;
    r.request_method = h::http::method::known(h::http::method_id::get);
    r.head_fields.append("Host", "example.com");
    r.head_fields.append("Upgrade", "websocket");
    r.head_fields.append("Connection", "Upgrade");
    r.head_fields.append("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    r.head_fields.append("Sec-WebSocket-Version", "13");
    return r;
}
}  // namespace
LT_BEGIN_SUITE(handshake_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(handshake_suite)
LT_BEGIN_AUTO_TEST(handshake_suite, accept_vector_and_extension_omission)
    auto r = valid(); r.head_fields.append("Sec-WebSocket-Extensions", "permessage-deflate");
    auto plan = h::detail::negotiate_http1_websocket(r, {});
    LT_CHECK(plan.status.ok());
    LT_CHECK(plan.accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
LT_END_AUTO_TEST(accept_vector_and_extension_omission)
LT_BEGIN_AUTO_TEST(handshake_suite, all_hostile_handshake_fields_refuse)
    for (auto name : {"Host", "Upgrade", "Connection", "Sec-WebSocket-Key", "Sec-WebSocket-Version"}) {
        auto r = valid(); r.head_fields.remove(name);
        LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
    }
    for (auto name : {"Host", "Sec-WebSocket-Key", "Sec-WebSocket-Version", "Origin"}) {
        auto r = valid(); r.head_fields.append(name, "x"); r.head_fields.append(name, "x");
        LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
    }
    for (auto value : {"xwebsocket", "websocket,", ",websocket", "websocket,,other", "websocket x", "websocket/1"}) {
        auto r = valid(); r.head_fields.replace("Upgrade", value);
        LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
    }
    for (auto value : {"", "YQ==", "dGhlIHNhbXBsZSBub25jZR==", "dGhlIHNhbXBsZSBub25jZQ", "dGhlIHNhbXBsZSBub25jZQ===", "********************************"}) {
        auto r = valid(); r.head_fields.replace("Sec-WebSocket-Key", value);
        LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
    }
    for (auto name : {"Transfer-Encoding", "Expect"}) {
        auto r = valid(); r.head_fields.append(name, "x");
        LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
    }
    for (auto value : {"1", "0,0", "-0", "00"}) {
        auto r = valid(); r.head_fields.append("Content-Length", value);
        LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
    }
LT_END_AUTO_TEST(all_hostile_handshake_fields_refuse)
LT_BEGIN_AUTO_TEST(handshake_suite, token_lists_ows_and_zero_body)
    auto r = valid(); r.head_fields.replace("Upgrade", " other, WeBsOcKeT ");
    r.head_fields.replace("Connection", "keep-alive"); r.head_fields.append("Connection", " upgrade ");
    r.head_fields.replace("Sec-WebSocket-Key", "  dGhlIHNhbXBsZSBub25jZQ==\t");
    r.head_fields.append("Content-Length", "0");
    LT_CHECK(h::detail::negotiate_http1_websocket(r, {}).status.ok());
    r.request_method = h::http::method::known(h::http::method_id::post);
    LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
    r = valid(); r.request_protocol = h::http::protocol::http_1_0;
    LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
LT_END_AUTO_TEST(token_lists_ows_and_zero_body)
LT_BEGIN_AUTO_TEST(handshake_suite, version_refusal_is_owned_426)
    auto r = valid(); r.head_fields.replace("Sec-WebSocket-Version", "12");
    auto plan = h::detail::negotiate_http1_websocket(r, {});
    LT_CHECK_EQ(plan.rejection_status.code(), 426);
    LT_CHECK(plan.rejection_fields.first("Sec-WebSocket-Version") == "13");
    r.head_fields.remove("Sec-WebSocket-Key");
    LT_CHECK_EQ(h::detail::negotiate_http1_websocket(r, {}).rejection_status.code(), 400);
LT_END_AUTO_TEST(version_refusal_is_owned_426)
LT_BEGIN_AUTO_TEST(handshake_suite, origin_policy_is_whole_serialized_origin)
    h::ws_upgrade_options options;
    options.allowed_origins = {"https://example.com", "https://[::1]:8443", "null"};
    options.allow_absent_origin = false;
    auto r = valid();
    LT_CHECK_EQ(h::detail::negotiate_http1_websocket(r, options).rejection_status.code(), 403);
    for (auto value : {"https://example.com", "https://[::1]:8443", "null"}) {
        r.head_fields.replace("Origin", value);
        LT_CHECK(h::detail::negotiate_http1_websocket(r, options).status.ok());
    }
    for (auto value : {"https://evil.example.com", "https://example.com:443", "https://EXAMPLE.com"}) {
        r.head_fields.replace("Origin", value);
        LT_CHECK_EQ(h::detail::negotiate_http1_websocket(r, options).rejection_status.code(), 403);
    }
    for (auto value : {"https://example.com/path", "https://example.com https://evil.com", "https://user@example.com", "file:///", "https://example.com:bad", "https://[not-ip]", "https://x:99999"}) {
        r.head_fields.replace("Origin", value);
        LT_CHECK_EQ(h::detail::negotiate_http1_websocket(r, {}).rejection_status.code(), 400);
    }
LT_END_AUTO_TEST(origin_policy_is_whole_serialized_origin)
LT_BEGIN_AUTO_TEST(handshake_suite, protocols_case_server_preference_and_options)
    h::ws_upgrade_options options; options.subprotocols = {"chat", "CHAT"};
    auto r = valid(); r.head_fields.append("Sec-WebSocket-Protocol", "CHAT, other");
    r.head_fields.append("Sec-WebSocket-Protocol", "chat");
    LT_CHECK(h::detail::negotiate_http1_websocket(r, options).selected_subprotocol == "chat");
    r.head_fields.replace("Sec-WebSocket-Protocol", "Chat");
    LT_CHECK(h::detail::negotiate_http1_websocket(r, options).selected_subprotocol.empty());
    options.require_subprotocol = true;
    LT_CHECK(!h::detail::negotiate_http1_websocket(r, options).status.ok());
    for (auto value : {"chat,chat", "chat,", "chat bad", "chat,CHAT,chat"}) {
        r.head_fields.replace("Sec-WebSocket-Protocol", value);
        LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
    }
    options.subprotocols = {"bad token"};
    LT_CHECK(h::detail::negotiate_http1_websocket(valid(), options).status.code() == h::http::outcome_code::invalid_argument);
    options = {}; options.allowed_origins = {"bad"};
    LT_CHECK(h::detail::negotiate_http1_websocket(valid(), options).status.code() == h::http::outcome_code::invalid_argument);
    options = {}; options.limits.incoming_messages = 0;
    LT_CHECK(h::detail::negotiate_http1_websocket(valid(), options).status.code() == h::http::outcome_code::invalid_argument);
    r = valid(); r.head_fields.append("Sec-WebSocket-Extensions", std::string(65536, 'x'));
    LT_CHECK(h::detail::negotiate_http1_websocket(r, {}).status.code() == h::http::outcome_code::limit_exceeded);
LT_END_AUTO_TEST(protocols_case_server_preference_and_options)
LT_BEGIN_AUTO_TEST(handshake_suite, hostile_token_tables_are_bounded)
    auto r = valid(); std::string offers;
    for (int i = 0; i < 257; ++i) {
        if (i) offers += ",";
        offers += "p" + std::to_string(i);
    }
    r.head_fields.append("Sec-WebSocket-Protocol", offers);
    LT_CHECK(!h::detail::negotiate_http1_websocket(r, {}).status.ok());
    h::ws_upgrade_options options;
    for (int i = 0; i < 257; ++i) options.subprotocols.push_back("p" + std::to_string(i));
    LT_CHECK(h::detail::negotiate_http1_websocket(valid(), options).status.code() == h::http::outcome_code::invalid_argument);
LT_END_AUTO_TEST(hostile_token_tables_are_bounded)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
