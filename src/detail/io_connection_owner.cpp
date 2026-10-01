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
#include <exception>
#include <utility>

namespace httpserver {
namespace detail {

io_connection_owner::io_connection_owner(executor& ex) noexcept
    : ex_(ex), state_(std::make_shared<shared_state>()) {
}

io_connection_owner::~io_connection_owner() {
    // Apply still-queued records with their already-decided results; a
    // claimed completion is never dropped. Resumptions posted from here
    // are witness-guarded, so frames destroyed by the same teardown are
    // safe. Documented teardown order keeps the backend quiet by now;
    // a drain job still queued finds the swept state empty and no-ops.
    std::deque<record> remaining;
    {
        std::lock_guard<std::mutex> lock(state_->mu);
        remaining.swap(state_->queue);
    }
    for (record& entry : remaining) {
        entry.state->apply(entry.result);
    }
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
            entry.state->apply(entry.result);
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
