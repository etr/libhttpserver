/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>
#include <httpserver/detail/io_managed_socket_backend.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/io_timer_sweep.hpp>

namespace httpserver {
namespace detail {
namespace {
constexpr std::uint64_t k_listener_id_base = 1ULL << 62;
constexpr std::uint32_t k_read = 1;
constexpr std::uint32_t k_write = 4;
io_result closed_result() { return {http::outcome_code::connection_closed, 0, 0, {}}; }
}  // namespace

thread_local io_managed_socket_backend* io_managed_socket_backend::current_driver_ = nullptr;

io_managed_socket_backend::~io_managed_socket_backend() { close(); }

http::outcome io_managed_socket_backend::ready() const {
    std::lock_guard<std::mutex> lock(mu_);
    return closed_ || wake_failed_ ? http::outcome(http::outcome_code::invalid_state,
        "httpserver: managed socket backend unavailable") : http::outcome::okay();
}

http::outcome io_managed_socket_backend::dispatch(std::span<const server::readiness_event>,
                                                 std::chrono::steady_clock::time_point) {
    return http::outcome(http::outcome_code::invalid_state, "httpserver: socket backend is managed");
}

void io_managed_socket_backend::deliver(const completions& done) {
    for (const auto& entry : done) entry.first->owner()->enqueue(entry.first, entry.second);
}

void io_managed_socket_backend::finish_locked(const std::shared_ptr<op_state>& op,
                                    io_result result, completions& done) {
    // Reserve the delivery record before consuming pending ownership.
    done.emplace_back(op, result);
    if (!op->claim_terminal()) done.pop_back();
    pending_.erase(op.get());
}

void io_managed_socket_backend::notify_locked() {
    if (!wake_pending_) {
        wake_pending_ = true;
        if (!wake_.signal()) wake_failed_ = true;
    }
}

void io_managed_socket_backend::submit(op_state& op) {
    const auto state = op.shared_from_this();
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        op.set_sequence(next_sequence_++);
        if (is_tls_control(op.kind())) {
            finish_locked(state, {http::outcome_code::not_supported}, done);
        } else if (closed_) {
            finish_locked(state, closed_result(), done);
        } else if (op.kind() == io_op_kind::cancel) {
            const auto target = std::get<cancel_payload>(op.payload()).target;
            const auto it = pending_.find(target.get());
            const bool found = it != pending_.end();
            if (found) finish_locked(target, {http::outcome_code::cancelled, 0, 0, {}}, done);
            finish_locked(state, {found ? http::outcome_code::ok
                : http::outcome_code::invalid_state, 0, 0, {}}, done);
        } else if (op.connection() != 0 && !connections_.contains(op.connection())) {
            finish_locked(state, closed_result(), done);
        } else if (is_udp(op.kind()) && op.connection() == 0) {
            finish_locked(state, {http::outcome_code::invalid_state}, done);
        } else {
            http::outcome_code admission = http::outcome_code::ok;
            const auto socket = connections_.find(op.connection());
            const bool fd_op = op.kind() == io_op_kind::read || op.kind() == io_op_kind::write
                || op.kind() == io_op_kind::accept || is_udp(op.kind());
            if (socket != connections_.end() && fd_op) {
                if (is_udp(op.kind()) != socket->second->datagram) {
                    admission = http::outcome_code::invalid_state;
                } else if (is_udp(op.kind())) {
                    admission = socket->second->udp.admit(state);
                }
            }
            if (admission == http::outcome_code::ok) pending_.emplace(&op, state);
            else finish_locked(state, {admission}, done);
        }
        notify_locked();
    }
    deliver(done);
}

http::outcome_code io_managed_socket_backend::request_cancel(op_state& target) {
    completions done;
    http::outcome_code result;
    {
        std::lock_guard<std::mutex> lock(mu_);
        result = closed_ ? http::outcome_code::connection_closed
                         : http::outcome_code::invalid_state;
        const auto it = pending_.find(&target);
        if (!closed_ && it != pending_.end()) {
            const auto state = it->second;
            finish_locked(state, {http::outcome_code::cancelled, 0, 0, {}}, done);
            result = http::outcome_code::ok;
            notify_locked();
        }
    }
    deliver(done);
    return result;
}

void io_managed_socket_backend::adopt_locked(std::uint64_t id, pollsys::native_socket_t socket,
                                   bool listener, bool datagram) {
    if (connections_.contains(id)) throw std::logic_error("httpserver: duplicate connection id");
    if (closed_) {
        pollsys::close_socket(socket);
        return;
    }
    if (next_token_ >= std::numeric_limits<std::uintptr_t>::max()
        || id == std::numeric_limits<std::uint64_t>::max()) {
        pollsys::close_socket(socket);
        throw std::overflow_error("httpserver: managed socket identity exhausted");
    }
    pollsys::set_nonblocking(socket, true);
    if (datagram) prepare_datagram_socket(socket);
    else pollsys::prepare_stream_socket(socket);
    std::shared_ptr<registration> record;
    try {
        record = std::make_shared<registration>(socket, next_token_++, listener);
    } catch (...) {
        pollsys::close_socket(socket);
        throw;
    }
    record->datagram = datagram;
    tokens_.emplace(record->token, id);
    try {
        connections_.emplace(id, record);
    } catch (...) {
        tokens_.erase(record->token);
        throw;
    }
    // Native listener identities must not advance the accepted-ID counter.
    if (id < k_listener_id_base) next_connection_ = std::max(next_connection_, id + 1);
}

void io_managed_socket_backend::adopt_connection(std::uint64_t id, pollsys::native_socket_t socket) {
    std::lock_guard<std::mutex> lock(mu_);
    adopt_locked(id, socket, false);
    notify_locked();
}

void io_managed_socket_backend::adopt_datagram(std::uint64_t id, pollsys::native_socket_t socket) {
    std::lock_guard<std::mutex> lock(mu_);
    adopt_locked(id, socket, false, true);
    notify_locked();
}

void io_managed_socket_backend::adopt_listener(std::uint64_t id, pollsys::native_socket_t socket) {
    std::lock_guard<std::mutex> lock(mu_);
    adopt_locked(id, socket, true);
    notify_locked();
}

pollsys::native_socket_t io_managed_socket_backend::native_handle(std::uint64_t id) const {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = connections_.find(id);
    return it == connections_.end() ? pollsys::k_invalid_socket : it->second->socket;
}

void io_managed_socket_backend::retire_locked(std::uint64_t id, completions& done) {
    std::vector<std::shared_ptr<op_state>> swept;
    for (const auto& entry : pending_) {
        if (entry.second->connection() == id) swept.push_back(entry.second);
    }
    done.reserve(done.size() + swept.size());
    const auto it = connections_.find(id);
    if (it != connections_.end()) {
        tokens_.erase(it->second->token);  // invalidate before fd can close or be reused
        // The final socket lease removes its kernel registrations.
        connections_.erase(it);
    }
    for (const auto& state : swept) finish_locked(state, closed_result(), done);
}

void io_managed_socket_backend::release_connection(std::uint64_t id) {
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        retire_locked(id, done);
        notify_locked();
    }
    deliver(done);
}

