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

// Platform shims for the poll / WSAPoll socket backend (TASK-100,
// architecture §3.4, DR-V3-004). This header and io_wake_source.cpp hold
// the driver's platform divergence: socket handle type, poll
// entry shape, readiness bits, nonblocking toggles, stream read/write/
// accept with a unified result vocabulary, loopback pair/listener
// construction, and the wake source. The driver logic that consumes
// these (io_poll_backend.cpp) is platform-neutral and POSIX-proven; the
// WSAPoll branch is verified by the existing Windows CI lanes, which
// run the same backend contract suite over `make check`.
//
// Platform dispatch: `#if defined(_WIN32)` selects the Winsock branch.
// This is the same compile-time pattern secure_zero.hpp uses. The MSYS
// (msys-runtime gcc) lane may not define _WIN32; if it mis-selects the
// POSIX branch there, the one-line follow-up is
// `#if defined(_WIN32) || defined(__MSYS__)`.
//
// WSAPoll quirks encoded here for the driver:
//   - no reliable POLLHUP; hangup is detected by recv()==0 and the
//     send-error family, which the dispatch treats as the
//     connection_closed outcome;
//   - readiness is POLLRDNORM / POLLWRNORM (vs POSIX POLLIN / POLLOUT);
//   - error codes map WSAEWOULDBLOCK -> would_block and the
//     WSAECONNRESET family -> closed_reset.
//
// Managed polling retains its 1000 ms idle cap when no deadline is nearer.
// External dispatch relies on the mutex-coordinated notification latch;
// it requires no idle timeout to recover missed wake notifications.

#if !defined(HTTPSERVER_COMPILATION)
#error "io_poll_sys.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_IO_POLL_SYS_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_POLL_SYS_HPP_

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include <httpserver/net/address.hpp>

#if defined(_WIN32)
#include <mutex>
#endif

