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
#include <vector>
#include "./websocket_test_helpers.hpp"
#include "./littletest.hpp"
using namespace ws_test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(websocket_session_test_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(websocket_session_test_suite)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_send_admission_and_owned_wire)
    test_session s(small()); auto data = bytes("12345678");
    auto r = s.try_send(ws::message_kind::binary, data);
    LT_CHECK(r.status.ok()); LT_CHECK(r.disposition == ws::send_disposition::accepted);
    data[0] = std::byte{0};
    LT_CHECK(s.try_send(ws::message_kind::binary, {}).disposition == ws::send_disposition::backpressured);
    LT_CHECK_EQ(s.usage().output_bytes, std::size_t{10});
    auto wire = output(s); LT_CHECK(wire == frame(2, bytes("12345678"), true, false));
    LT_CHECK_EQ(s.usage().output_bytes, std::size_t{0});
    LT_CHECK(s.try_send(ws::message_kind::text, bytes("\x80")).status.code() == httpserver::http::outcome_code::invalid_argument);
    LT_CHECK(s.try_send(ws::message_kind::binary, bytes("123456789")).status.code() == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK_EQ(s.usage().outgoing_messages, std::size_t{0});
    for (int i = 0; i < 50; ++i) {
        LT_CHECK(s.try_send(ws::message_kind::text, {}).disposition == ws::send_disposition::accepted);
        LT_CHECK(s.try_send(ws::message_kind::text, {}).disposition == ws::send_disposition::accepted);
        LT_CHECK(s.try_send(ws::message_kind::text, {}).disposition == ws::send_disposition::backpressured);
        LT_CHECK_EQ(output(s).size(), std::size_t{4});
    }
LT_END_AUTO_TEST(session_send_admission_and_owned_wire)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_text_send_preserves_opcode_and_payload)
    test_session s(small());
    auto sent = s.try_send(ws::message_kind::text, bytes("hello"));
    LT_CHECK(sent.status.ok());
    LT_CHECK(sent.disposition == ws::send_disposition::accepted);
    LT_CHECK(output(s) == frame(1, bytes("hello"), true, false));
LT_END_AUTO_TEST(session_text_send_preserves_opcode_and_payload)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_receive_and_release)
    test_session s(small()); auto wire = frame(1, bytes("hello"));
    LT_CHECK(s.feed(wire).status.ok()); auto m = receive(s);
    LT_CHECK(m.status.ok()); LT_CHECK(m.value.has_value()); LT_CHECK(m.value->data == bytes("hello"));
    LT_CHECK(m.value->kind == ws::message_kind::text);
    LT_CHECK_EQ(s.usage().incoming_bytes, std::size_t{0});
LT_END_AUTO_TEST(session_receive_and_release)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_receive_binary_preserves_kind)
    test_session s(small());
    LT_CHECK(s.feed(frame(2, bytes("\xff\x80"))).status.ok());
    auto m = receive(s);
    LT_CHECK(m.status.ok()); LT_CHECK(m.value.has_value());
    LT_CHECK(m.value->kind == ws::message_kind::binary);
    LT_CHECK(m.value->data == bytes("\xff\x80"));
LT_END_AUTO_TEST(session_receive_binary_preserves_kind)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_receive_fragmented_text_preserves_kind)
    test_session s(small());
    LT_CHECK(s.feed(frame(1, bytes("hel"), false)).status.ok());
    LT_CHECK(s.feed(frame(0, bytes("lo"))).status.ok());
    auto m = receive(s);
    LT_CHECK(m.status.ok()); LT_CHECK(m.value.has_value());
    LT_CHECK(m.value->kind == ws::message_kind::text);
    LT_CHECK(m.value->data == bytes("hello"));
LT_END_AUTO_TEST(session_receive_fragmented_text_preserves_kind)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_receive_fragmented_binary_preserves_kind)
    test_session s(small());
    LT_CHECK(s.feed(frame(2, bytes("\xff"), false)).status.ok());
    LT_CHECK(s.feed(frame(0, bytes("\x80"))).status.ok());
    auto m = receive(s);
    LT_CHECK(m.status.ok()); LT_CHECK(m.value.has_value());
    LT_CHECK(m.value->kind == ws::message_kind::binary);
    LT_CHECK(m.value->data == bytes("\xff\x80"));