std::size_t io_managed_socket_backend::wake() {
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<std::shared_ptr<op_state>> wakes;
        for (const auto& entry : pending_) {
            if (entry.second->kind() == io_op_kind::wake) wakes.push_back(entry.second);
        }
        done.reserve(wakes.size());
        for (const auto& op : wakes) finish_locked(op, {}, done);
        notify_locked();
    }
    deliver(done);
    return done.size();
}

std::size_t io_managed_socket_backend::close() {
    if (current_driver_ == this) return shutdown();
    std::lock_guard<std::mutex> lock(join_mu_);
    const auto count = shutdown();
    // Quiesce all selected completion deliveries before the executor can drain.
    if (thread_.joinable()) thread_.join();
    return count;
}

std::size_t io_managed_socket_backend::shutdown() {
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_) return 0;
        done.reserve(pending_.size());
        while (!pending_.empty()) {
            const auto state = pending_.begin()->second;
            finish_locked(state, closed_result(), done);
        }
        closed_ = true;
        notify_locked();
    }
    deliver(done);
    return done.size();
}

std::size_t io_managed_socket_backend::pending_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return pending_.size();
}

std::uint64_t io_managed_socket_backend::poll_iterations() const {
    std::lock_guard<std::mutex> lock(mu_);
    return iterations_;
}

