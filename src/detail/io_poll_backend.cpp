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

// io_poll_backend implementation (TASK-100): the driver thread, the
// pending/connection registries, and the claim-funneled completion
// paths. Platform divergence lives in io_poll_sys.hpp; everything here
// is platform-neutral. The completion discipline mirrors
// fake_io_backend: take the op out of the registry under the mutex,
// then let op_state::claim_terminal() decide the single winner before
// handing the record to the connection owner. The mutex is never held
// across poll() or a stream read/write/accept syscall; the one socket
// operation that may run under it is close(), which is synchronous and
// non-blocking by contract (no SO_LINGER is ever set).

#include "httpserver/detail/io_poll_backend.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "httpserver/detail/io_connection_owner.hpp"
#include "httpserver/detail/io_timer_sweep.hpp"

namespace httpserver {
namespace detail {
namespace {
constexpr std::uint64_t k_listener_id_base = 1ULL << 62;

// Claims @p state and enqueues @p result to its owner (claim-loser is a
// no-op) -- fake_io_backend::finish_now parity.
void finish_now(const std::shared_ptr<op_state>& state, io_result result) {
    if (state->claim_terminal()) {
        state->owner()->enqueue(state, result);
    }
}

// The terminal every hangup path completes with.
io_result closed_result() {
    return io_result{http::outcome_code::connection_closed};
}

bool sequence_before(const std::shared_ptr<op_state>& lhs,
                     const std::shared_ptr<op_state>& rhs) {
    return lhs->sequence() < rhs->sequence();
}

}  // namespace

io_poll_backend::io_poll_backend(server::loop_mode mode) : mode_(mode) {
    if (mode_ == server::loop_mode::managed && wake_.valid()) {
        thread_ = std::thread([this] { run_loop(); });
    }
}

io_poll_backend::~io_poll_backend() {
    close();
    stop_.store(true, std::memory_order_release);
    notify();
    if (thread_.joinable()) {
        thread_.join();
    }
    // Sockets are the backend's to close: release_connection may have
    // taken some out already (its record then holds k_invalid_socket).
    std::lock_guard<std::mutex> lock(mu_);
    connections_.clear();
}

void io_poll_backend::submit(op_state& op) {
    const auto state = op.shared_from_this();
    bool registered = false;
    bool rejected = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        op.set_sequence(next_sequence_++);
        rejected = closed_ || (op.kind() != io_op_kind::cancel
                               && unavailable_locked(op));
        if (!rejected && op.kind() != io_op_kind::cancel) {
            register_pending_locked(state);
            registered = true;
        }
    }
    if (rejected) {
        finish_now(state, closed_result());
        return;
    }
    if (registered) {
        notify();
        return;
    }
    const auto target = std::get<cancel_payload>(op.payload()).target;
    if (try_cancel(target)) {
        finish_now(state, io_result{http::outcome_code::ok});
        return;
    }
    bool closed = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        closed = closed_;
    }
    finish_now(state, io_result{closed ? http::outcome_code::connection_closed
                                      : http::outcome_code::invalid_state});
}

void io_poll_backend::register_pending_locked(const std::shared_ptr<op_state>& state) {
    op_state& op = *state;
    if (!pending_.emplace(&op, state).second) {
        throw std::logic_error(
            "httpserver::io operation submitted twice to backend");
    }
    const auto cit = connections_.find(op.connection());
    if (cit != connections_.end()
            && (op.kind() == io_op_kind::read || op.kind() == io_op_kind::write
                || op.kind() == io_op_kind::accept)) {
        bindings_[&op] = cit->second.lifetime;
    }
}

bool io_poll_backend::unavailable_locked(const op_state& state) const {
    const auto cit = connections_.find(state.connection());
    return cit == connections_.end() ? state.connection() != 0
                                     : cit->second.dead;
}

void io_poll_backend::forget_binding(const std::shared_ptr<op_state>& state) {
    std::lock_guard<std::mutex> lock(mu_);
    bindings_.erase(state.get());
}

http::outcome_code io_poll_backend::request_cancel(op_state& target) {
    if (try_cancel(target.shared_from_this())) {
        return http::outcome_code::ok;
    }
    bool closed = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        closed = closed_;
    }
    return closed ? http::outcome_code::connection_closed
                  : http::outcome_code::invalid_state;
}

