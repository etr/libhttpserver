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

// Header-hygiene sentinel for TASK-045:
//
// PRD-HOOK-REQ-007 / PRD-HDR-REQ-001: the four new hook bus public
// headers (hook_phase.hpp, hook_action.hpp, hook_handle.hpp,
// hook_context.hpp) must not leak backend headers (<microhttpd.h>,
// <gnutls/gnutls.h>, <sys/socket.h>, <sys/uio.h>) into a consumer
// translation unit. Mirrors test/unit/header_hygiene_iovec_test.cpp —
// the same per-header (not umbrella-level) scoping pattern from
// the umbrella_transitive_leaks memory.
//
// HTTPSERVER_COMPILATION is defined by AM_CPPFLAGS in test/Makefile.am
// so the inclusion guard in each hook header is satisfied.

#include "httpserver/hook_phase.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_context.hpp"

// --- preprocessor-based leak detection ------------------------------------

#ifdef MHD_VERSION
#  error "<microhttpd.h> was pulled in transitively by a hook public header (guard MHD_VERSION)"
#endif

#ifdef GNUTLS_GNUTLS_H
#  error "<gnutls/gnutls.h> was pulled in transitively by a hook public header (guard GNUTLS_GNUTLS_H)"
#endif

#ifdef _SYS_SOCKET_H
#  error "<sys/socket.h> was pulled in transitively by a hook public header (glibc/musl guard _SYS_SOCKET_H)"
#endif

#ifdef _SYS_SOCKET_H_
#  error "<sys/socket.h> was pulled in transitively by a hook public header (macOS/BSD guard _SYS_SOCKET_H_)"
#endif

#ifdef _SYS_UIO_H
#  error "<sys/uio.h> was pulled in transitively by a hook public header (glibc/musl guard _SYS_UIO_H)"
#endif

#ifdef _SYS_UIO_H_
#  error "<sys/uio.h> was pulled in transitively by a hook public header (macOS/BSD guard _SYS_UIO_H_)"
#endif

// --------------------------------------------------------------------------

#include "./littletest.hpp"

LT_BEGIN_SUITE(header_hygiene_hooks_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(header_hygiene_hooks_suite)

LT_BEGIN_AUTO_TEST(header_hygiene_hooks_suite, hook_headers_visible_without_backend)
    static_assert(static_cast<std::size_t>(httpserver::hook_phase::count_) == 11u,
                  "hook_phase::count_ must be 11");
    LT_CHECK_EQ(true, true);  // TU compiled clean: no backend leak detected
LT_END_AUTO_TEST(hook_headers_visible_without_backend)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
