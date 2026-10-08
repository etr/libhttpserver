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

#include "httpserver/detail/io_connection_owner.hpp"

#include <deque>
#include <memory>
#include <exception>
#include <utility>

namespace httpserver {
namespace detail {

io_connection_owner::io_connection_owner(executor& ex, std::size_t packet_count, std::size_t packet_bytes) noexcept
    : ex_(ex), state_(std::make_shared<shared_state>()) {
    state_->max_packet_count = packet_count;
    state_->max_packet_bytes = packet_bytes;
}

io_connection_owner::~io_connection_owner() {
    // Apply still-queued records with their already-decided results; a
    // claimed completion is never dropped. Resumptions posted from here
    // are witness-guarded, so frames destroyed by the same teardown are
    // safe. Documented teardown order keeps the backend quiet by now;
    // a drain job still queued finds the swept state empty and no-ops.
    std::lock_guard<std::recursive_mutex> posting_lock(state_->posting_mu);
    std::deque<record> remaining;
    {
        std::lock_guard<std::mutex> lock(state_->mu);
        state_->alive = false;
        remaining.swap(state_->queue);
    }
    for (record& entry : remaining) {
        apply_record(std::move(entry), state_);
    }
}

datagram_enqueue_code io_connection_owner::datagram_port::enqueue(
    std::shared_ptr<datagram_delivery> delivery, std::shared_ptr<const io_datagram> packet) const {
    const auto pending = state_.lock();
    if (!pending || !delivery || !packet) return datagram_enqueue_code::retired;
    std::lock_guard<std::recursive_mutex> posting_lock(pending->posting_mu);
    bool need_post = false;
    {
        std::lock_guard<std::mutex> lock(pending->mu);
        if (!pending->alive || delivery->retired.load(std::memory_order_acquire)) return datagram_enqueue_code::retired;
        if (pending->packet_count >= pending->max_packet_count
            || packet->bytes.size() > pending->max_packet_bytes - pending->packet_bytes) return datagram_enqueue_code::full;
        pending->queue.push_back(record{{}, {}, std::move(delivery), std::move(packet)});
        ++pending->packet_count;
        pending->packet_bytes += pending->queue.back().packet->bytes.size();
        if (!pending->drain_posted) {
            pending->drain_posted = true;
            need_post = true;
        }
    }
    if (need_post) {
        try {
            // Queue lock is released even for synchronous executors; posting
            // lifetime stays pinned until post returns, including owner teardown.
            ex_->post([pending] { drain(pending); });
        } catch (...) {
            std::lock_guard<std::mutex> lock(pending->mu);
            pending->drain_posted = false;
        }
    }
    return datagram_enqueue_code::queued;
}

void io_connection_owner::apply_record(record entry, const std::shared_ptr<shared_state>& pending) {
    if (entry.state) {
        entry.state->apply(entry.result);
        return;
    }
    bool alive;
    {
        std::lock_guard<std::mutex> lock(pending->mu);
        alive = pending->alive;
    }
    if (alive && !entry.delivery->retired.load(std::memory_order_acquire)) {
        entry.delivery->sink->on_datagram(entry.packet);
    }
    // Reservations cover detached drain batches and reentrant callbacks, too.
    std::lock_guard<std::mutex> lock(pending->mu);
    --pending->packet_count;
    pending->packet_bytes -= entry.packet->bytes.size();
}

void io_connection_owner::enqueue(std::shared_ptr<op_state> state,
                                  io_result result) {
    bool need_post = false;
    {
        std::lock_guard<std::mutex> lock(state_->mu);
        state_->queue.push_back(record{std::move(state), result});
        if (!state_->drain_posted) {
            state_->drain_posted = true;
            need_post = true;
        }
    }
    if (!need_post) return;
    try {
        // The job shares ownership of the queue state (not of this):
        // it may run after the owner's destruction, racing a
        // connection teardown, and must stay memory-safe when it does.
        ex_.post([pending = state_] { drain(std::move(pending)); });
    } catch (...) {
        // post() is documented not to throw; if a broken executor does,
        // release the coalescing flag so later enqueues can still post.
        std::lock_guard<std::mutex> lock(state_->mu);
        state_->drain_posted = false;
    }
}

std::size_t io_connection_owner::pending() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->queue.size();
}

void io_connection_owner::drain(std::shared_ptr<shared_state> pending) {
    std::deque<record> batch;
    {
        std::lock_guard<std::mutex> lock(pending->mu);
        pending->draining.store(true, std::memory_order_release);
        batch.swap(pending->queue);
    }
    for (;;) {
        // No reentrancy: while record N applies (possibly resuming a
        // task inline), record N+1 waits. Records enqueued by resumed
        // tasks land on the queue and are picked up by the loop below.
        while (!batch.empty()) {
            record entry = std::move(batch.front());
            batch.pop_front();
            apply_record(std::move(entry), pending);
        }
        std::lock_guard<std::mutex> lock(pending->mu);
        if (pending->queue.empty()) {
            pending->drain_posted = false;
            pending->draining.store(false, std::memory_order_release);
            return;
        }
        batch.swap(pending->queue);
    }
}

}  // namespace detail
}  // namespace httpserver