std::optional<std::chrono::steady_clock::time_point> io_managed_socket_backend::scan_interest_locked(
    std::unordered_map<std::uint64_t, std::uint32_t>& masks) const {
    std::optional<std::chrono::steady_clock::time_point> deadline;
    for (const auto& entry : pending_) {
        const auto& op = *entry.second;
        switch (op.kind()) {
            case io_op_kind::udp_receive:
            case io_op_kind::read:
            case io_op_kind::accept: masks[op.connection()] |= k_read; break;
            case io_op_kind::udp_send:
            case io_op_kind::write: masks[op.connection()] |= k_write; break;
            case io_op_kind::timer: {
                const auto time = std::get<timer_payload>(op.payload()).deadline;
                if (!deadline || time < *deadline) deadline = time;
                break;
            }
            default: break;
        }
    }
    return deadline;
}

bool io_managed_socket_backend::accept_locked(const std::shared_ptr<op_state>& op,
    const std::shared_ptr<registration>& lease, completions& done) {
    pollsys::native_socket_t socket = pollsys::k_invalid_socket;
    net::peer_address peer;
    const auto result = pollsys::accept_one(lease->socket, &socket, &peer);
    if (result.status == pollsys::sys_status::would_block) return false;
    if (result.status != pollsys::sys_status::ok) {
        retire_locked(op->connection(), done);
        return false;
    }
    if (next_connection_ >= k_listener_id_base) {
        pollsys::close_socket(socket);
        throw std::overflow_error("httpserver: accepted connection identity exhausted");
    }
    const auto id = next_connection_;
    adopt_locked(id, socket, false);
    finish_locked(op, {http::outcome_code::ok, 0, id, peer}, done);
    return true;
}

std::pair<std::size_t, bool> io_managed_socket_backend::transfer_stream(
    pollsys::native_socket_t socket, const op_state& op) {
    const bool read = op.kind() == io_op_kind::read;
    const auto bytes = read ? std::span<const std::byte>(std::get<read_payload>(op.payload()).buffer)
                            : std::get<write_payload>(op.payload()).bytes;
    std::size_t transferred = 0;
    while (transferred < bytes.size()) {
        const auto result = read
            ? read_(socket, std::get<read_payload>(op.payload()).buffer.data() + transferred,
                                 bytes.size() - transferred)
            : pollsys::write_some(socket, bytes.data() + transferred, bytes.size() - transferred);
        if (result.status == pollsys::sys_status::would_block) break;
        if (result.status != pollsys::sys_status::ok) return {transferred, true};
        transferred += result.transferred;
        if (result.transferred == 0) break;
    }
    return {transferred, false};
}

bool io_managed_socket_backend::step_locked(const std::shared_ptr<op_state>& op,
    const std::shared_ptr<registration>& lease, completions& done) {
    if (is_udp(op->kind())) {
        const auto result = datagram_step(lease->socket, *op);
        if (!result) return false;
        finish_locked(op, *result, done);
        return true;
    }
    if (op->kind() == io_op_kind::accept) return accept_locked(op, lease, done);
    const bool read = op->kind() == io_op_kind::read;
    if (!read && lease->write_closed) {
        finish_locked(op, closed_result(), done);
        return true;
    }
    const auto [transferred, hangup] = transfer_stream(lease->socket, *op);
    const bool empty = read ? std::get<read_payload>(op->payload()).buffer.empty()
                            : std::get<write_payload>(op->payload()).bytes.empty();
    if (transferred == 0 && !hangup && !empty) return false;
    complete_stream_locked(op, lease, {transferred, hangup}, done);
    return true;
}

