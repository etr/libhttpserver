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

#include "httpserver/iovec_response.hpp"

#include <microhttpd.h>
#ifndef _WIN32
#include <sys/uio.h>        // POSIX struct iovec — used for layout-pin asserts
#endif

#include <cstddef>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "httpserver/iovec_entry.hpp"

struct MHD_Response;

namespace httpserver {

// ---------------------------------------------------------------------------
// TASK-004: layout-pinning static_asserts.
//
// httpserver::iovec_entry is the public scatter/gather POD; libmicrohttpd's
// MHD_IoVec is the actual cast target on the dispatch path. POSIX struct
// iovec is asserted in parallel because the spec mandates it and because
// every platform we ship to defines all three with identical layout
// (glibc, musl, macOS, FreeBSD, NetBSD, OpenBSD, illumos).
//
// LIBHTTPSERVER_TODO_TASK004_MEMCPY_FALLBACK: if any of the asserts below
// ever fires on a divergent-layout platform, the fix is to replace the
// reinterpret_cast in the dispatch path with an element-by-element copy
// into a stack/heap MHD_IoVec[]. Until such a platform appears the
// asserts are the gate — a build failure on the divergent platform is
// the desired outcome (loud, immediate, with the assert string naming
// what diverged).
//
// The POSIX `struct iovec` asserts are gated on !_WIN32: MSYS2/mingw does
// not ship <sys/uio.h>. The MHD_IoVec asserts are unconditional — that's
// the type the dispatch path actually casts to.
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

// Alignment pinning: ensures the reinterpret_cast array stride is safe on
// architectures that trap on misaligned loads (SPARC, some ARM configs).
// CWE-704: without alignof equality the cast is UB even when size/offset match.
static_assert(alignof(::httpserver::iovec_entry) == alignof(MHD_IoVec),
    "iovec_entry alignment must match MHD_IoVec — MHD layout drift");

// Standard-layout guarantee: required so that reinterpret_cast between
// pointer-interconvertible types is well-defined under -fstrict-aliasing.
static_assert(std::is_standard_layout_v<::httpserver::iovec_entry>,
    "iovec_entry must be standard layout for reinterpret_cast to MHD_IoVec");

iovec_response::iovec_response(
        std::vector<std::string> owned_buffers,
        int response_code,
        const std::string& content_type)
    : http_response(response_code, content_type),
      owned_buffers_(std::move(owned_buffers)) {
    // Build the iovec_entry array eagerly so get_raw_response() is
    // allocation-free on the hot dispatch path.
    entries_.reserve(owned_buffers_.size());
    for (const auto& b : owned_buffers_) {
        entries_.push_back({b.data(), b.size()});
    }
}

iovec_response::iovec_response(
        std::vector<iovec_entry> caller_entries,
        int response_code,
        const std::string& content_type)
    : http_response(response_code, content_type),
      entries_(std::move(caller_entries)) {
    // owned_buffers_ is empty — buffer ownership stays with the caller.
}

MHD_Response* iovec_response::get_raw_response() {
    // Guard against integer narrowing: MHD_create_response_from_iovec takes
    // an unsigned int count. A vector with more than UINT_MAX entries would
    // silently truncate, causing MHD to read only part of the array while the
    // reported body length diverges from the actual allocation (CWE-190,
    // CWE-125). Return nullptr (the documented MHD "error" sentinel) instead.
    if (entries_.size() >
            static_cast<std::size_t>(
                std::numeric_limits<unsigned int>::max())) {
        return nullptr;
    }

    // The reinterpret_cast is well-defined because the layout-pinning
    // static_asserts above guarantee identical size, field offsets, and
    // alignment between iovec_entry and MHD_IoVec (C++ [basic.align],
    // CWE-704). entries_ was populated at construction time: no heap
    // allocation occurs on this path. The cast bridge will move into
    // detail/body.hpp when TASK-009 lands.
    return MHD_create_response_from_iovec(
        reinterpret_cast<const MHD_IoVec*>(entries_.data()),
        static_cast<unsigned int>(entries_.size()),
        nullptr,
        nullptr);
}

}  // namespace httpserver
