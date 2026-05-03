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

#include "httpserver/details/body.hpp"

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
// Duplicated from src/iovec_response.cpp during the M2 transition: the
// asserts must live next to every cast site, and TASK-013 will delete
// iovec_response.cpp once http_response::iovec() lands. Duplicate
// static_asserts on identical layouts are harmless.
//
// LIBHTTPSERVER_TODO_TASK013: drop the originals from iovec_response.cpp
// when iovec_response is removed.
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
    // PERSISTENT, not MUST_COPY: content_ is owned by *this and outlives the
    // returned MHD_Response (TASK-009 anchors the lifetime). This matches v1
    // string_response::get_raw_response.
    return MHD_create_response_from_buffer(
        content_.size(),
        const_cast<void*>(static_cast<const void*>(content_.data())),
        MHD_RESPMEM_PERSISTENT);
}

// ---------------------------------------------------------------------------
// file_body — opens the file and fstat's it at construction so size() is
// accurate immediately.  materialize() uses fstat's st_size; it never calls
// lseek(), so the fd's read position remains at 0 when handed to
// MHD_create_response_from_fd (security-reviewer-iter1-1 / CWE-367).
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
    // side-effect (security-reviewer-iter1-1 / performance-reviewer-iter1-4).
    size_ = static_cast<std::size_t>(sb.st_size);
}

file_body::~file_body() {
    // Close only if MHD never took ownership (materialized_ stays false until
    // MHD_create_response_from_fd returns non-null).
    if (!materialized_ && fd_ != -1) {
        ::close(fd_);
    }
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
    // Zero-byte file: serve empty response without giving the fd to MHD.
    ::close(fd_);
    fd_ = -1;
    materialized_ = true;  // suppress ~file_body's close (already closed)
    return MHD_create_response_from_buffer(
        0, nullptr, MHD_RESPMEM_PERSISTENT);
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
    // Guard against null cls or empty producer_ (security-reviewer-iter1-3 /
    // CWE-476). MHD's callback mechanism does not catch C++ exceptions, so
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
    // MHD_Response (TASK-009 enforces this via http_response's lifetime).
    return MHD_create_response_from_callback(
        MHD_SIZE_UNKNOWN, 1024, &deferred_body::trampoline, this, nullptr);
}

}  // namespace detail

}  // namespace httpserver
