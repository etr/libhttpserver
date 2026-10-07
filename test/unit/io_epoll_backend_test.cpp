/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#if defined(__linux__)
#include <sys/epoll.h>
#endif
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "./littletest.hpp"
#if defined(__linux__)
#include <httpserver/detail/io_epoll_backend.hpp>
#include <httpserver/detail/io_managed_backend.hpp>
#include "./io_backend_contract.hpp"

namespace {
using epoll_rig = io_contract::socket_rig<hd::io_epoll_backend>;

struct epoll_fixture final : io_contract::backend_fixture {
    epoll_fixture() {
        pair = io_loopback::pair::make();
        instance.adopt_connection(1, pair.detach_local());
    }
    hd::io_backend& backend() override { return instance; }
    void pump() override { }
    void deliver_read(hd::op_state&, std::string_view bytes) override {
        io_loopback::write_all(pair.peer(), bytes.data(), bytes.size());
    }
    void deliver_write(hd::op_state&, std::size_t) override { }
    void fire_wake() override { instance.wake(); }
    std::size_t close_backend() override { return instance.close(); }
    std::size_t pending_count() override { return instance.pending_count(); }
    hd::io_epoll_backend instance;
    io_loopback::pair pair;
};
// Test-only access retains a deterministic event seam without a runtime hook.
template<typename Tag, typename Tag::type Pointer>
struct epoll_member {
    friend typename Tag::type member(Tag) { return Pointer; }
};
template<typename Pointer>
struct epoll_tag {
    using type = Pointer;
    friend type member(epoll_tag);
};
using driver_thread = epoll_tag<std::thread hd::io_epoll_backend::*>;
using driver_fd = epoll_tag<int hd::io_epoll_backend::*>;
using driver_loop = epoll_tag<void (hd::io_epoll_backend::*)()>;
using driver_mutex = epoll_tag<std::mutex hd::io_epoll_backend::*>;
using driver_closed = epoll_tag<bool hd::io_epoll_backend::*>;
using driver_wake = epoll_tag<pollsys::wake_source hd::io_epoll_backend::*>;
using driver_tokens = epoll_tag<std::unordered_map<std::uint64_t, std::uint64_t> hd::io_epoll_backend::*>;
using selected_batch = std::vector<std::shared_ptr<hd::op_state>>;
using driver_select = epoll_tag<selected_batch (hd::io_epoll_backend::*)(std::uint64_t, std::uint32_t) const>;
using driver_batch = epoll_tag<void (hd::io_epoll_backend::*)(std::uint64_t, const selected_batch&)>;
using driver_dispatch = epoll_tag<void (hd::io_epoll_backend::*)(std::uint64_t, std::uint32_t)>;
template struct epoll_member<driver_thread, &hd::io_epoll_backend::thread_>;
template struct epoll_member<driver_fd, &hd::io_epoll_backend::epoll_>;
template struct epoll_member<driver_loop, &hd::io_epoll_backend::run_loop>;
template struct epoll_member<driver_mutex, &hd::io_epoll_backend::mu_>;
template struct epoll_member<driver_closed, &hd::io_epoll_backend::closed_>;
template struct epoll_member<driver_wake, &hd::io_epoll_backend::wake_>;
template struct epoll_member<driver_tokens, &hd::io_epoll_backend::tokens_>;
template struct epoll_member<driver_select, &hd::io_epoll_backend::collect_event_locked>;
template struct epoll_member<driver_batch, &hd::io_epoll_backend::dispatch_batch>;
template struct epoll_member<driver_dispatch, &hd::io_epoll_backend::dispatch_event>;

void pause_driver(hd::io_epoll_backend& backend) {
    {
        std::lock_guard<std::mutex> lock(backend.*member(driver_mutex{}));
        backend.*member(driver_closed{}) = true;
        (backend.*member(driver_wake{})).signal();
    }
    (backend.*member(driver_thread{})).join();
    backend.*member(driver_closed{}) = false;
}

bool wait_applied(io_contract::contract_rig& rig, const std::shared_ptr<hd::op_state>& state) {
    const auto deadline = std::chrono::steady_clock::now() + io_contract::kWaitBudget;
    while (std::chrono::steady_clock::now() < deadline) {
        rig.ex.run_pending();
        if (state->applied()) return true;
        std::this_thread::yield();
    }
    return false;
}
}  // namespace

