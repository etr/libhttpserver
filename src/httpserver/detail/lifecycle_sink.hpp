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

// TASK-118 (plan D5): the interceptor exchange_sink that gives
// after_handler and response_sent their uniform pre-commit firing
// point. v3 handlers commit inside their own task, so the one point
// every commit crosses -- value-shaped, streaming, synthesized, or
// hook-supplied -- is the engine-sink boundary: this wrapper fires the
// mutating after_handler phase BEFORE forwarding the head (for value
// routes this coincides with v2's post-handler point; for streaming
// commits it fires at head-commit time, mid-handler -- the documented
// timing delta), then fires response_sent AFTER the engine accepted
// the head -- a head the engine REFUSED (the framer rejected a field,
// say) never fires response_sent and is recorded as refused so the
// request_completed tail reports the failure verdict. The status/
// fields copies after_handler mutates exist only when the phase can
// run (zero-cost-when-unused). after_handler does not fire on the
// pre-handler short-circuits (the pinned v2 suppression). NOT part of
// the installed surface.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/lifecycle_sink.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_LIFECYCLE_SINK_HPP_
#define SRC_HTTPSERVER_DETAIL_LIFECYCLE_SINK_HPP_

#include <cstdint>
#include <utility>

#include <httpserver/exchange.hpp>
#include <httpserver/server/hooks.hpp>

namespace httpserver {

namespace detail {

class lifecycle_sink final : public exchange_sink {
 public:
    // The provenance of the next committed head; decides whether
    // after_handler fires.
    enum class provenance : std::uint8_t {
        handler,           // the route handler's own commit
        synthesis,         // a dispatcher-synthesized head (404/405/500/501)
        exception_supply,  // a handler_exception hook's response
        pre_handler,       // request_received/before_handler short-circuit
    };

    // @p head must outlive this sink (the engine frame owns it first).
    // @p engine_refusal, when not null, observes the owning engine
    // sink's acceptance verdict: the engine sets it when it refuses
    // the committed head (e.g. the framer rejects a field), and this
    // sink then records the refusal (see forward). Null (test rigs
    // whose stand-in sink never refuses) counts as always-accepted.
    lifecycle_sink(exchange_sink& inner, const server::hook_bus& bus,
                   const http::request_head& head,
                   const bool* engine_refusal = nullptr) noexcept
        : inner_(inner), bus_(bus), head_(head),
          engine_refusal_(engine_refusal) { }

    // Tags the next commit's provenance; the dispatcher sets it before
    // every commit path.
    void begin(provenance next) noexcept { next_ = next; }

    // True once a head was accepted by the engine sink (a refused
    // head never marks it).
    bool responded() const noexcept { return responded_; }

    // True once the engine sink refused a committed head.
    bool refused() const noexcept { return refused_; }

    std::uint16_t committed_status() const noexcept { return status_; }

    // True after a post-commit abort was forwarded.
    bool aborted() const noexcept { return aborted_; }

    void on_admit(const body_policy& policy) override {
        inner_.on_admit(policy);
    }

    void on_respond(const http::status& s, const http::fields& f) override {
        // Zero-cost-when-unused: the mutable copies exist exactly when
        // after_handler can run (the provenance allows it AND hooks
        // are registered); every other commit forwards the originals
        // untouched -- no status/fields copy on the hot path.
        if (next_ != provenance::pre_handler
                && bus_.any_hooks(server::hook_phase::after_handler)) {
            http::status final_status = s;
            http::fields final_fields = f;
            fire_after(final_status, final_fields);
            forward(final_status, final_fields);
        } else {
            forward(s, f);
        }
    }

    websocket_upgrade_result on_upgrade(const ws_upgrade_options& options) override {
        // No response head is queued on the upgrade path: neither
        // after_handler nor response_sent fires here (the ws milestone
        // owns the session lifecycle).
        return inner_.on_upgrade(options);
    }

    void on_abort() override {
        aborted_ = true;
        inner_.on_abort();
    }

 private:
    // The commit tail shared by both branches: forwards the head and,
    // only when the engine accepted it (a refused head -- the framer
    // rejected a field, say -- set the engine's refusal flag), records
    // the response and fires response_sent ("after the engine
    // accepted the committed head"). A refusal suppresses
    // response_sent and is recorded so the request_completed tail can
    // report the failure verdict.
    void forward(const http::status& s, const http::fields& f) {
        inner_.on_respond(s, f);
        if (engine_refusal_ != nullptr && *engine_refusal_) {
            refused_ = true;
            return;
        }
        responded_ = true;
        status_ = s.code();
        fire_sent();
    }

    // The mutating phase over copies of the about-to-be-committed
    // head; the mutated values are what get forwarded. Called only
    // from on_respond's gated branch (the copies exist exactly when
    // the hooks do).
    void fire_after(http::status& s, http::fields& f) const {
        server::after_handler_ctx ctx{head_, s, std::move(f)};
        // The action is ignored by contract: after_handler mutates,
        // full replacement is not offered (the v2 delta).
        (void)bus_.fire<server::hook_phase::after_handler>(ctx);
        s = ctx.status;
        f = std::move(ctx.fields);
    }

    void fire_sent() const {
        if (!bus_.any_hooks(server::hook_phase::response_sent)) return;
        server::response_sent_ctx ctx{head_, status_};
        (void)bus_.fire<server::hook_phase::response_sent>(ctx);
    }

    exchange_sink& inner_;
    const server::hook_bus& bus_;
    const http::request_head& head_;
    const bool* engine_refusal_ = nullptr;
    provenance next_ = provenance::handler;
    bool responded_ = false;
    bool refused_ = false;
    bool aborted_ = false;
    std::uint16_t status_ = 0;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_LIFECYCLE_SINK_HPP_
