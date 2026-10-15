/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <utility>

#include <httpserver.hpp>

namespace {
httpserver::task<int> wait_for_signal(httpserver::resume_signal signal,
                                     httpserver::manual_executor& executor) {
    if (httpserver::current_executor() != &executor) co_return -1;
    if (co_await signal.wait() != httpserver::resume_outcome::resumed) co_return -2;
    if (httpserver::current_executor() != &executor) co_return -3;
    co_return 42;
}
}  // namespace

bool compiled_operations_worker(httpserver::manual_executor& executor) {
    httpserver::resume_signal signal;
    int result = 0;
    httpserver::spawn(executor, wait_for_signal(signal, executor),
        [&](httpserver::task_result<int> value) {
            if (value.has_value()) result = value.value();
        });
    executor.run_pending();
    if (result != 0) return false;
    signal.signal();
    executor.run_pending();
    if (result != 42) return false;

    httpserver::http::request_head head;
    httpserver::exchange exchange(head, nullptr);
    if (!exchange.admit_body(httpserver::body_policy{32}).ok()) return false;
    bool complete = false;
    httpserver::spawn(executor, exchange.body().collect(32),
        [&](httpserver::task_result<httpserver::body_collect> value) {
            complete = value.has_value() && value.value().status.ok() && value.value().data.empty();
        });
    executor.run_pending();
    if (!complete) return false;
    bool writer_rejected = false;
    httpserver::spawn(executor, exchange.writer().finish(),
        [&](httpserver::task_result<httpserver::body_finish> value) {
            writer_rejected = value.has_value() && value.value().status.code() == httpserver::http::outcome_code::invalid_state;
        });
    executor.run_pending();
    return writer_rejected;
}
