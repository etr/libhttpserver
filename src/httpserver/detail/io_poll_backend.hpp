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

// poll / WSAPoll socket backend of the v3 native engine (architecture
// §3.4, DR-V3-004, TASK-100): the readiness fallback and test oracle
// that later readiness engines (epoll / kqueue / IOCP) must agree with.
// Implements the io_backend seam from io_operation.hpp on one dedicated
// driver thread.
//
// Observable semantics are fake_io_backend's, extended with sockets:
// every submitted operation ends with exactly one terminal result --
// ok (read/write/accept completed, timer expiry, wake), cancelled
// (cancel won the claim), or connection_closed (peer hangup, backend
// close, submit after close). Completion always goes through
// op_state::claim_terminal() and io_connection_owner::enqueue(), so the
// exactly-once and per-connection serialization guarantees of TASK-099
// hold unchanged.
//
// Threading: submit / request_cancel / wake / close / adopt / release
// are thread-safe. The registry mutex never crosses a stream syscall or
// owner enqueue. A shared registration lease keeps the native socket open
// through detached dispatch; retirement blocks later steps and stale rearm.
// Notifications coalesce under the registry mutex with acknowledgement, so
// external mode requires no idle polling fallback. Managed mode retains its
// dedicated polling thread and bounded idle timeout.
//
// Readiness dispatch drains each signalled direction to would-block and
// re-arms the rest (§3.4): reads complete {ok, transferred} or, on
// EOF/reset with nothing drained, {connection_closed} for every pending
// op of that connection; writes complete {ok, transferred} even on a
// partial send (transferred is the backpressure semantic; the caller
// re-arms); accept registers the accepted socket under a fabricated id
// retrievable via native_handle(). POLLERR / POLLNVAL are treated as
// hangup (use-after-close defense).
#if !defined(HTTPSERVER_COMPILATION)
#error "io_poll_backend.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_IO_POLL_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_POLL_BACKEND_HPP_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <httpserver/detail/io_operation.hpp>
#include <httpserver/detail/io_poll_sys.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/readiness.hpp>

namespace httpserver {
namespace detail {

// Outcome of one per-op socket step inside the readiness dispatch:
// completed means the op reached its terminal result; pending_again
// means spurious readiness returned the op to the registry; hangup
// means the op (and the connection's other pending ops) are done.
enum class step_outcome {
    completed,
    pending_again,
    hangup,
};

// Idle poll cap in milliseconds: how long the driver blocks when it has
// no nearer timer deadline. One wake-less iteration per second is the
// deliberate price for making a lost wake byte impossible to turn into
// a hang.
constexpr int k_poll_idle_cap_ms = 1000;

// Monotonic deadline -> poll timeout in milliseconds. Pure helper,
// unit-tested in io_backend_contract_test.cpp: no deadline returns the
// idle cap; a due deadline returns 0; otherwise the ceil-to-millisecond
// gap clamped to [0, k_poll_idle_cap_ms].
inline int poll_timeout_ms(
    std::chrono::steady_clock::time_point now,
    const std::optional<std::chrono::steady_clock::time_point>&
        next_deadline) {
    if (!next_deadline.has_value()) {
        return k_poll_idle_cap_ms;
    }
    if (*next_deadline <= now) {
        return 0;
    }
    const std::chrono::milliseconds gap = std::chrono::duration_cast<
        std::chrono::milliseconds>(
        std::chrono::ceil<std::chrono::milliseconds>(*next_deadline
                                                     - now));
    if (gap.count() >= k_poll_idle_cap_ms) {
        return k_poll_idle_cap_ms;
    }
    return static_cast<int>(gap.count());
}

class io_poll_backend final : public io_backend, public server::readiness_driver {
 public:
    // Managed mode starts the driver thread; external mode is host-driven.
    explicit io_poll_backend(server::loop_mode mode = server::loop_mode::managed);