bool io_poll_backend::try_cancel(const std::shared_ptr<op_state>& target) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = pending_.find(target.get());
        if (it == pending_.end()) {
            return false;
        }
        pending_.erase(it);
        bindings_.erase(target.get());
    }
    // The loop may hold a stale interest projection for this op.
    notify();
    if (!target->claim_terminal()) {
        // A concurrent dispatch/close won the claim; the cancel attempt
        // lost and the caller reports invalid_state.
        return false;
    }
    target->owner()->enqueue(target,
                             io_result{http::outcome_code::cancelled});
    return true;
}

void io_poll_backend::adopt_connection(std::uint64_t id,
                                       pollsys::native_socket_t socket) {
    adopt_socket(id, socket, false);
}

void io_poll_backend::adopt_listener(std::uint64_t id,
                                     pollsys::native_socket_t socket) {
    adopt_socket(id, socket, true);
}

std::shared_ptr<io_poll_backend::registration_lifetime>
io_poll_backend::make_lifetime_locked(pollsys::native_socket_t socket) {
    try {
        if (next_identity_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("httpserver::io registration identity exhausted");
        }
        return std::make_shared<registration_lifetime>(
            socket, server::socket_key{next_identity_++});
    } catch (...) {
        // No record owns the handle if registration allocation failed.
        pollsys::close_socket(socket);
        throw;
    }
}

void io_poll_backend::adopt_socket(std::uint64_t id,
                                   pollsys::native_socket_t socket,
                                   bool listener) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = connections_.find(id);
        if (it != connections_.end() && !it->second.dead) {
            throw std::logic_error("httpserver::io connection id already adopted");
        }
        if (closed_) {
            pollsys::close_socket(socket);
            return;
        }
        connection_record record;
        record.socket = socket;
        record.listener = listener;
        record.lifetime = make_lifetime_locked(socket);
        connections_[id] = std::move(record);
        if (id >= next_connection_id_) {
            if (id == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("httpserver::io connection identity exhausted");
            }
            // Native listener identities must not advance the accepted-ID counter.
            if (id < k_listener_id_base) next_connection_id_ = id + 1;
        }
    }
    notify();
}

pollsys::native_socket_t io_poll_backend::native_handle(
    std::uint64_t id) const {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = connections_.find(id);
    return it == connections_.end() || it->second.dead
        ? pollsys::k_invalid_socket : it->second.socket;
}

void io_poll_backend::release_connection(std::uint64_t id) {
    hangup_connection(id);
}

void io_poll_backend::rearm_after_would_block(
    const std::vector<std::shared_ptr<op_state>>& batch,
    std::size_t from) {
    std::vector<std::shared_ptr<op_state>> raced_closed;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (std::size_t i = from; i < batch.size(); ++i) {
            const auto cit = connections_.find(batch[i]->connection());
            const auto binding = bindings_.find(batch[i].get());
            const auto lifetime = binding == bindings_.end()
                ? nullptr : binding->second.lock();
            const bool changed = batch[i]->connection() != 0
                && (cit == connections_.end() || lifetime != cit->second.lifetime);
            if (closed_ || unavailable_locked(*batch[i]) || changed) {
                bindings_.erase(batch[i].get());
                raced_closed.push_back(batch[i]);
                continue;
            }
            pending_.emplace(batch[i].get(), batch[i]);
        }
    }
    for (const auto& state : raced_closed) {
        finish_now(state,
                   io_result{http::outcome_code::connection_closed});
    }
}

void io_poll_backend::hangup_connection(std::uint64_t id,
    const std::shared_ptr<registration_lifetime>& expected) {
    std::vector<std::shared_ptr<op_state>> swept;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto cit = connections_.find(id);
        if (expected && (cit == connections_.end()
                         || cit->second.lifetime != expected)) return;
        if (cit != connections_.end() && cit->second.lifetime) {
            cit->second.dead = true;
            cit->second.lifetime->retired.store(true, std::memory_order_release);
            cit->second.lifetime.reset();
            cit->second.socket = pollsys::k_invalid_socket;
        }
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->second->connection() == id) {
                swept.push_back(it->second);
                bindings_.erase(it->first);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    notify();
    for (const auto& state : swept) {
        finish_now(state,
                   io_result{http::outcome_code::connection_closed});
    }
}

