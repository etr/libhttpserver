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

#ifndef SRC_HTTPSERVER_EXCHANGE_HPP_
#define SRC_HTTPSERVER_EXCHANGE_HPP_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>

namespace httpserver {

// Progress of one routed request head through its exchange. A head
// starts at `head` with the decision pending; the handler takes at
// most one terminal action (respond or upgrade), optionally after
// admitting the body. A disconnect before a terminal decision moves a
// non-terminal state to `cancelled`; terminal states stay terminal.
enum class exchange_state : std::uint8_t {
    head,       // complete head routed; decision pending
    admitted,   // body admission accepted; response still available
    responded,  // terminal: response committed to the engine
    upgraded,   // terminal: connection ownership transferred
    cancelled,  // disconnected before a terminal decision
};

// Admission-time contract for the request body. The seed carries the
// buffering cap; the body delivery tasks extend it with the streaming
// knobs. Zero means "use the engine's configured default".
struct body_policy {
    std::uint64_t max_buffer_bytes = 0;
};

// Options of a ws upgrade decision. Subprotocols are offered in order
// for the upgrade-based subprotocol negotiation (RFC 6455); the
// session codec itself arrives with the ws milestone.
struct ws_upgrade_options {
    std::vector<std::string> subprotocols;
};

namespace detail {

// Engine-facing seam of one exchange, implemented once per protocol
// engine (the HTTP/1 engine arrives with the request-handling tasks).
// Consumers write handlers against exchange and never implement
// transports. The class is declared in this public header because the
// inline decision methods below must invoke it and a public header may
// not include a private one; it is engine plumbing, not consumer
// surface.
class exchange_sink {
 public:
    virtual ~exchange_sink() = default;

    // Body admission accepted: may emit the interim 100 response and
    // starts bounded body delivery.
    virtual void on_admit(const body_policy& policy) = 0;

    // Response head committed; serialized per protocol version rules.
    virtual void on_respond(const http::status& s,
                            const http::fields& f) = 0;

    // Connection ownership transferred to the upgrade session.
    virtual void on_upgrade(const ws_upgrade_options& options) = 0;

    // Post-commit failure (e.g. a handler threw after responding):
    // reset/close per protocol rules.
    virtual void on_abort() = 0;
};

}  // namespace detail

// One routed request head and its header-time decisions (architecture
// §3.1, DR-V3-003). The engine constructs an exchange when a complete
// head has been routed and hands it to the route handler by reference;
// consumers never construct one (the constructor requires the private
// engine seam).
//
// Decisions are legal only before body delivery, per state:
//   respond(status, fields)  from head or admitted  -> responded
//   admit_body(policy)       from head              -> admitted
//   suspend(out)             from head or admitted  (state unchanged)
//   upgrade(options)         from an HTTP/1.1 head  -> upgraded
// Every decision returns http::outcome; a typed failure leaves the
// state, the suspension flag, and the engine untouched (a double
// terminal action therefore reaches the engine exactly once).
//
// Threading contract: decisions run on the handler's executor thread.
// disconnect() is engine-facing and may run on any thread while the
// handler is suspended; its wake-ups are posted through the resume and
// cancellation machinery, which orders any later handler read after
// the disconnect writes. A disconnect is a final notification: there
// is no reconnect and no second exchange for the same head.
class exchange {
 public:
    // Engine construction only. `sink` receives every committed
    // decision; `connection_id` identifies the underlying connection
    // for engine bookkeeping.
    exchange(const http::request_head& head, detail::exchange_sink* sink,
             std::uint64_t connection_id = 0) noexcept
        : head_(head), sink_(sink), connection_id_(connection_id) { }

    exchange(exchange&& other) noexcept = default;
    exchange& operator=(exchange&& other) noexcept = default;
    exchange(const exchange&) = delete;
    exchange& operator=(const exchange&) = delete;

    // The routed head, exactly as received (raw_target preserved;
    // route_path is the matching input).
    const http::request_head& head() const noexcept { return head_; }

    exchange_state state() const noexcept { return state_; }

    // True in the two terminal states (responded, upgraded).
    bool terminal() const noexcept {
        return state_ == exchange_state::responded
            || state_ == exchange_state::upgraded;
    }

    bool suspended() const noexcept { return suspended_; }

    bool disconnected() const noexcept { return disconnected_; }

    // Reason and detail of the disconnect; meaningful only once
    // disconnected().
    const http::outcome& disconnect_reason() const noexcept {
        return disconnect_reason_;
    }

