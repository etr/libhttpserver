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

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/http1_websocket_handshake.hpp>
namespace httpserver::detail {
websocket_upgrade_result http1_exchange_sink::on_upgrade(const ws_upgrade_options& options) {
    websocket_upgrade_result out;
    auto plan = negotiate_http1_websocket(head_, options,
        engine_->config_.head.max_head_bytes, engine_->config_.head.max_fields);
    out.status = plan.status; out.rejection_status = plan.rejection_status;
    out.rejection_fields = std::move(plan.rejection_fields);
    if (!out.status.ok()) return out;
    if (slot_ == nullptr) {
        out.status = {http::outcome_code::invalid_state, "upgrade slot unavailable"};
        return out;
    }
    const auto& cap = engine_->config_.websocket_limits;
    auto limits = options.limits;
    // Default route values project down to server caps. Explicit values
    // can tighten, never enlarge, any per-connection allocation budget.
    limits.max_message_bytes = std::min(limits.max_message_bytes, cap.max_message_bytes);
    limits.incoming_bytes = std::min(limits.incoming_bytes, cap.incoming_bytes);
    limits.output_bytes = std::min(limits.output_bytes, cap.output_bytes);
    limits.incoming_messages = std::min(limits.incoming_messages, cap.incoming_messages);
    limits.outgoing_messages = std::min(limits.outgoing_messages, cap.outgoing_messages);
    out.status = limits.validate();
    if (!out.status.ok()) return out;
    auto driver = std::make_shared<websocket_driver>(limits);
    engine_->observe_websocket_driver(driver);
    auto session = driver->take_session();
    std::shared_ptr<op_state> pending_read;
    {
        std::lock_guard lock(engine_->mu_);
        if (engine_->shutdown_ || engine_->eof_ || engine_->phase_ != connection_engine::stream_phase::http) {
            out.status = {http::outcome_code::connection_closed, "upgrade connection unavailable"};
            return out;
        }
        std::string tail;
        tail.reserve(engine_->early_bytes_.size() + engine_->pending_tail_.size());
        tail.append(engine_->early_bytes_).append(engine_->pending_tail_);
        out.status = slot_->commit_upgrade(plan.accept, plan.selected_subprotocol);
        if (!out.status.ok()) return out;
        // All throwing allocations precede commitment. Only the reader
        // feeds this tail, ahead of any read already in flight.
        engine_->pending_tail_ = std::move(tail);
        engine_->early_bytes_.clear();
        engine_->websocket_ = driver;
        engine_->phase_ = connection_engine::stream_phase::upgrade_pending_flush;
        engine_->upgrade_anchor_ = std::chrono::steady_clock::now();
        engine_->gate_ = connection_engine::body_gate::none;
        upgraded_ = true;
        pending_read = engine_->pending_read_;
    }
    out.session.emplace(std::move(session)); out.selected_subprotocol = std::move(plan.selected_subprotocol);
    if (pending_read) static_cast<void>(engine_->backend_.request_cancel(*pending_read));
    engine_->wake_loops_ordered(); engine_->rearm_watchdog();
    return out;
}
void connection_engine::feed_websocket_tail() {
    std::shared_ptr<websocket_driver> driver;
    std::span<const std::byte> bytes;
    {
        std::lock_guard lock(mu_);
        driver = websocket_;
        if (!driver || (pending_tail_.empty() && !websocket_codec_blocked_)) return;
        // This sole reader owns the buffer until feed returns. Protocol
        // notifications run outside mu_ and cannot mutate the wire buffer.
        bytes = {reinterpret_cast<const std::byte*>(pending_tail_.data()) + websocket_tail_offset_,
            pending_tail_.size() - websocket_tail_offset_};
    }
    auto fed = driver->feed(bytes);
    {
        std::lock_guard lock(mu_);
        websocket_tail_offset_ += fed.consumed;
        websocket_codec_blocked_ = fed.blocked && fed.status.ok();
        if (websocket_tail_offset_ == pending_tail_.size()) {
            pending_tail_.clear();
            websocket_tail_offset_ = 0;
        }
    }
}
connection_engine::io_posture connection_engine::reader_posture(wake_operation& wake) {
    std::lock_guard lock(mu_);
    if (shutdown_ || close_after_drain_) return io_posture::stop;
    if (websocket_ && (!pending_tail_.empty() || websocket_codec_blocked_) && websocket_->snapshot().input_ready) return io_posture::retry;
    if (reader_may_read_locked()) return io_posture::proceed;
    wake.submit(backend_);
    return io_posture::park;
}
bool connection_engine::submit_read(read_operation& read) {
    std::lock_guard lock(mu_);
    if (shutdown_ || close_after_drain_) return false;
    if (websocket_ && (!pending_tail_.empty() || websocket_codec_blocked_)) return false;
    pending_read_ = read.state();
    read.submit(backend_);
    return true;
}
bool connection_engine::read_completed(const io_result& result) {
    std::lock_guard lock(mu_);
    pending_read_.reset();
    return result.code == http::outcome_code::cancelled && websocket_ != nullptr;
}
void connection_engine::handle_read_failure(const io_result& result) {
    bool upgraded;
    { std::lock_guard lock(mu_); upgraded = websocket_ != nullptr; }
    if (upgraded) {
        fail_websocket({http::outcome_code::connection_closed, "WebSocket transport read failed or EOF"});
        shutdown();
        return;
    }
    if (result.code == http::outcome_code::connection_closed) {
        bool routing;
        { std::lock_guard lock(mu_); eof_ = true; routing = current_ != nullptr; }
        if (routing) disconnect_current(http::outcome_code::connection_closed, "http1 connection engine: peer hangup mid-exchange");
    } else {
        disconnect_current(http::outcome_code::connection_closed, "http1 connection engine: transport read failed");
        request_close();
    }
    wake_loops();
}
std::shared_ptr<websocket_driver> connection_engine::websocket_output_driver() {
    std::lock_guard lock(mu_);
    if (!websocket_ || !outbox_.empty()) return {};
    if (phase_ == stream_phase::upgrade_pending_flush) {
        phase_ = stream_phase::websocket;
        upgrade_anchor_.reset();
        // Output queued while 101 was flushing gets its write interval now.
        websocket_write_anchor_.reset();
        if (websocket_->snapshot().output_pending) websocket_write_anchor_ = std::chrono::steady_clock::now();
    }
    return websocket_;
}
std::size_t connection_engine::copy_transport_output(
        std::span<std::byte> buffer, std::shared_ptr<websocket_driver>& driver) {
    const auto count = outbox_.copy_front(buffer);
    if (count > 0) return count;
    driver = websocket_output_driver();
    return driver ? driver->copy_output(buffer) : 0;
}
connection_engine::io_posture connection_engine::writer_posture(
        const std::shared_ptr<websocket_driver>& driver, wake_operation& wake) {
    std::lock_guard lock(mu_);
    const bool ws_done = driver && driver->snapshot().terminal;
    const bool ordinary_done = !websocket_ && route_done_;
    if (outbox_drained() && (shutdown_ || ws_done || ordinary_done)) {
        phase_ = stream_phase::terminal;
        backend_.release_connection(id_);
        return io_posture::stop;
    }
    // The observer mutates session state before acquiring this same
    // registration mutex. Rechecking closes the notify-before-park race.
    std::byte probe;
    if (outbox_.copy_front(std::span(&probe, 1)) > 0) return io_posture::retry;
    if (driver && driver->snapshot().output_pending) return io_posture::retry;
    wake.submit(backend_);
    return io_posture::park;
}
bool connection_engine::watchdog_terminal_locked() const {
    return shutdown_ || phase_ == stream_phase::terminal || (close_after_drain_ && !websocket_);
}
bool connection_engine::transport_output_pending_locked() const {
    if (outbox_.queued_bytes() > 0) return true;
    return websocket_ && websocket_->snapshot().output_pending;
}
bool connection_engine::arm_watchdog(timer_operation& timer, const watchdog_plan& plan) {
    std::lock_guard lock(mu_);
    const auto current = plan_watchdog_locked();
    if (current.exit) return false;
    if (current.defer != plan.defer || current.deadline != plan.deadline) return false;
    pending_timer_ = timer.state();
    timer.submit(backend_);
    return true;
}
void connection_engine::add_upgrade_deadline_locked(
        std::span<std::chrono::steady_clock::time_point> candidates, std::size_t& count) const {
    if (phase_ == stream_phase::upgrade_pending_flush && upgrade_anchor_) {
        candidates[count++] = *upgrade_anchor_ + config_.timeouts.handshake;
    }
}
void connection_engine::observe_websocket_driver(const std::shared_ptr<websocket_driver>& driver) {
    const std::weak_ptr<connection_engine> weak = shared_from_this();
    driver->observe_progress([weak] { if (auto engine = weak.lock()) engine->websocket_progressed(); });
}
void connection_engine::update_websocket_write_anchor_locked(const websocket_progress& progress) {
    if (!progress.output_pending) {
        websocket_write_anchor_.reset();
    } else if (progress.output_pending_since && (!websocket_write_anchor_ ||
            *websocket_write_anchor_ < *progress.output_pending_since)) {
        websocket_write_anchor_ = progress.output_pending_since;
    }
}
void connection_engine::websocket_progressed() {
    bool terminal = false;
    {
        std::lock_guard lock(mu_);
        const auto progress = websocket_ ? websocket_->snapshot() : websocket_progress{};
        terminal = progress.terminal;
        if (phase_ == stream_phase::websocket) {
            update_websocket_write_anchor_locked(progress);
        }
        if (terminal) close_after_drain_ = true;
        wake_loops();
    }
    if (terminal) disconnect_current(http::outcome_code::connection_closed, "WebSocket session terminal");
    rearm_watchdog();
}
void connection_engine::fail_websocket(http::outcome reason) {
    std::shared_ptr<websocket_driver> driver;
    { std::lock_guard lock(mu_); driver = websocket_; }
    if (driver) driver->transport_failed(std::move(reason));
}
}  // namespace httpserver::detail
