/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <httpserver/detail/tls_psk_runtime.hpp>
#include <httpserver/detail/tls_psk_attempt.hpp>
#include "./littletest.hpp"
namespace hd = httpserver::detail;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;
namespace {
struct wipe_counts { std::atomic<unsigned> releases{0}, dirty{0}; };
void observe(std::span<const std::byte> bytes, void* argument) noexcept {
    auto& counts = *static_cast<wipe_counts*>(argument);
    const bool dirty = std::any_of(bytes.begin(), bytes.end(), [](auto b) { return b != std::byte{0}; });
    if (dirty) ++counts.dirty;
    ++counts.releases;
}
const std::array key{std::byte{1}, std::byte{2}, std::byte{3}};
}
LT_BEGIN_SUITE(psk_runtime_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(psk_runtime_suite)
LT_BEGIN_AUTO_TEST(psk_runtime_suite, secure_storage_moves_and_wipes_before_release)
    static_assert(!std::is_copy_constructible_v<hd::secure_bytes>);
    wipe_counts counts;
    {
        hd::secure_bytes a(key, observe, &counts);
        const auto* storage = a.bytes().data();
        hd::secure_bytes b(std::move(a));
        LT_CHECK(a.bytes().empty());
        LT_CHECK(b.bytes().data() == storage);
        LT_CHECK_EQ(counts.releases.load(), 0u);
        hd::secure_bytes c(key, observe, &counts);
        c = std::move(b);
        LT_CHECK(b.bytes().empty());
        LT_CHECK(c.bytes().data() == storage);
        LT_CHECK_EQ(counts.releases.load(), 1u);
    }
    LT_CHECK_EQ(counts.releases.load(), 2u);
    LT_CHECK_EQ(counts.dirty.load(), 0u);
LT_END_AUTO_TEST(secure_storage_moves_and_wipes_before_release)
LT_BEGIN_AUTO_TEST(psk_runtime_suite, lookup_executes_off_owner_and_redacts_exceptions)
    hd::tls_psk_runtime runtime;
    const auto owner = std::this_thread::get_id();
    bool separate = false;
    hd::psk_handshake_context context;
    context.deadline = std::chrono::steady_clock::now() + 1s;
    auto result = runtime.lookup([&](auto identity, const auto& ctx) {
        separate = std::this_thread::get_id() != owner;
        if (identity.size() != 3 || ctx.maximum_key_bytes != 512) return hd::psk_lookup_result{};
        return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(key)};
    }, key, context);
    LT_CHECK(separate);
    LT_CHECK(result.status == hd::psk_lookup_status::accepted);
    LT_CHECK_EQ(result.key.bytes().size(), key.size());
    result = runtime.lookup([](auto, const auto&) -> hd::psk_lookup_result { throw std::runtime_error("secret"); }, key, context);
    LT_CHECK(result.status == hd::psk_lookup_status::provider_failure);
    runtime.stop();
    LT_CHECK(runtime.drain(std::chrono::steady_clock::now() + 1s));
LT_END_AUTO_TEST(lookup_executes_off_owner_and_redacts_exceptions)
LT_BEGIN_AUTO_TEST(psk_runtime_suite, held_lookup_times_out_without_releasing_capacity_and_late_key_is_wiped)
    hd::tls_psk_runtime_options options;
    options.lookup_workers = 1;
    options.lookup_queue = 0;
    hd::tls_psk_runtime runtime(options);
    wipe_counts counts;
    std::promise<void> started, release;
    auto released = release.get_future().share();
    hd::psk_handshake_context context;
    context.deadline = std::chrono::steady_clock::now() + 150ms;
    auto waiter = std::async(std::launch::async, [&] {
        return runtime.lookup([&](auto, const auto&) {
            started.set_value();
            released.wait();
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(key, observe, &counts)};
        }, key, context);
    });
    const bool entered = started.get_future().wait_for(1s) == std::future_status::ready;
    const bool timed = waiter.wait_for(1s) == std::future_status::ready;
    context.deadline = std::chrono::steady_clock::now() + 1s;
    auto saturated = runtime.lookup([](auto, const auto&) { return hd::psk_lookup_result{}; }, key, context);
    runtime.stop();
    const bool drained_early = runtime.drain(std::chrono::steady_clock::now() + 5ms);
    release.set_value();
    const auto result = waiter.get();
    const bool drained = runtime.drain(std::chrono::steady_clock::now() + 1s);
    LT_CHECK(entered);
    LT_CHECK(timed);
    LT_CHECK(result.status == hd::psk_lookup_status::timeout);
    LT_CHECK(saturated.status == hd::psk_lookup_status::limit_exceeded);
    LT_CHECK(!drained_early);
    LT_CHECK(drained);
    LT_CHECK_EQ(counts.releases.load(), 1u);
    LT_CHECK_EQ(counts.dirty.load(), 0u);
