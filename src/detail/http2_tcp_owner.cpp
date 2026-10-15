/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <memory>
#include <utility>
#include <httpserver/detail/protocol_limits.hpp>
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/http2_tcp_owner.hpp>
namespace httpserver::detail {
namespace {
class next_turn {
 public:
    explicit next_turn(executor& owner) : owner_(owner) {}
    next_turn& bind_frame(task_frame_base* frame) { witness_ = frame->frame_witness_ptr(); return *this; }
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> frame) { owner_.post([frame, witness = witness_] { guarded_resume(witness, frame); }); }
    void await_resume() const noexcept {}
 private:
    executor& owner_;
    std::shared_ptr<frame_witness> witness_;
};
}  // namespace
http2_tcp_owner::http2_tcp_owner(io_socket_backend& backend, executor& workers, const server::route_registry& routes,
    const server::resource_budget& budget, const connection_engine_config& config, std::uint64_t id, net::peer_address peer,
    const server::peer_policy* peers, const server::hook_bus& hooks, std::shared_ptr<const server::tls_peer_metadata> tls, executor::handler stopped)
    : backend_(backend), workers_(workers), routes_(routes), budget_(budget), wire_(std::make_shared<http2_dispatch>(workers)),
      io_(*wire_), id_(id), peer_(peer), stopped_(std::move(stopped)) {
    limits_.timeouts = config.timeouts;
    if (config.protocol_limits) limits_ = config.protocol_limits->http2;
    limits_.body_buffer_bytes = std::min<std::size_t>(16384, budget.capacity(server::resource::body_buffer_bytes) / 4);
    limits_.response_buffer_bytes = std::min<std::size_t>(16384, budget.capacity(server::resource::response_queue_bytes) / 4);
    limits_.websocket = config.websocket_limits;
    // One upgraded stream must leave independently admissible HTTP work and
    // control output. Account codec copies and bounded message metadata.
    limits_.websocket.incoming_messages = std::min<std::size_t>(16, limits_.websocket.incoming_messages);
    limits_.websocket.outgoing_messages = std::min<std::size_t>(16, limits_.websocket.outgoing_messages);
    limits_.websocket.incoming_bytes = std::min(limits_.websocket.incoming_bytes, budget.capacity(server::resource::body_buffer_bytes) / 32);
    limits_.websocket.output_bytes = std::min(limits_.websocket.output_bytes, budget.capacity(server::resource::response_queue_bytes) / 32);
    limits_.websocket.max_message_bytes = std::min({limits_.websocket.max_message_bytes, limits_.websocket.incoming_bytes,
        limits_.websocket.output_bytes > 10 ? limits_.websocket.output_bytes - 10 : 0});
    native_.workers = &workers_; native_.drain = config.drain_owner; native_.tls = std::move(tls);
    dispatch_.hooks = &hooks; dispatch_.peers = peers; dispatch_.diagnostics = config.diagnostics;
    if (config.pages) dispatch_.pages = *config.pages;
}
std::size_t http2_tcp_owner::storage_capacity() { return sizeof(http2_tcp_owner) + 4096; }
void http2_tcp_owner::start() {
    wire_->on_overflow([weak = weak_from_this()] { if (auto self = weak.lock()) self->close(http::outcome_code::limit_exceeded); });
    native_.progress = std::make_shared<executor::handler>([weak = weak_from_this()] { if (auto self = weak.lock()) self->notify(); });
    wire_->post([self = shared_from_this()] {
        if (!self->budget_.reserve(server::resource::body_buffer_bytes, storage_capacity(), self->storage_).ok() ||
            !self->budget_.reserve(server::resource::timers, 1, self->timer_charge_).ok()) {
            self->close(http::outcome_code::limit_exceeded); self->finalize_if_retired(); return;
        }
        self->engine_ = std::make_unique<http2_request_engine>(self->budget_, self->routes_, *self->wire_, self->limits_, self->dispatch_, self->id_, self->peer_, self->native_);
        self->loops_ = 3;
        spawn(*self->wire_, reader(self), [self](task_result<void>) { self->loop_finished(); });
        spawn(*self->wire_, writer(self), [self](task_result<void>) { self->loop_finished(); });
        spawn(*self->wire_, watchdog(self), [self](task_result<void>) { self->loop_finished(); });
        self->pump();
    });
}
void http2_tcp_owner::notify() {
    if (notified_.exchange(true)) return;
    wire_->post([self = shared_from_this()] { self->notified_ = false; self->pump(); });
}
void http2_tcp_owner::stop(http::outcome_code reason) {
    { std::lock_guard lock(control_); stop_requested_ = reason; }
    notify();
}
void http2_tcp_owner::drain(http2_connection::time_point deadline) {
    {
        std::lock_guard lock(control_);
        if (!drain_requested_) drain_requested_ = deadline;
    }
    notify();
}
void http2_tcp_owner::pump() {
    if (finalized_) return;
    http::outcome_code stop;
    std::optional<http2_connection::time_point> drain;
    { std::lock_guard lock(control_); stop = stop_requested_; drain = drain_requested_; }
    if (stop != http::outcome_code::ok) close(stop);
    if (engine_) {
        if (drain && !draining_ && !closed_) {
            draining_ = true; server::drain_ticket ticket;
            if (!engine_->begin_drain(*drain, ticket).ok()) close(http::outcome_code::cancelled);
        }
        engine_->begin_turn();
        if (timer_ && timer_deadline_ != deadline()) backend_.request_cancel(*timer_);
    }
    backend_.wake();
    finalize_if_retired();
}
void http2_tcp_owner::close(http::outcome_code reason) {
    if (closed_) return;
    closed_ = true;
    if (engine_) engine_->stop(reason);
    backend_.release_connection(id_);
}
void http2_tcp_owner::loop_finished() {
    if (loops_) --loops_;
    if (engine_) engine_->begin_turn();
    finalize_if_retired();
}
void http2_tcp_owner::finalize_if_retired() {
    if (finalized_ || loops_ || (engine_ && engine_->active_streams())) return;
    finalized_ = true;
    auto stopped = std::move(stopped_);
    if (stopped) stopped();
}
std::optional<http2_connection::time_point> http2_tcp_owner::deadline() {
    if (closed_ || !engine_) return {};
    auto deadline = engine_->next_deadline(std::chrono::steady_clock::now());
    if (write_anchor_) {
        const auto candidate = *write_anchor_ + limits_.timeouts.write_idle;
        if (!deadline || candidate < *deadline) deadline = candidate;
    }
    return deadline;
}
task<void> http2_tcp_owner::reader(std::shared_ptr<http2_tcp_owner> self) {
    while (!self->closed_ && !self->engine_->failure()) {
        read_operation operation(self->io_, self->id_, self->input_);
        operation.submit(self->backend_);
        const auto result = co_await operation;
        if (result.code != http::outcome_code::ok || !result.transferred) {
            self->close(http::outcome_code::connection_closed);
            co_return;
        }
        auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(self->input_.data()), result.transferred);
        self->engine_->begin_turn();
        while (!bytes.empty() && !self->closed_) {
            const auto fed = self->engine_->feed(bytes, std::chrono::steady_clock::now());
            bytes = bytes.subspan(fed.consumed);
            if (fed.progress == http2_progress::failed) break;
            if (fed.progress == http2_progress::yield) {
                co_await next_turn(*self->wire_);
                self->engine_->begin_turn();
           } else if (!fed.consumed) {
                self->close(http::outcome_code::protocol_error);
                break;
           }
        }
        self->pump();
    }
}
task<void> http2_tcp_owner::writer(std::shared_ptr<http2_tcp_owner> self) {
    while (!self->closed_) {
        const auto bytes = self->engine_->output(std::chrono::steady_clock::now());
        if (bytes.empty()) {
            self->write_anchor_.reset();
            if (self->engine_->failure() || self->engine_->draining_finished()) {
                self->close(http::outcome_code::connection_closed);
                co_return;
           }
            wake_operation operation(self->io_, self->id_);
            operation.submit(self->backend_);
            const auto result = co_await operation;
            if (result.code != http::outcome_code::ok) co_return;
            continue;
        }
        if (!self->write_anchor_) self->write_anchor_ = std::chrono::steady_clock::now();
        self->pump();
        write_operation operation(self->io_, self->id_, std::as_bytes(bytes));
        operation.submit(self->backend_);
        const auto result = co_await operation;
        if (result.code != http::outcome_code::ok || !result.transferred) {
            self->close(http::outcome_code::connection_closed);
            co_return;
        }
        self->write_anchor_ = std::chrono::steady_clock::now();
        self->engine_->advance_output(result.transferred);
        self->pump();
    }
}
task<void> http2_tcp_owner::watchdog(std::shared_ptr<http2_tcp_owner> self) {
    while (!self->closed_) {
        const auto deadline = self->deadline();
        if (!deadline) {
            wake_operation operation(self->io_, self->id_); operation.submit(self->backend_);
            if ((co_await operation).code != http::outcome_code::ok) co_return;
            continue;
        }
        timer_operation operation(self->io_, self->id_, *deadline);
        self->timer_ = operation.state(); self->timer_deadline_ = deadline;
        operation.submit(self->backend_);
        const auto result = co_await operation;
        self->timer_.reset(); self->timer_deadline_.reset();
        if (result.code == http::outcome_code::connection_closed) co_return;
        if (result.code != http::outcome_code::ok) continue;
        const auto now = std::chrono::steady_clock::now();
        if (self->write_anchor_ && now >= *self->write_anchor_ + self->limits_.timeouts.write_idle) {
            self->close(http::outcome_code::timeout);
        } else {
            self->engine_->check_drain(now);
            self->pump();
        }
    }
}
}  // namespace httpserver::detail