    // External mode has no polling thread. Activation occurs after listen.
    http::outcome ready() const;
    void activate_external();
    server::interest_snapshot interests() const override;
    http::outcome dispatch(std::span<const server::readiness_event> events,
                           std::chrono::steady_clock::time_point now) override;

    // close() + stop + join + closes every still-adopted socket.
    ~io_poll_backend() override;

    io_poll_backend(const io_poll_backend&) = delete;
    io_poll_backend& operator=(const io_poll_backend&) = delete;

    // io_backend seam (identical semantics to fake_io_backend).
    void submit(op_state& op) override;
    http::outcome_code request_cancel(op_state& target) override;

    // ---- concrete surface (the registration the seam deliberately
    // omits; TASK-108 consumes) ----------------------------------------

    // Registers an existing socket under @p id. The backend takes
    // ownership: the socket must be nonblocking (pollsys
    // ::set_nonblocking) and nobody else may close it. Adopting a live
    // id is a std::logic_error; re-adopting a released id is fine.
    void adopt_connection(std::uint64_t id,
                          pollsys::native_socket_t socket);

    // Same, flagged as a listener (accept ops poll for readability).
    void adopt_listener(std::uint64_t id, pollsys::native_socket_t socket);

    // The socket registered under @p id, or k_invalid_socket. Does not
    // transfer ownership: release_connection (or destruction) closes.
    pollsys::native_socket_t native_handle(std::uint64_t id) const;

    // Terminal-fails every pending op of the connection with
    // connection_closed, closes and forgets its socket, and rejects
    // further submits for that id. Unknown ids are a no-op.
    void release_connection(std::uint64_t id);

    // Completes every pending wake op with {ok} (fake fire_wake
    // parity). Returns how many fired.
    std::size_t wake();

    // Terminal teardown: claims every pending op with connection_closed
    // and enqueues each to its owner. Later submits complete
    // immediately; a second close returns 0. Does not close adopted
    // sockets: ops and connections have separate teardown surfaces.
    std::size_t close();

    // Operations registered but not yet terminal.
    std::size_t pending_count() const;

    // Completed poll() calls -- the busy-loop detector the contract
    // tests assert against (a healthy idle driver iterates ~once per
    // second at most).
    std::uint64_t poll_iterations() const;

 private:
    struct registration_lifetime {
        explicit registration_lifetime(pollsys::native_socket_t handle,
                                        server::socket_key identity)
            : socket(handle), key(identity) { }
        ~registration_lifetime() { pollsys::close_socket(socket); }
        pollsys::native_socket_t socket;
        server::socket_key key;
        server::registration_generation generation = 1;
        std::atomic_bool retired{false};
        bool published = false;  // guarded by mu_
    };

    struct connection_record {
        pollsys::native_socket_t socket = pollsys::k_invalid_socket;
        bool listener = false;
        bool dead = false;  // hangup/released: submits reject
        std::shared_ptr<registration_lifetime> lifetime;
    };

