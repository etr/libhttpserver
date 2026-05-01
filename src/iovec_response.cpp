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
#include "httpserver/iovec_entry.hpp"

#include <cstddef>
#include <microhttpd.h>
#include <sys/uio.h>
#include <vector>

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

MHD_Response* iovec_response::get_raw_response() {
    // MHD_create_response_from_iovec makes an internal copy of the iov array,
    // so the local vector is safe. The buffer data pointed to by iov_base must
    // remain valid until the response is destroyed — this is guaranteed because
    // the buffers are owned by this iovec_response object.
    //
    // The dispatch path builds a contiguous std::vector<iovec_entry> from the
    // owned std::strings, then reinterpret_casts it to const MHD_IoVec* when
    // calling MHD. The cast is well-defined because the layout-pinning
    // static_asserts above guarantee identical size and field offsets. This
    // same cast bridge will move into details/body.hpp when TASK-009 lands.
    std::vector<iovec_entry> entries(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        entries[i].base = buffers[i].data();
        entries[i].len = buffers[i].size();
    }
    return MHD_create_response_from_iovec(
        reinterpret_cast<const MHD_IoVec*>(entries.data()),
        static_cast<unsigned int>(entries.size()),
        nullptr,
        nullptr);
}

}  // namespace httpserver
