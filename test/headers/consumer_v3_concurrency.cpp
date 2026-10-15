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

// Public headers compile without internal macros; compiled behavior uses library linkage.

#include <exception>
#include <utility>

#include <httpserver/concurrency/concurrency.hpp>
#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/concurrency/task.hpp>

namespace {
httpserver::task<httpserver::resume_outcome> wait_for_resume(
        httpserver::resume_signal signal, bool* started) {
    *started = true;
    co_return co_await signal.wait();
}

bool check_delayed_resume(bool already_fired, bool cancel_wins) {
    httpserver::manual_executor manual;
    httpserver::resume_signal signal;
    httpserver::resume_signal signal_copy = signal;
    if (already_fired) {
        signal.signal();
        signal_copy.cancel();  // loses to the already-fired signal
    }
    bool started = false;
    bool valid_result = false;
    int deliveries = 0;
    auto outcome = httpserver::resume_outcome::timeout;
    auto pending = wait_for_resume(signal, &started);
    httpserver::spawn(manual, std::move(pending), [&](auto result) {
        ++deliveries;
        valid_result = result.has_value();
        if (valid_result) outcome = result.value();
    });
    if (started || deliveries != 0) return false;
    manual.run_pending();
    if (!started || deliveries != (already_fired ? 1 : 0)) return false;
    if (cancel_wins) signal_copy.cancel();
    else signal.signal();
    signal.signal();
    signal.cancel();
    manual.run_pending();
    const auto expected = cancel_wins ? httpserver::resume_outcome::cancelled
                                      : httpserver::resume_outcome::resumed;
    return valid_result && deliveries == 1 && outcome == expected
        && manual.pending() == 0;
}
}  // namespace

// Touch every public type once so a missing declaration is a link (not
// just a compile) failure. No timers are armed here: this sentinel must
// never need a background thread.
int use_v3_concurrency_types() {
    using httpserver::cancelled_exception;
    using httpserver::executor;
    using httpserver::concurrency::unique_function;
    using httpserver::current_executor;
    using httpserver::inline_executor;
    using httpserver::manual_executor;
    using httpserver::spawn;
    using httpserver::stop_source;
    using httpserver::stop_token;
    using httpserver::task;
    using httpserver::task_result;

    // Executors: the abstract seam is exercised through the two shipped
    // deterministic implementations.
    manual_executor manual;
    inline_executor synchronous;
    executor* const observed = current_executor();
    const bool current = manual.is_current() || synchronous.is_current();
    manual.post([] { });
    manual.run_pending();
    synchronous.post([] { });

    // unique_function: move-only type erasure behind executor::post.
    unique_function<void()> work([] { });
    work();

    // task<T> + spawn + task_result.
    int value_seen = 0;
    spawn(manual, []() -> task<int> { co_return 7; }(),
          [&](task_result<int> result) {
              if (result.has_value()) value_seen = result.value();
          });
    manual.run_pending();

    bool void_seen = false;
    spawn(manual, []() -> task<void> { co_return; }(),
          [&](task_result<void> result) {
              void_seen = result.has_value();
          });
    manual.run_pending();

    // Cancellation: single request point, copyable fan-out tokens, and
    // the typed marker the promise converts into outcome_code::cancelled.
    stop_source source;
    const stop_token token = source.get_token();
    const stop_token token_copy = token;
    const bool stop_now = source.request_stop();
    const bool stop_seen = token_copy.stop_requested();
    const bool stop_possible = token.stop_possible();

    // Resume signal: one-shot, idempotent, copyable.
    const bool resumed = check_delayed_resume(false, false);
    const bool cancelled = check_delayed_resume(false, true);
    const bool already_resumed = check_delayed_resume(true, false);

    // The typed cancellation marker is a plain exception type.
    try {
        throw cancelled_exception();
    } catch (const cancelled_exception&) {
    }

    return (value_seen == 7 && void_seen && observed == nullptr && !current
            && stop_now && stop_seen && stop_possible
            && resumed && cancelled && already_resumed) ? 0 : 1;
}

int main() {
    return use_v3_concurrency_types();
}
