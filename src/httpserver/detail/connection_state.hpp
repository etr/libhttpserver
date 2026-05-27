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

// Internal detail header. Strict gate: reachable only from libhttpserver
// translation units, never from the public umbrella.
// cppcheck-suppress-file unusedStructMember
#if !defined(HTTPSERVER_COMPILATION)
#error "connection_state.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_CONNECTION_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_CONNECTION_STATE_HPP_

#include <cstddef>
#include <cstring>

#include <array>
#include <memory_resource>

namespace httpserver {
namespace detail {

// connection_state: per-MHD_Connection arena anchor.
//
// Owns a std::pmr::monotonic_buffer_resource over an embedded initial
// buffer. The arena is allocated once per MHD connection (in
// webserver_impl::connection_notify on MHD_CONNECTION_NOTIFY_STARTED)
// and torn down on MHD_CONNECTION_NOTIFY_CLOSED. Between requests on a
// keep-alive connection, request_completed calls arena_.release() to
// rewind the bump pointer, so a second request reuses the same memory.
//
// Lifetime contract for views returned by http_request getters
// (string_view, const& to pmr::string / pmr::vector / pmr::map members):
// they remain valid until the request-completion callback fires for the
// request they were derived from. Capturing them past the user
// handler's return is undefined behavior. (See architecture doc
// 04-components/http-request.md.) (spec-alignment-checker-iter1-4)
//
// Initial-buffer sizing math (8 KiB):
//   - sizeof(http_request_impl) ~= 600-700 B with libstdc++/libc++
//     map/string layouts.
//   - A typical small GET populates ~1.5 KiB across header_view_map,
//     querystring, requestor_ip; a small POST with a few args ~2.5 KiB.
//   - Each std::pmr::map node (unescaped_args) is ~64-96 B on
//     libstdc++/libc++, so 5 headers/args already consume ~400-500 B
//     in tree nodes alone. 4 KiB was undersized for realistic requests
//     with moderate arg counts; 8 KiB matches the test's own generous
//     buffer and covers the common case without overflow to the upstream
//     heap. (performance-reviewer-iter1-1.)
//   - Overflow spills to the upstream resource (default = heap) silently
//     -- it is a correctness fall-through, not a hard limit.
//   - TODO(M5): expose ARENA_INITIAL_BYTES via create_webserver if/when
//     profiling shows tuning value.
struct connection_state {
    static constexpr std::size_t ARENA_INITIAL_BYTES = 8192;

    // The buffer aliases storage for any PMR-aware object the arena
    // hands out, so it must satisfy the strictest fundamental alignment.
    alignas(std::max_align_t) std::array<std::byte, ARENA_INITIAL_BYTES> initial_buffer_{};

    // upstream defaults to new_delete_resource (= get_default_resource).
    // We pass it explicitly to make the contract obvious in source.
    std::pmr::monotonic_buffer_resource arena_{
        initial_buffer_.data(), initial_buffer_.size(),
        std::pmr::new_delete_resource()};

    connection_state() = default;
    connection_state(const connection_state&) = delete;
    connection_state& operator=(const connection_state&) = delete;
    connection_state(connection_state&&) = delete;
    connection_state& operator=(connection_state&&) = delete;

    // reset_arena(): release the bump pointer AND zero the initial buffer.
    //
    // The plain arena_.release() rewinds the bump pointer so the next
    // request reuses the same memory, but it does NOT clear the reclaimed
    // bytes. Credentials (username, password, digested_user) written into
    // the arena by a previous request would therefore linger in the buffer
    // until overwritten by the next request's lazy-cache population.
    // Explicit zeroing after release() closes that residual-credential
    // window. (security-reviewer-iter1-3, CWE-226.)
    //
    // Scope limitation: zeroing covers only initial_buffer_ (ARENA_INITIAL_BYTES).
    // If a request's arena usage overflows the initial buffer, the monotonic_
    // buffer_resource silently allocates additional blocks from the upstream
    // resource (heap). Those overflow blocks are freed by arena_.release()
    // but NOT zeroed here. Credentials that spill past ARENA_INITIAL_BYTES
    // are therefore not cleared by this call. In practice the buffer is sized
    // to hold typical requests without overflow (see sizing comment above);
    // if sizing assumptions change, this limitation should be revisited.
    // (code-quality-reviewer-iter1-5)
    //
    // Using std::memset here (rather than explicit_bzero / SecureZeroMemory)
    // is acceptable because the buffer is accessed again immediately by the
    // next request's arena allocation, preventing the compiler from
    // optimising the clear away as a dead store. (security-reviewer-iter1-4,
    // CWE-14 risk acknowledged; std::atomic_signal_fence or volatile
    // initial_buffer_ would provide a language-level guarantee if needed
    // by future deployments.)
    void reset_arena() noexcept {
        arena_.release();
        std::memset(initial_buffer_.data(), 0, ARENA_INITIAL_BYTES);
    }
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_CONNECTION_STATE_HPP_