std::uint64_t io_poll_backend::register_accepted_socket(
    pollsys::native_socket_t socket, const std::shared_ptr<op_state>& accepting) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto binding = bindings_.find(accepting.get());
    const auto parent = binding == bindings_.end() ? nullptr : binding->second.lock();
    if (closed_ || !parent || parent->retired.load(std::memory_order_acquire)) {
        pollsys::close_socket(socket);
        return 0;
    }
    if (next_connection_id_ >= k_listener_id_base) {
        pollsys::close_socket(socket);
        throw std::overflow_error("httpserver::io connection identity exhausted");
    }
    const auto id = next_connection_id_++;
    connection_record record;
    record.socket = socket;
    record.lifetime = make_lifetime_locked(socket);
    connections_[id] = std::move(record);
    return id;
}

std::size_t io_poll_backend::wake() {
    std::vector<std::shared_ptr<op_state>> wakes;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->second->kind() == io_op_kind::wake) {
                wakes.push_back(it->second);
                bindings_.erase(it->first);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    notify();
    std::size_t fired = 0;
    for (const auto& state : wakes) {
        if (state->claim_terminal()) {
            state->owner()->enqueue(state, io_result{});
            ++fired;
        }
    }
    return fired;
}

std::size_t io_poll_backend::close() {
    std::vector<std::shared_ptr<op_state>> swept;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_) {
            return 0;
        }
        closed_ = true;
        active_ = false;
        for (auto& entry : connections_) {
            if (entry.second.lifetime) {
                entry.second.lifetime->retired.store(true, std::memory_order_release);
            }
        }
        swept.reserve(pending_.size());
        for (const auto& entry : pending_) {
            swept.push_back(entry.second);
        }
        pending_.clear();
        bindings_.clear();
    }
    notify();
    std::size_t completed = 0;
    for (const auto& state : swept) {
        if (state->claim_terminal()) {
            state->owner()->enqueue(
                state, io_result{http::outcome_code::connection_closed});
            ++completed;
        }
    }
    return completed;
}

std::size_t io_poll_backend::pending_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return pending_.size();
}

std::uint64_t io_poll_backend::poll_iterations() const {
    std::lock_guard<std::mutex> lock(mu_);
    return poll_iterations_;
}

void io_poll_backend::expire_due_timers(
    std::chrono::steady_clock::time_point now) {
    // Shared with fake_io_backend (io_timer_sweep.hpp) so both drivers
    // keep identical (deadline, sequence) expiry semantics.
    sweep_due_timers(pending_, mu_, now);
}

std::optional<std::chrono::steady_clock::time_point>
io_poll_backend::scan_interest_locked(
    std::unordered_map<std::uint64_t, pollsys::event_mask>& interest) const {
    std::optional<std::chrono::steady_clock::time_point> next_deadline;
    for (const auto& entry : pending_) {
        const op_state& op = *entry.second;
        pollsys::event_mask bits = 0;
        if (op.kind() == io_op_kind::read
            || op.kind() == io_op_kind::accept) {
            bits = pollsys::k_readable;
        } else if (op.kind() == io_op_kind::write) {
            bits = pollsys::k_writable;
        } else if (op.kind() == io_op_kind::timer) {
            const auto deadline =
                std::get<timer_payload>(op.payload()).deadline;
            if (!next_deadline || deadline < *next_deadline) {
                next_deadline = deadline;
            }
        }
        if (bits != 0) {
            interest[op.connection()] |= bits;
        }
    }
    return next_deadline;
}

void io_poll_backend::project_connections_locked(
    const std::unordered_map<std::uint64_t, pollsys::event_mask>& interest,
    std::vector<pollsys::poll_slot>& fds,
    std::vector<std::uint64_t>& ids) {
    std::vector<std::uint64_t> prunable;
    for (auto& entry : connections_) {
        const connection_record& record = entry.second;
        if (record.dead) {
            if (interest.count(entry.first) == 0) {
                prunable.push_back(entry.first);
            }
            continue;
        }
        const auto mask = interest.find(entry.first);
        if (mask == interest.end()) {
            continue;
        }
        fds.push_back(pollsys::poll_slot{record.socket, mask->second, 0});
        ids.push_back(entry.first);
    }
    for (const std::uint64_t id : prunable) {
        auto it = connections_.find(id);
        connections_.erase(it);
    }
}

std::optional<std::chrono::steady_clock::time_point>
io_poll_backend::build_projection(std::vector<pollsys::poll_slot>& fds,
                                  std::vector<std::uint64_t>& ids) {
    fds.push_back(
        pollsys::poll_slot{wake_.read_handle(), pollsys::k_readable, 0});
    ids.push_back(0);  // 0 marks the wake slot in the parallel id vector
    std::lock_guard<std::mutex> lock(mu_);
    std::unordered_map<std::uint64_t, pollsys::event_mask> interest;
    const std::optional<std::chrono::steady_clock::time_point> next_deadline =
        scan_interest_locked(interest);
    project_connections_locked(interest, fds, ids);
    return next_deadline;
}

