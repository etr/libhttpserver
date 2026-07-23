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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// MHD daemon handle + start/stop threading state, plus the option-array
// and start-flag builders that construct the daemon. Internal header;
// only reachable when compiling libhttpserver translation units. NOT part
// of the installed surface.
#if !defined(HTTPSERVER_COMPILATION)
#error "daemon_lifecycle.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_DAEMON_LIFECYCLE_HPP_
#define SRC_HTTPSERVER_DETAIL_DAEMON_LIFECYCLE_HPP_

#include <microhttpd.h>
#include <pthread.h>

#include <atomic>
#include <vector>

namespace httpserver {

class webserver;

namespace detail {

class webserver_impl;

// daemon_lifecycle -- owns the libmicrohttpd daemon handle and the
// start/stop synchronization primitives, and knows how to construct the
// daemon (the MHD option-array + start-flag builders).
//
// State ownership: the atomic `daemon` handle, the caller-supplied
// pre-bound `bind_socket`, the blocking-start mutex/cond pair, and the
// atomic `running` flag. The pthread primitives are initialised in the
// constructor and destroyed in the destructor (RAII), so webserver_impl
// no longer manages them by hand.
//
// The option-array / flag builders read the const config bag and register
// the dispatch trampolines. They reach both through a back-pointer to the
// owning webserver_impl (`owner_`): owner_->parent is the webserver* whose
// config they read and which is the closure pointer for the MHD callbacks;
// owner_->ws_ tells compose_runtime_flags whether to request
// MHD_ALLOW_UPGRADE. This is the one collaborator that legitimately needs
// broad config access, hence the back-pointer (the other collaborators
// take per-call arguments instead).
//
// Members are public: webserver's start/stop/is_running/get_bound_port/
// run/... methods (webserver_lifecycle.cpp) poke the handle + flags
// directly, the same posture as webserver_impl / route_table / hook_bus.
class daemon_lifecycle {
 public:
    // @p bind_socket_val is the caller-supplied pre-bound socket from
    // create_webserver().bind_socket(), or MHD_INVALID_SOCKET if none was
    // provided. @p owner is the owning webserver_impl (never null; the
    // builders reach parent config + the ws registry through it).
    explicit daemon_lifecycle(webserver_impl* owner,
                              MHD_socket bind_socket_val = MHD_INVALID_SOCKET);
    ~daemon_lifecycle();
    daemon_lifecycle(const daemon_lifecycle&) = delete;
    daemon_lifecycle& operator=(const daemon_lifecycle&) = delete;
    daemon_lifecycle(daemon_lifecycle&&) = delete;
    daemon_lifecycle& operator=(daemon_lifecycle&&) = delete;

    // Assemble the full MHD_OptionItem array (base + TLS + GnuTLS +
    // extended + HTTPS-extra options, terminated with MHD_OPTION_END).
    void build_mhd_option_array(std::vector<MHD_OptionItem>& iov) const;
    void add_base_mhd_options(std::vector<MHD_OptionItem>& iov) const;
    void add_tls_mhd_options(std::vector<MHD_OptionItem>& iov) const;
    void add_gnutls_mhd_options(std::vector<MHD_OptionItem>& iov) const;
    void add_extended_mhd_options(std::vector<MHD_OptionItem>& iov) const;
    void add_https_extra_options(std::vector<MHD_OptionItem>& iov) const;

    // Compose the daemon start-flag bitmask (start method + transport +
    // runtime flags), reading the const config bag on owner_->parent.
    int compose_start_flags() const;
    int compose_transport_flags() const;
    int compose_runtime_flags() const;

    // --- Daemon handle + start/stop threading state ----------------------
    // Atomic so start() publishes the daemon pointer (and the immutable
    // MHD daemon struct it points at, including the ephemeral bind port set
    // before publication) with release semantics, and get_bound_port() et al.
    // read it with acquire. This lets the ephemeral port be read safely from
    // another thread while a blocking start() runs on a worker thread; fixes
    // a TSan-flagged data race in the ws_start_stop integ test.
    std::atomic<struct MHD_Daemon*> daemon{nullptr};

    // Acquire-load the published daemon handle, or nullptr when the daemon
    // is not currently started. Single home for the acquire-semantics
    // contract documented on `daemon` above, so webserver's run/info
    // accessors (webserver_lifecycle.cpp) don't each repeat the load + the
    // memory_order argument.
    struct MHD_Daemon* handle() const noexcept {
        return daemon.load(std::memory_order_acquire);
    }

    // MHD_socket (int on POSIX, SOCKET on Windows) for a caller-supplied
    // pre-bound socket passed via create_webserver().bind_socket().
    // MHD_INVALID_SOCKET is the sentinel meaning "no pre-bound socket".
    MHD_socket bind_socket = MHD_INVALID_SOCKET;

    pthread_mutex_t mutexwait;
    pthread_cond_t  mutexcond;

    // Atomic to allow lock-free reads in stop()/is_running() concurrent
    // with the mutex-guarded writes in start()/stop(). TSan-flagged in the
    // ws_start_stop integ test (start on worker thread, stop on main).
    std::atomic<bool> running{false};

 private:
    // Back-pointer to the owning webserver_impl. Used by the builders to
    // reach owner_->parent (webserver*, for config + callback cls) and
    // owner_->ws_ (registry, for the MHD_ALLOW_UPGRADE decision). Never
    // dereferenced during construction.
    webserver_impl* owner_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_DAEMON_LIFECYCLE_HPP_
