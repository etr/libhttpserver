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

io_poll_backend::io_poll_backend() {
    thread_ = std::thread([this] { run_loop(); });
}

io_poll_backend::~io_poll_backend() {
    close();
    stop_.store(true, std::memory_order_release);
    wake_.signal();
    if (thread_.joinable()) {
        thread_.join();
    }
    // Sockets are the backend's to close: release_connection may have
    // taken some out already (its record then holds k_invalid_socket).
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& entry : connections_) {
        pollsys::close_socket(entry.second.socket);
    }
    connections_.clear();
}

void io_poll_backend::submit(op_state& op) {
    std::shared_ptr<op_state> state = op.shared_from_this();
    bool registered = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        op.set_sequence(next_sequence_++);
        if (closed_) {
            // Post-close submits complete immediately -- no silent drops.
            finish_now(state,
                       io_result{http::outcome_code::connection_closed});
            return;
        }
        if (op.kind() != io_op_kind::cancel) {
            const auto cit = connections_.find(op.connection());
            if (cit != connections_.end() && cit->second.dead) {
                finish_now(state,
                           io_result{http::outcome_code::connection_closed});
                return;
            }
            if (!pending_.emplace(&op, state).second) {
                throw std::logic_error(
                    "httpserver::io operation submitted twice to backend");
            }
            registered = true;
        }
    }
    if (registered) {
        // Doorbell: the loop rebuilds readiness from the registry.
        wake_.signal();
        return;
    }

    // A cancel op resolves at submit time from its target's state
    // (fake_io_backend parity). Cancel ops never occupy the registry.
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
    finish_now(state, io_result{closed
                                    ? http::outcome_code::connection_closed
                                    : http::outcome_code::invalid_state});
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
    }
    // The loop may hold a stale interest projection for this op.
    wake_.signal();
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

void io_poll_backend::adopt_socket(std::uint64_t id,
                                   pollsys::native_socket_t socket,
                                   bool listener) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = connections_.find(id);
        if (it != connections_.end()
            && it->second.socket != pollsys::k_invalid_socket) {
            throw std::logic_error(
                "httpserver::io connection id already adopted");
        }
        connection_record& record = connections_[id];
        record.socket = socket;
        record.listener = listener;
        record.dead = false;
        if (id >= next_connection_id_) {
            next_connection_id_ = id + 1;
        }
    }
    wake_.signal();
}

pollsys::native_socket_t io_poll_backend::native_handle(
    std::uint64_t id) const {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = connections_.find(id);
    return it == connections_.end() ? pollsys::k_invalid_socket
                                    : it->second.socket;
}

void io_poll_backend::release_connection(std::uint64_t id) {
    hangup_connection(id);
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = connections_.find(id);
        if (it == connections_.end()) {
            return;
        }
        pollsys::close_socket(it->second.socket);
    }
    wake_.signal();
}

void io_poll_backend::rearm_after_would_block(
    const std::vector<std::shared_ptr<op_state>>& batch,
    std::size_t from) {
    std::vector<std::shared_ptr<op_state>> raced_closed;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (std::size_t i = from; i < batch.size(); ++i) {
            if (closed_) {
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

void io_poll_backend::hangup_connection(std::uint64_t id) {
    std::vector<std::shared_ptr<op_state>> swept;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto cit = connections_.find(id);
        if (cit != connections_.end()) {
            cit->second.dead = true;
        }
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->second->connection() == id) {
                swept.push_back(it->second);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto& state : swept) {
        finish_now(state,
                   io_result{http::outcome_code::connection_closed});
    }
}

std::uint64_t io_poll_backend::register_accepted_socket(
    pollsys::native_socket_t socket) {
    std::lock_guard<std::mutex> lock(mu_);
    while (connections_.count(next_connection_id_) != 0) {
        ++next_connection_id_;
    }
    const std::uint64_t id = next_connection_id_++;
    connection_record& record = connections_[id];
    record.socket = socket;
    record.listener = false;
    record.dead = false;
    return id;
}

std::size_t io_poll_backend::wake() {
    std::vector<std::shared_ptr<op_state>> wakes;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->second->kind() == io_op_kind::wake) {
                wakes.push_back(it->second);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
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
        swept.reserve(pending_.size());
        for (const auto& entry : pending_) {
            swept.push_back(entry.second);
        }
        pending_.clear();
    }
    wake_.signal();
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
    std::unordered_map<std::uint64_t, pollsys::event_mask>& interest) {
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
        pollsys::close_socket(it->second.socket);
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
        wake_.drain();
        if (ready >= 0) {
            dispatch_revents(fds, ids);  // EINTR and friends: just re-arm
        }
        expire_due_timers(std::chrono::steady_clock::now());
    }
}

void io_poll_backend::take_direction_locked(
    std::uint64_t id, bool reads_and_accepts,
    std::vector<std::shared_ptr<op_state>>& batch) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        const std::shared_ptr<op_state>& op = it->second;
        const io_op_kind kind = op->kind();
        const io_op_kind wanted =
            reads_and_accepts ? io_op_kind::read : io_op_kind::write;
        const bool matches = op->connection() == id
                             && (kind == wanted
                                 || (reads_and_accepts
                                     && kind == io_op_kind::accept));
        if (matches) {
            batch.push_back(op);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void io_poll_backend::finish_batch_from(
    const std::vector<std::shared_ptr<op_state>>& batch, std::size_t from,
    io_result result) {
    for (std::size_t i = from; i < batch.size(); ++i) {
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
        finish_now(state, io_result{http::outcome_code::ok, 0,
                                    register_accepted_socket(fresh),
                                    peer});
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

void io_poll_backend::dispatch_batch(
    std::uint64_t id, std::vector<std::shared_ptr<op_state>>& batch,
    bool readable) {
    if (batch.empty()) {
        return;
    }
    std::sort(batch.begin(), batch.end(), sequence_before);

    pollsys::native_socket_t socket = pollsys::k_invalid_socket;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto cit = connections_.find(id);
        if (cit == connections_.end() || cit->second.dead) {
            // Released underneath us: the batch cannot make progress.
            finish_batch_from(batch, 0, closed_result());
            return;
        }
        socket = cit->second.socket;
    }

    for (std::size_t i = 0; i < batch.size(); ++i) {
        const step_outcome outcome =
            !readable ? write_step(socket, batch[i])
                      : (batch[i]->kind() == io_op_kind::accept
                             ? accept_step(socket, batch[i])
                             : read_step(socket, batch[i]));
        if (outcome == step_outcome::completed) {
            continue;
        }
        if (outcome == step_outcome::pending_again) {
            rearm_after_would_block(batch, i);
            return;
        }
        // Hangup: the step completed the op; the rest of the batch and
        // everything else pending on the connection follows it.
        finish_batch_from(batch, i + 1, closed_result());
        hangup_connection(id);
        return;
    }
}

void io_poll_backend::dispatch_readable(std::uint64_t id) {
    std::vector<std::shared_ptr<op_state>> batch;
    {
        std::lock_guard<std::mutex> lock(mu_);
        take_direction_locked(id, true, batch);
    }
    dispatch_batch(id, batch, true);
}

void io_poll_backend::dispatch_writable(std::uint64_t id) {
    std::vector<std::shared_ptr<op_state>> batch;
    {
        std::lock_guard<std::mutex> lock(mu_);
        take_direction_locked(id, false, batch);
    }
    dispatch_batch(id, batch, false);
}

}  // namespace detail
}  // namespace httpserver