LT_END_AUTO_TEST(held_lookup_times_out_without_releasing_capacity_and_late_key_is_wiped)
LT_BEGIN_AUTO_TEST(psk_runtime_suite, queue_expiry_cancellation_and_handshake_admission_are_bounded)
    hd::tls_psk_runtime_options options;
    options.handshake_workers = options.lookup_workers = 1;
    options.handshake_queue = options.lookup_queue = 1;
    hd::tls_psk_runtime runtime(options);
    std::promise<void> started, release;
    auto released = release.get_future().share();
    const auto admitted = runtime.submit_handshake([&] { started.set_value(); released.wait(); });
    const bool entered = started.get_future().wait_for(1s) == std::future_status::ready;
    const auto queued = runtime.submit_handshake([] {});
    const auto saturated = runtime.submit_handshake([] {});
    std::stop_source cancellation;
    cancellation.request_stop();
    hd::psk_handshake_context context;
    context.deadline = std::chrono::steady_clock::now() + 1s;
    context.cancellation = cancellation.get_token();
    unsigned calls = 0;
    auto result = runtime.lookup([&](auto, const auto&) { ++calls; return hd::psk_lookup_result{}; }, key, context);
    context.cancellation = {};
    context.deadline = std::chrono::steady_clock::now() - 1ms;
    auto expired = runtime.lookup([&](auto, const auto&) { ++calls; return hd::psk_lookup_result{}; }, key, context);
    release.set_value();
    runtime.stop();
    const bool drained = runtime.drain(std::chrono::steady_clock::now() + 1s);
    LT_CHECK(entered);
    LT_CHECK(admitted == httpserver::http::outcome_code::ok);
    LT_CHECK(queued == httpserver::http::outcome_code::ok);
    LT_CHECK(saturated == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK(result.status == hd::psk_lookup_status::cancelled);
    LT_CHECK(expired.status == hd::psk_lookup_status::timeout);
    LT_CHECK_EQ(calls, 0u);
    LT_CHECK(drained);
    options.lookup_workers = 0;
    LT_CHECK_THROW(hd::tls_psk_runtime{options});
LT_END_AUTO_TEST(queue_expiry_cancellation_and_handshake_admission_are_bounded)
LT_BEGIN_AUTO_TEST(psk_runtime_suite, duplicate_attempt_lookups_coalesce_and_unique_attempt_budget_rejects)
    auto runtime = std::make_shared<hd::tls_psk_runtime>();
    std::atomic<unsigned> calls{0};
    wipe_counts counts;
    hd::tls_psk_config config;
    config.runtime = runtime;
    config.maximum_attempts = 1;
    config.lookup = [&](auto, const auto&) {
        ++calls;
        return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(key, observe, &counts)};
    };
    hd::psk_handshake_context context;
    context.deadline = std::chrono::steady_clock::now() + 1s;
    {
        hd::tls_psk_attempt attempt(config, context);
        auto a = std::async(std::launch::async, [&] { return attempt.lookup(hd::psk_tls_version::tls13, key, 512); });
        auto b = std::async(std::launch::async, [&] { return attempt.lookup(hd::psk_tls_version::tls13, key, 512); });
        auto first = a.get();
        auto second = b.get();
        const std::array other{std::byte{4}};
        const auto exhausted = attempt.lookup(hd::psk_tls_version::tls13, other, 512);
        LT_CHECK(first.status == hd::psk_lookup_status::accepted);
        LT_CHECK(second.status == hd::psk_lookup_status::accepted);
        LT_CHECK(first.key.bytes().data() != second.key.bytes().data());
        LT_CHECK_EQ(calls.load(), 1u);
        LT_CHECK(exhausted.status == hd::psk_lookup_status::limit_exceeded);
        LT_CHECK_EQ(counts.releases.load(), 0u);
    }
    LT_CHECK_EQ(counts.releases.load(), 1u);
    LT_CHECK_EQ(counts.dirty.load(), 0u);
    runtime->stop();
    LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
