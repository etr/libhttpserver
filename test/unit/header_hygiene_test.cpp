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
//   <pthread.h>        -> _PTHREAD_H        (glibc/musl, AND macOS/Apple SDK)
//                         _PTHREAD_H_       (some BSDs)
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
// TASK-020 caveat (libc++ AND libstdc++ in thread mode): both
// mainstream STLs pull <pthread.h> in transitively from any STL
// container header (<vector>, <string>, <map>, etc.) when threading
// is enabled, so _PTHREAD_H / _PTHREAD_H_ being defined is an STL
// implementation detail, not a libhttpserver leak:
//   - libc++ (Apple's default STL on macOS) routes through
//     <__thread/support/pthread.h>.
//   - libstdc++ in thread-enabled mode (which is the default whenever
//     -D_REENTRANT is set, as configure.ac does) routes through
//     <bits/gthr-default.h>, which #include <pthread.h> directly.
// We therefore skip the pthread guards on both STLs (detected via
// _LIBCPP_VERSION and _GLIBCXX_HAS_GTHREADS respectively). The guards
// remain meaningful on STLs that don't use pthread for std::thread
// (e.g. MSVC's Microsoft STL, which uses Win32 threading).
//
// Cross-reference: the same forbidden-header list is enforced via the
// preprocessor-grep target `make check-hygiene` in the top-level
// Makefile.am. Keep both lists in sync.

#include <cstdio>

#include <httpserver.hpp>

int main() {
    int leaks = 0;

#ifdef MHD_VERSION
    std::fprintf(stderr, "LEAK: <microhttpd.h> reached the consumer TU (guard MHD_VERSION)\n");
    ++leaks;
#endif

// TASK-020: both libc++ and libstdc++ (in thread-enabled mode, which
// is the default whenever -D_REENTRANT is set) unconditionally drag
// <pthread.h> in from any STL container header. _PTHREAD_H /
// _PTHREAD_H_ being defined under either STL is an implementation
// detail, not a libhttpserver leak. Skip these guards on both;
// keep them strict on STLs that don't route std::thread through
// pthread (e.g. MSVC's Microsoft STL).
//
// Detection macros: _LIBCPP_VERSION is defined by libc++ (LLVM/Apple
// libcxx) as a numeric version; _GLIBCXX_HAS_GTHREADS is defined by
// libstdc++ when threading is enabled (the default when -D_REENTRANT
// is set, as configure.ac does). These are the correct stable guards
// for this detection — re-verify on each major libstdc++/libc++ upgrade.
// See also: Makefile.am HEADER_HYGIENE_FORBIDDEN rationale comment.
#if !defined(_LIBCPP_VERSION) && !defined(_GLIBCXX_HAS_GTHREADS)
#ifdef _PTHREAD_H
    std::fprintf(stderr, "LEAK: <pthread.h> reached the consumer TU (glibc/musl guard _PTHREAD_H)\n");
    ++leaks;
#endif

#ifdef _PTHREAD_H_
    std::fprintf(stderr, "LEAK: <pthread.h> reached the consumer TU (macOS/BSD guard _PTHREAD_H_)\n");
    ++leaks;
#endif
#endif  // !_LIBCPP_VERSION && !_GLIBCXX_HAS_GTHREADS

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
    // _PTHREAD_H/_PTHREAD_H_, GNUTLS_GNUTLS_H, _SYS_SOCKET_H/_H_, _SYS_UIO_H/_H_)
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