LT_BEGIN_SUITE(epoll_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(epoll_suite)

LT_BEGIN_AUTO_TEST(epoll_suite, managed_linux_selects_epoll)
    auto backend = hd::make_socket_backend(httpserver::server::loop_mode::managed);
    LT_CHECK(dynamic_cast<hd::io_epoll_backend*>(backend.get()) != nullptr);
    LT_CHECK(backend->ready().ok());
    auto external = hd::make_socket_backend(httpserver::server::loop_mode::external);
    LT_CHECK(dynamic_cast<hd::io_poll_backend*>(external.get()) != nullptr);
LT_END_AUTO_TEST(managed_linux_selects_epoll)

LT_BEGIN_AUTO_TEST(epoll_suite, latent_readiness_rearms_without_peer_write)
    epoll_rig rig;
    auto pair = rig.adopt_pair(1);
    io_loopback::write_all(pair.peer(), "abcdefgh", 8);
    std::string seen;
    for (int i = 0; i < 4; ++i) {
        std::byte buffer[2]{};
        hd::read_operation op(rig.rig.owner, 1, buffer);
        op.submit(rig.backend);
        io_contract::probe p;
        std::vector<task<void>> tasks;
        io_contract::launch_probe(rig.rig, std::move(op), &p, tasks);
        LT_CHECK(io_contract::wait_terminal(rig.rig, p));
        LT_CHECK_EQ(p.delivered.load(), 1);
        LT_CHECK_EQ(p.observed.transferred, std::size_t{2});
        seen.append(reinterpret_cast<char*>(buffer), p.observed.transferred);
    }
    LT_CHECK_EQ(seen, std::string("abcdefgh"));
LT_END_AUTO_TEST(latent_readiness_rearms_without_peer_write)

LT_BEGIN_AUTO_TEST(epoll_suite, accept_backlog_rearms_without_new_connection)
    epoll_rig rig;
    auto listener = rig.adopt_listener(2);
    pollsys::native_socket_t clients[3];
    for (auto& client : clients) client = io_loopback::connect_to(listener.port());
    for (int i = 0; i < 3; ++i) {
        hd::accept_operation op(rig.rig.owner, 2);
        op.submit(rig.backend);
        io_contract::probe p;
        std::vector<task<void>> tasks;
        io_contract::launch_probe(rig.rig, std::move(op), &p, tasks);
        LT_CHECK(io_contract::wait_terminal(rig.rig, p));
        LT_CHECK_EQ(p.delivered.load(), 1);
        LT_CHECK(p.observed.code == hh::outcome_code::ok);
        LT_CHECK(p.observed.accepted_id != 0);
        rig.backend.release_connection(p.observed.accepted_id);
    }
    for (auto& client : clients) pollsys::close_socket(client);
LT_END_AUTO_TEST(accept_backlog_rearms_without_new_connection)

LT_BEGIN_AUTO_TEST(epoll_suite, read_delivers_bytes_exactly_once)
    epoll_fixture fx;
    io_contract::read_delivers_bytes_exactly_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(read_delivers_bytes_exactly_once)

LT_BEGIN_AUTO_TEST(epoll_suite, write_completes_with_transferred)
    epoll_fixture fx;
    io_contract::write_completes_with_transferred(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(write_completes_with_transferred)

LT_BEGIN_AUTO_TEST(epoll_suite, timer_fires_at_deadline_not_before)
    epoll_fixture fx;
    io_contract::timer_fires_at_deadline_not_before(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(timer_fires_at_deadline_not_before)

LT_BEGIN_AUTO_TEST(epoll_suite, two_timers_earliest_first_sequence_tie)
    epoll_fixture fx;
    io_contract::two_timers_earliest_first_sequence_tie(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(two_timers_earliest_first_sequence_tie)

LT_BEGIN_AUTO_TEST(epoll_suite, wake_completes_all_wakes_once)
    epoll_fixture fx;
    io_contract::wake_completes_all_wakes_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(wake_completes_all_wakes_once)

LT_BEGIN_AUTO_TEST(epoll_suite, cancel_pending_target)
    epoll_fixture fx;
    io_contract::cancel_pending_target(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(cancel_pending_target)

LT_BEGIN_AUTO_TEST(epoll_suite, cancel_terminal_target_reports_invalid_state)
    epoll_fixture fx;
    io_contract::cancel_terminal_target_reports_invalid_state(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(cancel_terminal_target_reports_invalid_state)

LT_BEGIN_AUTO_TEST(epoll_suite, close_sweeps_every_pending_once)
    epoll_fixture fx;
    io_contract::close_sweeps_every_pending_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(close_sweeps_every_pending_once)

LT_BEGIN_AUTO_TEST(epoll_suite, submit_after_close_connection_closed)
    epoll_fixture fx;
    io_contract::submit_after_close_connection_closed(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(submit_after_close_connection_closed)

LT_BEGIN_AUTO_TEST(epoll_suite, late_request_cancel_reports_invalid_state)
    epoll_fixture fx;
    io_contract::late_request_cancel_reports_invalid_state(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(late_request_cancel_reports_invalid_state)

LT_BEGIN_AUTO_TEST(epoll_suite, n_awaiter_resume_exactly_once)
    epoll_fixture fx;
    io_contract::n_awaiter_resume_exactly_once(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(n_awaiter_resume_exactly_once)

LT_BEGIN_AUTO_TEST(epoll_suite, cancel_vs_stimulus_race)
    epoll_fixture fx;
    io_contract::cancel_vs_stimulus_race(__lt_tr__, __lt_name__, fx);
LT_END_AUTO_TEST(cancel_vs_stimulus_race)

LT_BEGIN_AUTO_TEST(epoll_suite, accept_round_trip)
    epoll_rig rig;
    io_contract::accept_round_trip(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(accept_round_trip)

LT_BEGIN_AUTO_TEST(epoll_suite, interleaved_native_listeners_keep_distinct_ids)
    epoll_rig rig;
    io_contract::interleaved_native_listeners_keep_distinct_ids(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(interleaved_native_listeners_keep_distinct_ids)

LT_BEGIN_AUTO_TEST(epoll_suite, http1_round_trip)
    epoll_rig rig;
    io_contract::http1_round_trip(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(http1_round_trip)

LT_BEGIN_AUTO_TEST(epoll_suite, partial_read)
    epoll_rig rig;
    io_contract::partial_read(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(partial_read)

LT_BEGIN_AUTO_TEST(epoll_suite, read_hangup)
    epoll_rig rig;
    io_contract::read_hangup(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(read_hangup)

LT_BEGIN_AUTO_TEST(epoll_suite, write_hangup)
    epoll_rig rig;
    io_contract::write_hangup(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(write_hangup)

LT_BEGIN_AUTO_TEST(epoll_suite, idle_iterations_stay_bounded)
    epoll_rig rig;
    io_contract::idle_iterations_stay_bounded(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(idle_iterations_stay_bounded)

LT_BEGIN_AUTO_TEST(epoll_suite, slow_reader_no_busy_loop)
    epoll_rig rig;
    io_contract::slow_reader_no_busy_loop(__lt_tr__, __lt_name__, rig);
LT_END_AUTO_TEST(slow_reader_no_busy_loop)

LT_BEGIN_AUTO_TEST(epoll_suite, close_joins_driver_before_owner_teardown)
    epoll_rig rig;
    auto pair = rig.adopt_pair(1);
    hd::read_operation read(rig.rig.owner, 1, rig.rig.buffer);
    read.submit(rig.backend);
    LT_CHECK_EQ(rig.backend.close(), std::size_t{1});
    LT_CHECK(!(rig.backend.*member(driver_thread{})).joinable());
    LT_CHECK(wait_applied(rig.rig, read.state()));
    LT_CHECK(read.state()->stored_result().code == hh::outcome_code::connection_closed);
LT_END_AUTO_TEST(close_joins_driver_before_owner_teardown)

LT_BEGIN_AUTO_TEST(epoll_suite, pending_batch_survives_eagain_and_combines_directions)
    epoll_rig rig;
    auto pair = rig.adopt_pair(1);
    std::byte buffers[3][2]{};
    hd::read_operation a(rig.rig.owner, 1, buffers[0]);
    hd::read_operation b(rig.rig.owner, 1, buffers[1]);
    hd::read_operation c(rig.rig.owner, 1, buffers[2]);
    a.submit(rig.backend);
    b.submit(rig.backend);
    c.submit(rig.backend);
    hd::write_operation reply(rig.rig.owner, 1, io_contract::as_bytes("response"));
    reply.submit(rig.backend);
    io_loopback::write_all(pair.peer(), "abcd", 4);
    LT_CHECK(wait_applied(rig.rig, a.state()));
    LT_CHECK(wait_applied(rig.rig, b.state()));
    LT_CHECK(wait_applied(rig.rig, reply.state()));
    LT_CHECK(!c.is_terminal());
    io_loopback::write_all(pair.peer(), "ef", 2);
    LT_CHECK(wait_applied(rig.rig, c.state()));
    LT_CHECK(std::memcmp(buffers, "abcdef", 6) == 0);
    std::byte response[8]{};
    LT_CHECK(io_loopback::read_exact(pair.peer(), response, sizeof(response)));
    LT_CHECK(std::memcmp(response, "response", 8) == 0);
LT_END_AUTO_TEST(pending_batch_survives_eagain_and_combines_directions)

LT_BEGIN_AUTO_TEST(epoll_suite, data_hangup_is_delivered_before_eof)
    epoll_rig rig;
    auto pair = rig.adopt_pair(1);
    io_loopback::write_all(pair.peer(), "payload", 7);
    pair.close_peer();
    hd::read_operation data(rig.rig.owner, 1, rig.rig.buffer);
    data.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, data.state()));
    LT_CHECK(data.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK_EQ(data.state()->stored_result().transferred, std::size_t{7});
    LT_CHECK(std::memcmp(rig.rig.buffer, "payload", 7) == 0);
    hd::read_operation eof(rig.rig.owner, 1, rig.rig.buffer);
    eof.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, eof.state()));
    LT_CHECK(eof.state()->stored_result().code == hh::outcome_code::connection_closed);
LT_END_AUTO_TEST(data_hangup_is_delivered_before_eof)

LT_BEGIN_AUTO_TEST(epoll_suite, stale_event_rejects_reused_connection_and_fd)
    epoll_rig rig;
    pause_driver(rig.backend);
    auto original = rig.adopt_pair(1);
    const auto fd = rig.backend.native_handle(1);
    const auto old_token = (rig.backend.*member(driver_tokens{})).begin()->first;
    hd::read_operation old(rig.rig.owner, 1, rig.rig.buffer);
    old.submit(rig.backend);
    rig.backend.release_connection(1);
    LT_CHECK(wait_applied(rig.rig, old.state()));
    LT_CHECK(old.state()->stored_result().code == hh::outcome_code::connection_closed);
    auto replacement = io_loopback::pair::make();
    auto handle = replacement.detach_local();
    if (handle != fd) {
        LT_CHECK(::dup2(handle, fd) == fd);
        pollsys::close_socket(handle);
    }
    rig.backend.adopt_connection(1, fd);
    const auto new_token = (rig.backend.*member(driver_tokens{})).begin()->first;
    LT_CHECK(new_token != old_token);
    std::byte buffer[4]{std::byte{0x55}, std::byte{0x55}, std::byte{0x55}, std::byte{0x55}};
    hd::read_operation current(rig.rig.owner, 1, buffer);
    current.submit(rig.backend);
    io_loopback::write_all(replacement.peer(), "new!", 4);
    (rig.backend.*member(driver_dispatch{}))(old_token, EPOLLIN | EPOLLHUP);
    rig.rig.ex.run_pending();
    LT_CHECK(!current.is_terminal());
    LT_CHECK(buffer[0] == std::byte{0x55});
    LT_CHECK(rig.backend.request_cancel(*current.state()) == hh::outcome_code::ok);
    (rig.backend.*member(driver_dispatch{}))(new_token, EPOLLIN);
    LT_CHECK(buffer[0] == std::byte{0x55});
    hd::read_operation next(rig.rig.owner, 1, buffer);
    next.submit(rig.backend);
    (rig.backend.*member(driver_dispatch{}))(new_token, EPOLLIN);
    LT_CHECK(wait_applied(rig.rig, next.state()));
    LT_CHECK(std::memcmp(buffer, "new!", 4) == 0);
    LT_CHECK(wait_applied(rig.rig, current.state()));
    LT_CHECK(current.state()->stored_result().code == hh::outcome_code::cancelled);
LT_END_AUTO_TEST(stale_event_rejects_reused_connection_and_fd)

LT_BEGIN_AUTO_TEST(epoll_suite, release_cancel_close_race_completes_each_operation_once)
    for (int iteration = 0; iteration < 32; ++iteration) {
        epoll_rig rig;
        auto pair = rig.adopt_pair(1);
        std::byte buffers[4][4]{};
        std::vector<std::shared_ptr<hd::op_state>> states;
        io_contract::probe probes[4];
        std::vector<task<void>> tasks;
        for (int i = 0; i < 4; ++i) {
            hd::read_operation op(rig.rig.owner, 1, buffers[i]);
            op.submit(rig.backend);
            states.push_back(op.state());
            io_contract::launch_probe(rig.rig, std::move(op), &probes[i], tasks);
        }
        std::atomic<int> arrived{0};
        io_contract::gate gate(&arrived);
        std::thread cancel([&] {
            gate.arrive(); gate.wait(3);
            for (const auto& state : states) rig.backend.request_cancel(*state);
        });
        std::thread release([&] {
            gate.arrive(); gate.wait(3);
            rig.backend.release_connection(1);
        });
        gate.arrive(); gate.wait(3);
        rig.backend.close();
        cancel.join();
        release.join();
        for (auto& probe : probes) {
            LT_CHECK(io_contract::wait_terminal(rig.rig, probe));
            LT_CHECK_EQ(probe.delivered.load(), 1);
            LT_CHECK(probe.observed.code == hh::outcome_code::cancelled
                || probe.observed.code == hh::outcome_code::connection_closed);
        }
        LT_CHECK_EQ(rig.backend.pending_count(), std::size_t{0});
    }
LT_END_AUTO_TEST(release_cancel_close_race_completes_each_operation_once)

LT_BEGIN_AUTO_TEST(epoll_suite, epoll_control_failure_resolves_pending_work)
    epoll_rig rig;
    const auto handle = ::open("/dev/null", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    LT_CHECK(handle >= 0);
    rig.backend.adopt_connection(1, handle);
    hd::read_operation op(rig.rig.owner, 1, rig.rig.buffer);
    op.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, op.state()));
    LT_CHECK(op.state()->stored_result().code == hh::outcome_code::connection_closed);
    LT_CHECK(!rig.backend.ready().ok());
    rig.backend.close();
    LT_CHECK(!(rig.backend.*member(driver_thread{})).joinable());
LT_END_AUTO_TEST(epoll_control_failure_resolves_pending_work)

namespace {
template<typename Backend>
std::vector<std::string> semantic_trace(littletest::test_runner* __lt_tr__, const char* __lt_name__) {
    io_contract::socket_rig<Backend> rig;
    std::vector<std::string> trace;
    auto record = [&](const auto& op, std::string_view label, std::string_view payload = {}) {
        LT_CHECK(wait_applied(rig.rig, op.state()));
        const auto result = op.state()->stored_result();
        trace.push_back(std::string(label) + ":" + std::to_string(static_cast<int>(result.code))
            + ":" + std::to_string(result.transferred) + ":" + std::string(payload));
    };
    auto listener = rig.adopt_listener(20);
    pollsys::native_socket_t clients[3];
    for (auto& client : clients) client = io_loopback::connect_to(listener.port());
    for (int i = 0; i < 3; ++i) {
        hd::accept_operation accept(rig.rig.owner, 20);
        accept.submit(rig.backend);
        record(accept, "listener/accept/" + std::to_string(i));
        const auto result = accept.state()->stored_result();
        LT_CHECK(result.accepted_id != 0);
        LT_CHECK(result.peer.port != 0);
        trace.push_back("accepted/" + result.peer.address.to_string());
        rig.backend.release_connection(result.accepted_id);
    }
    for (auto& client : clients) pollsys::close_socket(client);
    rig.backend.release_connection(20);
    auto pair = rig.adopt_pair(1);
    io_loopback::write_all(pair.peer(), "abcdefghijkl", 12);
    for (int i = 0; i < 3; ++i) {
        std::byte buffer[4]{};
        hd::read_operation op(rig.rig.owner, 1, buffer);
        op.submit(rig.backend);
        LT_CHECK(wait_applied(rig.rig, op.state()));
        record(op, "connection/read/" + std::to_string(i),
            std::string_view(reinterpret_cast<char*>(buffer), op.state()->stored_result().transferred));
    }
    hd::write_operation reply(rig.rig.owner, 1, io_contract::as_bytes("response"));
    reply.submit(rig.backend);
    record(reply, "connection/write/0");
    std::byte response[8]{};
    LT_CHECK(io_loopback::read_exact(pair.peer(), response, 8));
    trace.push_back("sent/" + std::string(reinterpret_cast<char*>(response), 8));
    io_loopback::write_all(pair.peer(), "tail", 4);
    pair.close_peer();
    hd::read_operation tail(rig.rig.owner, 1, rig.rig.buffer);
    tail.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, tail.state()));
    record(tail, "connection/read/3", std::string_view(reinterpret_cast<char*>(rig.rig.buffer), 4));
    hd::read_operation eof(rig.rig.owner, 1, rig.rig.buffer);
    eof.submit(rig.backend);
    record(eof, "connection/eof/4");
    rig.backend.release_connection(1);
    auto replacement = rig.adopt_pair(1);
    hd::read_operation cancelled(rig.rig.owner, 1, rig.rig.buffer);
    cancelled.submit(rig.backend);
    LT_CHECK(rig.backend.request_cancel(*cancelled.state()) == hh::outcome_code::ok);
    record(cancelled, "replacement/cancel/0");
    io_loopback::write_all(replacement.peer(), "new", 3);
    hd::read_operation fresh(rig.rig.owner, 1, rig.rig.buffer);
    fresh.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, fresh.state()));
    record(fresh, "replacement/read/1", std::string_view(reinterpret_cast<char*>(rig.rig.buffer), 3));
    hd::read_operation released(rig.rig.owner, 1, rig.rig.buffer);
    released.submit(rig.backend);
    rig.backend.release_connection(1);
    record(released, "replacement/release/2");
    LT_CHECK(rig.backend.native_handle(1) == pollsys::k_invalid_socket);
    hd::wake_operation global(rig.rig.owner, 0);
    global.submit(rig.backend);
    rig.backend.close();
    record(global, "global/close/0");
    LT_CHECK_EQ(rig.backend.pending_count(), std::size_t{0});
    return trace;
}

template<typename Backend>
std::vector<std::byte> backpressure_trace(littletest::test_runner* __lt_tr__, const char* __lt_name__) {
    io_contract::socket_rig<Backend> rig;
    auto pair = rig.adopt_pair(1);
    int capacity = 1024;
    LT_CHECK(::setsockopt(rig.backend.native_handle(1), SOL_SOCKET, SO_SNDBUF,
                          &capacity, sizeof(capacity)) == 0);
    constexpr std::size_t total = 256 * 1024;
    std::vector<std::byte> bytes(total);
    for (std::size_t i = 0; i < total; ++i) bytes[i] = static_cast<std::byte>(i % 251);
    hd::read_operation read(rig.rig.owner, 1, rig.rig.buffer);
    read.submit(rig.backend);
    hd::write_operation first(rig.rig.owner, 1, bytes);
    first.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, first.state()));
    std::size_t sent = first.state()->stored_result().transferred;
    LT_CHECK(sent > 0);
    LT_CHECK(sent < total);  // backpressure actually occurred before any peer read
    std::vector<std::byte> received(total);
    std::atomic<std::size_t> count{0};
    std::atomic_bool stop{false};
    LT_CHECK(pollsys::set_nonblocking(pair.peer(), true));
    std::thread reader([&] {
        const auto deadline = std::chrono::steady_clock::now() + io_contract::kWaitBudget;
        while (!stop.load() && count.load() < total && std::chrono::steady_clock::now() < deadline) {
            const auto offset = count.load();
            const auto result = pollsys::read_some(pair.peer(), received.data() + offset, total - offset);
            if (result.status == pollsys::sys_status::ok) count.fetch_add(result.transferred);
            else if (result.status != pollsys::sys_status::would_block) break;
            std::this_thread::yield();
        }
    });
    io_loopback::write_all(pair.peer(), "input", 5);
    const auto before = rig.backend.poll_iterations();
    std::size_t operations = 0;
    const auto deadline = std::chrono::steady_clock::now() + io_contract::kWaitBudget;
    while (sent < total && std::chrono::steady_clock::now() < deadline) {
        hd::write_operation next(rig.rig.owner, 1, std::span<const std::byte>(bytes).subspan(sent));
        next.submit(rig.backend);
        if (!wait_applied(rig.rig, next.state())) break;
        const auto result = next.state()->stored_result();
        LT_CHECK(result.code == hh::outcome_code::ok);
        sent += result.transferred;
        ++operations;
    }
    while (count.load() < total && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    stop.store(true);
    reader.join();
    LT_CHECK(wait_applied(rig.rig, read.state()));
    LT_CHECK_EQ(read.state()->stored_result().transferred, std::size_t{5});
    LT_CHECK(std::memcmp(rig.rig.buffer, "input", 5) == 0);
    LT_CHECK_EQ(sent, total);
    LT_CHECK_EQ(count.load(), total);
    LT_CHECK(received == bytes);
    LT_CHECK(rig.backend.poll_iterations() - before <= 3 * operations + 20);
    rig.backend.release_connection(1);
    return received;
}
}  // namespace

LT_BEGIN_AUTO_TEST(epoll_suite, accept_read_write_cancel_release_close_match_poll_trace)
    const auto poll = semantic_trace<hd::io_poll_backend>(__lt_tr__, __lt_name__);
    const auto epoll = semantic_trace<hd::io_epoll_backend>(__lt_tr__, __lt_name__);
    LT_CHECK(poll == epoll);
LT_END_AUTO_TEST(accept_read_write_cancel_release_close_match_poll_trace)

LT_BEGIN_AUTO_TEST(epoll_suite, backpressure_stream_matches_poll_with_combined_rearm)
    const auto poll = backpressure_trace<hd::io_poll_backend>(__lt_tr__, __lt_name__);
    const auto epoll = backpressure_trace<hd::io_epoll_backend>(__lt_tr__, __lt_name__);
    LT_CHECK(poll == epoll);
LT_END_AUTO_TEST(backpressure_stream_matches_poll_with_combined_rearm)

LT_BEGIN_AUTO_TEST(epoll_suite, selected_batch_cancel_and_release_never_touch_retired_buffers)
    epoll_rig rig;
    pause_driver(rig.backend);
    auto pair = rig.adopt_pair(1);
    std::byte buffers[3][4]{};
    hd::read_operation first(rig.rig.owner, 1, buffers[0]);
    hd::read_operation cancelled(rig.rig.owner, 1, buffers[1]);
    hd::read_operation last(rig.rig.owner, 1, buffers[2]);
    first.submit(rig.backend);
    cancelled.submit(rig.backend);
    last.submit(rig.backend);
    const auto token = (rig.backend.*member(driver_tokens{})).begin()->first;
    const auto batch = (rig.backend.*member(driver_select{}))(1, EPOLLIN);
    LT_CHECK_EQ(batch.size(), std::size_t{3});
    LT_CHECK(rig.backend.request_cancel(*cancelled.state()) == hh::outcome_code::ok);
    io_loopback::write_all(pair.peer(), "abcdefgh", 8);
    (rig.backend.*member(driver_batch{}))(token, batch);
    LT_CHECK(wait_applied(rig.rig, first.state()));
    LT_CHECK(wait_applied(rig.rig, last.state()));
    LT_CHECK(wait_applied(rig.rig, cancelled.state()));
    LT_CHECK(std::memcmp(buffers[0], "abcd", 4) == 0);
    LT_CHECK(std::memcmp(buffers[2], "efgh", 4) == 0);
    const std::byte zero[4]{};
    LT_CHECK(std::memcmp(buffers[1], zero, 4) == 0);
    hd::read_operation released(rig.rig.owner, 1, buffers[1]);
    released.submit(rig.backend);
    const auto selected = (rig.backend.*member(driver_select{}))(1, EPOLLIN);
    rig.backend.release_connection(1);
    auto replacement = rig.adopt_pair(1);
    io_loopback::write_all(replacement.peer(), "next", 4);
    (rig.backend.*member(driver_batch{}))(token, selected);
    LT_CHECK(wait_applied(rig.rig, released.state()));
    LT_CHECK(released.state()->stored_result().code == hh::outcome_code::connection_closed);
    LT_CHECK(std::memcmp(buffers[1], zero, 4) == 0);
LT_END_AUTO_TEST(selected_batch_cancel_and_release_never_touch_retired_buffers)

LT_BEGIN_AUTO_TEST(epoll_suite, fatal_epoll_wait_error_closes_global_operations)
    epoll_rig rig;
    pause_driver(rig.backend);
    ::close(rig.backend.*member(driver_fd{}));
    rig.backend.*member(driver_fd{}) = -1;
    hd::wake_operation wake(rig.rig.owner, 0);
    wake.submit(rig.backend);
    (rig.backend.*member(driver_loop{}))();
    LT_CHECK(wait_applied(rig.rig, wake.state()));
    LT_CHECK(wake.state()->stored_result().code == hh::outcome_code::connection_closed);
    LT_CHECK(!rig.backend.ready().ok());
LT_END_AUTO_TEST(fatal_epoll_wait_error_closes_global_operations)

LT_BEGIN_AUTO_TEST(epoll_suite, cancelling_read_preserves_backpressured_write_interest)
    epoll_rig rig;
    auto pair = rig.adopt_pair(1);
    int capacity = 1024;
    LT_CHECK(::setsockopt(rig.backend.native_handle(1), SOL_SOCKET, SO_SNDBUF,
                          &capacity, sizeof(capacity)) == 0);
    const std::vector<std::byte> bytes(65536, std::byte{0x42});
    hd::read_operation read(rig.rig.owner, 1, rig.rig.buffer);
    read.submit(rig.backend);
    hd::write_operation first(rig.rig.owner, 1, bytes);
    first.submit(rig.backend);
    LT_CHECK(wait_applied(rig.rig, first.state()));
    const auto sent = first.state()->stored_result().transferred;
    LT_CHECK(sent < bytes.size());
    hd::write_operation next(rig.rig.owner, 1, std::span<const std::byte>(bytes).subspan(sent));
    next.submit(rig.backend);
    LT_CHECK(rig.backend.request_cancel(*read.state()) == hh::outcome_code::ok);
    LT_CHECK(wait_applied(rig.rig, read.state()));
    LT_CHECK(pollsys::set_nonblocking(pair.peer(), true));
    std::size_t received = 0;
    std::byte buffer[4096];
    const auto deadline = std::chrono::steady_clock::now() + io_contract::kWaitBudget;
    while (!next.state()->applied() && std::chrono::steady_clock::now() < deadline) {
        const auto result = pollsys::read_some(pair.peer(), buffer, sizeof(buffer));
        received += result.transferred;
        rig.rig.ex.run_pending();
        std::this_thread::yield();
    }
    LT_CHECK(next.state()->applied());
    LT_CHECK(next.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK(next.state()->stored_result().transferred > 0);
    LT_CHECK(received > 0);
LT_END_AUTO_TEST(cancelling_read_preserves_backpressured_write_interest)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
#else
#include <iostream>
int main() {
    std::cout << "SKIP: Linux epoll backend requires Linux\n";
    return 77;
}
#endif