    void adopt_socket(std::uint64_t id, pollsys::native_socket_t socket,
                      bool listener);
    void run_loop();
    // Rebuilds the pollfd projection from the registries (wake slot
    // plus one entry per live connection with pending fd work); dead
    // records without pending ops are pruned. Returns the earliest
    // pending timer deadline, if any.
    std::optional<std::chrono::steady_clock::time_point> build_projection(
        std::vector<pollsys::poll_slot>& fds, std::vector<std::uint64_t>& ids);
    std::optional<std::chrono::steady_clock::time_point> scan_interest_locked(
        std::unordered_map<std::uint64_t, pollsys::event_mask>& interest) const;
    void project_connections_locked(
        const std::unordered_map<std::uint64_t, pollsys::event_mask>&
            interest,
        std::vector<pollsys::poll_slot>& fds,
        std::vector<std::uint64_t>& ids);
    // Routes one iteration's readiness bits to the per-direction
    // dispatchers (driver thread, no locks held).
    void dispatch_revents(const std::vector<pollsys::poll_slot>& fds,
                          const std::vector<std::uint64_t>& ids);
    void dispatch_readable(std::uint64_t id);
    void dispatch_writable(std::uint64_t id);
    // Runs one direction's batch against its socket: sorted by
    // sequence, one syscall per op until would-block or hangup.
    void dispatch_batch(std::uint64_t id,
                        std::vector<std::shared_ptr<op_state>>& batch,
                        const std::shared_ptr<registration_lifetime>& lease = {});
    // One syscall for one op; completes it (or returns pending_again
    // for spurious readiness with nothing drained).
    step_outcome accept_step(pollsys::native_socket_t socket,
                             const std::shared_ptr<op_state>& state);
    step_outcome read_step(pollsys::native_socket_t socket,
                           const std::shared_ptr<op_state>& state);
    step_outcome write_step(pollsys::native_socket_t socket,
                            const std::shared_ptr<op_state>& state);
    // Takes every pending op of @p id in the requested direction out of
    // the registry. Caller holds mu_.
    void take_direction_locked(std::uint64_t id, bool reads_and_accepts,
                               std::vector<std::shared_ptr<op_state>>& batch);
    // Completes batch[from..] with @p result.
    void finish_batch_from(const std::vector<std::shared_ptr<op_state>>&
                               batch,
                           std::size_t from, io_result result);
    // Puts a batch whose head would-block back into the registry
    // (spurious readiness); ops raced by close() complete
    // connection_closed instead.
    void rearm_after_would_block(
        const std::vector<std::shared_ptr<op_state>>& batch,
        std::size_t from);
    // Marks the connection dead and completes every pending op on it
    // with connection_closed.
    void hangup_connection(std::uint64_t id,
        const std::shared_ptr<registration_lifetime>& expected = {});
    // Registers an accepted socket under a fresh fabricated id.
    std::uint64_t register_accepted_socket(pollsys::native_socket_t socket,
        const std::shared_ptr<op_state>& accepting);
    std::shared_ptr<registration_lifetime> make_lifetime_locked(
        pollsys::native_socket_t socket);
    void register_pending_locked(const std::shared_ptr<op_state>& state);
    step_outcome socket_step(pollsys::native_socket_t socket,
                             const std::shared_ptr<op_state>& state);
    std::pair<std::uint64_t, std::shared_ptr<registration_lifetime>>
        find_registration_locked(const server::readiness_event& event) const;
    http::outcome begin_dispatch(std::span<const server::readiness_event> events,
                                 std::chrono::steady_clock::time_point now);
    void notify();
    void acknowledge_wake();  // caller holds mu_; bounded coalesced drain
    void dispatch_event(const server::readiness_event& event);
    void forget_binding(const std::shared_ptr<op_state>& state);
    bool unavailable_locked(const op_state& state) const;
    // Fake-parity primitives.
    bool try_cancel(const std::shared_ptr<op_state>& target);
    void expire_due_timers(std::chrono::steady_clock::time_point now);

    mutable std::mutex mu_;
    std::unordered_map<op_state*, std::shared_ptr<op_state>> pending_;
    mutable std::unordered_map<std::uint64_t, connection_record> connections_;
    std::unordered_map<op_state*, std::weak_ptr<registration_lifetime>> bindings_;
    std::uint64_t next_identity_ = 2;  // 1 is reserved for the wake source
    server::loop_mode mode_;
    bool active_ = false;
    bool wake_pending_ = false;
    bool wake_failed_ = false;
    std::atomic_bool dispatching_{false};
    std::optional<std::chrono::steady_clock::time_point> last_now_;
    std::uint64_t next_sequence_ = 1;        // guarded by mu_
    std::uint64_t next_connection_id_ = 1;   // guarded by mu_
    bool closed_ = false;                    // guarded by mu_
    std::uint64_t poll_iterations_ = 0;      // guarded by mu_
    pollsys::wake_source wake_;
    std::atomic_bool stop_{false};
    std::thread thread_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_IO_POLL_BACKEND_HPP_