    std::uint64_t connection_id() const noexcept { return connection_id_; }

    // Fan-out handle observing this exchange's disconnect.
    stop_token cancellation() const noexcept { return stop_.get_token(); }

    // Terminal response decision; doubles as the reject decision when
    // called with an error status straight from the head. Commits via
    // the engine sink and clears any suspension.
    http::outcome respond(const http::status& s, const http::fields& f) {
        if (disconnected_) return closed_failure();
        if (state_ != exchange_state::head
                && state_ != exchange_state::admitted) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "exchange: respond() after a terminal decision");
        }
        state_ = exchange_state::responded;
        suspended_ = false;
        if (sink_ != nullptr) sink_->on_respond(s, f);
        return http::outcome::okay();
    }

    // Body admission decision (head-time). Hands the policy to the
    // engine, which owns bounded delivery of the body.
    http::outcome admit_body(const body_policy& policy) {
        if (disconnected_) return closed_failure();
        if (state_ != exchange_state::head) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "exchange: admit_body() is a head-time decision");
        }
        state_ = exchange_state::admitted;
        if (sink_ != nullptr) sink_->on_admit(policy);
        return http::outcome::okay();
    }

    // Suspension decision: stores a fresh resume signal, hands a copy
    // to the caller, and marks the exchange suspended. The engine
    // applies its configured suspension timeout while suspended().
    // The state does not change; the handler still owns the decision.
    http::outcome suspend(resume_signal& out) {
        if (disconnected_) return closed_failure();
        if (state_ != exchange_state::head
                && state_ != exchange_state::admitted) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "exchange: suspend() requires an open exchange");
        }
        resume_signal fresh;
        out = fresh;
        resume_signals_.push_back(std::move(fresh));
        suspended_ = true;
        return http::outcome::okay();
    }

    // Upgrade decision: transfers connection ownership to the upgrade
    // session. The upgrade handshake exists on HTTP/1.1 only; other
    // versions negotiate differently and report not_supported.
    http::outcome upgrade(const ws_upgrade_options& options) {
        if (disconnected_) return closed_failure();
        if (state_ != exchange_state::head) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "exchange: upgrade() is a head-time decision");
        }
        if (head_.request_protocol != http::protocol::http_1_1) {
            return http::outcome(
                http::outcome_code::not_supported,
                "exchange: upgrade requires an HTTP/1.1 request head");
        }
        state_ = exchange_state::upgraded;
        if (sink_ != nullptr) sink_->on_upgrade(options);
        return http::outcome::okay();
    }

    // Engine-facing disconnect notification (PRD-V3N-REQ-025). Reasons
    // mirror http::outcome_code; `ok` is not one. Idempotent: later
    // calls are no-ops. Fans out to the stop token (handler-safe per
    // DR-V3-008: request_stop returns before any waiter runs) and to
    // every recorded resume signal, then moves a non-terminal state to
    // cancelled. Terminal states stay terminal.
    http::outcome disconnect(http::outcome_code reason, std::string detail) {
        if (reason == http::outcome_code::ok) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "exchange: disconnect() requires a failure reason");
        }
        if (disconnected_) return http::outcome::okay();
        disconnected_ = true;
        disconnect_reason_ = http::outcome(reason, std::move(detail));
        stop_.request_stop();
        for (resume_signal& sig : resume_signals_) {
            sig.cancel();
        }
        if (!terminal()) state_ = exchange_state::cancelled;
        return http::outcome::okay();
    }

    // Engine-facing post-commit failure notification (architecture
    // §3.1): the response was already committed but the exchange failed
    // afterwards (e.g. a handler threw), so the engine resets or closes
    // the connection per protocol rules instead of committing another
    // response. No state change: a terminal exchange stays terminal.
    http::outcome abort() {
        if (sink_ != nullptr) sink_->on_abort();
        return http::outcome::okay();
    }

 private:
    // Decisions after a disconnect fail closed, carrying the stored
    // detail so the diagnostic names the original reason.
    http::outcome closed_failure() const {
        return http::outcome(http::outcome_code::connection_closed,
                             disconnect_reason_.message());
    }

    http::request_head head_;
    detail::exchange_sink* sink_;
    std::uint64_t connection_id_ = 0;
    exchange_state state_ = exchange_state::head;
    bool suspended_ = false;
    bool disconnected_ = false;
    http::outcome disconnect_reason_;
    stop_source stop_;
    std::vector<resume_signal> resume_signals_;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_EXCHANGE_HPP_
