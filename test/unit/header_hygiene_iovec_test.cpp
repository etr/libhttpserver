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

// Header-hygiene sentinel for TASK-004:
//
// AC #4 of TASK-004 ("public header must not include <sys/uio.h>") is
// scoped to the new iovec_entry header itself; the broader umbrella-leak
// concern (current umbrella transitively pulls <sys/uio.h> via gnutls/
// <sys/socket.h>) is the remit of TASK-007's header-hygiene CI gate.
//
// To enforce the local guarantee, this TU declares a colliding
// `struct iovec` BEFORE including iovec_entry.hpp directly. If the
// header (or anything it pulls in) pulls <sys/uio.h>, the system
// definition collides with this sentinel and the build fails with a
// redefinition error. The TU compiling at all is the assertion.
struct iovec {
    int libhttpserver_hygiene_sentinel;
};

// Include the new POD header in isolation to verify it pulls no
// surprise dependencies. HTTPSERVER_COMPILATION is already defined by
// AM_CPPFLAGS in test/Makefile.am, so the gate is satisfied.
#include "httpserver/iovec_entry.hpp"

#include "./littletest.hpp"

LT_BEGIN_SUITE(header_hygiene_iovec_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(header_hygiene_iovec_suite)

LT_BEGIN_AUTO_TEST(header_hygiene_iovec_suite, iovec_entry_visible_without_sys_uio)
    httpserver::iovec_entry e{nullptr, 0};
    LT_CHECK_EQ(e.base, nullptr);
    LT_CHECK_EQ(e.len, 0u);
LT_END_AUTO_TEST(iovec_entry_visible_without_sys_uio)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
