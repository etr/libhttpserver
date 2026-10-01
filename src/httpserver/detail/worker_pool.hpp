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

// Thread-pool executor of the v3 native server (TASK-108, architecture
// §3.1). The connection engine runs every connection task tree on one
// shared worker pool sized from server_options::concurrency_options.
//
// Semantics:
//   - FIFO: one mutex- and condition-variable-guarded queue; workers
//     take the front item, so posted order is execution order per
//     waiting worker (multi-worker execution interleaves by design --
//     ordering guarantees belong to the per-connection owner).
//   - workers == 0 resolves to the detected hardware parallelism,
//     clamped to [1, 16]; explicit counts pass through (server_options
//     validation owns the documented ceiling).
//   - post() never blocks and never drops while the queue lives. After
//     drain_and_join() the queue outlives the workers only for the
//     destructor's final inline drain; posting from user code after
//     drain_and_join() returns queues work nothing will run -- the
//     stop contract (native_server::stop) posts nothing afterwards.
//   - drain_and_join(): wakes the workers (they exit once the queue is
//     empty), joins them, then runs anything still queued (including
//     items chained by the last running work items) inline on the
//     calling thread. Concurrent producers during the inline tail are
//     outside the stop contract. The destructor drains implicitly.
//
// is_current() follows the executor.h convention: true while a work
// item of this pool is executing on the calling thread (the slot is
// installed around every work invocation, worker or inline drain).

#if !defined(HTTPSERVER_COMPILATION)
#error "worker_pool.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_WORKER_POOL_HPP_
#define SRC_HTTPSERVER_DETAIL_WORKER_POOL_HPP_

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <httpserver/concurrency/executor.hpp>

namespace httpserver {

namespace detail {

class worker_pool final : public executor {
 public:
    // Spawns resolve_workers(@p requested) worker threads.
    explicit worker_pool(std::size_t requested);

    worker_pool(const worker_pool&) = delete;
    worker_pool& operator=(const worker_pool&) = delete;
    worker_pool(worker_pool&&) = delete;
    worker_pool& operator=(worker_pool&&) = delete;

    // Implicit drain: drain_and_join().
    ~worker_pool() override;

    // executor seam.
    void post(handler work) override;
    bool is_current() const noexcept override;

    // Live worker threads.
    std::size_t thread_count() const noexcept;

    // Work items queued but not yet taken.
    std::size_t pending() const;

    // Quiesces the pool: joins the workers, then drains the queue
    // inline on the calling thread. Idempotent (the second call finds
    // no threads and an empty queue).
    void drain_and_join() noexcept;

 private:
    // workers == 0 -> hardware_concurrency clamped to [1, 16];
    // otherwise the requested count.
    static std::size_t resolve_workers(std::size_t requested) noexcept;

    // Worker body: take-and-run until stopped and drained.
    void run_worker() noexcept;

    // Runs one item with this pool installed as the current executor.
    void run_one(handler& work) noexcept;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<handler> queue_;      // guarded by mu_
    bool stopped_ = false;           // guarded by mu_
    std::vector<std::thread> threads_;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_WORKER_POOL_HPP_
