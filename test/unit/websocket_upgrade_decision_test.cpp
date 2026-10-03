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
#include <utility>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/detail/http1_response_outbox.hpp>
#include <httpserver/concurrency/executor.hpp>
#include "./littletest.hpp"
namespace h = httpserver;
namespace {
class rejecting_sink : public h::detail::exchange_sink {
 public:
    int attempts = 0;
    void on_admit(const h::body_policy&) override { }
    void on_respond(const h::http::status&, const h::http::fields&) override { }
    void on_abort() override { }
    h::websocket_upgrade_result on_upgrade(const h::ws_upgrade_options&) override {
        ++attempts; h::websocket_upgrade_result out;
        out.status = {h::http::outcome_code::protocol_error, "refused"};
        return out;
    }
};
h::websocket_upgrade_result complete(h::task<h::websocket_upgrade_result> t) {
    h::manual_executor ex; std::optional<h::websocket_upgrade_result> result;
    h::spawn(ex, std::move(t), [&](auto r) { result.emplace(std::move(r.value())); });
    ex.run_pending(); return std::move(*result);
}
h::http::request_head head() { h::http::request_head r; r.request_protocol = h::http::protocol::http_1_1; return r; }
}  // namespace
LT_BEGIN_SUITE(upgrade_decision_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(upgrade_decision_suite)
LT_BEGIN_AUTO_TEST(upgrade_decision_suite, lazy_upgrade_owns_options_and_rechecks_terminal)
    h::detail::recording_sink sink; auto request = head(); h::exchange x(request, &sink);
    h::ws_upgrade_options options; options.subprotocols = {"chat"};
    auto pending = x.upgrade(options); options.subprotocols.clear();
    LT_CHECK(x.state() == h::exchange_state::head); LT_CHECK_EQ(sink.upgrade_calls, 0);
    auto result = complete(std::move(pending));
    LT_CHECK(result.status.ok()); LT_CHECK(result.session.has_value());
    LT_CHECK_EQ(sink.upgrade_subprotocols, std::size_t{1});
    LT_CHECK(x.state() == h::exchange_state::upgraded);
    LT_CHECK(complete(x.upgrade({})).status.code() == h::http::outcome_code::invalid_state);
    LT_CHECK_EQ(sink.upgrade_calls, 1);
    h::exchange other(request, &sink); auto abandoned = other.upgrade({});
    other.respond(h::http::status::from_code(200), {});
    LT_CHECK(complete(std::move(abandoned)).status.code() == h::http::outcome_code::invalid_state);
    LT_CHECK_EQ(sink.upgrade_calls, 1);
LT_END_AUTO_TEST(lazy_upgrade_owns_options_and_rechecks_terminal)
LT_BEGIN_AUTO_TEST(upgrade_decision_suite, refusal_retains_response_and_suspension)
    rejecting_sink sink; auto request = head(); h::exchange x(request, &sink); h::resume_signal signal;
    x.suspend(signal); auto result = complete(x.upgrade({}));
    LT_CHECK(!result.status.ok()); LT_CHECK(!result.session);
    LT_CHECK(x.state() == h::exchange_state::head); LT_CHECK(x.suspended());
    LT_CHECK(x.respond(result.rejection_status, result.rejection_fields).ok());
    LT_CHECK_EQ(sink.attempts, 1);
LT_END_AUTO_TEST(refusal_retains_response_and_suspension)
LT_BEGIN_AUTO_TEST(upgrade_decision_suite, trusted_upgrade_commit_preserves_order_and_bounds)
    h::detail::http1_outbox_budget budget; budget.max_queue_bytes = 200;
    h::detail::http1_response_outbox outbox(budget); h::stop_source cancel;
    auto& first = outbox.open(0, cancel.get_token());
    auto& upgrade = outbox.open(1, cancel.get_token());
    LT_CHECK(first.commit_upgrade("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "bad\r\nInjected: yes").code() == h::http::outcome_code::invalid_argument);
    LT_CHECK(first.commit_upgrade("s3pPLMBiTxaQ9kYGzzhZRbK+xOp=", "").code() == h::http::outcome_code::invalid_argument);
    LT_CHECK(first.commit_upgrade("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", std::string(1024, 'x')).code() == h::http::outcome_code::limit_exceeded);
    LT_CHECK(first.commit_upgrade("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "").ok());
    LT_CHECK(upgrade.commit_upgrade("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "").code() == h::http::outcome_code::limit_exceeded);
    std::byte buffer[512]; auto count = outbox.copy_front(buffer); outbox.consume_front(count);
    LT_CHECK(upgrade.commit_upgrade("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "chat").ok());
    count = outbox.copy_front(buffer);
    std::string wire(reinterpret_cast<char*>(buffer), count);
    LT_CHECK(wire.starts_with("HTTP/1.1 101 Switching Protocols\r\n"));
    LT_CHECK(wire.find("Connection: Upgrade\r\n") != std::string::npos);
    LT_CHECK(wire.find("Sec-WebSocket-Protocol: chat\r\n") != std::string::npos);
    LT_CHECK(wire.find("Content-Length") == std::string::npos);
    outbox.consume_front(count); LT_CHECK(outbox.empty());
LT_END_AUTO_TEST(trusted_upgrade_commit_preserves_order_and_bounds)
LT_BEGIN_AUTO_TEST(upgrade_decision_suite, interim_consumption_releases_all_queue_capacity)
    h::detail::http1_response_outbox outbox; h::stop_source stop;
    auto& slot = outbox.open(0, stop.get_token());
    LT_CHECK(slot.interim(100).ok());
    std::byte buffer[512]; auto count = outbox.copy_front(buffer); outbox.consume_front(count);
    auto request = head(); h::http::fields fields; fields.append("Content-Length", "0");
    LT_CHECK(slot.start(request, h::http::status::from_code(200), fields).ok());
    slot.push_end({}); count = outbox.copy_front(buffer); outbox.consume_front(count);
    LT_CHECK(outbox.empty()); LT_CHECK_EQ(outbox.queued_bytes(), std::size_t{0});
LT_END_AUTO_TEST(interim_consumption_releases_all_queue_capacity)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
