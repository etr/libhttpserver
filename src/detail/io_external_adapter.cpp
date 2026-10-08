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

#include <httpserver/detail/io_poll_backend.hpp>

#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace httpserver {
namespace detail {
namespace {
server::native_handle borrowed_handle(pollsys::native_socket_t socket) {
#if defined(_WIN32)
    constexpr auto kind = server::native_handle_kind::winsock_socket;
#else
    constexpr auto kind = server::native_handle_kind::posix_descriptor;
#endif
    return {static_cast<std::uintptr_t>(socket), kind,
            socket != pollsys::k_invalid_socket};
}

struct dispatch_guard {
    explicit dispatch_guard(std::atomic_bool& busy) : busy(busy) { }
    ~dispatch_guard() { busy.store(false, std::memory_order_release); }
    std::atomic_bool& busy;
};
}  // namespace

http::outcome io_poll_backend::ready() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (!wake_.valid() || wake_failed_) {
        return {http::outcome_code::connection_closed,
                "io backend: wake source unavailable; stop the server"};
    }
    return http::outcome::okay();
}

void io_poll_backend::activate_external() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        active_ = mode_ == server::loop_mode::external && !closed_ && wake_.valid();
    }
    notify();
}

void io_poll_backend::notify() {
    bool failed = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (wake_failed_ || wake_pending_) return;
        if (wake_.signal()) {
            wake_pending_ = true;
        } else {
            wake_failed_ = true;
            active_ = false;
            failed = true;
        }
    }
    // Hard doorbell failure terminates pending work; it never silently
    // leaves an external loop dependent on a periodic polling fallback.
    if (failed) close();
}

void io_poll_backend::acknowledge_wake() {
    if (!wake_.acknowledge()) wake_failed_ = true;
    wake_pending_ = false;
}

server::interest_snapshot io_poll_backend::interests() const {
    server::interest_snapshot snapshot;
    std::lock_guard<std::mutex> lock(mu_);
    if (!active_ || closed_ || wake_failed_) return snapshot;
    // Release swept the registration's pending work; no tombstone is needed.
    std::erase_if(connections_, [](const auto& entry) { return entry.second.dead; });
    snapshot.wake = server::socket_interest{server::socket_key {1}, 1,
        borrowed_handle(wake_.read_handle()), true, false};
    std::unordered_map<std::uint64_t, pollsys::event_mask> masks;
    snapshot.next_deadline = scan_interest_locked(masks);
    for (const auto& entry : connections_) {
        const auto& record = entry.second;
        const auto& lifetime = record.lifetime;
        if (record.dead || !lifetime) continue;
        const auto found = masks.find(entry.first);
        const bool wanted = found != masks.end();
        if (!wanted) {
            if (lifetime->published) {
                if (lifetime->generation == std::numeric_limits<std::uint64_t>::max()) {
                    lifetime->retired.store(true, std::memory_order_release);
                    throw std::overflow_error("io backend: registration generation exhausted");
                }
                ++lifetime->generation;
                lifetime->published = false;
            }
            continue;
        }
        snapshot.sockets.push_back({lifetime->key, lifetime->generation,
            borrowed_handle(lifetime->socket),
            (found->second & pollsys::k_readable) != 0,
            (found->second & pollsys::k_writable) != 0});
        lifetime->published = true;
    }
    return snapshot;
}

http::outcome io_poll_backend::dispatch(
    std::span<const server::readiness_event> events,
    std::chrono::steady_clock::time_point now) {
    if (dispatching_.exchange(true, std::memory_order_acq_rel)) {
        return {http::outcome_code::invalid_state, "io backend: dispatch already active"};
    }
    dispatch_guard guard(dispatching_);
    if (const auto status = begin_dispatch(events, now); !status.ok()) {
        return status;
    }
    if (const auto status = ready(); !status.ok()) {
        close();
        return status;
    }
    for (const auto& event : events) dispatch_event(event);
    expire_due_timers(now);
    return ready();
}

http::outcome io_poll_backend::begin_dispatch(
    std::span<const server::readiness_event> events,
    std::chrono::steady_clock::time_point now) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!active_ || closed_) {
            return {http::outcome_code::invalid_state, "io backend: inactive external driver"};
        }
        if (last_now_ && now < *last_now_) {
            return {http::outcome_code::invalid_state, "io backend: decreasing host time"};
        }
        last_now_ = now;
        // Acknowledge once, before socket processing. Mutation publishers
        // share this mutex; a later submission leaves the doorbell readable.
        for (const auto& event : events) {
            if (event.key == server::socket_key {1} && event.generation == 1
                    && event.readable) {
                acknowledge_wake();
                break;
            }
        }
    }
    return http::outcome::okay();
}

std::pair<std::uint64_t, std::shared_ptr<io_poll_backend::registration_lifetime>>
io_poll_backend::find_registration_locked(const server::readiness_event& event) const {
    for (const auto& entry : connections_) {
        const auto& lifetime = entry.second.lifetime;
        if (!lifetime || !lifetime->published || entry.second.dead) continue;
        if (lifetime->key == event.key && lifetime->generation == event.generation) {
            return {entry.first, lifetime};
        }
    }
    return {};
}

void io_poll_backend::dispatch_event(const server::readiness_event& event) {
    std::shared_ptr<registration_lifetime> lease;
    std::uint64_t id = 0;
    bool datagram = false;
    std::vector<std::shared_ptr<op_state>> reads;
    std::vector<std::shared_ptr<op_state>> writes;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_) return;
        std::tie(id, lease) = find_registration_locked(event);
        if (!lease) return;
        datagram = connections_.at(id).datagram;
        // Prepare both directions before detaching either one. A failure in
        // writable collection must also leave the readable batch recoverable.
        if (event.readable || (datagram && event.error)) collect_direction_locked(id, true, reads);
        if (event.writable || (datagram && event.error)) collect_direction_locked(id, false, writes);
        detach_batch_locked(reads);
        detach_batch_locked(writes);
    }
    // Drain readable data before honoring a simultaneous close indication.
    dispatch_batch(id, reads, lease);
    dispatch_batch(id, writes, lease);
    if (!datagram && (event.error || (event.closed && !event.readable && !event.writable))) {
        hangup_connection(id, lease);
    }
}

}  // namespace detail
}  // namespace httpserver
