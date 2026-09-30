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

// io_connection_owner implementation (TASK-099): the FIFO queue, the
// coalesced posted drain, and the apply loop that gives every
// connection one deterministic linear completion order.

#include "httpserver/detail/io_connection_owner.hpp"

#include <exception>
#include <utility>

namespace httpserver {
namespace detail {

io_connection_owner::io_connection_owner(executor& ex) noexcept : ex_(ex) { }

io_connection_owner::~io_connection_owner() {
    // Apply still-queued records with their already-decided results; a
    // claimed completion is never dropped. Resumptions posted from here
    // are witness-guarded, so frames destroyed by the same teardown are
    // safe. Documented teardown order keeps the backend quiet by now.
    std::deque<record> remaining;
    {
        std::lock_guard<std::mutex> lock(mu_);
        remaining.swap(queue_);
    }
    for (record& entry : remaining) {
        entry.state->apply(entry.result);
    }
}

void io_connection_owner::enqueue(std::shared_ptr<op_state> state,
                                  io_result result) {
    bool need_post = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back(record{std::move(state), result});
        if (!drain_posted_) {
            drain_posted_ = true;
            need_post = true;
        }
    }
    if (!need_post) return;
    try {
        ex_.post([this] { drain(); });
    } catch (...) {
        // post() is documented not to throw; if a broken executor does,
        // release the coalescing flag so later enqueues can still post.
        std::lock_guard<std::mutex> lock(mu_);
        drain_posted_ = false;
    }
}

std::size_t io_connection_owner::pending() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
}

void io_connection_owner::drain() {
    std::deque<record> batch;
    {
        std::lock_guard<std::mutex> lock(mu_);
        draining_.store(true, std::memory_order_release);
        batch.swap(queue_);
    }
    for (;;) {
        // No reentrancy: while record N applies (possibly resuming a
        // task inline), record N+1 waits. Records enqueued by resumed
        // tasks land on queue_ and are picked up by the loop below.
        while (!batch.empty()) {
            record entry = std::move(batch.front());
            batch.pop_front();
            entry.state->apply(entry.result);
        }
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.empty()) {
            drain_posted_ = false;
            draining_.store(false, std::memory_order_release);
            return;
        }
        batch.swap(queue_);
    }
}

}  // namespace detail
}  // namespace httpserver
