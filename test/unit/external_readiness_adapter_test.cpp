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

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <memory>
#include <limits>
#include <stdexcept>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/server/readiness.hpp>
#include "./io_backend_contract.hpp"
#include "./littletest.hpp"

namespace srv = httpserver::server;
namespace {
using clock_type = std::chrono::steady_clock;
using batch_type = std::vector<std::shared_ptr<hd::op_state>>;

srv::readiness_event event_for(const srv::socket_interest& interest,
                               bool read = true, bool write = false) {
    return {interest.key, interest.generation, read, write, false, false};
}

bool wake_ready(const srv::interest_snapshot& snapshot) {
    if (!snapshot.wake) return false;
    pollsys::poll_slot fd{static_cast<pollsys::native_socket_t>(snapshot.wake->handle.value),
                          pollsys::k_readable, 0};
    return pollsys::poll_call(&fd, 1, 0) == 1
        && (fd.revents & pollsys::k_readable) != 0;
}

struct fixture {
    fixture() { backend.activate_external(); }
    ~fixture() { backend.close(); rig.ex.run_pending(); }
    template<typename Op>
    std::shared_ptr<hd::op_state> submit(Op op, io_contract::probe& observed) {
        auto state = op.state();
        op.submit(backend);
        io_contract::launch_probe(rig, std::move(op), &observed, tasks);
        rig.ex.run_pending();
        return state;
    }
    httpserver::http::outcome dispatch(const srv::readiness_event& event,
                                       clock_type::time_point now = clock_type::now()) {
        return backend.dispatch(std::span(&event, 1), now);
    }
    io_contract::contract_rig rig;
    hd::io_poll_backend backend{srv::loop_mode::external};
    std::vector<task<void>> tasks;
};

// Explicit-instantiation access controls scheduling boundaries without
// production test hooks or replacement socket semantics.
template<typename Tag, typename Tag::type Pointer>
struct private_member {
    friend typename Tag::type member(Tag) { return Pointer; }
};
template<typename Pointer, int Index = 0>
struct tag {
    using type = Pointer;
    friend type member(tag);
};
template<typename Tag, auto Pointer>
struct private_auto_member {
    friend auto member(Tag) { return Pointer; }
};
struct connections_tag {
    friend auto member(connections_tag);
};
template struct private_auto_member<connections_tag, &hd::io_poll_backend::connections_>;

using detach_tag = tag<void (hd::io_poll_backend::*)(std::uint64_t, bool, batch_type&)>;
using rearm_tag = tag<void (hd::io_poll_backend::*)(const batch_type&, std::size_t)>;
using read_tag = tag<hd::step_outcome (hd::io_poll_backend::*)(
    pollsys::native_socket_t, const std::shared_ptr<hd::op_state>&)>;
using accept_tag = tag<read_tag::type, 1>;
using mutex_tag = tag<std::mutex hd::io_poll_backend::*>;
using busy_tag = tag<std::atomic_bool hd::io_poll_backend::*>;
using identity_tag = tag<std::uint64_t hd::io_poll_backend::*>;
using wake_tag = tag<pollsys::wake_source hd::io_poll_backend::*>;
using write_end_tag = tag<pollsys::native_socket_t pollsys::wake_source::*>;
using acknowledge_tag = tag<void (hd::io_poll_backend::*)()>;
template struct private_member<detach_tag, &hd::io_poll_backend::take_direction_locked>;
template struct private_member<rearm_tag, &hd::io_poll_backend::rearm_after_would_block>;
template struct private_member<read_tag, &hd::io_poll_backend::read_step>;
template struct private_member<accept_tag, &hd::io_poll_backend::accept_step>;
template struct private_member<mutex_tag, &hd::io_poll_backend::mu_>;
template struct private_member<busy_tag, &hd::io_poll_backend::dispatching_>;
template struct private_member<identity_tag, &hd::io_poll_backend::next_identity_>;
template struct private_member<wake_tag, &hd::io_poll_backend::wake_>;
template struct private_member<write_end_tag, &pollsys::wake_source::write_end_>;
template struct private_member<acknowledge_tag, &hd::io_poll_backend::acknowledge_wake>;

void detached_reopen(littletest::test_runner* __lt_tr__, const char* __lt_name__,
                     bool listener) {
    fixture fx;
    auto pair = io_loopback::pair::make();
    auto listening = io_loopback::listener::open();
    LT_ASSERT(pair.ok());
    LT_ASSERT(listening.ok());
    if (listener) {
        fx.backend.adopt_listener(1, listening.socket());
        listening.detach();
    } else {
        fx.backend.adopt_connection(1, pair.detach_local());
    }
    io_contract::probe observed;
    if (listener) fx.submit(hd::accept_operation(fx.rig.owner, 1), observed);
    else fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), observed);
    batch_type batch;
    {
        std::lock_guard lock(fx.backend.*member(mutex_tag{}));
        (fx.backend.*member(detach_tag{}))(1, true, batch);
    }
    LT_ASSERT(batch.size() == 1);
    const auto step = listener ? member(accept_tag{}) : member(read_tag{});
    LT_CHECK((fx.backend.*step)(fx.backend.native_handle(1), batch.front())
             == hd::step_outcome::pending_again);
    fx.backend.release_connection(1);
    auto replacement = io_loopback::pair::make();
    LT_ASSERT(replacement.ok());
    fx.backend.adopt_connection(1, replacement.detach_local());
    (fx.backend.*member(rearm_tag{}))(batch, 0);
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(observed.delivered.load(), 1);
    LT_CHECK(observed.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(fx.backend.pending_count(), std::size_t{0});
}
}  // namespace

