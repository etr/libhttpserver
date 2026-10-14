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

// Installed public timed-wait consumer; executor and signal are borrowed
// only through the checked completion, including immediate owner teardown.
#include <chrono>
#include <thread>
#include <utility>

#include <httpserver/concurrency/concurrency.hpp>

namespace {

httpserver::task<httpserver::resume_outcome> timed_wait(
        httpserver::resume_signal signal, std::chrono::steady_clock::duration timeout) {
    co_return co_await signal.wait_for(timeout);
}

bool check(httpserver::resume_outcome expected) {
    httpserver::manual_executor executor;
    httpserver::resume_signal signal;
    int deliveries = 0;
    bool valid = false;
    const auto timeout = expected == httpserver::resume_outcome::timeout
        ? std::chrono::steady_clock::duration::zero() : std::chrono::hours(1);
    httpserver::spawn(executor, timed_wait(signal, timeout), [&](auto result) {
        ++deliveries;
        valid = result.has_value() && result.value() == expected
            && executor.is_current() && httpserver::current_executor() == &executor;
    });
    executor.run_pending();
    if (expected == httpserver::resume_outcome::resumed) signal.signal();
    if (expected == httpserver::resume_outcome::cancelled) signal.cancel();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (deliveries == 0 && std::chrono::steady_clock::now() < limit) {
        executor.run_pending();
        std::this_thread::yield();
    }
    // Retired frames must stay retired through repeated losing triggers.
    signal.signal();
    signal.cancel();
    executor.run_pending();
    return valid && deliveries == 1 && executor.pending() == 0;
}

}  // namespace

int main() {
    return check(httpserver::resume_outcome::timeout)
        && check(httpserver::resume_outcome::resumed)
        && check(httpserver::resume_outcome::cancelled) ? 0 : 1;
}
