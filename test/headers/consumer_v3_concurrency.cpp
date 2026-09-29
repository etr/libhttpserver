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

// TASK-098 Step 6 (Check A): a consumer including the v3 concurrency
// umbrella <httpserver/concurrency/concurrency.hpp> — and each
// sub-header directly — without HTTPSERVER_COMPILATION (or any other
// build/TLS configuration macro) must compile and link cleanly. The v3
// concurrency core is header-only, so the empty LDADD of this target is
// itself part of the contract. The test passes by virtue of compiling
// and linking; main() asserts nothing.

#include <exception>
#include <utility>

#include <httpserver/concurrency/concurrency.hpp>
#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/concurrency/task.hpp>

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
    using httpserver::resume_outcome;
    using httpserver::resume_signal;
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

    spawn(manual, []() -> task<void> { co_return; }(),
          [](task_result<void> result) {
              if (result.is_exception()) std::rethrow_exception(result.exception());
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
    resume_signal signal;
    resume_signal signal_copy = signal;
    signal.signal();
    signal_copy.cancel();  // loses the race: no-op
    resume_outcome outcome = resume_outcome::timeout;
    spawn(manual, [signal]() -> task<resume_outcome> {
        co_return co_await signal.wait();
    }(), [&](task_result<resume_outcome> result) {
        if (result.has_value()) outcome = result.value();
    });
    manual.run_pending();

    // The typed cancellation marker is a plain exception type.
    try {
        throw cancelled_exception();
    } catch (const cancelled_exception&) {
    }

    return (value_seen == 7 && observed == nullptr && !current
            && stop_now && stop_seen && stop_possible
            && outcome == resume_outcome::resumed) ? 0 : 1;
}

int main() {
    return use_v3_concurrency_types() == 0 ? 0 : 0;
}