void io_managed_socket_backend::complete_stream_locked(const std::shared_ptr<op_state>& op,
    const std::shared_ptr<registration>& lease, std::pair<std::size_t, bool> result, completions& done) {
    const auto [transferred, hangup] = result;
    finish_locked(op, hangup && transferred == 0
        ? closed_result() : io_result{http::outcome_code::ok, transferred, 0, {}}, done);
    if (!hangup) return;
    if (op->kind() == io_op_kind::write) {
        // A broken write direction says nothing about unread inbound bytes.
        lease->write_closed = true;
    } else if (transferred == 0) {
        retire_locked(op->connection(), done);
    }
}

bool io_managed_socket_backend::wants_event(const op_state& op, std::uint32_t events) {
    const bool read = op.kind() == io_op_kind::udp_receive || op.kind() == io_op_kind::read || op.kind() == io_op_kind::accept;
    return read ? (events & k_read) : (op.kind() == io_op_kind::udp_send || op.kind() == io_op_kind::write) && (events & k_write);
}

std::vector<std::shared_ptr<op_state>> io_managed_socket_backend::collect_event_locked(
    std::uint64_t id, std::uint32_t events) const {
    std::vector<std::shared_ptr<op_state>> batch;
    for (const auto& entry : pending_) {
        const auto& op = entry.second;
        if (op->connection() == id && wants_event(*op, events)) batch.push_back(op);
    }
    std::sort(batch.begin(), batch.end(), [](const auto& a, const auto& b) {
        return a->sequence() < b->sequence();
    });
    return batch;
}

void io_managed_socket_backend::dispatch_batch(std::uint64_t token,
    const std::vector<std::shared_ptr<op_state>>& batch) {
    std::shared_ptr<registration> lease;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto found = tokens_.find(token);
        if (closed_ || found == tokens_.end()) return;
        lease = connections_.at(found->second);
    }
    bool read_blocked = false;
    bool write_blocked = false;
    for (const auto& op : batch) {
        completions done;
        {
            std::lock_guard<std::mutex> lock(mu_);
            // Cancel/release can win between operations, including selected ones.
            if (closed_ || !tokens_.contains(token)) break;
            if (!pending_.contains(op.get())) continue;
            bool& blocked = (op->kind() == io_op_kind::write || op->kind() == io_op_kind::udp_send) ? write_blocked : read_blocked;
            if (!blocked) blocked = !step_locked(op, lease, done);
        }
        deliver(done);
    }
}

void io_managed_socket_backend::run_managed() {
    struct driver_scope {
        io_managed_socket_backend*& active;
        io_managed_socket_backend* previous;
        ~driver_scope() { active = previous; }
    };
    driver_scope scope{current_driver_, std::exchange(current_driver_, this)};
    run_loop();
}

int io_managed_socket_backend::prepare_wait() {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_) return -1;
    if (wake_failed_) throw std::runtime_error("httpserver: managed wake failed");
    const auto deadline = reconcile_locked();
    return poll_timeout_ms(std::chrono::steady_clock::now(), deadline);
}

void io_managed_socket_backend::finish_wait() {
    std::lock_guard<std::mutex> lock(mu_);
    ++iterations_;
    wake_.drain();
    wake_pending_ = false;
}

void io_managed_socket_backend::expire_timers() {
    sweep_due_timers(pending_, mu_, std::chrono::steady_clock::now());
}

void io_managed_socket_backend::dispatch_ready(std::uint64_t token, std::uint32_t directions,
                                              std::uint32_t disabled) {
    std::vector<std::shared_ptr<op_state>> batch;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto found = tokens_.find(token);
        if (closed_ || found == tokens_.end()) return;
        connections_.at(found->second)->armed &= ~disabled;
        batch = collect_event_locked(found->second, directions);
    }
    dispatch_batch(token, batch);
}

}  // namespace detail
}  // namespace httpserver