LT_END_AUTO_TEST(session_receive_fragmented_binary_preserves_kind)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_controls_and_partial_frame_priority)
    test_session s(small()); s.try_send(ws::message_kind::binary, bytes("12345678"));
    std::byte first;
    LT_CHECK_EQ(s.copy_output(std::span(&first, 1)), std::size_t{1});
    LT_CHECK(s.consume_output(1).ok()); LT_CHECK(s.feed(frame(9, bytes("p"))).status.ok());
    auto wire = output(s); auto expected = frame(2, bytes("12345678"), true, false);
    expected.erase(expected.begin()); auto pong = frame(10, bytes("p"), true, false);
    expected.insert(expected.end(), pong.begin(), pong.end()); LT_CHECK(wire == expected);
    test_session full(small()); full.try_send(ws::message_kind::binary, bytes("12345678"));
    for (int i = 0; i < 100; ++i) LT_CHECK(full.feed(frame(9, bytes("p"))).status.ok());
    auto all = output(full); auto priority = pong; auto data = frame(2, bytes("12345678"), true, false);
    priority.insert(priority.end(), data.begin(), data.end()); LT_CHECK(all == priority);
LT_END_AUTO_TEST(session_controls_and_partial_frame_priority)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_close_handshake_exactly_once)
    int closed = 0; ws::close_info reason;
    test_session s(small(), [&](ws::close_info info) { ++closed; reason = info; });
    s.try_send(ws::message_kind::binary, bytes("12345678"));
    LT_CHECK(s.close(1000, "done").ok()); LT_CHECK_EQ(closed, 0);
    LT_CHECK(s.try_send(ws::message_kind::text, {}).disposition == ws::send_disposition::closed);
    LT_CHECK(s.feed(frame(8, bytes("\x03\xe8" "bye"))).status.ok()); LT_CHECK_EQ(closed, 0);
    LT_CHECK(output(s) == frame(8, bytes("\x03\xe8" "done"), true, false));
    LT_CHECK_EQ(closed, 1); LT_CHECK(reason.clean); LT_CHECK(reason.code == 1000); LT_CHECK_EQ(reason.reason, "bye");
    s.eof(); s.cancel({httpserver::http::outcome_code::cancelled, "again"});
    LT_CHECK_EQ(closed, 1); LT_CHECK_EQ(s.usage().output_bytes, std::size_t{0});
LT_END_AUTO_TEST(session_close_handshake_exactly_once)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_invalid_close_and_abnormal_terminal)
    test_session s(small());
    for (unsigned c : {999U, 1004U, 1005U, 1006U, 1015U, 2000U, 5000U}) LT_CHECK(!s.close(c).ok());
    LT_CHECK(!s.close(1000, "\x80").ok()); LT_CHECK(!s.close(1000, std::string(124, 'x')).ok());
    LT_CHECK(s.try_send(ws::message_kind::text, {}).disposition == ws::send_disposition::accepted);
    int count = 0; ws::close_info info;
    test_session abnormal(small(), [&](ws::close_info r) { ++count; info = r; });
    abnormal.feed(frame(1, bytes("x"), false)); abnormal.eof(); abnormal.eof();
    LT_CHECK_EQ(count, 1); LT_CHECK(!info.clean); LT_CHECK(!info.code.has_value());
    LT_CHECK(info.status.code() == httpserver::http::outcome_code::connection_closed);
    LT_CHECK_EQ(abnormal.usage().incoming_bytes, std::size_t{0});
LT_END_AUTO_TEST(session_invalid_close_and_abnormal_terminal)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_async_writable_receive_cancel)
    httpserver::manual_executor ex;
    test_session s(small()); s.try_send(ws::message_kind::binary, bytes("12345678"));
    int writable = 0, received = 0;
    httpserver::spawn(ex, s.writable(), [&](httpserver::task_result<httpserver::http::outcome> r) { LT_CHECK(r.has_value()); LT_CHECK(r.value().ok()); ++writable; });
    httpserver::spawn(ex, s.receive(), [&](httpserver::task_result<ws::receive_result> r) {
        LT_CHECK(r.has_value()); LT_CHECK(r.value().status.code() == httpserver::http::outcome_code::cancelled); ++received;
    });
    ex.run_pending(); LT_CHECK_EQ(writable, 0); LT_CHECK_EQ(received, 0);
    output(s); ex.run_pending(); LT_CHECK_EQ(writable, 1);
    s.cancel({httpserver::http::outcome_code::cancelled, "disconnect"}); ex.run_pending(); LT_CHECK_EQ(received, 1);
LT_END_AUTO_TEST(session_async_writable_receive_cancel)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_options_projection)
    httpserver::server::budget_limits b;
    b.set(httpserver::server::resource::ws_message_bytes, 100);
    b.set(httpserver::server::resource::body_buffer_bytes, 200);
    b.set(httpserver::server::resource::response_queue_bytes, 150);
    auto o = ws::options::from_budgets(b); LT_CHECK(o.validate().ok());
    LT_CHECK_EQ(o.max_message_bytes, std::size_t{100}); LT_CHECK_EQ(o.incoming_bytes, std::size_t{200});
    o.incoming_bytes = 99; LT_CHECK(!o.validate().ok());
    o.incoming_bytes = 200; o.output_bytes = 101; LT_CHECK(!o.validate().ok());
