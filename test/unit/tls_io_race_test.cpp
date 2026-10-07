/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/worker_pool.hpp>
#include "./tls_io_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hh = httpserver::http;
using std::chrono_literals::operator""s;
LT_BEGIN_SUITE(tls_io_race_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_io_race_suite)
LT_BEGIN_AUTO_TEST(tls_io_race_suite, deadlines_cover_read_write_and_shutdown)
    for (auto kind : {hd::io_op_kind::read, hd::io_op_kind::write, hd::io_op_kind::tls_shutdown}) {
        tls_test::pair p;
        LT_CHECK(p.handshake());
        std::array<std::byte, 100> bytes{};
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        hd::read_operation read(p.owner, 1, bytes, deadline);
        hd::write_operation write(p.owner, 1, bytes, deadline);
        hd::tls_shutdown_operation shutdown(p.owner, 1, deadline);
        auto* op = kind == hd::io_op_kind::read    ? static_cast<hd::op_handle*>(&read)
                   : kind == hd::io_op_kind::write ? static_cast<hd::op_handle*>(&write)
                                                   : static_cast<hd::op_handle*>(&shutdown);
        p.hold_writes = true;
        op->submit(p.client);
        p.drive();
        LT_CHECK(!op->is_terminal());
        p.client_raw.expire_timers(deadline);
        p.ex.run_pending();
        LT_CHECK(op->state()->applied());
        LT_CHECK(op->state()->stored_result().code == hh::outcome_code::timeout);
        LT_CHECK_EQ(p.client_raw.pending_count(), std::size_t{0});
        LT_CHECK(!op->state()->claim_terminal());
    }
LT_END_AUTO_TEST(deadlines_cover_read_write_and_shutdown)
LT_BEGIN_AUTO_TEST(tls_io_race_suite, concurrent_cancellation_and_late_events_do_not_rearm)
    for (bool cancel_first : {false, true}) {
        tls_test::pair p;
        LT_CHECK(p.handshake());
        std::array<std::byte, 64> bytes{};
        hd::read_operation read(p.owner, 1, bytes, std::chrono::steady_clock::now() + 1s);
        hd::write_operation write(p.owner, 1, bytes);
        p.hold_writes = true;
        read.submit(p.client);
        write.submit(p.client);
        p.drive();
        const auto pending = p.client_raw.pending_ops();
        std::stop_source stop;
        read.arm_stop(stop.get_token());
        if (cancel_first) {
            stop.request_stop();
        }
        p.client_raw.close();
        if (!cancel_first) {
            stop.request_stop();
        }
        p.ex.run_pending();
        LT_CHECK(read.state()->applied());
        LT_CHECK(write.state()->applied());
        LT_CHECK(read.state()->stored_result().code == (cancel_first ? hh::outcome_code::cancelled : hh::outcome_code::protocol_error) ||
                 read.state()->stored_result().code == hh::outcome_code::cancelled);
        LT_CHECK(write.state()->stored_result().code == hh::outcome_code::connection_closed || write.state()->stored_result().code == hh::outcome_code::protocol_error);
        for (auto child : pending) {
            LT_CHECK(!p.client_raw.complete(*child, {hh::outcome_code::ok, 1}));
        }
        LT_CHECK_EQ(p.client_raw.pending_count(), std::size_t{0});
        LT_CHECK_EQ(p.ex.pending(), std::size_t{0});
        LT_CHECK(!read.state()->claim_terminal());
        LT_CHECK(!write.state()->claim_terminal());
    }
LT_END_AUTO_TEST(concurrent_cancellation_and_late_events_do_not_rearm)

