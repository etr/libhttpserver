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

#include "httpserver/detail/body.hpp"

#include <fcntl.h>
#include <microhttpd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/uio.h>        // POSIX struct iovec — used for layout-pin asserts
#endif

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace httpserver {

namespace detail {

// ---------------------------------------------------------------------------
// Layout-pinning static_asserts for iovec_entry → MHD_IoVec / struct iovec.
// These asserts live next to the cast site (iovec_body::materialize below)
// to catch platform layout drift at compile time rather than at runtime.
//
// The POSIX `struct iovec` asserts are gated on !_WIN32 (no <sys/uio.h> on
// MSYS2/mingw); the MHD_IoVec asserts run everywhere because that's the
// type the dispatch path actually casts to.
// ---------------------------------------------------------------------------
#ifndef _WIN32
static_assert(sizeof(::httpserver::iovec_entry) == sizeof(struct iovec),
    "iovec_entry size must match POSIX struct iovec — divergent platform; "
    "implement memcpy fallback (see TASK-004)");
static_assert(offsetof(::httpserver::iovec_entry, base) ==
                  offsetof(struct iovec, iov_base),
    "iovec_entry::base offset must match struct iovec::iov_base");
static_assert(offsetof(::httpserver::iovec_entry, len) ==
                  offsetof(struct iovec, iov_len),
    "iovec_entry::len offset must match struct iovec::iov_len");
static_assert(alignof(::httpserver::iovec_entry) == alignof(struct iovec),
    "iovec_entry alignment must match POSIX struct iovec — divergent platform; "
    "implement memcpy fallback (see TASK-004)");
#endif  // !_WIN32

static_assert(sizeof(::httpserver::iovec_entry) == sizeof(MHD_IoVec),
    "iovec_entry size must match libmicrohttpd MHD_IoVec — MHD layout drift");
static_assert(offsetof(::httpserver::iovec_entry, base) ==
                  offsetof(MHD_IoVec, iov_base),
    "iovec_entry::base offset must match MHD_IoVec::iov_base");
static_assert(offsetof(::httpserver::iovec_entry, len) ==
                  offsetof(MHD_IoVec, iov_len),
    "iovec_entry::len offset must match MHD_IoVec::iov_len");
static_assert(alignof(::httpserver::iovec_entry) == alignof(MHD_IoVec),
    "iovec_entry alignment must match MHD_IoVec — MHD layout drift");

static_assert(std::is_standard_layout_v<::httpserver::iovec_entry>,
    "iovec_entry must be standard layout for reinterpret_cast to MHD_IoVec");

// ---------------------------------------------------------------------------
// body — virtual destructor anchor (forces vtable emission in this TU).
// ---------------------------------------------------------------------------
body::~body() = default;

// ---------------------------------------------------------------------------
// empty_body
// ---------------------------------------------------------------------------
MHD_Response* empty_body::materialize() {
    return MHD_create_response_empty(static_cast<MHD_ResponseFlags>(flags_));
}

// ---------------------------------------------------------------------------
// string_body
// ---------------------------------------------------------------------------
MHD_Response* string_body::materialize() {
    // _static variant: accepts const void* directly, no const_cast needed,
    // and documents the same PERSISTENT ownership semantics — the buffer is
    // owned by *this and outlives the MHD_Response. Requires
    // MHD >= 0x00097701, well below the project minimum 0x01000000.
    return MHD_create_response_from_buffer_static(
        content_.size(),
        static_cast<const void*>(content_.data()));
}

// ---------------------------------------------------------------------------
// file_body — opens the file and fstat's it at construction so size() is
// accurate immediately.  materialize() uses fstat's st_size; it never calls
// lseek(), so the fd's read position remains at 0 when handed to
// MHD_create_response_from_fd (CWE-367).
// ---------------------------------------------------------------------------
file_body::file_body(std::string path) noexcept
    : path_(std::move(path)) {
#ifndef _WIN32
    fd_ = ::open(path_.c_str(), O_RDONLY | O_NOFOLLOW);
#else
    fd_ = ::open(path_.c_str(), O_RDONLY);
#endif
    if (fd_ == -1) return;

    struct stat sb;
    if (::fstat(fd_, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        ::close(fd_);
        fd_ = -1;
        return;
    }

    // Use fstat's st_size directly — no lseek, no TOCTOU, no fd-position
    // side-effect.
    //
    // CWE-190 guard: on 32-bit platforms std::size_t is 32 bits while off_t
    // can be 64 bits; a file larger than 4 GiB would silently truncate via
    // the cast. Reject oversized files so MHD_create_response_from_fd always
    // receives the correct size.  On 64-bit targets the comparison is a
    // compile-time no-op and the branch is dead.
    if (sb.st_size < 0 ||
        static_cast<uint64_t>(sb.st_size) >
            static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        ::close(fd_);
        fd_ = -1;
        return;
    }
    size_ = static_cast<std::size_t>(sb.st_size);
}

file_body::~file_body() {
    // Close only if MHD never took ownership (materialized_ stays false until
    // MHD_create_response_from_fd returns non-null).
    if (!materialized_ && fd_ != -1) {
        ::close(fd_);
    }
}

// Hand-written move ctor: transfers fd_ ownership to the destination and
// flips the source's materialized_ to true so the source's destructor
// skips the close path. Without this, the moved-from file_body would
// close the fd we just handed off — a classic double-close bug
// (CWE-415). std::exchange keeps the move noexcept.
file_body::file_body(file_body&& o) noexcept
    : path_(std::move(o.path_)),
      size_(o.size_),
      fd_(std::exchange(o.fd_, -1)),
      materialized_(std::exchange(o.materialized_, true)) {
}

MHD_Response* file_body::materialize() {
    if (fd_ == -1) return nullptr;

    if (size_) {
        MHD_Response* r = MHD_create_response_from_fd(size_, fd_);
        if (r != nullptr) {
            materialized_ = true;  // MHD now owns fd_
        }
        return r;
    }
    // Zero-byte file: serve an empty response without giving the fd to MHD.
    // Close the fd first, then set fd_ = -1 so ~file_body's guard
    // (`!materialized_ && fd_ != -1`) cannot reach ::close() on an already-
    // closed descriptor. fd_ == -1 is the sole sentinel here; there is no
    // need to also set materialized_ = true in this branch.
    ::close(fd_);
    fd_ = -1;
    return MHD_create_response_from_buffer_static(0, nullptr);
}

// ---------------------------------------------------------------------------
// iovec_body
// ---------------------------------------------------------------------------
MHD_Response* iovec_body::materialize() {
    // CWE-190 guard preserved from v1 iovec_response::get_raw_response.
    if (entries_.size() >
            static_cast<std::size_t>(
                std::numeric_limits<unsigned int>::max())) {
        return nullptr;
    }
    return MHD_create_response_from_iovec(
        reinterpret_cast<const MHD_IoVec*>(entries_.data()),
        static_cast<unsigned int>(entries_.size()),
        nullptr,
        nullptr);
}

// ---------------------------------------------------------------------------
// pipe_body
// ---------------------------------------------------------------------------
pipe_body::~pipe_body() {
    // Only close if MHD never took ownership. After a successful
    // materialize(), libmicrohttpd closes fd_ when the MHD_Response is
    // destroyed.
    if (!materialized_ && fd_ != -1) {
        ::close(fd_);
    }
}

// Same shape as file_body's move ctor: transfer fd_, mark source as
// already-materialized so its destructor skips close.
pipe_body::pipe_body(pipe_body&& o) noexcept
    : fd_(std::exchange(o.fd_, -1)),
      materialized_(std::exchange(o.materialized_, true)) {
}

MHD_Response* pipe_body::materialize() {
    MHD_Response* r = MHD_create_response_from_pipe(fd_);
    if (r != nullptr) {
        materialized_ = true;  // MHD now owns fd_
    }
    return r;
}

// ---------------------------------------------------------------------------
// deferred_body — trampoline + materialize.
// ---------------------------------------------------------------------------
ssize_t deferred_body::trampoline(void* cls, std::uint64_t pos,
                                  char* buf, std::size_t max) {
    // Guard against null cls or empty producer_ (CWE-476).
    // MHD's callback mechanism does not catch C++ exceptions, so
    // throwing std::bad_function_call here would call std::terminate().
    // Return MHD_CONTENT_READER_END_WITH_ERROR instead.
    auto* self = static_cast<deferred_body*>(cls);
    if (!self || !self->producer_) {
        return MHD_CONTENT_READER_END_WITH_ERROR;
    }
    return self->producer_(pos, buf, max);
}

MHD_Response* deferred_body::materialize() {
    // Block size 1024 mirrors v1 deferred_response::get_raw_response_helper.
    // Free-callback is nullptr because *this owns producer_ and outlives the
    // MHD_Response (http_response's lifetime enforces this).
    return MHD_create_response_from_callback(
        MHD_SIZE_UNKNOWN, 1024, &deferred_body::trampoline, this, nullptr);
}

// ---------------------------------------------------------------------------
// digest_challenge_body — RFC 7616 Digest auth challenge.
//
// Returns a body-only MHD_Response carrying the "access denied" payload.
// The WWW-Authenticate header itself is NOT attached here -- the dispatch
// path branches on body_kind::digest_challenge and routes through
// MHD_queue_auth_required_response3, which writes the authoritative
// challenge with libmicrohttpd's MHD_OPTION_DIGEST_AUTH_RANDOM-keyed nonce
// machinery (this satisfies the "MHD MD5/SHA-256 helpers remain the
// underlying primitive" acceptance criterion).
// ---------------------------------------------------------------------------
MHD_Response* digest_challenge_body::materialize() {
    if (!params_) {
        // Defence in depth: a moved-from body should never reach the
        // dispatch path, but if a regression causes it, hand MHD an
        // empty body rather than a null pointer.
        return MHD_create_response_from_buffer_static(0, nullptr);
    }
    return MHD_create_response_from_buffer_static(
        params_->body_text.size(),
        static_cast<const void*>(params_->body_text.data()));
}

}  // namespace detail

}  // namespace httpserver