LT_BEGIN_SUITE(external_adapter_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(external_adapter_suite)

LT_BEGIN_AUTO_TEST(external_adapter_suite, host_drives_read_write_and_snapshot_is_owned)
    fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    LT_ASSERT(pollsys::set_nonblocking(pair.peer(), true));
    fx.backend.adopt_connection(1, pair.detach_local());
    io_contract::probe read, write;
    fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), read);
    const std::byte bytes[]{std::byte{'y'}};
    fx.submit(hd::write_operation(fx.rig.owner, 1, bytes), write);
    auto snapshot = fx.backend.interests();
    LT_ASSERT(snapshot.wake.has_value());
    LT_CHECK(snapshot.wake->handle.valid);
    LT_CHECK(snapshot.wake->readable && !snapshot.wake->writable);
    LT_ASSERT(snapshot.sockets.size() == 1);
    LT_CHECK(snapshot.sockets[0].readable && snapshot.sockets[0].writable);
    LT_CHECK(!snapshot.next_deadline);
    LT_CHECK(wake_ready(snapshot));
    io_loopback::write_all(pair.peer(), "x", 1);
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 0);
    LT_CHECK_EQ(write.delivered.load(), 0);
    LT_CHECK_EQ(fx.backend.poll_iterations(), std::uint64_t{0});
    auto event = event_for(snapshot.sockets[0], true, true);
    LT_CHECK(fx.dispatch(event).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 1);
    LT_CHECK_EQ(write.delivered.load(), 1);
    LT_CHECK_EQ(read.observed.transferred, std::size_t{1});
    LT_CHECK_EQ(write.observed.transferred, std::size_t{1});
    LT_CHECK(fx.rig.buffer[0] == std::byte{'x'});
    char received = 0;
    LT_CHECK(io_loopback::read_exact(pair.peer(), reinterpret_cast<std::byte*>(&received), 1));
    LT_CHECK_EQ(received, 'y');
    LT_CHECK(fx.dispatch(event).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 1);
    LT_CHECK(fx.backend.interests().sockets.empty());
    LT_CHECK_EQ(snapshot.sockets.size(), std::size_t{1});
    LT_CHECK(snapshot.sockets[0].readable);
LT_END_AUTO_TEST(host_drives_read_write_and_snapshot_is_owned)