LT_END_AUTO_TEST(duplicate_attempt_lookups_coalesce_and_unique_attempt_budget_rejects)
LT_BEGIN_AUTO_TEST(psk_runtime_suite, runtime_stop_reaches_application_context_and_destruction_never_joins_held_work)
    auto runtime = std::make_unique<hd::tls_psk_runtime>();
    std::promise<void> started, release, finished;
    auto released = release.get_future().share();
    std::stop_token observed;
    wipe_counts counts;
    hd::psk_handshake_context context;
    context.deadline = std::chrono::steady_clock::now() + 1s;
    auto waiter = std::async(std::launch::async, [&] {
        return runtime->lookup([&](auto, const auto& actual) {
            observed = actual.cancellation;
            started.set_value();
            released.wait();
            finished.set_value();
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(key, observe, &counts)};
        }, key, context);
    });
    const bool entered = started.get_future().wait_for(1s) == std::future_status::ready;
    runtime->stop();
    const auto result = waiter.get();
    const bool cooperative_stop = observed.stop_requested();
    const auto before = std::chrono::steady_clock::now();
    runtime.reset();
    const auto duration = std::chrono::steady_clock::now() - before;
    release.set_value();
    const bool returned = finished.get_future().wait_for(1s) == std::future_status::ready;
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!counts.releases.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    LT_CHECK(entered);
    LT_CHECK(cooperative_stop);
    LT_CHECK(result.status == hd::psk_lookup_status::cancelled);
    LT_CHECK(duration < 100ms);
    LT_CHECK(returned);
    LT_CHECK_EQ(counts.releases.load(), 1u);
    LT_CHECK_EQ(counts.dirty.load(), 0u);
LT_END_AUTO_TEST(runtime_stop_reaches_application_context_and_destruction_never_joins_held_work)
LT_BEGIN_AUTO_TEST(psk_runtime_suite, queued_lookup_expires_without_invoking_application)
    hd::tls_psk_runtime_options options;
    options.lookup_workers = 1;
    options.lookup_queue = 1;
    hd::tls_psk_runtime runtime(options);
    std::promise<void> started, release;
    auto released = release.get_future().share();
    hd::psk_handshake_context context;
    context.deadline = std::chrono::steady_clock::now() + 1s;
    auto running = std::async(std::launch::async, [&] {
        return runtime.lookup([&](auto, const auto&) { started.set_value(); released.wait(); return hd::psk_lookup_result{}; }, key, context);
    });
    const bool entered = started.get_future().wait_for(1s) == std::future_status::ready;
    std::atomic<unsigned> calls{0};
    auto queued_context = context;
    queued_context.deadline = std::chrono::steady_clock::now() + 20ms;
    const auto queued = runtime.lookup([&](auto, const auto&) { ++calls; return hd::psk_lookup_result{}; }, key, queued_context);
    release.set_value();
    running.get();
    runtime.stop();
    const bool drained = runtime.drain(std::chrono::steady_clock::now() + 1s);
    LT_CHECK(entered);
    LT_CHECK(queued.status == hd::psk_lookup_status::timeout);
    LT_CHECK_EQ(calls.load(), 0u);
    LT_CHECK(drained);
LT_END_AUTO_TEST(queued_lookup_expires_without_invoking_application)
LT_BEGIN_AUTO_TEST(psk_runtime_suite, runtime_rejects_nonfinite_budgets_and_returned_keys)
    const auto invalid = [](unsigned failure) {
        hd::tls_psk_runtime_options options;
        if (failure == 0) options.handshake_workers = 0;
        if (failure == 1) options.lookup_workers = 65;
        if (failure == 2) options.handshake_queue = 65537;
        if (failure == 3) options.lookup_queue = 65537;
        if (failure == 4) options.handshake_timeout = 0ms;
        if (failure == 5) options.handshake_timeout = std::chrono::milliseconds::max();
        return options;
    };
    for (unsigned failure = 0; failure < 6; ++failure) LT_CHECK_THROW(hd::tls_psk_runtime{invalid(failure)});
    hd::tls_psk_runtime runtime;
    hd::psk_handshake_context context;
    context.deadline = std::chrono::steady_clock::now() + 1s;
    context.maximum_key_bytes = 2;
    wipe_counts counts;
    const auto oversized = runtime.lookup([&](auto, const auto&) {
        return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(key, observe, &counts)};
    }, key, context);
    const auto empty = runtime.lookup([](auto, const auto&) { return hd::psk_lookup_result{hd::psk_lookup_status::accepted, {}}; }, key, context);
    runtime.stop();
    LT_CHECK(runtime.drain(std::chrono::steady_clock::now() + 1s));
    LT_CHECK(oversized.status == hd::psk_lookup_status::rejected);
    LT_CHECK(oversized.key.bytes().empty());
    LT_CHECK(empty.status == hd::psk_lookup_status::rejected);
    LT_CHECK_EQ(counts.releases.load(), 1u);
    LT_CHECK_EQ(counts.dirty.load(), 0u);
LT_END_AUTO_TEST(runtime_rejects_nonfinite_budgets_and_returned_keys)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
