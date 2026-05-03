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

// Header-hygiene sentinel for TASK-007:
//
// PRD-HDR-REQ-001..003 demand that the public umbrella header
// <httpserver.hpp> not transitively pull in libmicrohttpd, pthread,
// gnutls, or BSD-socket internals. This translation unit includes ONLY
// the umbrella, then uses the well-known include-guard macros that
// each forbidden header defines on every supported platform to detect
// transitive leakage.
//
// Detection mechanism: each forbidden header defines a stable include
// guard. After the umbrella include, we report (at runtime) which of
// those macros are now defined. If any are, the test exits with a
// non-zero status and prints a list of leaked headers; if none are,
// the test exits 0.
//
// We deliberately use *runtime* reporting (not #error) so that:
//   1. Automake's XFAIL_TESTS mechanism can mark the expected failure
//      (XFAIL_TESTS only matters if the test program builds and then
//      exits non-zero -- a compile-time #error would break `make check`
//      outright instead of being captured as XFAIL).
//   2. CI logs clearly show which specific headers are still leaking,
//      so M2-M5 progress is observable.
//   3. When the umbrella is clean and this exits 0, Automake reports
//      XPASS (a hard error by default) -- which is the explicit signal
//      for TASK-020 to remove the XFAIL_TESTS marker.
//
// Guard-macro mapping (verified on glibc, musl, macOS/BSD):
//
//   <microhttpd.h>     -> MHD_VERSION       (defined unconditionally inside)
//   <pthread.h>        -> _PTHREAD_H        (glibc/musl)
//                         _PTHREAD_H_       (macOS/BSD)
//   <gnutls/gnutls.h>  -> GNUTLS_GNUTLS_H   (the library's own include guard)
//   <sys/socket.h>     -> _SYS_SOCKET_H     (glibc/musl)
//                         _SYS_SOCKET_H_    (macOS/BSD)
//   <sys/uio.h>        -> _SYS_UIO_H        (glibc/musl)
//                         _SYS_UIO_H_       (macOS/BSD)
//
// IMPORTANT: Do NOT edit the detection list below to "fix" intermediate
// red states during M2-M5 -- the leaks must be removed in production
// code, not here.
//
// Cross-reference: the same forbidden-header list is enforced via the
// preprocessor-grep target `make check-hygiene` in the top-level
// Makefile.am. Keep both lists in sync.

#include <httpserver.hpp>

#include <cstdio>

int main() {
    int leaks = 0;

#ifdef MHD_VERSION
    std::fprintf(stderr, "LEAK: <microhttpd.h> reached the consumer TU (guard MHD_VERSION)\n");
    ++leaks;
#endif

#ifdef _PTHREAD_H
    std::fprintf(stderr, "LEAK: <pthread.h> reached the consumer TU (glibc/musl guard _PTHREAD_H)\n");
    ++leaks;
#endif

#ifdef _PTHREAD_H_
    std::fprintf(stderr, "LEAK: <pthread.h> reached the consumer TU (macOS/BSD guard _PTHREAD_H_)\n");
    ++leaks;
#endif

#ifdef GNUTLS_GNUTLS_H
    std::fprintf(stderr, "LEAK: <gnutls/gnutls.h> reached the consumer TU (guard GNUTLS_GNUTLS_H)\n");
    ++leaks;
#endif

#ifdef _SYS_SOCKET_H
    std::fprintf(stderr, "LEAK: <sys/socket.h> reached the consumer TU (glibc/musl guard _SYS_SOCKET_H)\n");
    ++leaks;
#endif

#ifdef _SYS_SOCKET_H_
    std::fprintf(stderr, "LEAK: <sys/socket.h> reached the consumer TU (macOS/BSD guard _SYS_SOCKET_H_)\n");
    ++leaks;
#endif

#ifdef _SYS_UIO_H
    std::fprintf(stderr, "LEAK: <sys/uio.h> reached the consumer TU (glibc/musl guard _SYS_UIO_H)\n");
    ++leaks;
#endif

#ifdef _SYS_UIO_H_
    std::fprintf(stderr, "LEAK: <sys/uio.h> reached the consumer TU (macOS/BSD guard _SYS_UIO_H_)\n");
    ++leaks;
#endif

    if (leaks > 0) {
        std::fprintf(stderr,
                     "header-hygiene FAIL: %d forbidden header(s) leaked through <httpserver.hpp>\n",
                     leaks);
        return 1;
    }

    return 0;
}
