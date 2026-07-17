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

// Header-hygiene sentinel:
//
// The public umbrella header <httpserver.hpp> must not transitively
// pull in libmicrohttpd, pthread, gnutls, or BSD-socket internals.
// This translation unit includes ONLY
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
//      to remove the XFAIL_TESTS marker.
//
// Guard-macro mapping (verified on glibc, musl, macOS/BSD, MSYS2/MINGW64):
//
//   <microhttpd.h>     -> MHD_VERSION       (defined unconditionally inside)
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
// pthread-detector removal: a previous revision included guards
// for <pthread.h> (_PTHREAD_H / _PTHREAD_H_ / _WINPTHREADS_H), conditional
// on the consumer's STL not transitively dragging it in. Investigation
// confirmed that the libhttpserver public surface uses STL container
// headers (std::string, std::vector, std::map, etc. in http_request.hpp,
// http_resource.hpp, ip_representation.hpp, ...), and that BOTH mainstream
// STLs (libc++ via <__thread/support/pthread.h>, libstdc++ in
// thread-enabled mode via <bits/gthr-default.h>) unconditionally drag
// <pthread.h> in from those container headers when threading is enabled
// (the default whenever -D_REENTRANT is set, as configure.ac does).
// Since libhttpserver cannot rewrite its public surface to avoid STL
// containers without breaking source compatibility, the pthread guard
// is impossible to satisfy on any libhttpserver-supported STL/CI lane.
// The detector was therefore deleted rather than left as dead code
// guarded on STL detection macros. See RELEASE_NOTES.md (Test
// infrastructure) for the rationale.
//
// Cross-reference: the same forbidden-header list is enforced via the
// preprocessor-grep target `make check-hygiene` in the top-level
// Makefile.am. Keep both lists in sync.

// <cstdio> is included before <httpserver.hpp> for fprintf availability.
// The forbidden-header macro checks in main() are compile-time (#ifdef) guards
// evaluated after the umbrella include, so <cstdio>'s transitive headers cannot
// mask any umbrella-leaked macro.  Unlike consumer_umbrella_no_backend.cpp
// (which avoids all standard-library includes for a strict Layer 2 isolation
// test), this sentinel requires fprintf for human-readable runtime reporting.
#include <cstdio>

#include <httpserver.hpp>

// Deliberate multi-assert design: all forbidden-header checks share a single
// accumulator so CI logs show every leaked header in one run, not just the
// first.  Each #ifdef block is independent; a single-assert style would stop
// at the first failure and hide remaining leaks.
int main() {
    int leaks = 0;

#ifdef MHD_VERSION
    std::fprintf(stderr, "LEAK: <microhttpd.h> reached the consumer TU (guard MHD_VERSION)\n");
    ++leaks;
#endif

// The <pthread.h> detector was removed. The libhttpserver
// public surface uses STL container headers, and BOTH mainstream STLs
// (libc++ AND libstdc++ in thread-enabled mode) unconditionally drag
// <pthread.h> in from those headers. The detector could not be
// satisfied on any supported CI lane without rewriting the public
// surface to drop STL containers, which would break source
// compatibility. See the comment block above and RELEASE_NOTES.md
// (Test infrastructure) for the rationale.

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

    // `leaks` is incremented from inside #ifdef blocks whose macros (MHD_VERSION,
    // GNUTLS_GNUTLS_H, _SYS_SOCKET_H/_SYS_SOCKET_H_, _SYS_UIO_H/_SYS_UIO_H_)
    // are platform/STL/config dependent. cppcheck statically assumes they are
    // undefined and reports the comparison as always-false; the conditional is
    // load-bearing for any platform where a forbidden header does leak.
    // cppcheck-suppress knownConditionTrueFalse
    if (leaks > 0) {
        std::fprintf(stderr,
                     "header-hygiene FAIL: %d forbidden header(s) leaked through <httpserver.hpp>\n",
                     leaks);
        return 1;
    }

    return 0;
}