void io_poll_backend::dispatch_revents(
    const std::vector<pollsys::poll_slot>& fds,
    const std::vector<std::uint64_t>& ids) {
    for (std::size_t i = 1; i < fds.size(); ++i) {
        const pollsys::event_mask revents = fds[i].revents;
        if (revents == 0) {
            continue;
        }
        if ((revents & (pollsys::k_poll_error
                        | pollsys::k_poll_invalid)) != 0) {
            hangup_connection(ids[i]);  // use-after-close defense
            continue;
        }
        if ((revents & pollsys::k_readable) != 0) {
            dispatch_readable(ids[i]);
        }
        if ((revents & pollsys::k_writable) != 0) {
            dispatch_writable(ids[i]);
        } else if ((revents & pollsys::k_poll_hangup) != 0) {
            // HUP without readable data (WSAPoll never reports a
            // reliable HUP, so on Windows the recv()/send() results
            // carry this decision instead).
            hangup_connection(ids[i]);
        }
    }
}

void io_poll_backend::run_loop() {
    std::vector<pollsys::poll_slot> fds;
    std::vector<std::uint64_t> ids;  // parallel to fds
    while (!stop_.load(std::memory_order_acquire)) {
        fds.clear();
        ids.clear();
        const std::optional<std::chrono::steady_clock::time_point>
            next_deadline = build_projection(fds, ids);

        // The only blocking point: no locks held, bounded by the
        // nearest timer deadline or the idle cap.
        const int timeout =
            poll_timeout_ms(std::chrono::steady_clock::now(), next_deadline);
        const int ready =
            pollsys::poll_call(fds.data(), fds.size(), timeout);
        {
            std::lock_guard<std::mutex> lock(mu_);
            ++poll_iterations_;
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            acknowledge_wake();
        }
        if (ready >= 0) {
            dispatch_revents(fds, ids);  // EINTR and friends: just re-arm
        }
        expire_due_timers(std::chrono::steady_clock::now());
    }
}

void io_poll_backend::collect_direction_locked(
    std::uint64_t id, bool reads_and_accepts,
    std::vector<std::shared_ptr<op_state>>& batch) const {
    const io_op_kind wanted =
        reads_and_accepts ? io_op_kind::read : io_op_kind::write;
    for (const auto& entry : pending_) {
        const auto& op = entry.second;
        const io_op_kind kind = op->kind();
        if (op->connection() == id
            && (kind == wanted || (reads_and_accepts && kind == io_op_kind::accept))) {
            batch.push_back(op);
        }
    }
}

void io_poll_backend::detach_batch_locked(
    const std::vector<std::shared_ptr<op_state>>& batch) {
    for (const auto& op : batch) pending_.erase(op.get());
}

void io_poll_backend::take_direction_locked(
    std::uint64_t id, bool reads_and_accepts,
    std::vector<std::shared_ptr<op_state>>& batch) {
    collect_direction_locked(id, reads_and_accepts, batch);
    detach_batch_locked(batch);
}

void io_poll_backend::finish_batch_from(
    const std::vector<std::shared_ptr<op_state>>& batch, std::size_t from,
    io_result result) {
    for (std::size_t i = from; i < batch.size(); ++i) {
        forget_binding(batch[i]);
        finish_now(batch[i], result);
    }
}

step_outcome io_poll_backend::accept_step(
    pollsys::native_socket_t socket,
    const std::shared_ptr<op_state>& state) {
    pollsys::native_socket_t fresh = pollsys::k_invalid_socket;
    net::peer_address peer;
    const pollsys::sys_result r =
        pollsys::accept_one(socket, &fresh, &peer);
    if (r.status == pollsys::sys_status::ok) {
        const auto id = register_accepted_socket(fresh, state);
        finish_now(state, id == 0 ? closed_result()
            : io_result{http::outcome_code::ok, 0, id, peer});
        return step_outcome::completed;
    }
    if (r.status == pollsys::sys_status::would_block) {
        return step_outcome::pending_again;  // spurious readiness
    }
    finish_now(state, closed_result());
    return step_outcome::hangup;
}

