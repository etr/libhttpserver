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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include <microhttpd.h>

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
// ---------------------------------------------------------------------------
static_assert(sizeof(::httpserver::iovec_entry) == sizeof(struct iovec),
    "iovec_entry size must match POSIX struct iovec — divergent platform; "
    "implement memcpy fallback (see TASK-004)");
static_assert(offsetof(::httpserver::iovec_entry, base) ==
                  offsetof(struct iovec, iov_base),
    "iovec_entry::base offset must match struct iovec::iov_base");
static_assert(offsetof(::httpserver::iovec_entry, len) ==
                  offsetof(struct iovec, iov_len),
    "iovec_entry::len offset must match struct iovec::iov_len");

static_assert(sizeof(::httpserver::iovec_entry) == sizeof(MHD_IoVec),
    "iovec_entry size must match libmicrohttpd MHD_IoVec — MHD layout drift");
static_assert(offsetof(::httpserver::iovec_entry, base) ==
                  offsetof(MHD_IoVec, iov_base),
    "iovec_entry::base offset must match MHD_IoVec::iov_base");
static_assert(offsetof(::httpserver::iovec_entry, len) ==
                  offsetof(MHD_IoVec, iov_len),
    "iovec_entry::len offset must match MHD_IoVec::iov_len");

static_assert(alignof(::httpserver::iovec_entry) == alignof(struct iovec),
    "iovec_entry alignment must match POSIX struct iovec — divergent platform; "
    "implement memcpy fallback (see TASK-004)");
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
// file_body — replicates v1 file_response::get_raw_response exactly.
// ---------------------------------------------------------------------------
MHD_Response* file_body::materialize() {
#ifndef _WIN32
    int fd = ::open(path_.c_str(), O_RDONLY | O_NOFOLLOW);
#else
    int fd = ::open(path_.c_str(), O_RDONLY);
#endif
    if (fd == -1) return nullptr;

    struct stat sb;
    if (::fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        ::close(fd);
        return nullptr;
    }

    off_t size = ::lseek(fd, 0, SEEK_END);
    if (size == static_cast<off_t>(-1)) {
        ::close(fd);
        return nullptr;
    }

    if (size) {
        size_cached_ = static_cast<std::size_t>(size);
        return MHD_create_response_from_fd(
            static_cast<std::size_t>(size), fd);
    }
    ::close(fd);
    size_cached_ = 0;
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
    auto* self = static_cast<deferred_body*>(cls);
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
