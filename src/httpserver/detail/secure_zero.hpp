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

// TASK-068: portable secure-zero primitive.
//
// CWE-14 mitigation: the per-connection arena clearing path needs a write
// that the optimizer is forbidden to elide as a dead store, since the
// bytes being cleared are credentials (basic-auth username/password,
// digest-auth digested_user) that linger in the arena across keep-alive
// requests until the next allocation overwrites them. A plain
// std::memset(p, 0, n) followed by no observable read is exactly the
// shape that GCC / clang dead-store elimination targets at -O2 and above.
//
// This header centralizes a `secure_zero(void*, size_t)` helper that
// dispatches to:
//   - explicit_bzero        on glibc / musl / FreeBSD / OpenBSD / *BSD
//                           (POSIX-ish lanes; declared in <string.h> when
//                           the libc supports it)
//   - RtlSecureZeroMemory   on Windows (the canonical kernel-grade
//                           primitive; declared in <winnt.h> / <windows.h>)
//   - a portable fallback   that walks the buffer through a `volatile`
//                           unsigned-char pointer and follows the loop
//                           with an inline-asm memory clobber. This is
//                           the same pattern used by OpenSSL
//                           OPENSSL_cleanse and by the Linux kernel's
//                           barrier_data(); see also CERT C MSC06-C.
//
// Selection happens at compile time via HAVE_EXPLICIT_BZERO emitted by
// configure.ac. The macOS path intentionally takes the portable
// fallback: although Apple libc exposes memset_s when
// __STDC_WANT_LIB_EXT1__ is defined before <string.h>, this header is
// included transitively from many TUs whose include order we cannot
// control, so the macro might be set after a sibling header already
// pulled in <string.h>. The volatile + asm-memory-clobber fallback has
// the same security guarantee with no preprocessor-order coupling, so
// the trade-off is worth it.

// Internal detail header. Strict gate: reachable only from libhttpserver
// translation units, never from the public umbrella.
//
// This #error check intentionally precedes the include guard below: a
// consumer TU must never reach this file at all, so the check has to fire
// on *every* inclusion attempt, not just the first. The include guard
// below only needs to protect internal TUs that legitimately include this
// header more than once (which is the normal, allowed case); if it were
// placed first, a second internal include would short-circuit past the
// #error and silently hide a first, illegitimate include from a consumer.
#if !defined(HTTPSERVER_COMPILATION)
#error "secure_zero.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_SECURE_ZERO_HPP_
#define SRC_HTTPSERVER_DETAIL_SECURE_ZERO_HPP_

#include <cstddef>

// HAVE_EXPLICIT_BZERO and HAVE_MEMSET_S are emitted as -D flags by
// configure.ac (pushed into AM_CXXFLAGS, mirroring the HAVE_GNUTLS /
// HAVE_BAUTH propagation pattern). This header therefore consults them
// without an explicit config.h include.

#if defined(_WIN32)
// RtlSecureZeroMemory lives in <windows.h>. We forward-declare it instead
// of pulling the full Win32 header here to keep this internal helper
// self-contained: the broader codebase already includes <winsock2.h> in
// the network path, and the two declarations are ODR-compatible (both
// are `extern "C" void* __stdcall RtlSecureZeroMemory(void*, size_t)`
// in <winnt.h>).
extern "C" void* __stdcall RtlSecureZeroMemory(void*, std::size_t);
#elif defined(HAVE_EXPLICIT_BZERO)
#include <string.h>
#endif

namespace httpserver {
namespace detail {

// secure_zero(p, n): zero `n` bytes starting at `p` with a write the
// optimizer is forbidden to eliminate as a dead store.
//
// Contract:
//   - secure_zero(nullptr, 0) is a no-op (precondition for callers that
//     hold a possibly-empty buffer).
//   - secure_zero(p, 0) is a no-op for ANY p, including non-null p: the
//     n == 0 guard short-circuits before p is ever dereferenced.
//   - For n > 0, p must be non-null and point at a writable region of at
//     least n bytes. secure_zero(nullptr, n) with n > 0 is undefined
//     behaviour -- callers must not pass a null pointer with a nonzero
//     length.
//   - The function is noexcept.
//
// Trade-off: relative to plain std::memset, the fallback walks the
// buffer byte-by-byte through a volatile pointer and emits a memory
// clobber. For the ARENA_INITIAL_BYTES = 8192 case in
// connection_state::reset_arena() this is a few thousand extra cycles
// per keep-alive request. The CWE-226 mitigation is worth it because
// the alternative is credential residue across requests. (Profiling
// has not surfaced this as a hot-path concern for the bench workloads.)
inline void secure_zero(void* p, std::size_t n) noexcept {
    if (p == nullptr || n == 0) {
        return;
    }
#if defined(_WIN32)
    ::RtlSecureZeroMemory(p, n);
#elif defined(HAVE_EXPLICIT_BZERO)
    ::explicit_bzero(p, n);
#else
    // Portable fallback. The two ingredients:
    //   (1) Walk through a volatile pointer so every store is a
    //       visible side effect [intro.execution].
    //   (2) Follow the loop with an inline-asm memory clobber so the
    //       compiler cannot prove the writes are dead even under LTO.
    volatile unsigned char* q = static_cast<volatile unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) {
        q[i] = 0;
    }
    // Memory barrier for GCC/Clang; on other compilers the volatile loop
    // above is relied upon as the sole DCE-resistance mechanism.
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r"(p) : "memory");
#else
    // No verified DCE barrier exists for this compiler: the volatile
    // pointer loop is not, by itself, a proof against an aggressive/LTO
    // optimizer eliding the writes. Fail loudly at compile time rather
    // than silently ship an unverified CWE-14 gap; add and verify a
    // barrier for this compiler before removing this static_assert. This
    // only compiles for a non-_WIN32, non-HAVE_EXPLICIT_BZERO,
    // non-GCC/Clang target, so it is a no-op for every currently
    // supported toolchain.
    static_assert(false,
        "secure_zero: no verified DCE barrier for this compiler; add and "
        "verify one before enabling this fallback target.");
#endif
#endif
}

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_SECURE_ZERO_HPP_
