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

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/net/address.hpp>
#include <httpserver/response_writer.hpp>
#include <httpserver/server/routes.hpp>
#include <httpserver/websocket/session.hpp>

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

// HTTP/1.1 WebSocket policy. Subprotocols use server preference order;
// configured origins compare whole serialized values. These values are
// owned by the lazy upgrade task and validated before output commitment.
struct ws_upgrade_options {
    std::vector<std::string> subprotocols;
    // Empty allowlist accepts any syntactically valid origin. Matching
    // uses exact serialized origins, without case/default-port rewriting.
    std::vector<std::string> allowed_origins;
    bool allow_absent_origin = true;
    bool require_subprotocol = false;
    websocket::options limits;
};

// Success is bounded 101 admission, not peer acknowledgement. Refusals
// carry an ordinary HTTP response suggestion and leave the exchange open.
struct websocket_upgrade_result {
    http::outcome status;
    std::optional<websocket::session> session;
    std::string selected_subprotocol;
    http::status rejection_status = http::status::from_code(400);
    http::fields rejection_fields;
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

    // Default delegation preserves existing protocol sinks. Streaming engines
    // override this to leave the response head open for DATA.
    virtual void on_start_response(const http::status& s, const http::fields& f) { on_respond(s, f); }

    // Connection ownership transferred to the upgrade session.
    virtual websocket_upgrade_result on_upgrade(const ws_upgrade_options& options) = 0;

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
//   start_response(s, f)     from head or admitted  -> responded
//     (streaming: respond's head commit, then writer() becomes usable)
//   admit_body(policy)       from head              -> admitted
//   suspend(out)             from head or admitted  (state unchanged)
//   upgrade(options)         from a supported protocol head  -> upgraded
// Upgrade returns task<websocket_upgrade_result>; other decisions return
// http::outcome. A typed precommit failure leaves the
// state, the suspension flag, and the engine untouched (a double
// terminal action therefore reaches the engine exactly once).
//
// respond() is the one-shot response: the whole body is known and the
// writer stays inactive. start_response() is the streaming action: the
// head commits the same way and exchange::writer() streams the body
// afterwards. Both reach one engine head callback exactly once.
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
    // for engine bookkeeping; `body_source` is the engine's delivery
    // seam for the admitted body (null until an engine provides one);
    // `response_sink` is the engine's delivery seam for the streaming
    // response body (null until an engine provides one); `peer` is
    // the transport peer snapshot stamped at accept (TASK-119) --
    // immutable for the exchange's lifetime, unspec for a rigged
    // exchange.
    exchange(const http::request_head& head, detail::exchange_sink* sink,
             std::uint64_t connection_id = 0,
             detail::body_source* body_source = nullptr,
             detail::body_sink* response_sink = nullptr,
             net::peer_address peer = net::peer_address{}) noexcept
        : head_(head), sink_(sink), connection_id_(connection_id),
          body_source_(body_source), body_(stop_.get_token()),
          response_sink_(response_sink), writer_(stop_.get_token()),
          peer_(peer) { }

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

    // TASK-119 (plan D3): the transport peer snapshot taken at accept
    // and carried immutably -- the address and port the peer policy
    // consulted and the value a handler observes. There is no setter:
    // a reconnect is a new connection with a new snapshot. Unspec for
    // a rigged exchange.
    const net::peer_address& peer() const noexcept { return peer_; }

    // The admitted request body. Operations are legal only after a
    // successful admit_body() and fail typed otherwise.
    body_reader& body() noexcept { return body_; }

    // The streaming response writer. Operations are legal only after a
    // successful start_response() and fail typed otherwise.
    response_writer& writer() noexcept { return writer_; }

    // Fan-out handle observing this exchange's disconnect.
    stop_token cancellation() const noexcept { return stop_.get_token(); }

    // TASK-118 (plan D2): the named path captures of the resolved
    // route, stamped by the engine's dispatcher before the handler
    // runs; empty when no parameterized route matched. Names view the
    // registry's stored pattern text (valid while the registry is not
    // mutated -- registration is closed once listen() runs); values
    // are owned by the exchange for its whole lifetime.
    void set_path_args(std::vector<server::route_captures> args) {
        path_args_ = std::move(args);
    }

