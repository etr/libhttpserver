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

#ifndef TEST_UNIT_MANUAL_EXECUTOR_POST_PROBE_HPP_
#define TEST_UNIT_MANUAL_EXECUTOR_POST_PROBE_HPP_

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

// Linker interposition leaves the shipped executor untouched. Pause after
// its real queue unlock, then observe actual notifications after retirement.
#if defined(LHS_TEST_POST_TAIL_WRAP)
#include <pthread.h>

namespace post_probe {

inline bool await(const std::atomic<bool>& flag) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!flag.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= limit) return false;
        std::this_thread::yield();
    }
    return true;
}

struct publication {
    pthread_mutex_t* mutex = nullptr;
    const std::thread::id owner = std::this_thread::get_id();
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<bool> retired{false};
    std::atomic<bool> expired{false};
    std::atomic<int> late_notifications{0};
    std::atomic<int> publications{0};
};

// Each lookup retains ownership before any dereference, including unrelated
// mutex unlocks. Clearing active retires discovery, not readers already inside
// the wrapper. Producer ownership also covers the later notification wrapper.
// C++20 atomic shared_ptr uses a spin lock in the wrapped libstdc++ library;
// it does not recursively enter this pthread mutex-unlock wrapper.
inline std::atomic<std::shared_ptr<publication>> active{nullptr};
inline thread_local publication* capture = nullptr;
inline thread_local std::shared_ptr<publication> producer;

// Only the explicitly armed reader pauses. The fixture joins it before
// destroying this gate, including when a bounded diagnostic expires.
struct lookup_gate {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<bool> expired{false};
};

inline thread_local lookup_gate* lookup_pause = nullptr;

}  // namespace post_probe

extern "C" int __real_pthread_mutex_unlock(pthread_mutex_t* mutex);
extern "C" void __real__ZNSt18condition_variable10notify_oneEv(void* condition);

extern "C" int __wrap_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    const int result = __real_pthread_mutex_unlock(mutex);
    if (post_probe::capture) post_probe::capture->mutex = mutex;
    auto probe = post_probe::active.load(std::memory_order_acquire);
    if (probe && post_probe::lookup_pause) {
        auto* gate = post_probe::lookup_pause;
        gate->entered.store(true, std::memory_order_release);
        if (!post_probe::await(gate->release)) gate->expired = true;
    }
    if (probe && probe->mutex == mutex
        && probe->owner != std::this_thread::get_id()) {
        post_probe::producer = probe;
        ++probe->publications;
        probe->entered.store(true, std::memory_order_release);
        if (!post_probe::await(probe->release)) probe->expired = true;
    }
    return result;
}

extern "C" void __wrap__ZNSt18condition_variable10notify_oneEv(void* condition) {
    auto probe = post_probe::producer;
    if (probe && probe->retired.load(std::memory_order_acquire)) {
        ++probe->late_notifications;
    }
    __real__ZNSt18condition_variable10notify_oneEv(condition);
}
#endif

#endif  // TEST_UNIT_MANUAL_EXECUTOR_POST_PROBE_HPP_
