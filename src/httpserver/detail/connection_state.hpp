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

#include <array>
#include <memory_resource>

#include "httpserver/detail/secure_zero.hpp"

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
//   - ARENA_INITIAL_BYTES stays compile-time intentionally: the buffer is
//     an embedded std::array, so making it runtime-sized would require an
//     extra heap allocation per connection. The compile-time default is
//     sized to cover typical small-GET / small-POST shapes without
//     overflow; profiling has not shown a deployed workload where the
//     per-connection extra allocation pays for itself. If a future
//     deployment needs tuning, replace initial_buffer_ with a
//     unique_ptr<std::byte[]> + runtime size carried here.
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

    // Per-connection args DoS limits, copied from webserver::max_args_count
    // / max_args_bytes by webserver_impl::connection_notify at
    // MHD_CONNECTION_NOTIFY_STARTED. populate_args() reads these from the
    // socket_context to set up the per-request arguments_accumulator. A
    // value of 0 means "use the arguments_accumulator compile-time
    // default", matching create_webserver's sentinel convention.
    std::size_t max_args_count = 0;
    std::size_t max_args_bytes = 0;

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
    // CWE-14 mitigation (TASK-068): the clear uses
    // httpserver::detail::secure_zero (defined in
    // httpserver/detail/secure_zero.hpp), which dispatches to
    // explicit_bzero / RtlSecureZeroMemory where available
    // and falls back to a volatile-pointer loop plus an inline-asm
    // memory clobber. The previous std::memset path was vulnerable to
    // dead-store elimination at -O2 / LTO under the right conditions
    // (the buffer's bytes are not observably read on the no-keep-alive
    // tear-down path, only on the next request's allocation). The new
    // helper is non-elidable by construction and is pinned by the
    // unit test secure_zero_dce_test.cpp.
    //
    // Trade-off: secure_zero walks the 8 KiB buffer byte-by-byte rather
    // than letting the compiler emit a vectorised store. The cost is a
    // few thousand cycles per keep-alive request -- well below the
    // arena-allocation cost the next request will pay anyway -- in
    // exchange for the CWE-14 guarantee. Profiling has not surfaced
    // this as a bench-relevant hot path.
    //
    // Scope limitation (accepted residue): zeroing covers only
    // initial_buffer_ (ARENA_INITIAL_BYTES = 8 KiB). If a request's
    // arena usage overflows the initial buffer, the monotonic_
    // buffer_resource silently allocates additional blocks from the
    // upstream resource (heap). Those overflow blocks are freed by
    // arena_.release() but NOT zeroed here -- the trailing bytes can
    // still be observed by a future unrelated allocation in the same
    // process. The buffer is sized to hold typical requests without
    // overflow (see the sizing comment above); credentials are several
    // hundred bytes at most, so they are reliably inside the 8 KiB
    // window in practice. TASK-095 (specs/tasks/M7-v2-cleanup/
    // TASK-095.md) tracks closing the overflow gap (hand-rolled arena
    // or a zero-on-deallocate upstream adapter); TASK-068 explicitly
    // accepted this residue. (security-reviewer-iter1-3,
    // code-quality-reviewer-iter1-5, TASK-068.)
    void reset_arena() noexcept {
        arena_.release();
        secure_zero(initial_buffer_.data(), ARENA_INITIAL_BYTES);
    }

    // Named accessor documenting the invariant relied on at every call
    // site that reads MHD's `MHD_ConnectionInfo::socket_context`: for
    // this library, that field always holds exactly a connection_state*
    // (set in webserver_impl::connection_notify on
    // MHD_CONNECTION_NOTIFY_STARTED). Prefer this over a raw
    // static_cast so the invariant is documented once rather than
    // repeated at each cast site.
    static connection_state* from_socket_context(void* ctx) noexcept {
        return static_cast<connection_state*>(ctx);
    }
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_CONNECTION_STATE_HPP_
