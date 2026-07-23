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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#ifndef TEST_INTEG_TEST_UTILS_HPP_
#define TEST_INTEG_TEST_UTILS_HPP_

#include <chrono>
#include <cstdlib>

// Shared helpers for the integration test suite.
//
// under_valgrind(): the check-valgrind-* lanes run with VALGRIND=valgrind in
// the environment (set by the Automake TESTS_ENVIRONMENT). Under valgrind
// every thread is serialised onto one core, so writer/reader threads can
// starve each other for the entire window of a concurrency stress test -- a
// scheduler artifact, not a lock-discipline bug. Concurrency stress tests
// (route_table_concurrency.cpp, ws_registry_concurrency.cpp) use this to
// relax ONLY their liveness assertions, never the race/crash gate.
inline bool under_valgrind() { return std::getenv("VALGRIND") != nullptr; }

// Concurrency-stress sizing knobs (route_table_concurrency.cpp,
// ws_registry_concurrency.cpp). helgrind/drd track a happens-before
// relation over every lock and atomic across every pair of threads, so the
// native-size stress (up to 20 threads hammering a single shared_mutex for a
// 30 s real-time window) makes the tool's internal segment/vector-clock
// state grow superlinearly -- enough to blow the 90-minute CI budget (the
// drd lane was observed hanging inside route_table_concurrency, producing no
// output for 88 minutes). memcheck, which tracks no thread ordering, runs the
// same binary in ~60 s. A race detector needs only a short window of genuine
// concurrency to flag a missing-synchronisation access, so shrink BOTH the
// per-role thread fan-out and the run window sharply under valgrind. The
// native and TSan lanes (VALGRIND unset) keep the full-size stress.
inline std::chrono::seconds stress_window() {
    return under_valgrind() ? std::chrono::seconds(3)
                            : std::chrono::seconds(30);
}
inline int stress_threads(int full_native) {
    return under_valgrind() ? (full_native < 3 ? full_native : 3)
                            : full_native;
}
//
// Historical note: this header previously exposed `as_shared(http_resource&)`,
// a thin wrapper that returned a std::shared_ptr<http_resource> with a no-op
// deleter pointing at a stack-allocated resource. That helper was incompatible
// with MHD's request_completed callback, which fires from a daemon worker
// thread during webserver::stop(). When the test body returned, the stack-
// allocated resource was destroyed before stop() drained the daemon, so the
// callback dereferenced freed memory through the still-live no-op-deleter
// shared_ptr the webserver was holding. A wildcard valgrind suppression had
// been masking the UAF.
//
// The fix: tests now register resources via `std::make_shared<T>(...)`. The
// resource's storage lives on the heap and stays alive until the webserver
// releases its last reference (which happens AFTER stop() has drained MHD).
// The `as_shared` helper is gone -- there is no safe shape that preserves
// stack-allocation as the storage strategy.

#endif  // TEST_INTEG_TEST_UTILS_HPP_