LT_END_AUTO_TEST(session_options_projection)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_output_length_boundaries_and_partial_release)
    for (std::size_t n : {125U, 126U, 65535U, 65536U}) {
        ws::options o; o.max_message_bytes = n; o.incoming_bytes = n; o.output_bytes = n + 10;
        test_session s(o); std::vector<std::byte> payload(n, std::byte{0xff});
        LT_CHECK(s.try_send(ws::message_kind::binary, payload).disposition == ws::send_disposition::accepted);
        LT_CHECK(output(s, 17) == frame(2, payload, true, false));
        LT_CHECK_EQ(s.usage().output_bytes, std::size_t{0});
    }
    test_session s(small()); s.try_send(ws::message_kind::binary, bytes("12345678"));
    std::byte into[3]; LT_CHECK_EQ(s.copy_output(into), std::size_t{3});
    LT_CHECK(!s.consume_output(4).ok()); LT_CHECK_EQ(s.usage().output_bytes, std::size_t{10});
    LT_CHECK(s.consume_output(3).ok()); LT_CHECK_EQ(s.usage().output_bytes, std::size_t{7});
    LT_CHECK(s.try_send(ws::message_kind::text, {}).disposition == ws::send_disposition::accepted);
    LT_CHECK_EQ(output(s).size(), std::size_t{9});
LT_END_AUTO_TEST(session_output_length_boundaries_and_partial_release)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_peer_close_without_status_and_local_sent_first)
    int count = 0; ws::close_info info;
    test_session s(small(), [&](ws::close_info r) { ++count; info = r; });
    LT_CHECK(s.feed(frame(8, {})).status.ok()); LT_CHECK_EQ(count, 0);
    LT_CHECK(output(s) == frame(8, {}, true, false)); LT_CHECK_EQ(count, 1);
    LT_CHECK(info.clean); LT_CHECK(!info.code.has_value());
    test_session local(small(), [&](ws::close_info r) { ++count; info = r; });
    LT_CHECK(local.close(1001, "exit").ok()); output(local); LT_CHECK_EQ(count, 1);
    LT_CHECK(local.feed(frame(8, bytes("\x03\xe8"))).status.ok()); LT_CHECK_EQ(count, 2);
    LT_CHECK(info.clean); LT_CHECK(info.code == 1000);
LT_END_AUTO_TEST(session_peer_close_without_status_and_local_sent_first)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_protocol_failure_releases_fragment_and_output)
    int count = 0; ws::close_info info;
    test_session s(small(), [&](ws::close_info r) { ++count; info = r; });
    s.try_send(ws::message_kind::binary, bytes("12345678"));
    s.feed(frame(1, bytes("x"), false));
    LT_CHECK(s.feed(frame(0, bytes("\x80"))).status.code() == httpserver::http::outcome_code::protocol_error);
    s.eof(); LT_CHECK_EQ(count, 1); LT_CHECK(!info.clean);
    LT_CHECK(info.status.code() == httpserver::http::outcome_code::protocol_error);
    LT_CHECK_EQ(s.usage().incoming_bytes, std::size_t{0}); LT_CHECK_EQ(s.usage().output_bytes, std::size_t{0});
    LT_CHECK(s.try_send(ws::message_kind::binary, {}).disposition == ws::send_disposition::closed);
LT_END_AUTO_TEST(session_protocol_failure_releases_fragment_and_output)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_close_callback_registration_is_single)
    test_session s(small()); int count = 0;
    LT_CHECK(!s.on_close({}).ok());
    LT_CHECK(s.on_close([&](ws::close_info) { ++count; }).ok());
    LT_CHECK(!s.on_close([](ws::close_info) {}).ok());
    s.eof(); LT_CHECK_EQ(count, 1);
    LT_CHECK(!s.on_close([](ws::close_info) {}).ok());
LT_END_AUTO_TEST(session_close_callback_registration_is_single)
LT_BEGIN_AUTO_TEST(websocket_session_test_suite, session_answers_ping_while_local_close_awaits_peer)
    test_session s(small());
    LT_CHECK(s.close(1000).ok());
    LT_CHECK(s.feed(frame(9, bytes("p"))).status.ok());
    auto expected = frame(8, bytes("\x03\xe8"), true, false);
    auto pong = frame(10, bytes("p"), true, false);
    expected.insert(expected.end(), pong.begin(), pong.end());
    LT_CHECK(output(s) == expected);
LT_END_AUTO_TEST(session_answers_ping_while_local_close_awaits_peer)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
