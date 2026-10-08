/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <iostream>
#include <string>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <utility>
#include <vector>
#include <memory>
#include <cstring>
#include <httpserver/server/server.hpp>
#include <httpserver/response_definition.hpp>
#include <httpserver/server/route_sync.hpp>
#if defined(_WIN32)
#include <httpserver/detail/io_iocp_backend.hpp>
#include <httpserver/detail/io_managed_backend.hpp>
#include "./io_backend_contract.hpp"

namespace httpserver {
namespace detail {
struct io_iocp_test_access {
    static void install_wait(io_iocp_backend& backend, decltype(&::GetQueuedCompletionStatus) wait) {
        std::lock_guard<std::mutex> lock(backend.mu_);
        backend.wait_ = wait;
        backend.notify_locked();
    }
    static LPFN_ACCEPTEX install_accept(io_iocp_backend& backend, std::uint64_t id, LPFN_ACCEPTEX accept) {
        std::lock_guard<std::mutex> lock(backend.mu_);
        return std::exchange(backend.connections_.at(id)->accept, accept);
    }
    static std::size_t outstanding(io_iocp_backend& backend) {
        std::lock_guard<std::mutex> lock(backend.mu_);
        return backend.outstanding_.size();
    }
    static bool posted(io_iocp_backend& backend, op_state* op) {
        std::lock_guard<std::mutex> lock(backend.mu_);
        return backend.posted_.contains(op);
    }
    static pollsys::native_socket_t candidate(io_iocp_backend& backend, OVERLAPPED* address) {
        std::lock_guard<std::mutex> lock(backend.mu_);
        return backend.outstanding_.at(address)->candidate;
    }
    static HANDLE port(io_iocp_backend& backend) { return backend.port_; }
    static void dispatch(io_iocp_backend& backend, OVERLAPPED* address, ULONG_PTR token, DWORD bytes, DWORD error) {
        io_iocp_backend::completions done;
        {
            std::lock_guard<std::mutex> lock(backend.mu_);
            backend.packet_locked(address, token, bytes, error, done);
        }
        backend.deliver(done);
    }
};
}  // namespace detail
}  // namespace httpserver

namespace {
using iocp_rig = io_contract::socket_rig<hd::io_iocp_backend>;
struct iocp_fixture final : io_contract::backend_fixture {
    iocp_fixture() {
        pair = io_loopback::pair::make();
        instance.adopt_connection(1, pair.detach_local());
        listener = io_loopback::listener::open();
        instance.adopt_listener(2, listener.socket());
        listener.detach();
    }
    hd::io_backend& backend() override { return instance; }
    std::uint64_t accept_connection() const override { return 2; }
    void pump() override { }
    void deliver_read(hd::op_state&, std::string_view bytes) override {
        io_loopback::write_all(pair.peer(), bytes.data(), bytes.size());
    }
    void deliver_write(hd::op_state&, std::size_t) override { }
    void fire_wake() override { instance.wake(); }
    std::size_t close_backend() override { return instance.close(); }
    std::size_t pending_count() override { return instance.pending_count(); }
    hd::io_iocp_backend instance;
    io_loopback::pair pair;
    io_loopback::listener listener;
};

using native_access = hd::io_iocp_test_access;

bool until(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + io_contract::kWaitBudget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::yield();
    }
    return false;
}

bool wait_applied(io_contract::contract_rig& rig, const std::shared_ptr<hd::op_state>& op) {
    return until([&] { rig.ex.run_pending(); return op->applied(); });
}

// The gate stops AFTER a real native packet was dequeued, BEFORE the backend
// interprets it. Cancellation/release now race a known kernel completion.
struct packet_gate {
    explicit packet_gate(hd::io_iocp_backend& backend) : backend(backend) {
        active = this;
        native_access::install_wait(backend, &wait);
    }
    ~packet_gate() {
        release();
        backend.close();
        active = nullptr;
    }
    bool captured() {
        std::unique_lock<std::mutex> lock(mu);
        return cv.wait_for(lock, io_contract::kWaitBudget, [&] { return address != nullptr; });
    }
    void release() {
        std::lock_guard<std::mutex> lock(mu);
        released = true;
        cv.notify_all();
    }
    void fail(DWORD error) {
        std::lock_guard<std::mutex> lock(mu);
        injected_error = error;
        released = true;
        cv.notify_all();
    }
    static BOOL WINAPI wait(HANDLE port, LPDWORD bytes, PULONG_PTR token, LPOVERLAPPED* address, DWORD timeout) {
        BOOL result = ::GetQueuedCompletionStatus(port, bytes, token, address, timeout);
        DWORD error = result ? ERROR_SUCCESS : ::GetLastError();
        if (*address != nullptr) {
            auto& gate = *active;
            std::unique_lock<std::mutex> lock(gate.mu);
            if (!gate.released) {
                gate.address = *address;
                gate.token = *token;
                gate.bytes = *bytes;
                gate.error = error;
                gate.cv.notify_all();
                gate.cv.wait(lock, [&] { return gate.released; });
                if (gate.injected_error) {
                    error = *gate.injected_error;
                    result = FALSE;
                }
            }
        }
        ::SetLastError(error);
        return result;
    }
    hd::io_iocp_backend& backend;
    std::mutex mu;
    std::condition_variable cv;
    OVERLAPPED* address = nullptr;
    ULONG_PTR token = 0;
    DWORD bytes = 0;
    DWORD error = 0;
    bool released = false;
    std::optional<DWORD> injected_error;
    static inline packet_gate* active = nullptr;
};