LT_BEGIN_AUTO_TEST(external_adapter_suite, spurious_unknown_and_wrong_generation_do_not_consume)
    fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    fx.backend.adopt_connection(1, pair.detach_local());
    io_contract::probe read;
    fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), read);
    const auto socket = fx.backend.interests().sockets.front();
    LT_CHECK(fx.dispatch(event_for(socket)).ok());  // real would-block
    LT_CHECK_EQ(fx.backend.pending_count(), std::size_t{1});
    io_loopback::write_all(pair.peer(), "z", 1);
    auto event = event_for(socket, false, false);
    LT_CHECK(fx.dispatch(event).ok());
    event = event_for(socket);
    ++event.generation;
    LT_CHECK(fx.dispatch(event).ok());
    event.key = srv::socket_key{99999};
    LT_CHECK(fx.dispatch(event).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 0);
    LT_CHECK(fx.dispatch(event_for(socket)).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 1);
    LT_CHECK(fx.rig.buffer[0] == std::byte{'z'});
LT_END_AUTO_TEST(spurious_unknown_and_wrong_generation_do_not_consume)

LT_BEGIN_AUTO_TEST(external_adapter_suite, omitted_then_republished_registration_rejects_old_callbacks)
    fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    fx.backend.adopt_connection(1, pair.detach_local());
    io_contract::probe first, next;
    auto state = fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), first);
    const auto old = fx.backend.interests().sockets.front();
    LT_CHECK(fx.backend.request_cancel(*state) == hh::outcome_code::ok);
    LT_CHECK(fx.backend.interests().sockets.empty());
    fx.rig.ex.run_pending();
    fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), next);
    const auto fresh = fx.backend.interests().sockets.front();
    LT_CHECK(old.key != fresh.key || old.generation != fresh.generation);
    io_loopback::write_all(pair.peer(), "n", 1);
    LT_CHECK(fx.dispatch(event_for(old)).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(next.delivered.load(), 0);
    LT_CHECK(fx.dispatch(event_for(fresh)).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(next.delivered.load(), 1);
LT_END_AUTO_TEST(omitted_then_republished_registration_rejects_old_callbacks)

LT_BEGIN_AUTO_TEST(external_adapter_suite, reopen_same_id_and_descriptor_rejects_every_old_flag_combination)
    fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    const auto descriptor = pair.local();
    fx.backend.adopt_connection(1, pair.detach_local());
    io_contract::probe first, next;
    fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), first);
    const auto old = fx.backend.interests().sockets.front();
    fx.backend.release_connection(1);
    fx.rig.ex.run_pending();
    auto replacement = io_loopback::pair::make();
    LT_ASSERT(replacement.ok());
    auto replacement_fd = replacement.detach_local();
#if !defined(_WIN32)
    if (replacement_fd != descriptor) {
        LT_ASSERT(::dup2(replacement_fd, descriptor) == descriptor);
        pollsys::close_socket(replacement_fd);
        replacement_fd = descriptor;
    }
    LT_CHECK_EQ(replacement_fd, descriptor);
