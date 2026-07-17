/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

// webserver_runtime.hpp -- event-loop, connection-management, and
// feature-reporting surface of class webserver.
//
// This header carries member DECLARATIONS (and a few inline getters)
// only; it is meant to be included from WITHIN the body of `class
// webserver` defined in httpserver/webserver.hpp. Including it in any
// other context produces a compile error (see the inner gate below).
//
// Keeping this surface in its own file lets the webserver.hpp class
// definition stay under the project-wide per-file line-count ceiling
// (FILE_LOC_MAX in scripts/check-file-size.sh) without splitting the
// public class across translation units.
#ifndef SRC_HTTPSERVER_WEBSERVER_RUNTIME_HPP_
#define SRC_HTTPSERVER_WEBSERVER_RUNTIME_HPP_

#ifndef SRC_HTTPSERVER_WEBSERVER_HPP_INSIDE_CLASS_
#error "httpserver/webserver_runtime.hpp must be included from inside the webserver class body in <httpserver/webserver.hpp>."
#endif

     /**
      * Stop the webserver and wait for in-flight handlers to complete
      * before returning.
      *
      * The wait guarantee is provided by @c MHD_stop_daemon(), which is a
      * blocking call that drains all active connections and joins
      * libmicrohttpd's worker threads before returning.  @c stop() calls
      * @c MHD_stop_daemon() internally, and this wrapper delegates to it,
      * so the two entry-points are behaviourally equivalent today.
      * @c stop_and_wait() exists as a semantically richer named entry-point;
      * any future quiesce or application-level waiting logic should be added
      * here rather than in @c stop().
      *
      * @see stop()
     **/
     void stop_and_wait();

     /**
      * Run the webserver's event loop once (non-blocking).
      * For use with external event loops when the server is started
      * without internal threading.
      * @return true on success, false on error
      * @note Handler exceptions are caught on the dispatch path and
      *       routed through `internal_error_handler`; no exception
      *       propagates out of this call.
      * @see webserver (handler error-propagation contract)
     **/
     bool run();

     /**
      * Run the webserver's event loop, blocking until there is activity
      * or the timeout expires.
      * @param millisec timeout in milliseconds (-1 for indefinite)
      * @return true on success, false on error
      * @note Handler exceptions are caught on the dispatch path and
      *       routed through `internal_error_handler`; no exception
      *       propagates out of this call.
      * @see webserver (handler error-propagation contract)
     **/
     bool run_wait(int32_t millisec);

     /**
      * Get the file descriptor sets for select()-based event loop integration.
      * `fd_set` is a typedef (anonymous-struct in glibc), so the public header
      * includes the platform header that defines it rather than relying on a
      * forward declaration. The typed parameters restore compile-time type
      * safety: the compiler rejects non-fd_set* arguments that the previous
      * void* signature silently accepted (CWE-704).
      * @param read_fd_set set of FDs to watch for reading
      * @param write_fd_set set of FDs to watch for writing
      * @param except_fd_set set of FDs to watch for exceptions
      * @param max_fd highest FD number set in any of the sets
      * @return true on success, false on error
     **/
     bool get_fdset(fd_set* read_fd_set, fd_set* write_fd_set,
                    fd_set* except_fd_set, int* max_fd);

     /**
      * Get the timeout until the next MHD action is needed.
      * @param timeout output: timeout in milliseconds
      * @return true if a timeout was set, false if no timeout is needed
     **/
     bool get_timeout(uint64_t* timeout);

     /**
      * Add an externally-accepted socket connection.
      * `addrlen` is typed as `unsigned int` rather than `socklen_t` so
      * this header does not have to include the BSD-socket header.
      * `socklen_t` is a 32-bit integer on every supported platform
      * (unsigned on POSIX, signed `int` on Windows winsock2); `unsigned
      * int` is at least as wide as either, so the conversion in the
      * implementation is value-preserving for any realistic `sockaddr`
      * length. The implementation passes the value directly to
      * MHD_add_connection.
      * @param client_socket the accepted client socket
      * @param addr the client address (forward-declared `struct sockaddr*`)
      * @param addrlen length of the address (in bytes)
      * @return true on success, false on error
     **/
     bool add_connection(int client_socket, const struct sockaddr* addr, unsigned int addrlen);

     /**
      * Quiesce the daemon: stop accepting new connections while letting
      * in-flight requests complete.
      * @return the listen socket FD (caller can close it), or -1 on error
     **/
     int quiesce();

     /**
      * Get the listen socket file descriptor.
      * @return the listen FD, or -1 if not available
     **/
     int get_listen_fd() const;

     /**
      * Get the number of currently active connections.
      * @return active connection count
     **/
     unsigned int get_active_connections() const;

     /**
      * Get the actual port the daemon is bound to.
      * Useful when port 0 was specified to let the OS choose.
      * @return the bound port, or 0 if not available
     **/
     uint16_t get_bound_port() const;

     /**
      * Reports build-time feature availability.
      *
      * The four boolean fields of the returned struct reflect the
      * HAVE_BAUTH / HAVE_DAUTH / HAVE_GNUTLS / HAVE_WEBSOCKET macros at
      * the time libhttpserver was compiled. Use this at runtime to
      * decide whether to register a feature-dependent handler or to
      * surface the configuration to the operator.
      *
      * The values are determined at library build time (not consumer
      * build time): linking against a TLS-disabled libhttpserver always
      * reports `tls == false`, even when the consumer TU was compiled
      * with HAVE_GNUTLS defined.
      *
      * Safe to call before start() and from any thread; never throws.
      **/
     struct features {
         bool basic_auth;
         bool digest_auth;
         bool tls;
         bool websocket;
     };
     static features features() noexcept;

#endif  // SRC_HTTPSERVER_WEBSERVER_RUNTIME_HPP_