// Inject only the native initiation outcome; all request ownership and retries
// still execute in the real backend. Subsequent calls use real AcceptEx.
struct accept_gate {
    accept_gate(hd::io_iocp_backend& backend, std::uint64_t id, int error) : error(error) {
        active = this;
        original = native_access::install_accept(backend, id, &accept);
    }
    ~accept_gate() { active = nullptr; }
    static BOOL PASCAL accept(SOCKET listener, SOCKET candidate, PVOID buffer, DWORD bytes,
                             DWORD local, DWORD remote, LPDWORD received, LPOVERLAPPED address) {
        auto& gate = *active;
        if (gate.calls.fetch_add(1) == 0 && gate.error != 0) {
            ::WSASetLastError(gate.error);
            return FALSE;
        }
        return gate.original(listener, candidate, buffer, bytes, local, remote, received, address);
    }
    int error;
    LPFN_ACCEPTEX original = nullptr;
    std::atomic<int> calls{0};
    static inline accept_gate* active = nullptr;
};

}  // namespace

LT_BEGIN_SUITE(iocp_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(iocp_suite)

LT_BEGIN_AUTO_TEST(iocp_suite, managed_windows_selects_iocp)
    auto backend = hd::make_socket_backend(httpserver::server::loop_mode::managed);
    LT_CHECK(dynamic_cast<hd::io_iocp_backend*>(backend.get()) != nullptr);
    auto external = hd::make_socket_backend(httpserver::server::loop_mode::external);
    LT_CHECK(dynamic_cast<hd::io_poll_backend*>(external.get()) != nullptr);
LT_END_AUTO_TEST(managed_windows_selects_iocp)

LT_BEGIN_AUTO_TEST(iocp_suite, read_delivers_bytes_exactly_once)
    iocp_fixture fx;
    io_contract::read_delivers_bytes_exactly_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(read_delivers_bytes_exactly_once)

LT_BEGIN_AUTO_TEST(iocp_suite, write_completes_with_transferred)
    iocp_fixture fx;
    io_contract::write_completes_with_transferred(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(write_completes_with_transferred)

LT_BEGIN_AUTO_TEST(iocp_suite, timer_fires_at_deadline_not_before)
    iocp_fixture fx;
    io_contract::timer_fires_at_deadline_not_before(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(timer_fires_at_deadline_not_before)

LT_BEGIN_AUTO_TEST(iocp_suite, two_timers_earliest_first_sequence_tie)
    iocp_fixture fx;
    io_contract::two_timers_earliest_first_sequence_tie(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(two_timers_earliest_first_sequence_tie)

LT_BEGIN_AUTO_TEST(iocp_suite, wake_completes_all_wakes_once)
    iocp_fixture fx;
    io_contract::wake_completes_all_wakes_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(wake_completes_all_wakes_once)

LT_BEGIN_AUTO_TEST(iocp_suite, cancel_pending_target)
    iocp_fixture fx;
    io_contract::cancel_pending_target(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(cancel_pending_target)

LT_BEGIN_AUTO_TEST(iocp_suite, cancel_terminal_target_reports_invalid_state)
    iocp_fixture fx;
    io_contract::cancel_terminal_target_reports_invalid_state(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(cancel_terminal_target_reports_invalid_state)

LT_BEGIN_AUTO_TEST(iocp_suite, close_sweeps_every_pending_once)
    iocp_fixture fx;
    io_contract::close_sweeps_every_pending_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(close_sweeps_every_pending_once)

LT_BEGIN_AUTO_TEST(iocp_suite, submit_after_close_connection_closed)
    iocp_fixture fx;
    io_contract::submit_after_close_connection_closed(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(submit_after_close_connection_closed)

LT_BEGIN_AUTO_TEST(iocp_suite, late_request_cancel_reports_invalid_state)
    iocp_fixture fx;
    io_contract::late_request_cancel_reports_invalid_state(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(late_request_cancel_reports_invalid_state)

LT_BEGIN_AUTO_TEST(iocp_suite, n_awaiter_resume_exactly_once)
    iocp_fixture fx;
    io_contract::n_awaiter_resume_exactly_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(n_awaiter_resume_exactly_once)

LT_BEGIN_AUTO_TEST(iocp_suite, cancel_vs_stimulus_race)
    iocp_fixture fx;
    io_contract::cancel_vs_stimulus_race(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(cancel_vs_stimulus_race)

LT_BEGIN_AUTO_TEST(iocp_suite, accept_round_trip)
    iocp_rig rig;
    io_contract::accept_round_trip(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(accept_round_trip)

LT_BEGIN_AUTO_TEST(iocp_suite, interleaved_native_listeners_keep_distinct_ids)
    iocp_rig rig;
    io_contract::interleaved_native_listeners_keep_distinct_ids(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(interleaved_native_listeners_keep_distinct_ids)

LT_BEGIN_AUTO_TEST(iocp_suite, http1_round_trip)
    iocp_rig rig;
    io_contract::http1_round_trip(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(http1_round_trip)

LT_BEGIN_AUTO_TEST(iocp_suite, partial_read)
    iocp_rig rig;
    io_contract::partial_read(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(partial_read)

LT_BEGIN_AUTO_TEST(iocp_suite, read_hangup)
    iocp_rig rig;
    io_contract::read_hangup(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(read_hangup)

LT_BEGIN_AUTO_TEST(iocp_suite, write_hangup)
    iocp_rig rig;
    io_contract::write_hangup(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(write_hangup)

LT_BEGIN_AUTO_TEST(iocp_suite, idle_iterations_stay_bounded)
    iocp_rig rig;
    io_contract::idle_iterations_stay_bounded(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(idle_iterations_stay_bounded)

LT_BEGIN_AUTO_TEST(iocp_suite, slow_reader_no_busy_loop)
    iocp_rig rig;
    io_contract::slow_reader_no_busy_loop(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(slow_reader_no_busy_loop)

LT_BEGIN_AUTO_TEST(iocp_suite, synchronous_candidate_abort_retries_same_logical_accept)
    for (const int error : {WSAECONNRESET, WSAECONNABORTED}) {
        iocp_rig rig;
        auto listener = rig.adopt_listener(2);
        accept_gate posting(rig.backend, 2, error);
        hd::accept_operation op(rig.rig.owner, 2);
        op.submit(rig.backend);
        LT_ASSERT(until([&] { return posting.calls.load() >= 2; }));
        LT_CHECK(!op.is_terminal());
        LT_CHECK(rig.backend.native_handle(2) != pollsys::k_invalid_socket);
        auto client = io_loopback::connect_to(listener.port());
        LT_CHECK(wait_applied(rig.rig, op.state()));
        LT_CHECK(op.state()->stored_result().code == hh::outcome_code::ok);
        LT_CHECK(op.state()->stored_result().accepted_id != 0);
        LT_CHECK(rig.backend.request_cancel(*op.state()) == hh::outcome_code::invalid_state);
        rig.backend.close();
        pollsys::close_socket(client);
    }
LT_END_AUTO_TEST(synchronous_candidate_abort_retries_same_logical_accept)

LT_BEGIN_AUTO_TEST(iocp_suite, failed_candidate_packet_retries_same_logical_accept)
    for (const DWORD error : {DWORD{WSAECONNRESET}, DWORD{WSAECONNABORTED},
                              DWORD{ERROR_NETNAME_DELETED}, DWORD{ERROR_CONNECTION_ABORTED}}) {
        iocp_rig rig;
        auto listener = rig.adopt_listener(2);
        accept_gate posting(rig.backend, 2, 0);
        packet_gate packet(rig.backend);
        hd::accept_operation op(rig.rig.owner, 2);
        op.submit(rig.backend);
        auto aborted = io_loopback::connect_to(listener.port());
        LT_ASSERT(packet.captured());
        linger reset{1, 0};
        LT_CHECK_EQ(::setsockopt(aborted, SOL_SOCKET, SO_LINGER,
            reinterpret_cast<const char*>(&reset), sizeof(reset)), 0);
        pollsys::close_socket(aborted);
        // Force each native error mapping after aborting the real peer. The
        // packet has been dequeued; request lifetime remains backend-owned.
        packet.fail(error);
        LT_ASSERT(until([&] { return posting.calls.load() >= 2; }));
        LT_CHECK(!op.is_terminal());
        LT_CHECK(rig.backend.native_handle(2) != pollsys::k_invalid_socket);
        LT_CHECK_EQ(native_access::outstanding(rig.backend), std::size_t{1});
        auto healthy = io_loopback::connect_to(listener.port());
        LT_CHECK(wait_applied(rig.rig, op.state()));
        LT_CHECK(op.state()->stored_result().code == hh::outcome_code::ok);
        LT_CHECK(op.state()->stored_result().accepted_id != 0);
        rig.backend.close();
        pollsys::close_socket(healthy);
    }
LT_END_AUTO_TEST(failed_candidate_packet_retries_same_logical_accept)

LT_BEGIN_AUTO_TEST(iocp_suite, fatal_accept_initiation_error_retires_listener)
    iocp_rig rig;
    auto listener = rig.adopt_listener(2);
    accept_gate posting(rig.backend, 2, WSAENOTSOCK);
    hd::accept_operation op(rig.rig.owner, 2);
    op.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, op.state()));
    LT_CHECK(op.state()->stored_result().code == hh::outcome_code::connection_closed);
    LT_CHECK(rig.backend.native_handle(2) == pollsys::k_invalid_socket);
    LT_CHECK_EQ(posting.calls.load(), 1);
    rig.backend.close();
LT_END_AUTO_TEST(fatal_accept_initiation_error_retires_listener)

LT_BEGIN_AUTO_TEST(iocp_suite, fatal_accept_packet_error_retires_listener)
    iocp_rig rig;
    auto listener = rig.adopt_listener(2);
    packet_gate packet(rig.backend);
    hd::accept_operation op(rig.rig.owner, 2);
    op.submit(rig.backend);
    auto client = io_loopback::connect_to(listener.port());
    LT_ASSERT(packet.captured());
    packet.fail(ERROR_INVALID_HANDLE);
    LT_CHECK(wait_applied(rig.rig, op.state()));
    LT_CHECK(op.state()->stored_result().code == hh::outcome_code::connection_closed);
    LT_CHECK(rig.backend.native_handle(2) == pollsys::k_invalid_socket);
    LT_CHECK_EQ(native_access::outstanding(rig.backend), std::size_t{0});
    pollsys::close_socket(client);
LT_END_AUTO_TEST(fatal_accept_packet_error_retires_listener)

LT_BEGIN_AUTO_TEST(iocp_suite, success_claim_before_cancel_delivers_exactly_once)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    packet_gate packet(rig.backend);
    hd::read_operation op(rig.rig.owner, 1, rig.rig.buffer);
    op.submit(rig.backend);
    const auto state = op.state();
    io_contract::probe result;
    std::vector<httpserver::task<void>> tasks;
    io_contract::launch_probe(rig.rig, std::move(op), &result, tasks);
    rig.rig.ex.run_pending();
    io_loopback::write_all(pair.peer(), "x", 1);
    LT_ASSERT(packet.captured());
    packet.release();
    LT_CHECK(io_contract::wait_terminal(rig.rig, result));
    LT_CHECK(rig.backend.request_cancel(*state) == hh::outcome_code::invalid_state);
    LT_CHECK(result.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(result.delivered.load(), 1);
LT_END_AUTO_TEST(success_claim_before_cancel_delivers_exactly_once)

LT_BEGIN_AUTO_TEST(iocp_suite, cancel_claim_before_packet_delivers_exactly_once)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    packet_gate packet(rig.backend);
    hd::read_operation op(rig.rig.owner, 1, rig.rig.buffer);
    op.submit(rig.backend);
    const auto state = op.state();
    io_contract::probe result;
    std::vector<httpserver::task<void>> tasks;
    io_contract::launch_probe(rig.rig, std::move(op), &result, tasks);
    rig.rig.ex.run_pending();
    io_loopback::write_all(pair.peer(), "x", 1);
    LT_ASSERT(packet.captured());
    LT_CHECK(rig.backend.request_cancel(*state) == hh::outcome_code::ok);
    LT_CHECK(io_contract::wait_terminal(rig.rig, result));
    packet.release();
    LT_CHECK(until([&] { return native_access::outstanding(rig.backend) == 0; }));
    rig.rig.ex.run_pending();
    LT_CHECK(result.observed.code == hh::outcome_code::cancelled);
    LT_CHECK_EQ(result.delivered.load(), 1);
LT_END_AUTO_TEST(cancel_claim_before_packet_delivers_exactly_once)

LT_BEGIN_AUTO_TEST(iocp_suite, queued_success_cancel_not_found_retains_storage_until_packet)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    packet_gate gate(rig.backend);
    auto destination = std::make_unique<std::byte[]>(16);
    hd::read_operation op(rig.rig.owner, 1, std::span<std::byte>(destination.get(), 16));
    op.submit(rig.backend);
    LT_ASSERT(until([&] { return native_access::posted(rig.backend, op.state().get()); }));
    io_loopback::write_all(pair.peer(), "late", 4);
    LT_ASSERT(gate.captured());
    LT_CHECK_EQ(gate.error, DWORD{ERROR_SUCCESS});
    LT_CHECK_EQ(gate.bytes, DWORD{4});
    // The request is known to have completed in the kernel. ERROR_NOT_FOUND
    // from a targeted native cancellation still leaves its storage owned.
    LT_CHECK(!::CancelIoEx(reinterpret_cast<HANDLE>(rig.backend.native_handle(1)), gate.address));
    LT_CHECK_EQ(::GetLastError(), DWORD{ERROR_NOT_FOUND});
    LT_CHECK(rig.backend.request_cancel(*op.state()) == hh::outcome_code::ok);
    LT_CHECK(wait_applied(rig.rig, op.state()));
    LT_CHECK(op.state()->stored_result().code == hh::outcome_code::cancelled);
    destination.reset();
    LT_CHECK_EQ(native_access::outstanding(rig.backend), std::size_t{1});
    gate.release();
    LT_CHECK(until([&] { return native_access::outstanding(rig.backend) == 0; }));
    LT_CHECK(rig.backend.request_cancel(*op.state()) == hh::outcome_code::invalid_state);
LT_END_AUTO_TEST(queued_success_cancel_not_found_retains_storage_until_packet)

LT_BEGIN_AUTO_TEST(iocp_suite, failed_packet_cancellation_preserves_socket_and_next_read)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    packet_gate gate(rig.backend);
    auto destination = std::make_unique<std::byte[]>(16);
    hd::read_operation cancelled(rig.rig.owner, 1, std::span<std::byte>(destination.get(), 16));
    cancelled.submit(rig.backend);
    LT_ASSERT(until([&] { return native_access::posted(rig.backend, cancelled.state().get()); }));
    LT_CHECK(rig.backend.request_cancel(*cancelled.state()) == hh::outcome_code::ok);
    LT_ASSERT(gate.captured());
    LT_CHECK_EQ(gate.error, DWORD{ERROR_OPERATION_ABORTED});
    LT_CHECK(wait_applied(rig.rig, cancelled.state()));
    destination.reset();
    LT_CHECK_EQ(native_access::outstanding(rig.backend), std::size_t{1});
    hd::write_operation reply(rig.rig.owner, 1, io_contract::as_bytes("reply"));
    reply.submit(rig.backend);
    gate.release();
    LT_CHECK(wait_applied(rig.rig, reply.state()));
    LT_CHECK(reply.state()->stored_result().code == hh::outcome_code::ok);
    std::byte wire[5]{};
    LT_CHECK(io_loopback::read_exact(pair.peer(), wire, 5));
    LT_CHECK_EQ(std::memcmp(wire, "reply", 5), 0);
    hd::read_operation next(rig.rig.owner, 1, rig.rig.buffer);
    next.submit(rig.backend);
    io_loopback::write_all(pair.peer(), "next", 4);
    LT_CHECK(wait_applied(rig.rig, next.state()));
    LT_CHECK_EQ(std::memcmp(rig.rig.buffer, "next", 4), 0);
    LT_CHECK(next.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK(rig.backend.request_cancel(*next.state()) == hh::outcome_code::invalid_state);
LT_END_AUTO_TEST(failed_packet_cancellation_preserves_socket_and_next_read)

LT_BEGIN_AUTO_TEST(iocp_suite, stale_packet_after_release_and_readoption_cannot_touch_new_buffer)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    packet_gate gate(rig.backend);
    auto destination = std::make_unique<std::byte[]>(16);
    hd::read_operation old(rig.rig.owner, 1, std::span<std::byte>(destination.get(), 16));
    old.submit(rig.backend);
    io_loopback::write_all(pair.peer(), "old", 3);
    LT_ASSERT(gate.captured());
    rig.backend.release_connection(1);
    LT_CHECK(wait_applied(rig.rig, old.state()));
    destination.reset();
    auto replacement = rig.adopt_pair(1);
    std::byte buffer[4]{};
    hd::read_operation current(rig.rig.owner, 1, buffer);
    current.submit(rig.backend);
    // Unknown addresses and wrong tokens are rejected before dereference.
    native_access::dispatch(rig.backend, reinterpret_cast<OVERLAPPED*>(std::uintptr_t{1}), gate.token, 0, 0);
    native_access::dispatch(rig.backend, gate.address, gate.token + 1, gate.bytes, gate.error);
    LT_CHECK_EQ(native_access::outstanding(rig.backend), std::size_t{1});
    LT_CHECK(!current.is_terminal());
    gate.release();
    io_loopback::write_all(replacement.peer(), "new!", 4);
    LT_CHECK(wait_applied(rig.rig, current.state()));
    LT_CHECK_EQ(std::memcmp(buffer, "new!", 4), 0);
    LT_CHECK_EQ(current.state()->stored_result().transferred, std::size_t{4});
    LT_CHECK(old.state()->stored_result().code == hh::outcome_code::connection_closed);
LT_END_AUTO_TEST(stale_packet_after_release_and_readoption_cannot_touch_new_buffer)

LT_BEGIN_AUTO_TEST(iocp_suite, late_successful_accept_cancellation_closes_candidate_once)
    iocp_rig rig;
    auto listener = rig.adopt_listener(2);
    packet_gate gate(rig.backend);
    hd::accept_operation op(rig.rig.owner, 2);
    op.submit(rig.backend);
    LT_ASSERT(until([&] { return native_access::posted(rig.backend, op.state().get()); }));
    auto client = io_loopback::connect_to(listener.port());
    LT_CHECK(client != pollsys::k_invalid_socket);
    LT_ASSERT(gate.captured());
    const auto candidate = native_access::candidate(rig.backend, gate.address);
    LT_CHECK(candidate != pollsys::k_invalid_socket);
    LT_CHECK(rig.backend.request_cancel(*op.state()) == hh::outcome_code::ok);
    LT_CHECK(wait_applied(rig.rig, op.state()));
    LT_CHECK_EQ(op.state()->stored_result().accepted_id, std::uint64_t{0});
    gate.release();
    LT_CHECK(until([&] { return native_access::outstanding(rig.backend) == 0; }));
    // close() joins past registry removal and request destruction.
    rig.backend.close();
    int type = 0;
    int size = sizeof(type);
    LT_CHECK_EQ(::getsockopt(candidate, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size), SOCKET_ERROR);
    LT_CHECK_EQ(::WSAGetLastError(), WSAENOTSOCK);
    pollsys::close_socket(client);
LT_END_AUTO_TEST(late_successful_accept_cancellation_closes_candidate_once)

LT_BEGIN_AUTO_TEST(iocp_suite, pending_accept_cancel_and_concurrent_close_drain_every_native_request)
    iocp_rig rig;
    auto listener = rig.adopt_listener(2);
    auto pair = rig.adopt_pair(1);
    hd::accept_operation accept(rig.rig.owner, 2);
    hd::read_operation read(rig.rig.owner, 1, rig.rig.buffer);
    accept.submit(rig.backend);
    read.submit(rig.backend);
    LT_ASSERT(until([&] { return native_access::outstanding(rig.backend) == 2; }));
    LT_CHECK(rig.backend.request_cancel(*accept.state()) == hh::outcome_code::ok);
    LT_CHECK(wait_applied(rig.rig, accept.state()));
    LT_CHECK(accept.state()->stored_result().code == hh::outcome_code::cancelled);
    std::atomic<std::size_t> swept{0};
    std::thread a([&] { swept.fetch_add(rig.backend.close()); });
    std::thread b([&] { swept.fetch_add(rig.backend.close()); });
    a.join();
    b.join();
    LT_CHECK_EQ(swept.load(), std::size_t{1});
    LT_CHECK_EQ(native_access::outstanding(rig.backend), std::size_t{0});
    LT_CHECK_EQ(rig.backend.pending_count(), std::size_t{0});
    LT_CHECK(wait_applied(rig.rig, read.state()));
    LT_CHECK(read.state()->stored_result().code == hh::outcome_code::connection_closed);
LT_END_AUTO_TEST(pending_accept_cancel_and_concurrent_close_drain_every_native_request)

LT_BEGIN_AUTO_TEST(iocp_suite, out_of_order_native_packets_across_owners_preserve_each_result)
    iocp_rig rig;
    io_contract::contract_rig other_owner;
    auto first = rig.adopt_pair(1);
    auto second = rig.adopt_pair(2);
    hd::read_operation a(rig.rig.owner, 1, rig.rig.buffer);
    hd::read_operation b(other_owner.owner, 2, other_owner.buffer);
    a.submit(rig.backend);
    b.submit(rig.backend);
    LT_ASSERT(until([&] { return native_access::outstanding(rig.backend) == 2; }));
    packet_gate gate(rig.backend);
    io_loopback::write_all(first.peer(), "first", 5);
    LT_ASSERT(gate.captured());
    io_loopback::write_all(second.peer(), "second", 6);
    DWORD bytes = 0;
    ULONG_PTR token = 0;
    OVERLAPPED* address = nullptr;
    const auto deadline = std::chrono::steady_clock::now() + io_contract::kWaitBudget;
    while (address == nullptr && std::chrono::steady_clock::now() < deadline) {
        ::GetQueuedCompletionStatus(native_access::port(rig.backend), &bytes, &token, &address, 100);
    }
    LT_ASSERT(address != nullptr);
    native_access::dispatch(rig.backend, address, token, bytes, ERROR_SUCCESS);
    LT_CHECK(wait_applied(other_owner, b.state()));
    LT_CHECK(!a.is_terminal());
    LT_CHECK_EQ(std::memcmp(other_owner.buffer, "second", 6), 0);
    gate.release();
    LT_CHECK(wait_applied(rig.rig, a.state()));
    LT_CHECK_EQ(std::memcmp(rig.rig.buffer, "first", 5), 0);
    LT_CHECK_EQ(a.state()->stored_result().transferred, std::size_t{5});
    LT_CHECK_EQ(b.state()->stored_result().transferred, std::size_t{6});
    rig.backend.close();
LT_END_AUTO_TEST(out_of_order_native_packets_across_owners_preserve_each_result)

LT_BEGIN_AUTO_TEST(iocp_suite, empty_spans_complete_ok_without_eof)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    hd::read_operation read(rig.rig.owner, 1, {});
    hd::write_operation write(rig.rig.owner, 1, {});
    read.submit(rig.backend);
    write.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, read.state()));
    LT_CHECK(wait_applied(rig.rig, write.state()));
    LT_CHECK(read.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK(write.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK_EQ(native_access::outstanding(rig.backend), std::size_t{0});
    LT_CHECK(rig.backend.native_handle(1) != pollsys::k_invalid_socket);
LT_END_AUTO_TEST(empty_spans_complete_ok_without_eof)

LT_BEGIN_AUTO_TEST(iocp_suite, write_failure_preserves_unread_inbound_bytes)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    io_loopback::write_all(pair.peer(), "input", 5);
    LT_CHECK_EQ(::shutdown(rig.backend.native_handle(1), SD_SEND), 0);
    hd::write_operation write(rig.rig.owner, 1, io_contract::as_bytes("reply"));
    write.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, write.state()));
    LT_CHECK(write.state()->stored_result().code == hh::outcome_code::connection_closed);
    hd::read_operation read(rig.rig.owner, 1, rig.rig.buffer);
    read.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, read.state()));
    LT_CHECK(read.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK_EQ(std::memcmp(rig.rig.buffer, "input", 5), 0);
LT_END_AUTO_TEST(write_failure_preserves_unread_inbound_bytes)

LT_BEGIN_AUTO_TEST(iocp_suite, managed_http_loopback_uses_completion_backend)
    namespace srv = httpserver::server;
    srv::server_options options;
    srv::listener_options listener;
    listener.address = "127.0.0.1";
    listener.port = 0;
    options.add_listener(listener);
    options.loop() = srv::loop_mode::managed;
    srv::native_server server(std::move(options));
    LT_ASSERT(server.route_sync(hh::method::known(hh::method_id::get), "/iocp",
        [](const hh::request_head&, std::span<const std::byte>) {
            srv::sync_response response;
            response.status = hh::status::from_code(200);
            const auto body = io_contract::as_bytes("iocp-reply");
            response.body.assign(body.begin(), body.end());
            return response;
        }, 1024).ok());
    LT_ASSERT(server.listen().ok());
    auto client = io_loopback::connect_to(server.get_bound_port(0));
    LT_ASSERT(client != pollsys::k_invalid_socket);
    const std::string request = "GET /iocp HTTP/1.0\r\nHost: localhost\r\n\r\n";
    io_loopback::write_all(client, request.data(), request.size());
    pollsys::set_nonblocking(client, true);
    std::string wire;
    std::byte buffer[1024]{};
    LT_CHECK(until([&] {
        const auto read = pollsys::read_some(client, buffer, sizeof(buffer));
        wire.append(reinterpret_cast<const char*>(buffer), read.transferred);
        return wire.find("iocp-reply") != std::string::npos;
    }));
    LT_CHECK(wire.find("200 OK") != std::string::npos);
    LT_CHECK(wire.find("iocp-reply") != std::string::npos);
    pollsys::close_socket(client);
    server.stop();
    LT_CHECK(!server.is_running());
LT_END_AUTO_TEST(managed_http_loopback_uses_completion_backend)

LT_BEGIN_AUTO_TEST(iocp_suite, cancel_unposted_operation_never_borrows_destroyed_storage)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    packet_gate gate(rig.backend);
    hd::read_operation first(rig.rig.owner, 1, rig.rig.buffer);
    first.submit(rig.backend);
    io_loopback::write_all(pair.peer(), "first", 5);
    LT_ASSERT(gate.captured());
    auto destination = std::make_unique<std::byte[]>(16);
    hd::read_operation unposted(rig.rig.owner, 1, std::span<std::byte>(destination.get(), 16));
    unposted.submit(rig.backend);
    LT_CHECK(!native_access::posted(rig.backend, unposted.state().get()));
    LT_CHECK(rig.backend.request_cancel(*unposted.state()) == hh::outcome_code::ok);
    LT_CHECK(wait_applied(rig.rig, unposted.state()));
    destination.reset();
    gate.release();
    LT_CHECK(wait_applied(rig.rig, first.state()));
    rig.backend.close();
    LT_CHECK_EQ(native_access::outstanding(rig.backend), std::size_t{0});
    LT_CHECK(unposted.state()->stored_result().code == hh::outcome_code::cancelled);
LT_END_AUTO_TEST(cancel_unposted_operation_never_borrows_destroyed_storage)

LT_BEGIN_AUTO_TEST(iocp_suite, driver_delivery_can_close_reentrantly_without_self_join)
    class closing_executor final : public httpserver::executor {
     public:
        void post(handler work) override {
            work();
            backend->close();
            closed.store(true, std::memory_order_release);
        }
        bool is_current() const noexcept override { return true; }
        hd::io_iocp_backend* backend = nullptr;
        std::atomic<bool> closed{false};
    } ex;
    hd::io_connection_owner owner(ex);
    hd::io_iocp_backend backend;
    ex.backend = &backend;
    auto pair = io_loopback::pair::make();
    backend.adopt_connection(1, pair.detach_local());
    std::byte destination[4]{};
    hd::read_operation op(owner, 1, destination);
    op.submit(backend);
    io_loopback::write_all(pair.peer(), "done", 4);
    LT_CHECK(until([&] { return ex.closed.load(std::memory_order_acquire); }));
    backend.close();
    LT_CHECK(op.state()->applied());
    LT_CHECK(op.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK_EQ(std::memcmp(destination, "done", 4), 0);
    LT_CHECK_EQ(native_access::outstanding(backend), std::size_t{0});
LT_END_AUTO_TEST(driver_delivery_can_close_reentrantly_without_self_join)

LT_BEGIN_AUTO_TEST(iocp_suite, opposite_direction_completes_before_earlier_pending_receive)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    packet_gate gate(rig.backend);
    hd::read_operation read(rig.rig.owner, 1, rig.rig.buffer);
    hd::write_operation write(rig.rig.owner, 1, io_contract::as_bytes("reply"));
    read.submit(rig.backend);
    write.submit(rig.backend);
    LT_ASSERT(gate.captured());
    LT_CHECK(native_access::posted(rig.backend, read.state().get()));
    LT_CHECK(!read.is_terminal());
    gate.release();
    LT_CHECK(wait_applied(rig.rig, write.state()));
    LT_CHECK(write.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK(!read.is_terminal());
    io_loopback::write_all(pair.peer(), "input", 5);
    LT_CHECK(wait_applied(rig.rig, read.state()));
    LT_CHECK_EQ(std::memcmp(rig.rig.buffer, "input", 5), 0);
    LT_CHECK_EQ(write.state()->stored_result().transferred, std::size_t{5});
LT_END_AUTO_TEST(opposite_direction_completes_before_earlier_pending_receive)

LT_BEGIN_AUTO_TEST(iocp_suite, receive_eof_retires_registration_and_sweeps_connection_controls)
    iocp_rig rig;
    auto pair = rig.adopt_pair(1);
    hd::wake_operation wake(rig.rig.owner, 1);
    wake.submit(rig.backend);
    pair.close_peer();
    hd::read_operation read(rig.rig.owner, 1, rig.rig.buffer);
    read.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, read.state()));
    LT_CHECK(wait_applied(rig.rig, wake.state()));
    LT_CHECK(read.state()->stored_result().code == hh::outcome_code::connection_closed);
    LT_CHECK(wake.state()->stored_result().code == hh::outcome_code::connection_closed);
    LT_CHECK(rig.backend.native_handle(1) == pollsys::k_invalid_socket);
    hd::wake_operation late(rig.rig.owner, 1);
    late.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, late.state()));
    LT_CHECK(late.state()->stored_result().code == hh::outcome_code::connection_closed);
LT_END_AUTO_TEST(receive_eof_retires_registration_and_sweeps_connection_controls)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
#else
int main() {
    std::cout << "SKIP: Windows IOCP backend requires native Windows\n";
    return 77;
}
#endif