    std::span<const server::route_captures> path_args() const noexcept {
        return path_args_;
    }

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
        body_.close();
        if (sink_ != nullptr) sink_->on_respond(s, f);
        return http::outcome::okay();
    }

    // Streaming response decision (head-time or after admission):
    // commits the response head exactly like respond(), then activates
    // the writer, so the handler streams the body through writer()
    // with backpressure (architecture §3.1). The engine's on_start_response
    // fires exactly once: afterwards the state is responded, so
    // any second terminal decision fails invalid_state.
    http::outcome start_response(const http::status& s, const http::fields& f) {
        if (disconnected_) return closed_failure();
        if (state_ != exchange_state::head
                && state_ != exchange_state::admitted) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "exchange: start_response() after a terminal decision");
        }
        state_ = exchange_state::responded;
        suspended_ = false;
        body_.close();
        if (sink_ != nullptr) sink_->on_start_response(s, f);
        writer_.activate(response_sink_);
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
        body_.activate(body_source_);
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

    // Upgrade decision: the protocol sink transfers the ordered stream to
    // the session (one connection for HTTP/1.1, one stream for HTTP/2).
    // Lazy like other tasks: options are owned before first suspension.
    // Await immediately while the engine-owned exchange is alive.
    task<websocket_upgrade_result> upgrade(ws_upgrade_options options) {
        websocket_upgrade_result result;
        if (disconnected_) {
            result.status = closed_failure(); co_return result;
        }
        if (state_ != exchange_state::head) {
            result.status = {http::outcome_code::invalid_state,
                "exchange: upgrade() is a head-time decision"};
            co_return result;
        }
        if ((head_.request_protocol != http::protocol::http_1_1 && head_.request_protocol != http::protocol::http_2) || sink_ == nullptr) {
            result.status = {http::outcome_code::not_supported,
                "exchange: upgrade protocol unsupported"};
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

    // Engine-facing disconnect notification (PRD-V3N-REQ-025). Reasons
    // mirror http::outcome_code; `ok` is not one. Idempotent: later
    // calls are no-ops. Publishes the cancelled state and reason before
    // waking stop-token and resume-signal waiters. Stop initiation is
    // handler-safe per DR-V3-008; a posted waiter may run concurrently
    // before request_stop returns. Terminal states stay terminal.
    http::outcome disconnect(http::outcome_code reason, std::string detail) {
        if (reason == http::outcome_code::ok) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "exchange: disconnect() requires a failure reason");
        }
        if (disconnected_) return http::outcome::okay();
        disconnected_ = true;
        disconnect_reason_ = http::outcome(reason, std::move(detail));
        if (!terminal()) state_ = exchange_state::cancelled;
        body_.note_disconnect(disconnect_reason_);
        writer_.note_disconnect(disconnect_reason_);
        stop_.request_stop();
        for (resume_signal& sig : resume_signals_) {
            sig.cancel();
        }
        return http::outcome::okay();
    }

    // Engine-facing post-commit failure notification (architecture
    // §3.1): the response was already committed but the exchange failed
    // afterwards (e.g. a handler threw), so the engine resets or closes
    // the connection per protocol rules instead of committing another
    // response. No state change: a terminal exchange stays terminal.
    http::outcome abort() {
        // A streaming handler may have thrown mid-body; shut the
        // writer down (idempotent) so no further write passes its
        // gate, then let the engine reset per protocol rules.
        writer_.close();
        if (sink_ != nullptr) sink_->on_abort();
        return http::outcome::okay();
    }

 private:
    // Watchdog reads race with handler decisions; copies retain value semantics
    // so the exchange's public move operations can remain defaulted.
    class suspension_flag {
     public:
        suspension_flag() noexcept = default;
        suspension_flag(const suspension_flag& other) noexcept
            : value_(other.value_.load()) { }
        suspension_flag& operator=(const suspension_flag& other) noexcept {
            value_.store(other.value_.load());
            return *this;
        }
        suspension_flag& operator=(bool value) noexcept {
            value_.store(value);
            return *this;
        }
        operator bool() const noexcept { return value_.load(); }  // NOLINT(runtime/explicit)

     private:
        std::atomic<bool> value_{false};
    };

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
    suspension_flag suspended_;
    bool disconnected_ = false;
    http::outcome disconnect_reason_;
    stop_source stop_;
    detail::body_source* body_source_ = nullptr;
    // Declared after stop_: the reader's constructor copies the stop
    // token, so disconnects fan out to parked body reads.
    body_reader body_;
    detail::body_sink* response_sink_ = nullptr;
    // Declared after stop_: the writer's constructor copies the stop
    // token, so disconnects fan out to parked response writes.
    response_writer writer_;
    std::vector<server::route_captures> path_args_;
    std::vector<resume_signal> resume_signals_;
    net::peer_address peer_;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_EXCHANGE_HPP_
