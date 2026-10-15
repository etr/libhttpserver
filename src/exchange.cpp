/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/exchange.hpp>
#include <memory>
#include <string>
#include <utility>

namespace httpserver {

// cpplint cannot track this constructor's nested qualifiers or braced initializers.
// NOLINTBEGIN(whitespace/indent_namespace)
exchange::exchange(const http::request_head& head, detail::exchange_sink* sink, std::uint64_t connection_id, detail::body_source* body_source, detail::body_sink* response_sink, net::peer_address peer,
                   std::shared_ptr<const server::tls_peer_metadata> tls) noexcept
    : head_(head),
      sink_(sink),
      connection_id_(connection_id),
      body_source_(body_source),
      body_(stop_.get_token()),
      response_sink_(response_sink),
      writer_(stop_.get_token()),
      peer_(peer),
      tls_(std::move(tls)) {}
// NOLINTEND

auto exchange::respond(const http::status& s, const http::fields& f) -> http::outcome {
    if (disconnected_) return closed_failure();
    if (state_ != exchange_state::head && state_ != exchange_state::admitted) {
        return http::outcome(http::outcome_code::invalid_state, "exchange: respond() after a terminal decision");
    }
    state_ = exchange_state::responded;
    suspended_ = false;
    body_.close();
    if (sink_ != nullptr) sink_->on_respond(s, f);
    return http::outcome::okay();
}

auto exchange::start_response(const http::status& s, const http::fields& f) -> http::outcome {
    if (disconnected_) return closed_failure();
    if (state_ != exchange_state::head && state_ != exchange_state::admitted) {
        return http::outcome(http::outcome_code::invalid_state, "exchange: start_response() after a terminal decision");
    }
    state_ = exchange_state::responded;
    suspended_ = false;
    body_.close();
    if (sink_ != nullptr) sink_->on_start_response(s, f);
    writer_.activate(response_sink_);
    return http::outcome::okay();
}

auto exchange::admit_body(const body_policy& policy) -> http::outcome {
    if (disconnected_) return closed_failure();
    if (state_ != exchange_state::head) {
        return http::outcome(http::outcome_code::invalid_state, "exchange: admit_body() is a head-time decision");
    }
    state_ = exchange_state::admitted;
    if (sink_ != nullptr) sink_->on_admit(policy);
    body_.activate(body_source_);
    return http::outcome::okay();
}

auto exchange::suspend(resume_signal& out) -> http::outcome {
    if (disconnected_) return closed_failure();
    if (state_ != exchange_state::head && state_ != exchange_state::admitted) {
        return http::outcome(http::outcome_code::invalid_state, "exchange: suspend() requires an open exchange");
    }
    resume_signal fresh;
    out = fresh;
    resume_signals_.push_back(std::move(fresh));
    suspended_ = true;
    return http::outcome::okay();
}

auto exchange::upgrade(ws_upgrade_options options) -> task<websocket_upgrade_result> {
    websocket_upgrade_result result;
    if (disconnected_) {
        result.status = closed_failure();
        co_return result;
    }
    if (state_ != exchange_state::head) {
        result.status = {http::outcome_code::invalid_state, "exchange: upgrade() is a head-time decision"};
        co_return result;
    }
    if ((head_.request_protocol != http::protocol::http_1_1 && head_.request_protocol != http::protocol::http_2 && head_.request_protocol != http::protocol::http_3) || sink_ == nullptr) {
        result.status = {http::outcome_code::not_supported, "exchange: upgrade protocol unsupported"};
        co_return result;
    }
    result = sink_->on_upgrade(options);
    if (result.status.ok()) {
        state_ = exchange_state::upgraded;
        suspended_ = false;
        body_.close();
    }
    co_return result;
}

auto exchange::disconnect(http::outcome_code reason, std::string detail) -> http::outcome {
    if (reason == http::outcome_code::ok) {
        return http::outcome(http::outcome_code::invalid_argument, "exchange: disconnect() requires a failure reason");
    }
    if (disconnected_) return http::outcome::okay();
    disconnect_reason_ = http::outcome(reason, std::move(detail));
    if (!terminal()) state_ = exchange_state::cancelled;
    body_.note_disconnect(disconnect_reason_);
    writer_.note_disconnect(disconnect_reason_);
    // Publish the immutable reason only after its dependent state is ready.
    // Completion may observe the flag while stop fan-out is still running.
    disconnected_ = true;
    stop_.request_stop();
    for (resume_signal& sig : resume_signals_) {
        sig.cancel();
    }
    return http::outcome::okay();
}

auto exchange::abort() -> http::outcome {
    // A streaming handler may have thrown mid-body; shut the
    // writer down (idempotent) so no further write passes its
    // gate, then let the engine reset per protocol rules.
    writer_.close();
    if (sink_ != nullptr) sink_->on_abort();
    return http::outcome::okay();
}

auto exchange::concurrent_flag::operator=(const concurrent_flag& other) noexcept -> concurrent_flag& {
    value_.store(other.value_.load());
    return *this;
}

auto exchange::concurrent_flag::operator=(bool value) noexcept -> concurrent_flag& {
    value_.store(value);
    return *this;
}

}  // namespace httpserver