#endif
    fx.backend.adopt_connection(1, replacement_fd);
    fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), next);
    const auto fresh = fx.backend.interests().sockets.front();
    LT_CHECK(old.key != fresh.key || old.generation != fresh.generation);
    io_loopback::write_all(replacement.peer(), "r", 1);
    for (int flags = 0; flags < 16; ++flags) {
        srv::readiness_event stale{old.key, old.generation,
            (flags & 1) != 0, (flags & 2) != 0,
            (flags & 4) != 0, (flags & 8) != 0};
        LT_CHECK(fx.dispatch(stale).ok());
        fx.rig.ex.run_pending();
        LT_CHECK_EQ(next.delivered.load(), 0);
        LT_CHECK_EQ(fx.backend.pending_count(), std::size_t{1});
    }
    LT_CHECK(fx.dispatch(event_for(fresh)).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(next.delivered.load(), 1);
    LT_CHECK(fx.rig.buffer[0] == std::byte{'r'});
    LT_CHECK_EQ(first.delivered.load(), 1);
LT_END_AUTO_TEST(reopen_same_id_and_descriptor_rejects_every_old_flag_combination)

LT_BEGIN_AUTO_TEST(external_adapter_suite, detached_read_cannot_rearm_into_reopened_id)
    detached_reopen(__lt_tr__, __lt_name__, false);
LT_END_AUTO_TEST(detached_read_cannot_rearm_into_reopened_id)
LT_BEGIN_AUTO_TEST(external_adapter_suite, detached_accept_cannot_rearm_into_reopened_id)
    detached_reopen(__lt_tr__, __lt_name__, true);
LT_END_AUTO_TEST(detached_accept_cannot_rearm_into_reopened_id)

LT_BEGIN_AUTO_TEST(external_adapter_suite, timers_use_host_clock_order_and_reject_decreasing_samples)
    fixture fx;
    const auto now = clock_type::now();
    io_contract::order_log order;
    hd::timer_operation late(fx.rig.owner, 0, now + 2ms);
    hd::timer_operation first(fx.rig.owner, 0, now + 1ms);
    hd::timer_operation second(fx.rig.owner, 0, now + 1ms);
    late.submit(fx.backend); first.submit(fx.backend); second.submit(fx.backend);
    io_contract::launch_logged(fx.rig, std::move(late), &order, 3, fx.tasks);
    io_contract::launch_logged(fx.rig, std::move(first), &order, 1, fx.tasks);
    io_contract::launch_logged(fx.rig, std::move(second), &order, 2, fx.tasks);
    fx.rig.ex.run_pending();
    LT_CHECK(fx.backend.interests().next_deadline == now + 1ms);
    LT_CHECK(fx.backend.dispatch({}, now).ok());
    fx.rig.ex.run_pending();
    LT_CHECK(order.snapshot().empty());
    LT_CHECK(fx.backend.dispatch({}, now + 1ms).ok());
    fx.rig.ex.run_pending();
    LT_CHECK(order.snapshot() == std::vector<int>({1, 2}));
    LT_CHECK(fx.backend.dispatch({}, now).code() == hh::outcome_code::invalid_state);
    LT_CHECK_EQ(fx.backend.pending_count(), std::size_t{1});
    LT_CHECK(fx.backend.dispatch({}, now + 1ms).ok());
    LT_CHECK(fx.backend.dispatch({}, now + 2ms).ok());
    fx.rig.ex.run_pending();
    LT_CHECK(order.snapshot() == std::vector<int>({1, 2, 3}));
    LT_CHECK(!fx.backend.interests().next_deadline);
LT_END_AUTO_TEST(timers_use_host_clock_order_and_reject_decreasing_samples)

LT_BEGIN_AUTO_TEST(external_adapter_suite, timer_cancel_expiry_race_is_exactly_once)
    fixture fx;
    auto now = clock_type::now();
    for (int i = 0; i < 32; ++i) {
        io_contract::probe observed;
        auto state = fx.submit(hd::timer_operation(fx.rig.owner, 0, now), observed);
        std::barrier barrier(2);
        std::thread cancel([&] { barrier.arrive_and_wait(); fx.backend.request_cancel(*state); });
        barrier.arrive_and_wait();
        LT_CHECK(fx.backend.dispatch({}, now).ok());
        cancel.join();
        fx.rig.ex.run_pending();
        LT_CHECK_EQ(observed.delivered.load(), 1);
        LT_CHECK(observed.observed.code == hh::outcome_code::ok
                 || observed.observed.code == hh::outcome_code::cancelled);
        ++now;
    }
    LT_CHECK_EQ(fx.backend.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(timer_cancel_expiry_race_is_exactly_once)

LT_BEGIN_AUTO_TEST(external_adapter_suite, wake_is_doorbell_and_post_snapshot_work_survives_acknowledgement)
    fixture fx;
    const auto before = fx.backend.interests();
    io_contract::probe parked;
    fx.submit(hd::wake_operation(fx.rig.owner, 0), parked);
    LT_CHECK(wake_ready(before));
    auto stale_wake = event_for(*before.wake);
    ++stale_wake.generation;
    LT_CHECK(fx.dispatch(stale_wake).ok());
    LT_CHECK(wake_ready(before));
    LT_CHECK(fx.dispatch(event_for(*before.wake)).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(parked.delivered.load(), 0);
    LT_CHECK(!wake_ready(before));
    LT_CHECK_EQ(fx.backend.wake(), std::size_t{1});
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(parked.delivered.load(), 1);
    for (int i = 0; i < 16; ++i) {
        std::barrier barrier(2);
        hd::timer_operation timer(fx.rig.owner, 0, clock_type::now() + std::chrono::hours(1));
        std::thread submitter([&] { barrier.arrive_and_wait(); timer.submit(fx.backend); });
        {
            std::lock_guard lock(fx.backend.*member(mutex_tag{}));
            barrier.arrive_and_wait();
            // Submission is waiting on the mutation mutex at acknowledgement.
            (fx.backend.*member(acknowledge_tag{}))();
        }
        submitter.join();
        LT_CHECK(wake_ready(before));
        LT_CHECK(fx.dispatch(event_for(*before.wake)).ok());
        LT_CHECK(!wake_ready(before));
        fx.backend.request_cancel(*timer.state());
    }
    fx.backend.close();
    fx.rig.ex.run_pending();
    LT_CHECK(!fx.backend.interests().wake);
    LT_CHECK(fx.dispatch(event_for(*before.wake)).code() == hh::outcome_code::invalid_state);
LT_END_AUTO_TEST(wake_is_doorbell_and_post_snapshot_work_survives_acknowledgement)

LT_BEGIN_AUTO_TEST(external_adapter_suite, overlapping_dispatch_rejects_without_consuming_valid_read)
    fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    fx.backend.adopt_connection(1, pair.detach_local());
    io_contract::probe read;
    fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), read);
    const auto event = event_for(fx.backend.interests().sockets.front());
    io_loopback::write_all(pair.peer(), "b", 1);
    (fx.backend.*member(busy_tag{})).store(true);
    LT_CHECK(fx.dispatch(event).code() == hh::outcome_code::invalid_state);
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 0);
    (fx.backend.*member(busy_tag{})).store(false);
    LT_CHECK(fx.dispatch(event).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 1);