step_outcome io_poll_backend::read_step(
    pollsys::native_socket_t socket,
    const std::shared_ptr<op_state>& state) {
    const read_payload& payload = std::get<read_payload>(state->payload());
    std::span<std::byte> buffer = payload.buffer;
    if (buffer.size() == 0) {
        finish_now(state, io_result{http::outcome_code::ok, 0});
        return step_outcome::completed;
    }
    std::size_t filled = 0;
    bool hangup = false;
    while (filled < buffer.size()) {
        const pollsys::sys_result r = pollsys::read_some(
            socket, buffer.data() + filled, buffer.size() - filled);
        if (r.status == pollsys::sys_status::ok) {
            if (r.transferred == 0) {
                break;
            }
            filled += r.transferred;
            continue;
        }
        if (r.status == pollsys::sys_status::would_block) {
            break;
        }
        hangup = true;  // EOF/reset/error for this direction
        break;
    }
    if (filled == 0) {
        if (!hangup) {
            return step_outcome::pending_again;  // spurious readiness
        }
        finish_now(state, closed_result());
        return step_outcome::hangup;
    }
    // Full read, or bytes drained before the would-block / the hangup:
    // those bytes are delivered.
    finish_now(state, io_result{http::outcome_code::ok, filled});
    return hangup ? step_outcome::hangup : step_outcome::completed;
}

step_outcome io_poll_backend::write_step(
    pollsys::native_socket_t socket,
    const std::shared_ptr<op_state>& state) {
    const write_payload& payload = std::get<write_payload>(state->payload());
    std::span<const std::byte> bytes = payload.bytes;
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const pollsys::sys_result r = pollsys::write_some(
            socket, bytes.data() + sent, bytes.size() - sent);
        if (r.status == pollsys::sys_status::ok) {
            sent += r.transferred;
            if (r.transferred == 0) {
                break;
            }
            continue;
        }
        if (r.status != pollsys::sys_status::would_block) {
            finish_now(state, closed_result());
            return step_outcome::hangup;
        }
        break;  // partial send: transferred is the backpressure semantic
    }
    finish_now(state, io_result{http::outcome_code::ok, sent});
    return step_outcome::completed;
}

step_outcome io_poll_backend::socket_step(pollsys::native_socket_t socket,
    const std::shared_ptr<op_state>& state) {
    if (state->kind() == io_op_kind::accept) return accept_step(socket, state);
    if (state->kind() == io_op_kind::read) return read_step(socket, state);
    return write_step(socket, state);
}

void io_poll_backend::dispatch_batch(
    std::uint64_t id, std::vector<std::shared_ptr<op_state>>& batch,
    const std::shared_ptr<registration_lifetime>& expected) {
    if (batch.empty()) return;
    std::sort(batch.begin(), batch.end(), sequence_before);
    std::shared_ptr<registration_lifetime> lease = expected;
    if (!lease) {
        std::lock_guard<std::mutex> lock(mu_);
        const auto binding = bindings_.find(batch.front().get());
        if (binding != bindings_.end()) lease = binding->second.lock();
    }
    for (std::size_t i = 0; i < batch.size(); ++i) {
        if (!lease || lease->retired.load(std::memory_order_acquire)) {
            finish_batch_from(batch, i, closed_result());
            return;
        }
        const step_outcome outcome = socket_step(lease->socket, batch[i]);
        if (outcome == step_outcome::pending_again) {
            rearm_after_would_block(batch, i);
            return;
        }
        forget_binding(batch[i]);
        if (outcome == step_outcome::completed) continue;
        finish_batch_from(batch, i + 1, closed_result());
        hangup_connection(id, lease);
        return;
    }
}

void io_poll_backend::dispatch_readable(std::uint64_t id) {
    std::vector<std::shared_ptr<op_state>> batch;
    std::shared_ptr<registration_lifetime> lease;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto cit = connections_.find(id);
        if (cit == connections_.end() || cit->second.dead) return;
        lease = cit->second.lifetime;
        take_direction_locked(id, true, batch);
    }
    dispatch_batch(id, batch, lease);
}

void io_poll_backend::dispatch_writable(std::uint64_t id) {
    std::vector<std::shared_ptr<op_state>> batch;
    std::shared_ptr<registration_lifetime> lease;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto cit = connections_.find(id);
        if (cit == connections_.end() || cit->second.dead) return;
        lease = cit->second.lifetime;
        take_direction_locked(id, false, batch);
    }
    dispatch_batch(id, batch, lease);
}

}  // namespace detail
}  // namespace httpserver
