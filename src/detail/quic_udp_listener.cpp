/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/rand.h>
#include <algorithm>
#include <memory>
#include <array>
#include <utility>
#include <httpserver/detail/quic_udp_listener.hpp>
#include <httpserver/detail/protocol_limits.hpp>
#include <httpserver/detail/io_udp_backend.hpp>
#include <httpserver/detail/resource_scope.hpp>
namespace httpserver::detail {
namespace {
pollsys::native_socket_t open_udp(std::string_view address, std::uint16_t port, std::uint16_t& bound) {
#if defined(_WIN32)
    if (!pollsys::ensure_winsock()) return pollsys::k_invalid_socket;
#endif
    sockaddr_storage storage{};
    if (!pollsys::fill_listener_address(address, port, storage)) return pollsys::k_invalid_socket;
    const auto socket = ::socket(storage.ss_family, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == pollsys::k_invalid_socket) return socket;
    const socklen_t length = storage.ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    if (::bind(socket, reinterpret_cast<const sockaddr*>(&storage), length) != 0) return pollsys::listener_failure(socket);
    bound = pollsys::bound_listener_port(socket);
    pollsys::set_nonblocking(socket, true);
    prepare_datagram_socket(socket);
    return socket;
}
}  // namespace
quic_udp_listener::quic_udp_listener(io_socket_backend& backend, executor& workers, const server::route_registry& routes,
    const server::hook_bus& hooks, const server::peer_policy& peers, server::resource_budget budget, connection_engine_config config)
: backend_(backend), workers_(workers), routes_(routes), hooks_(hooks), peers_(peers), budget_(child_budget(budget)),
config_(std::move(config)), wire_(std::make_shared<http2_dispatch>(workers)), io_(*wire_, 16, 32768) {}
http::outcome quic_udp_listener::listen(const server::listener_options& endpoint, std::size_t index, std::shared_ptr<const tls_credentials_binding> binding) {
    endpoint_ = endpoint; binding_ = std::move(binding);
    id_ = (std::uint64_t{1} << 62) + index;
    next_connection_ = (std::uint64_t{1} << 61) + (index << 32);
    if (!budget_.reserve(server::resource::quic_reassembly_bytes, 131072, storage_).ok() ||
        !budget_.reserve(server::resource::timers, 1, timer_charge_).ok()) return {http::outcome_code::limit_exceeded, "UDP listener capacity exhausted"};
    std::array<std::byte, 32> key{};
    if (RAND_bytes(reinterpret_cast<unsigned char*>(key.data()), key.size()) != 1) return {http::outcome_code::protocol_error, "UDP token initialization failed"};
    quic_admission_limits limits;
    limits.max_pending = std::min<std::size_t>(64, budget_.capacity(server::resource::connections));
    limits.max_retained_bytes = 32768;
    limits.max_routes = std::min<std::size_t>(128, 4 * budget_.capacity(server::resource::connections));
    limits.pending_lifetime_seconds = std::max<std::uint64_t>(1, std::chrono::ceil<std::chrono::seconds>(config_.timeouts.handshake).count());
    limits.listener_id = id_; limits.diagnostics = config_.diagnostics;
    admission_ = std::make_unique<quic_server_admission>(limits, key);
    const auto socket = open_udp(endpoint.address, endpoint.port, port_);
    if (socket == pollsys::k_invalid_socket) {
        port_ = 0; return {http::outcome_code::connection_closed, "UDP listener bind failed"};
    }
    backend_.adopt_datagram(id_, socket);
    return {};
}
std::uint64_t quic_udp_listener::seconds() const {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - epoch_).count();
}
void quic_udp_listener::activate() {
    activated_ = true;
    wire_->on_overflow([weak = weak_from_this()] { if (auto self = weak.lock()) self->request_stop(http::outcome_code::limit_exceeded); });
    wire_->post([self = shared_from_this()] {
        self->loops_ = 3;
        spawn(*self->wire_, reader(self), [self](task_result<void>) { self->loop_finished(); });
        spawn(*self->wire_, writer(self), [self](task_result<void>) { self->loop_finished(); });
        spawn(*self->wire_, watchdog(self), [self](task_result<void>) { self->loop_finished(); });
    });
}
void quic_udp_listener::request_stop(http::outcome_code) noexcept {
    stop_requested_ = true;
    if (!activated_) {
        if (port_) backend_.release_connection(id_);
        port_ = 0; retired_ = true; return;
    }
    notify();
}
void quic_udp_listener::quiesce(std::chrono::steady_clock::time_point deadline) {
    { std::lock_guard lock(control_); draining_ = deadline; }
    notify();
}
void quic_udp_listener::notify() {
    if (notified_.exchange(true)) return;
    wire_->post([self = shared_from_this()] { self->notified_ = false; self->pump(); });
    backend_.wake();
}
void quic_udp_listener::update_connections(std::chrono::steady_clock::time_point now, std::optional<std::chrono::steady_clock::time_point> drain) {
    for (auto it = connections_.begin(); it != connections_.end();) {
        auto& owner = *it->second->connection;
        if (stop_requested_) owner.request_close({true, 0x100, 0, {}}, now);
        else if (drain) owner.drain(*drain);
        owner.tick(now);
        if (owner.connected() || owner.terminal()) it->second->handshake.reset();
        if (owner.retired()) it = connections_.erase(it);
        else ++it;
    }
}
void quic_udp_listener::pump() {
    if (transport_released_) return;
    const auto now = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> drain;
    { std::lock_guard lock(control_); drain = draining_; }
    update_connections(now, drain);
    if (admission_) admission_->expire(seconds());
    if (timer_ && deadline() != timer_deadline_) backend_.request_cancel(*timer_);
    backend_.wake();
    if (stop_requested_ && connections_.empty() && admissions_.load() == 0) {
        for (auto& reply : replies_) reply->cancel_unsent();
        replies_.clear();
        transport_released_ = true;
        backend_.release_connection(id_);
    }
    changed_.notify_all();
}
void quic_udp_listener::admit(quic_server_admission::pending_handle handle) {
    const auto facts = admission_->inspect(handle);
    if (!facts || !admission_->claim(handle)) return;
    auto budget = child_budget(budget_);
    server::reservation seat, timer;
    const auto verdict = peers_.classify(facts->initial->peer.peer);
    if (admissions_.load() >= 64 || (verdict.accepted &&
        (!budget.reserve(server::resource::connections, 1, seat).ok() || !budget.reserve(server::resource::timers, 1, timer).ok()))) {
        admission_->cancel(handle); return;
    }
    ++admissions_;
    const auto peer = facts->initial->peer.peer;
    auto pending = std::make_shared<record>();
    pending->budget = budget; pending->seat = std::move(seat); pending->timer = std::move(timer);
    if (verdict.accepted) pending->handshake.emplace(*config_.drain_owner);
    workers_.post([self = shared_from_this(), handle, pending, peer, verdict] {
        server::accept_decision_ctx seen{peer, verdict.accepted, verdict.reason};
        static_cast<void>(self->hooks_.fire<server::hook_phase::accept_decision>(seen));
        self->wire_->post([self, handle, pending, accepted = verdict.accepted] {
            self->finish_admission(handle, pending, accepted);
        });
    });
}
void quic_udp_listener::finish_admission(quic_server_admission::pending_handle handle, const std::shared_ptr<record>& pending, bool accepted) {
    --admissions_;
    const auto facts = admission_->inspect(handle);
    bool draining;
    { std::lock_guard lock(control_); draining = draining_.has_value(); }
    // Retain the bounded, already claimed refusal until admission expiry.
    // Retransmitted authenticated Initials must not repeat the policy hook.
    if (!accepted && facts && !stop_requested_.load() && !draining) {
        pump(); return;
    }
    if (!accepted || !facts || stop_requested_.load() || draining) {
        admission_->cancel(handle); pump(); return;
    }
    try {
        const auto snapshot = tls_credentials_access::acquire(*endpoint_.credentials);
        quic_network_config config;
        config.timeouts = config_.timeouts; config.port = port_;
        const auto& plan = *config_.protocol_limits;
        config.data_bytes = plan.data_bytes; config.critical_bytes = plan.critical_bytes;
        config.max_requests = plan.report.http3->max_concurrent_requests;
        config.max_stream_records = plan.report.quic->max_stream_records;
        config.request_limits = plan.http3;
        request_dispatch_context dispatch;
        dispatch.hooks = &hooks_; dispatch.peers = &peers_; dispatch.diagnostics = config_.diagnostics;
        if (config_.pages) dispatch.pages = *config_.pages;
        http3_native_context native; native.workers = &workers_; native.drain = config_.drain_owner;
        native.progress = std::make_shared<executor::handler>([weak = weak_from_this()] { if (auto owner = weak.lock()) owner->notify(); });
        const auto id = ++next_connection_;
        pending->connection = std::make_shared<quic_network_connection>(snapshot->select_default(), *facts, routes_, *wire_, id,
            peers_, pending->budget, config, dispatch, native);
        auto promoted = admission_->promote(handle, io_.datagrams(), pending->connection->route_sink(), seconds());
        if (promoted) {
            pending->connection->adopt_routes(admission_->routes(), io_.datagrams(), std::move(promoted->registration));
            connections_.emplace(id, pending);
        }
    } catch (...) { admission_->cancel(handle); }
    pump();
}
void quic_udp_listener::receive(std::shared_ptr<const io_datagram> packet) {
    auto result = admission_->receive(std::move(packet), seconds());
    if (result.code == quic_admission_code::reply) {
        if (!stop_requested_ && replies_.size() < 16) replies_.push_back(std::move(result.reply));
        else result.reply->cancel_unsent();
    } else if (result.code == quic_admission_code::pending) {
        bool drain; { std::lock_guard lock(control_); drain = draining_.has_value(); }
        if (!stop_requested_ && !drain) admit(result.pending);
        else admission_->cancel(result.pending);
    }
    pump();
}
task<void> quic_udp_listener::reader(std::shared_ptr<quic_udp_listener> self) {
    while (!self->transport_released_) {
        udp_receive_operation operation(self->io_, self->id_, 2048); operation.submit(self->backend_);
        const auto result = co_await operation;
        if (result.code == http::outcome_code::connection_closed) co_return;
        if (result.code == http::outcome_code::ok && result.datagram) self->receive(result.datagram);
    }
}
quic_udp_listener::pending_output quic_udp_listener::next_output() {
    pending_output output;
    if (!replies_.empty()) {
        output.reply = std::move(replies_.front()); replies_.pop_front();
        output.packet = quic_transmission{output.reply->packet()->bytes, output.reply->packet()->peer};
    } else {
        auto it = connections_.upper_bound(sending_id_);
        for (std::size_t n = 0; n < connections_.size(); ++n) {
            if (it == connections_.end()) it = connections_.begin();
            auto current = it++;
            output.packet = current->second->connection->prepare(std::chrono::steady_clock::now());
            if (output.packet) {
                output.owner = current->second->connection; sending_id_ = current->first; break;
            }
        }
    }
    return output;
}
task<void> quic_udp_listener::writer(std::shared_ptr<quic_udp_listener> self) {
    while (!self->transport_released_) {
        auto output = self->next_output();
        auto& packet = output.packet;
        if (!packet) {
            wake_operation wake(self->io_, self->id_); wake.submit(self->backend_);
            if ((co_await wake).code != http::outcome_code::ok) co_return;
            continue;
        }
        udp_send_operation operation(self->io_, self->id_, packet->bytes, packet->peer); operation.submit(self->backend_);
        const auto result = co_await operation;
        // Cancellation races the datagram syscall. An error cannot prove that
        // no bytes reached the peer; retain its debit conservatively.
        const auto emission = result.code == http::outcome_code::ok && result.transferred == packet->bytes.size() ? quic_identity_emission::sent : quic_identity_emission::ambiguous;
        if (output.owner) output.owner->emitted(emission, std::chrono::steady_clock::now());
        if (output.reply) output.reply->complete_send();
        self->pump();
    }
}
std::optional<std::chrono::steady_clock::time_point> quic_udp_listener::deadline() const {
    std::optional<std::chrono::steady_clock::time_point> next;
    for (const auto& [id, record] : connections_) {
        const auto candidate = record->connection->deadline();
        if (candidate && (!next || *candidate < *next)) next = candidate;
    }
    return next;
}
task<void> quic_udp_listener::watchdog(std::shared_ptr<quic_udp_listener> self) {
    while (!self->transport_released_) {
        const auto due = self->deadline();
        if (!due) {
            wake_operation wake(self->io_, self->id_); wake.submit(self->backend_);
            if ((co_await wake).code != http::outcome_code::ok) co_return;
            continue;
        }
        timer_operation timer(self->io_, self->id_, *due); self->timer_ = timer.state(); self->timer_deadline_ = due;
        timer.submit(self->backend_); const auto result = co_await timer;
        self->timer_.reset(); self->timer_deadline_.reset();
        if (result.code == http::outcome_code::connection_closed) co_return;
        self->pump();
    }
}
void quic_udp_listener::loop_finished() {
    if (--loops_ == 0) {
        storage_.release(); timer_charge_.release(); retired_ = true; changed_.notify_all();
    }
}
void quic_udp_listener::await_progress(std::chrono::milliseconds maximum) {
    std::unique_lock lock(control_); changed_.wait_for(lock, maximum);
}
}  // namespace httpserver::detail
