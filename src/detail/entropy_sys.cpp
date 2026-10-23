/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/entropy_sys.hpp>

namespace httpserver::detail::entropy {
#if defined(_WIN32)
bool draw_via_bcrypt(std::byte* out, std::size_t count) noexcept {
    // MS-ERREF defines STATUS_SUCCESS as zero; avoid depending on ntstatus.h.
    return ::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out), static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == NTSTATUS{0};
}
#endif

#if defined(_WIN32)
#elif defined(__linux__)
bool draw_via_getrandom(std::byte* out, std::size_t count) noexcept {
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
#endif

#if defined(_WIN32)
#elif defined(__linux__)
#elif defined(__APPLE__)
bool draw_via_arc4(std::byte* out, std::size_t count) noexcept {
    ::arc4random_buf(out, count);
    return true;
}
#endif

#if defined(_WIN32)
#elif defined(__linux__)
#elif defined(__APPLE__)
#else
bool draw_via_getentropy(std::byte* out, std::size_t count) noexcept {
    while (count > 0) {
        const std::size_t chunk = count < 256 ? count : 256;
        if (::getentropy(out, chunk) != 0) return false;
        out += chunk;
        count -= chunk;
    }
    return true;
}
#endif

bool draw_bytes(std::byte* out, std::size_t count) noexcept {
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

http::outcome fill(std::span<std::byte> out) {
    if (out.empty()) return http::outcome::okay();
    if (!draw_bytes(out.data(), out.size())) {
        return http::outcome(http::outcome_code::protocol_error, "entropy: OS randomness source failed");
    }
    for (const std::byte b : out) {
        if (b != std::byte{0}) return http::outcome::okay();
    }
    return http::outcome(http::outcome_code::protocol_error, "entropy: OS randomness source returned zeros");
}

}  // namespace httpserver::detail::entropy
