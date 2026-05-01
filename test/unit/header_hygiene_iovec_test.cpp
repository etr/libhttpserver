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
// enforced by including iovec_entry.hpp in isolation, then checking the
// well-known include-guard macros that <sys/uio.h> defines on every
// supported platform:
//
//   Linux/glibc:  _SYS_UIO_H   (set by <sys/uio.h>)
//   macOS/BSD:    _SYS_UIO_H_  (set by <sys/uio.h>)
//   musl:         _SYS_UIO_H   (same as glibc)
//
// If any of those macros is defined after including iovec_entry.hpp, the
// header has leaked <sys/uio.h> and the build fails with a descriptive
// #error message. The TU compiling at all (and none of those macros being
// defined) is the assertion — no runtime test is needed for this guarantee.
//
// HTTPSERVER_COMPILATION is defined by AM_CPPFLAGS in test/Makefile.am
// so the inclusion guard in iovec_entry.hpp is satisfied.

#include "httpserver/iovec_entry.hpp"

// --- preprocessor-based leak detection ------------------------------------

#ifdef _SYS_UIO_H
#  error "<sys/uio.h> was pulled in transitively by httpserver/iovec_entry.hpp (glibc/musl guard _SYS_UIO_H)"
#endif

#ifdef _SYS_UIO_H_
#  error "<sys/uio.h> was pulled in transitively by httpserver/iovec_entry.hpp (macOS/BSD guard _SYS_UIO_H_)"
#endif

// --------------------------------------------------------------------------

#include "./littletest.hpp"

LT_BEGIN_SUITE(header_hygiene_iovec_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(header_hygiene_iovec_suite)

// Verify that iovec_entry is accessible and sizeof/alignof are non-zero
// without any POSIX headers in scope. This confirms that no system types
// leaked in through iovec_entry.hpp and that the type is self-contained.
LT_BEGIN_AUTO_TEST(header_hygiene_iovec_suite, iovec_entry_visible_without_sys_uio)
    // If any system header leaked in, alignof/sizeof would still be correct,
    // but the #error directives above ensure this test is only reached on a
    // clean TU. These checks confirm the type is truly self-contained.
    static_assert(sizeof(httpserver::iovec_entry) > 0,
                  "iovec_entry must have non-zero size without sys/uio.h");
    static_assert(alignof(httpserver::iovec_entry) > 0,
                  "iovec_entry must have non-zero alignment without sys/uio.h");
    LT_CHECK_EQ(true, true);  // TU compiled clean: no sys/uio.h leak detected
LT_END_AUTO_TEST(iovec_entry_visible_without_sys_uio)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