namespace httpserver {
namespace detail {
namespace pollsys {

#if defined(_WIN32)
using native_socket_t = SOCKET;
constexpr native_socket_t k_invalid_socket = INVALID_SOCKET;
#else
using native_socket_t = int;
constexpr native_socket_t k_invalid_socket = -1;
#endif

// The poll entry shape, named uniformly for both platforms (both are
// aggregates with fd / events / revents fields).
#if defined(_WIN32)
using poll_slot = WSAPOLLFD;
#else
using poll_slot = pollfd;
#endif

// Readiness bits in the driver's vocabulary; event_mask matches the
// short-width events/revents fields of both poll entry shapes.
using event_mask = std::int16_t;
#if defined(_WIN32)
constexpr event_mask k_readable = POLLRDNORM;
constexpr event_mask k_writable = POLLWRNORM;
#else
constexpr event_mask k_readable = POLLIN;
constexpr event_mask k_writable = POLLOUT;
#endif
constexpr event_mask k_poll_error = POLLERR;
constexpr event_mask k_poll_hangup = POLLHUP;
constexpr event_mask k_poll_invalid = POLLNVAL;

// Unified syscall outcome. `closed_reset` covers graceful EOF
// (recv == 0) and the connection-reset family; `error` is anything
// else non-retryable. transferred is meaningful only with ok.
enum class sys_status { ok, would_block, closed_reset, error };

struct sys_result {
    sys_status status = sys_status::error;
    std::size_t transferred = 0;
};

// The only blocking point of the driver thread. Returns the raw poll
// result (-1 on error, e.g. EINTR; the caller re-arms).
int poll_call(poll_slot* slots, std::size_t count, int timeout_ms);

// ---- socket lifecycle -----------------------------------------------------

// A blocking AF_INET stream socket (unconnected).
native_socket_t open_stream();

// A blocking AF_INET6 stream socket (unconnected) -- the v6 twin of
// open_stream (TASK-119 e2e: a ::1 dial needs the matching family).
native_socket_t open_stream_v6();

// listen() backlog of every adopted server listener (the make_listener
// convention).
constexpr int k_listen_backlog = 16;

// Closes a socket and clears the handle; k_invalid_socket is a no-op
// (idempotent teardown).
bool close_socket(native_socket_t& socket);

// Toggles the nonblocking mode. The driver contract requires every
// adopted socket to be nonblocking before use.
bool set_nonblocking(native_socket_t socket, bool nonblocking);

// Best-effort stream tuning applied to every adopted / accepted socket:
// TCP_NODELAY for latency (a no-op failure on non-TCP handles such as
// AF_UNIX pairs) and, where the platform delivers SIGPIPE for socket
// sends, SO_NOSIGPIPE (the MSG_NOSIGNAL flag covers the send sites
// directly where that flag exists; this option covers the macOS libc,
// which has neither).
void prepare_stream_socket(native_socket_t socket);

#if defined(_WIN32)
// Process-wide Winsock lifecycle. Refcounted so construction order
// (any number of wake sources / loopback pairs in any order) is safe;
// every ensure has one matching release at handle teardown, except the
// loopback pair, which deliberately pins one reference for the process
// lifetime (no teardown site exists for a raw pair, and a leaked
// WSAStartup reference is reclaimed at process exit).
std::mutex& winsock_mutex();

int& winsock_refcount();

bool ensure_winsock();

void release_winsock();
#endif

// ---- stream syscalls ------------------------------------------------------

#if !defined(_WIN32)
// Shared POSIX errno -> status mapping after a stream syscall. EPIPE
// and ENOBUFS are classified for the send path; recv never reports
// them, so the shared table is honest for both directions.
sys_result posix_status(ssize_t seen);
#endif  // !defined(_WIN32)

#if defined(_WIN32)
// Shared WSA error -> status mapping after a stream syscall.
sys_result wsa_error_status(int error);
#endif  // defined(_WIN32)

// recv() into @p data; empty sizes complete immediately with 0 bytes.
sys_result read_some(native_socket_t socket, std::byte* data,
                            std::size_t size);

// send() the leading slice; the caller re-arms from the transferred
// count (partial sends are the backpressure semantic). Sizes beyond
// INT_MAX are truncated by the Winsock int parameter on the _WIN32
// branch; HTTP operation buffers are bounded far below that.
sys_result write_some(native_socket_t socket, const std::byte* data,
                             std::size_t size);

// accept() one connection; the accepted socket comes back nonblocking
// and prepared. Only would_block is non-terminal for the listener.
// When @p peer is non-null it receives the accepted transport's peer
// snapshot (network-order bytes, host-order port; a v4-mapped v6
// address normalizes to family ipv4; any other family reports
// unspec). TASK-119: the v2 engine discarded this address at accept;
// the v3 peer policy consults it.

// The accept-time peer capture (TASK-119): the family dispatch and
// the host-order port conversion stay here; the address bytes decode
// through the ONE shared rule -- net::detail::address_from_bytes, the
// same decode the text parser uses -- so a v4-mapped v6 arrival
// normalizes to family ipv4 exactly like its parsed spelling, and a
// genuine IPv6 address that merely carries 0xffff at bytes[10..11]
// stays ipv6. Any other family reports unspec.
void fill_peer(const sockaddr_storage& storage,
                      net::peer_address& peer) noexcept;

sys_result accept_one(native_socket_t listener,
                             native_socket_t* out,
                             net::peer_address* peer = nullptr);

// ---- loopback plumbing (wake source + test harness) -----------------------

// Shared failure cleanup for make_listener (no-op close is fine for a
// fresh handle that never opened).
native_socket_t listener_failure(native_socket_t socket);

// Fills @p storage for @p address:@p per the listener address spellings:
// "" and "*" mean any local address (AF_INET INADDR_ANY); otherwise the
// address must be a numeric IPv4 or IPv6 literal (name resolution is the
// caller's job, mirroring server_options validation). False when the
// spelling is neither. One half of the open_listener decomposition (the
// complexity gate split the family dispatch out of the driver entry).
bool fill_listener_address(std::string_view address,
                                  std::uint16_t port,
                                  sockaddr_storage& storage);

// bind() + listen() on an open listener handle; false on either
// failure. The sockaddr size follows the stored family.
bool bind_listener(native_socket_t socket,
                          const sockaddr_storage& storage);

// The bound host-order port of a listening handle, read through
// getsockname and the stored family.
std::uint16_t bound_listener_port(native_socket_t socket);

// A listening stream socket bound to @p address:@p port, left
// nonblocking, with SO_REUSEADDR set (a restarted server rebinds its
// configured port immediately). Address spellings per
// fill_listener_address; port 0 requests an ephemeral port and the
// resolved host-order port is always reported through @p bound_port.
// Any failure closes the handle and returns k_invalid_socket.
// TASK-108: the native_server listener engine opens every configured
// endpoint through this one helper -- the only address-parsing
// divergence the driver layer carries.
native_socket_t open_listener(std::string_view address,
                                     std::uint16_t port,
                                     std::uint16_t& bound_port);

// A listening stream socket bound to the loopback address with an
// ephemeral port (reported host-order via @p port), left nonblocking.
// On Windows the socket pins one Winsock reference for the process
// lifetime (see the refcount note above).
native_socket_t make_listener(std::uint16_t& port);

// Blocking connect to 127.0.0.1:@p port. True on success.
bool connect_loopback(native_socket_t socket, std::uint16_t port);

// The IPv6 loopback twin (TASK-119 e2e: ::1-served endpoints). Same
// contract as connect_loopback.
bool connect_loopback_v6(native_socket_t socket,
                                std::uint16_t port);

// A connected bidirectional pair for local transfers: an AF_UNIX
// socket pair on POSIX, a 127.0.0.1 TCP pair on Windows (no socketpair
// exists there). Both ends come back blocking; callers choose modes. On
// Windows a successful pair pins one Winsock reference for the process
// lifetime (see the refcount note above).
bool make_loopback_pair(native_socket_t (&ends)[2]);

// ---- wake source ----------------------------------------------------------

// Local wake doorbell for managed and host-driven loops. The backend
// coalesces signal/acknowledge under its registry mutex; a full buffer
// already contains a notification. Construction failure leaves valid()
// false for the typed pre-bind gate. POSIX uses an AF_UNIX pair; Windows
// uses loopback TCP with TCP_NODELAY.
class wake_source {
 public:
    wake_source();

    wake_source(const wake_source&) = delete;
    wake_source& operator=(const wake_source&) = delete;

    ~wake_source();

    // The end registered in the driver's poll projection.
    native_socket_t read_handle() const noexcept { return read_end_; }

    bool valid() const noexcept {
        return read_end_ != k_invalid_socket && write_end_ != k_invalid_socket;
    }

    // Retry interruptions; only a full buffer proves notification is pending.
    // ENOBUFS/WSAEINPROGRESS are not evidence of a readable doorbell.
    bool signal() noexcept;

    // Coalesced external notifications contain at most one byte. No
    // producer may signal during this read (the registry mutex serializes it).
    bool acknowledge() noexcept;

    // Empties every queued token so the next signal interrupts poll.
    void drain() noexcept;

 private:
    native_socket_t read_end_ = k_invalid_socket;
    native_socket_t write_end_ = k_invalid_socket;
#if defined(_WIN32)
    bool winsock_held_ = false;
#endif
};

}  // namespace pollsys
}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_IO_POLL_SYS_HPP_