LT_BEGIN_AUTO_TEST(tls_io_race_suite, cancellation_operation_and_foreign_target)
    tls_test::pair p;
    LT_CHECK(p.handshake());
    std::array<std::byte, 16> bytes{};
    hd::read_operation read(p.owner, 1, bytes), foreign(p.owner, 1, bytes);
    LT_CHECK(p.client.request_cancel(*foreign.state()) == hh::outcome_code::invalid_state);
    read.submit(p.client);
    p.drive();
    hd::cancel_operation cancel(p.owner, 1, read);
    cancel.submit(p.client);
    p.ex.run_pending();
    LT_CHECK(cancel.state()->applied());
    LT_CHECK(cancel.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK(read.state()->stored_result().code == hh::outcome_code::cancelled);
    LT_CHECK_EQ(p.client_raw.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(cancellation_operation_and_foreign_target)
LT_BEGIN_AUTO_TEST(tls_io_race_suite, write_success_and_timeout_event_orders)
    for (bool timeout_first : {false, true}) {
        tls_test::pair p;
        LT_CHECK(p.handshake());
        std::array<std::byte, 16> bytes{};
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        hd::write_operation write(p.owner, 1, bytes, deadline);
        write.submit(p.client);
        p.hold_writes = true;
        p.drive();
        std::shared_ptr<hd::op_state> child;
        for (auto pending : p.client_raw.pending_ops()) {
            if (pending->kind() == hd::io_op_kind::write) {
                child = pending;
            }
        }
        LT_ASSERT(child != nullptr);
        const auto size = std::get<hd::write_payload>(child->payload()).bytes.size();
        if (timeout_first) {
            p.client_raw.expire_timers(deadline);
        }
        p.client_raw.complete(*child, {hh::outcome_code::ok, size});
        if (!timeout_first) {
            p.client_raw.expire_timers(deadline);
        }
        p.ex.run_pending();
        LT_CHECK(write.state()->applied());
        LT_CHECK(write.state()->stored_result().code == (timeout_first ? hh::outcome_code::timeout : hh::outcome_code::ok));
        LT_CHECK(!write.state()->claim_terminal());
        LT_CHECK_EQ(p.client_raw.pending_count(), std::size_t{0});
    }
LT_END_AUTO_TEST(write_success_and_timeout_event_orders)
LT_BEGIN_AUTO_TEST(tls_io_race_suite, dropped_handles_and_destroyed_awaiter_are_safe)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::fake_io_backend raw;
    std::shared_ptr<hd::op_state> state;
    {
        hd::tls_io_backend tls(raw, ex, 1, hd::tls_context::client(), false);
        hd::tls_handshake_operation op(owner, 1);
        state = op.state();
        op.submit(tls);
        ex.run_pending();
        {
            auto awaiter = op.operator co_await();
            awaiter.await_suspend(std::noop_coroutine());
        }
    }
    ex.run_pending();
    LT_CHECK(state->applied());
    LT_CHECK(state->stored_result().code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(raw.pending_count(), std::size_t{0});
    LT_CHECK_EQ(ex.pending(), std::size_t{0});
    LT_CHECK(!state->claim_terminal());
LT_END_AUTO_TEST(dropped_handles_and_destroyed_awaiter_are_safe)

LT_BEGIN_AUTO_TEST(tls_io_race_suite, stalled_session_keeps_pool_and_unrelated_io_live)
    hd::worker_pool ex(4);
    hd::io_connection_owner owner(ex);
    hd::fake_io_backend raw;
    auto tls = std::make_unique<hd::tls_io_backend>(raw, ex, 1, hd::tls_context::client(), false);
    hd::tls_handshake_operation handshake(owner, 1);
    handshake.submit(*tls);
    auto until = [](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        return predicate();
    };
    LT_CHECK(until([&] { return raw.pending_count() != 0; }));
    std::atomic<unsigned> jobs{0};
    for (unsigned i = 0; i < 100; ++i) {
        ex.post([&] { ++jobs; });
    }
    hd::wake_operation wake(owner, 2);
    wake.submit(raw);
    raw.complete(*wake.state(), {});
    LT_CHECK(until([&] { return jobs == 100 && wake.state()->applied(); }));
    LT_CHECK(!handshake.is_terminal());
    std::vector<hd::tls_handshake_operation> overlapping;
    for (unsigned i = 0; i < 100; ++i) {
        overlapping.emplace_back(owner, 1);
        overlapping.back().submit(*tls);
    }
    LT_CHECK(until([&] { return overlapping.back().state()->applied(); }));
    for (auto& op : overlapping) {
        LT_CHECK(op.state()->stored_result().code == hh::outcome_code::invalid_state);
    }
    tls->request_cancel(*handshake.state());
    LT_CHECK(until([&] { return handshake.state()->applied(); }));
    LT_CHECK(handshake.state()->stored_result().code == hh::outcome_code::cancelled);
    tls.reset();
    ex.drain_and_join();
    LT_CHECK_EQ(raw.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(stalled_session_keeps_pool_and_unrelated_io_live)

LT_BEGIN_AUTO_TEST(tls_io_race_suite, stop_token_cancels_every_active_tls_kind)
    for (auto kind : {hd::io_op_kind::tls_handshake, hd::io_op_kind::read, hd::io_op_kind::write, hd::io_op_kind::tls_shutdown}) {
        tls_test::pair p;
        if (kind != hd::io_op_kind::tls_handshake) {
            LT_ASSERT(p.handshake());
        }
        std::array<std::byte, 100> bytes{};
        hd::tls_handshake_operation handshake(p.owner, 1);
        hd::read_operation read(p.owner, 1, bytes);
        hd::write_operation write(p.owner, 1, bytes);
        hd::tls_shutdown_operation shutdown(p.owner, 1);
        hd::op_handle* op = &handshake;
        if (kind == hd::io_op_kind::read) {
            op = &read;
        }
        if (kind == hd::io_op_kind::write) {
            op = &write;
        }
        if (kind == hd::io_op_kind::tls_shutdown) {
            op = &shutdown;
        }
        p.hold_writes = true;
        op->submit(p.client);
        p.ex.run_pending();
        const auto children = p.client_raw.pending_ops();
        std::stop_source stop;
        op->arm_stop(stop.get_token());
        stop.request_stop();
        p.ex.run_pending();
        LT_CHECK(op->state()->applied());
        LT_CHECK(op->state()->stored_result().code == hh::outcome_code::cancelled);
        std::fill(bytes.begin(), bytes.end(), std::byte{0x33});
        for (auto child : children) {
            LT_CHECK(!p.client_raw.complete(*child, {hh::outcome_code::ok, 1}));
        }
        p.ex.run_pending();
        LT_CHECK(std::all_of(bytes.begin(), bytes.end(), [](std::byte b) { return b == std::byte{0x33}; }));
        LT_CHECK_EQ(p.client_raw.pending_count(), std::size_t{0});
        LT_CHECK(!op->state()->claim_terminal());
    }
LT_END_AUTO_TEST(stop_token_cancels_every_active_tls_kind)

LT_BEGIN_AUTO_TEST(tls_io_race_suite, success_cancel_and_close_event_orders)
    for (unsigned stimulus = 0; stimulus < 3; ++stimulus) {
        for (bool success_first : {false, true}) {
            tls_test::pair p;
            LT_CHECK(p.handshake());
            std::array<std::byte, 16> bytes{};
            hd::write_operation write(p.owner, 1, bytes);
            p.hold_writes = true;
            write.submit(p.client);
            p.drive();
            std::shared_ptr<hd::op_state> child;
            for (auto pending : p.client_raw.pending_ops()) {
                if (pending->kind() == hd::io_op_kind::write) {
                    child = pending;
                }
            }
            LT_ASSERT(child != nullptr);
            const auto size = std::get<hd::write_payload>(child->payload()).bytes.size();
            if (success_first) {
                p.client_raw.complete(*child, {hh::outcome_code::ok, size});
                p.ex.run_pending();
            }
            if (stimulus == 0) {
                p.client.request_cancel(*write.state());
            }
            if (stimulus == 1) {
                p.client.close();
            }
            if (stimulus == 2) {
                p.client_raw.close();
            }
            p.ex.run_pending();
            if (!success_first) {
                LT_CHECK(!p.client_raw.complete(*child, {hh::outcome_code::ok, size}));
            }
            LT_CHECK(write.state()->applied());
            const auto expected = success_first ? hh::outcome_code::ok : stimulus == 0 ? hh::outcome_code::cancelled : hh::outcome_code::connection_closed;
            LT_CHECK(write.state()->stored_result().code == expected);
            LT_CHECK(!write.state()->claim_terminal());
            LT_CHECK_EQ(p.client_raw.pending_count(), std::size_t{0});
        }
    }
LT_END_AUTO_TEST(success_cancel_and_close_event_orders)

LT_BEGIN_AUTO_TEST(tls_io_race_suite, closed_adapter_destruction_releases_session_without_posting)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::fake_io_backend raw;
    auto context = hd::tls_context::client();
    std::weak_ptr<hd::tls_context> weak = context;
    auto tls = std::make_unique<hd::tls_io_backend>(raw, ex, 1, context, false);
    context.reset();
    hd::tls_handshake_operation handshake(owner, 1);
    handshake.submit(*tls);
    ex.run_pending();
    tls->close();
    ex.run_pending();
    LT_CHECK(handshake.state()->applied());
    tls.reset();
    LT_CHECK_EQ(ex.pending(), std::size_t{0});
    LT_CHECK(weak.expired());
    LT_CHECK_EQ(raw.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(closed_adapter_destruction_releases_session_without_posting)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
