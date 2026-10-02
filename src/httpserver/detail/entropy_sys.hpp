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
     License along with this library; if not, see the file LICENSE in
     the distribution; if not, write to the Free Software Foundation,
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

// Platform shims for OS randomness (TASK-114, architecture section 4:
// "Digest nonce generation and replay checks use OS randomness").
// This header is the ONLY place where the randomness platform
// divergence lives, mirroring the io_poll_sys.hpp convention:
//   - _WIN32  : BCryptGenRandom with the system-preferred RNG (no
//               algorithm handle to manage);
//   - linux   : getrandom(2) with EINTR retry and a partial-fill
//               loop (the syscall may return short on signal
//               interruption and for sizes above the kernel's
//               per-call ceiling);
//   - apple   : arc4random_buf (the BSD-family generator the libc
//               guarantees cannot fail);
//   - other POSIX: getentropy(3) chunked at 256 bytes (its documented
//               per-call limit).
// The MSYS (msys-runtime gcc) lane may not define _WIN32; if it
// mis-selects a POSIX branch there, the one-line follow-up is the
// same `|| defined(__MSYS__)` io_poll_sys.hpp documents.
//
// The public shape is one outcome-returning fill(): a zero-length
// request is vacuously ok; a syscall failure and an all-zero result
// (the degenerate generator the callers must never treat as a nonce)
// both report protocol_error.

#if !defined(HTTPSERVER_COMPILATION)
#error "entropy_sys.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_ENTROPY_SYS_HPP_
#define SRC_HTTPSERVER_DETAIL_ENTROPY_SYS_HPP_

#include <cstddef>
#include <span>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <cstdlib>
#else
#include <unistd.h>
#endif

#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace detail {

namespace entropy {

// Each platform draw lives in its own named function: the divergence
// is one dispatch (below), not one multiply-branching body, so the
// complexity gate reads each platform shim at its own size.
#if defined(_WIN32)
inline bool draw_via_bcrypt(std::byte* out, std::size_t count) noexcept {
    return ::BCryptGenRandom(nullptr,
                             reinterpret_cast<PUCHAR>(out),
                             static_cast<ULONG>(count),
                             BCRYPT_USE_SYSTEM_PREFERRED_RNG)
           == STATUS_SUCCESS;
}
#elif defined(__linux__)
// getrandom(2) may return short on signal interruption and for sizes
// above the kernel's per-call ceiling: loop until the request is
// satisfied, retrying EINTR.
inline bool draw_via_getrandom(std::byte* out, std::size_t count) noexcept {
    std::size_t done = 0;
    while (done < count) {
        const ssize_t got = ::getrandom(out + done, count - done, 0);
        const bool interrupted = got < 0 && errno == EINTR;
        if (interrupted) continue;
        if (got <= 0) return false;
        done += static_cast<std::size_t>(got);
    }
    return true;
}
#elif defined(__APPLE__)
inline bool draw_via_arc4(std::byte* out, std::size_t count) noexcept {
    ::arc4random_buf(out, count);
    return true;
}
#else
// getentropy(3) documents a 256-byte per-call limit: chunk above it.
inline bool draw_via_getentropy(std::byte* out, std::size_t count) noexcept {
    while (count > 0) {
        const std::size_t chunk = count < 256 ? count : 256;
        if (::getentropy(out, chunk) != 0) return false;
        out += chunk;
        count -= chunk;
    }
    return true;
}
#endif

// The raw platform draw: true when every requested byte was produced.
inline bool draw_bytes(std::byte* out, std::size_t count) noexcept {
#if defined(_WIN32)
    return draw_via_bcrypt(out, count);
#elif defined(__linux__)
    return draw_via_getrandom(out, count);
#elif defined(__APPLE__)
    return draw_via_arc4(out, count);
#else
    return draw_via_getentropy(out, count);
#endif
}

// Fills @p out from the OS randomness source. A zero-length request
// is ok; a failed or all-zero draw reports protocol_error (the caller
// must not mint nonces from either).
inline http::outcome fill(std::span<std::byte> out) {
    if (out.empty()) return http::outcome::okay();
    if (!draw_bytes(out.data(), out.size())) {
        return http::outcome(http::outcome_code::protocol_error,
                             "entropy: OS randomness source failed");
    }
    for (const std::byte b : out) {
        if (b != std::byte{0}) return http::outcome::okay();
    }
    return http::outcome(http::outcome_code::protocol_error,
                         "entropy: OS randomness source returned zeros");
}

}  // namespace entropy

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_ENTROPY_SYS_HPP_