LT_END_AUTO_TEST(overlapping_dispatch_rejects_without_consuming_valid_read)

LT_BEGIN_AUTO_TEST(external_adapter_suite, host_accept_and_close_deliver_once)
    fixture fx;
    auto listener = io_loopback::listener::open();
    LT_ASSERT(listener.ok());
    fx.backend.adopt_listener(1, listener.socket());
    listener.detach();
    io_contract::probe accepted;
    fx.submit(hd::accept_operation(fx.rig.owner, 1), accepted);
    const auto old = fx.backend.interests().sockets.front();
    auto client = pollsys::open_stream();
    LT_ASSERT(pollsys::connect_loopback(client, listener.port()));
    pollsys::poll_slot listener_ready{fx.backend.native_handle(1), pollsys::k_readable, 0};
    LT_ASSERT(pollsys::poll_call(&listener_ready, 1, 5000) == 1);
    LT_CHECK_EQ(accepted.delivered.load(), 0);
    LT_CHECK(fx.dispatch(event_for(old)).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(accepted.delivered.load(), 1);
    LT_CHECK(accepted.observed.accepted_id != 0);
    const auto id = accepted.observed.accepted_id;
    io_contract::probe closed;
    fx.submit(hd::read_operation(fx.rig.owner, id, fx.rig.buffer), closed);
    auto event = event_for(fx.backend.interests().sockets.front(), false);
    event.closed = true;
    LT_CHECK(fx.dispatch(event).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(closed.delivered.load(), 1);
    LT_CHECK(closed.observed.code == hh::outcome_code::connection_closed);
    fx.backend.close(); fx.rig.ex.run_pending();
    LT_CHECK_EQ(closed.delivered.load(), 1);
    pollsys::close_socket(client);
LT_END_AUTO_TEST(host_accept_and_close_deliver_once)

LT_BEGIN_AUTO_TEST(external_adapter_suite, wake_published_during_dispatch_survives_and_recursive_overlap_reject)
    httpserver::inline_executor ex;
    hd::io_connection_owner owner(ex);
    hd::io_poll_backend backend(srv::loop_mode::external);
    backend.activate_external();
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    backend.adopt_connection(1, pair.detach_local());
    std::byte buffer[1]{};
    hd::read_operation read(owner, 1, buffer);
    read.submit(backend);
    const auto snapshot = backend.interests();
    const auto read_event = event_for(snapshot.sockets.front());
    std::mutex gate_mu;
    std::condition_variable gate_cv;
    bool entered = false, released = false, timed_out = false;
    hh::outcome_code recursive = hh::outcome_code::ok;
    auto continuation = [&]() -> task<void> {
        co_await std::move(read);
        recursive = backend.dispatch(std::span(&read_event, 1), clock_type::now()).code();
        std::unique_lock lock(gate_mu);
        entered = true;
        gate_cv.notify_all();
        timed_out = !gate_cv.wait_for(lock, 5s, [&] { return released; });
    };
    spawn(ex, continuation(), [](task_result<void>) { });
    io_loopback::write_all(pair.peer(), "a", 1);
    hh::outcome result;
    std::thread dispatcher([&] {
        const srv::readiness_event events[]{event_for(*snapshot.wake), read_event};
        result = backend.dispatch(events, clock_type::now());
    });
    {
        std::unique_lock lock(gate_mu);
        LT_CHECK(gate_cv.wait_for(lock, 5s, [&] { return entered; }));
    }
    hd::timer_operation timer(owner, 0, clock_type::now() + std::chrono::hours(1));
    timer.submit(backend);
    LT_CHECK(wake_ready(snapshot));
    LT_CHECK(backend.dispatch(std::span(&read_event, 1), clock_type::now()).code()
             == hh::outcome_code::invalid_state);
    {
        std::lock_guard lock(gate_mu);
        released = true;
    }
    gate_cv.notify_all();
    dispatcher.join();
    LT_CHECK(result.ok());
    LT_CHECK(!timed_out);
    LT_CHECK(recursive == hh::outcome_code::invalid_state);
    LT_CHECK(wake_ready(snapshot));
    LT_CHECK(backend.interests().next_deadline.has_value());
    backend.close();
LT_END_AUTO_TEST(wake_published_during_dispatch_survives_and_recursive_overlap_reject)

LT_BEGIN_AUTO_TEST(external_adapter_suite, release_during_completion_defers_close_and_cannot_step_replacement)
    httpserver::inline_executor ex;
    hd::io_connection_owner owner(ex);
    hd::io_poll_backend backend(srv::loop_mode::external);
    backend.activate_external();
    auto pair = io_loopback::pair::make();
    auto replacement = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    LT_ASSERT(replacement.ok());
    const auto descriptor = pair.local();
    backend.adopt_connection(1, pair.detach_local());
    std::byte first_buffer[1]{}, old_buffer[1]{}, new_buffer[1]{};
    hd::read_operation first(owner, 1, first_buffer);
    hd::read_operation old(owner, 1, old_buffer);
    hd::read_operation fresh(owner, 1, new_buffer);
    first.submit(backend);
    old.submit(backend);
    io_contract::probe old_result, fresh_result;
    bool retained = false;
    auto continuation = [&]() -> task<void> {
        co_await std::move(first);
        backend.release_connection(1);
#if !defined(_WIN32)
        retained = ::fcntl(descriptor, F_GETFD) != -1;
#else
        int error = 0, length = sizeof(error);
        retained = ::getsockopt(descriptor, SOL_SOCKET, SO_ERROR,
                               reinterpret_cast<char*>(&error), &length) == 0;
#endif
        backend.adopt_connection(1, replacement.detach_local());
        fresh.submit(backend);
    };
    spawn(ex, continuation(), [](task_result<void>) { });
    spawn(ex, io_contract::await_into(std::move(old), &old_result), [](task_result<void>) { });
    io_loopback::write_all(pair.peer(), "a", 1);
    io_loopback::write_all(replacement.peer(), "n", 1);
    const auto old_snapshot = backend.interests();
    const auto event = event_for(old_snapshot.sockets.front());
    LT_CHECK(backend.dispatch(std::span(&event, 1), clock_type::now()).ok());
    LT_CHECK(retained);
#if !defined(_WIN32)
    LT_CHECK(::fcntl(descriptor, F_GETFD) == -1 && errno == EBADF);
#endif
    LT_CHECK_EQ(old_result.delivered.load(), 1);
    LT_CHECK(old_result.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(fresh_result.delivered.load(), 0);
    LT_CHECK(new_buffer[0] == std::byte{0});
    spawn(ex, io_contract::await_into(std::move(fresh), &fresh_result), [](task_result<void>) { });
    const auto fresh_event = event_for(backend.interests().sockets.front());
    LT_CHECK(backend.dispatch(std::span(&fresh_event, 1), clock_type::now()).ok());
    LT_CHECK_EQ(fresh_result.delivered.load(), 1);
    LT_CHECK(new_buffer[0] == std::byte{'n'});
    backend.close();
LT_END_AUTO_TEST(release_during_completion_defers_close_and_cannot_step_replacement)

LT_BEGIN_AUTO_TEST(external_adapter_suite, hard_wake_failure_stops_and_unwinds_pending_work)
    fixture fx;
    const auto snapshot = fx.backend.interests();
    LT_CHECK(fx.dispatch(event_for(*snapshot.wake)).ok());
    auto& source = fx.backend.*member(wake_tag{});
    pollsys::close_socket(source.*member(write_end_tag{}));
    io_contract::probe pending;
    fx.submit(hd::timer_operation(fx.rig.owner, 0, clock_type::now() + 1s), pending);
    LT_CHECK(!fx.backend.ready().ok());
    LT_CHECK_EQ(pending.delivered.load(), 1);
    LT_CHECK(pending.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK_EQ(fx.backend.pending_count(), std::size_t{0});
    LT_CHECK(!fx.backend.interests().wake);
    fx.backend.close(); fx.rig.ex.run_pending();
    LT_CHECK_EQ(pending.delivered.load(), 1);
LT_END_AUTO_TEST(hard_wake_failure_stops_and_unwinds_pending_work)

LT_BEGIN_AUTO_TEST(external_adapter_suite, decreasing_clock_rejects_a_valid_socket_event_before_io)
    fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    fx.backend.adopt_connection(1, pair.detach_local());
    io_contract::probe read;
    fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), read);
    const auto event = event_for(fx.backend.interests().sockets.front());
    const auto now = clock_type::now();
    LT_CHECK(fx.backend.dispatch({}, now).ok());
    io_loopback::write_all(pair.peer(), "t", 1);
    LT_CHECK(fx.dispatch(event, now - 1ms).code() == hh::outcome_code::invalid_state);
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 0);
    LT_CHECK(fx.rig.buffer[0] == std::byte{0});
    LT_CHECK(fx.dispatch(event, now).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 1);
    LT_CHECK(fx.rig.buffer[0] == std::byte{'t'});
LT_END_AUTO_TEST(decreasing_clock_rejects_a_valid_socket_event_before_io)

LT_BEGIN_AUTO_TEST(external_adapter_suite, exhausted_identity_never_wraps_and_closes_unregistered_socket)
    fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    const auto socket = pair.detach_local();
    (fx.backend.*member(identity_tag{})) = std::numeric_limits<std::uint64_t>::max();
    bool refused = false;
    try {
        fx.backend.adopt_connection(1, socket);
    } catch (const std::overflow_error&) {
        refused = true;
    }
    LT_CHECK(refused);
    LT_CHECK(fx.backend.interests().sockets.empty());
#if !defined(_WIN32)
    LT_CHECK(::fcntl(socket, F_GETFD) == -1 && errno == EBADF);
#else
    int error = 0, length = sizeof(error);
    LT_CHECK(::getsockopt(socket, SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char*>(&error), &length) != 0);
#endif
LT_END_AUTO_TEST(exhausted_identity_never_wraps_and_closes_unregistered_socket)

LT_BEGIN_AUTO_TEST(external_adapter_suite, buffered_read_precedes_combined_close_error_and_duplicate_callback)
    fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    fx.backend.adopt_connection(1, pair.detach_local());
    io_contract::probe bytes, closed;
    std::byte other_buffer[16]{};
    fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), bytes);
    fx.submit(hd::read_operation(fx.rig.owner, 1, other_buffer), closed);
    auto event = event_for(fx.backend.interests().sockets.front());
    event.closed = true;
    event.error = true;
    io_loopback::write_all(pair.peer(), "b", 1);
    pair.close_peer();
    LT_CHECK(fx.dispatch(event).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(bytes.delivered.load(), 1);
    LT_CHECK(bytes.observed.code == hh::outcome_code::ok);
    LT_CHECK_EQ(bytes.observed.transferred, std::size_t{1});
    LT_CHECK(fx.rig.buffer[0] == std::byte{'b'});
    LT_CHECK_EQ(closed.delivered.load(), 1);
    LT_CHECK(closed.observed.code == hh::outcome_code::connection_closed);
    LT_CHECK(fx.backend.interests().sockets.empty());
    LT_CHECK(fx.dispatch(event).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(bytes.delivered.load(), 1);
    LT_CHECK_EQ(closed.delivered.load(), 1);
LT_END_AUTO_TEST(buffered_read_precedes_combined_close_error_and_duplicate_callback)

#if !defined(_WIN32)
LT_BEGIN_AUTO_TEST(external_adapter_suite, descriptor_zero_is_a_valid_readiness_registration)
    struct restore_stdin {
        int saved = ::dup(STDIN_FILENO);
        ~restore_stdin() {
            if (saved < 0) return;
            ::dup2(saved, STDIN_FILENO);
            ::close(saved);
        }
    } restore;
    fixture fx;
    auto pair = io_loopback::pair::make();
    LT_ASSERT(pair.ok());
    auto socket = pair.detach_local();
    LT_ASSERT(::dup2(socket, STDIN_FILENO) == STDIN_FILENO);
    pollsys::close_socket(socket);
    fx.backend.adopt_connection(1, STDIN_FILENO);
    io_contract::probe read;
    fx.submit(hd::read_operation(fx.rig.owner, 1, fx.rig.buffer), read);
    const auto snapshot = fx.backend.interests();
    LT_ASSERT(snapshot.sockets.size() == 1);
    LT_CHECK(snapshot.sockets.front().handle.valid);
    LT_CHECK_EQ(snapshot.sockets.front().handle.value, std::uintptr_t{0});
    LT_CHECK(snapshot.sockets.front().handle.kind == srv::native_handle_kind::posix_descriptor);
    io_loopback::write_all(pair.peer(), "0", 1);
    LT_CHECK(fx.dispatch(event_for(snapshot.sockets.front())).ok());
    fx.rig.ex.run_pending();
    LT_CHECK_EQ(read.delivered.load(), 1);
    LT_CHECK(fx.rig.buffer[0] == std::byte{'0'});
LT_END_AUTO_TEST(descriptor_zero_is_a_valid_readiness_registration)
#endif

LT_BEGIN_AUTO_TEST(external_adapter_suite, complete_snapshots_prune_retired_connection_records)
    fixture fx;
    for (std::uint64_t id = 1; id <= 16; ++id) {
        auto pair = io_loopback::pair::make();
        LT_ASSERT(pair.ok());
        fx.backend.adopt_connection(id, pair.detach_local());
        fx.backend.release_connection(id);
        LT_CHECK(fx.backend.interests().sockets.empty());
        LT_CHECK_EQ((fx.backend.*member(connections_tag{})).size(), std::size_t{0});
    }
LT_END_AUTO_TEST(complete_snapshots_prune_retired_connection_records)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
