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

// http_request_impl_args.cpp -- GET-argument and querystring population.
//
// Carved out of src/detail/http_request_impl.cpp to keep both
// translation units under the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh). Holds the build_request_args /
// build_request_querystring MHD enumeration callbacks, populate_args, and
// the anonymous-namespace unescape_in_arena helper they share.
//
// Sibling translation units: the TLS / client-cert section lives in
// src/detail/http_request_impl_tls.cpp; the public-API forwarders + ctor
// live in src/http_request.cpp; the auth-surface public-API forwarders
// live in src/http_request_auth.cpp.

#include "httpserver/http_request.hpp"

#include <microhttpd.h>  // NOLINT(build/include_order)

#include <algorithm>
#include <cassert>
#include <map>
#include <memory_resource>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "httpserver/detail/connection_state.hpp"
#include "httpserver/detail/http_request_impl.hpp"
#include "httpserver/detail/unescape_helpers.hpp"
#include "httpserver/http_utils.hpp"

namespace httpserver {

namespace detail {

namespace {

// Arena-routed unescape. The caller passes an arena-backed
// pmr::string already holding the raw bytes; we run the unescape
// transformation directly on the pmr::string's storage so no
// global-heap allocation occurs on the warm path. The default (no
// user-callback) path delegates to httpserver::detail::unescape_buf_raw
// (unescape_helpers.hpp), which documents the decode algorithm itself.
//
// User-callback path: the public callback signature is
// `void(std::string&)` (ABI-locked), so we route through a
// thread_local std::string (thread_value) whose capacity amortises
// across all requests on the same worker thread.
//
// Amortisation contract: zero global-heap allocations on the warm
// path per thread, *after* thread_value has grown to the peak value
// length seen on that thread. The first call on a new thread, or the
// first call that sees a value longer than any previous one, triggers
// exactly one global-heap allocation to grow thread_value's buffer;
// all subsequent calls at or below that length are allocation-free.
//
// Arena-waste note: when the user callback grows the value
// (thread_value.size() > original value.size()), value's original
// arena allocation is abandoned in the monotonic arena until
// reset_arena(). This is an accepted trade-off of the ABI-locked
// void(std::string&) signature; the arena is reset per request so
// the waste is bounded by the request's total value bytes.
inline void unescape_in_arena(std::pmr::string& value,
                              unescaper_ptr user_fn) {
    if (value.empty()) return;
    if (user_fn == nullptr) {
        // Default %HH / '+' decode: run in-place on the arena
        // buffer, then truncate to the new size.
        const std::size_t new_size = httpserver::detail::unescape_buf_raw(
            value.data(), value.size());
        value.resize(new_size);
        return;
    }
    // User-callback path: route through a per-thread reusable
    // std::string scratch buffer so the warm-path cost is zero
    // global-heap allocations after the first call on this thread.
    thread_local std::string thread_value;
    thread_value.assign(value.data(), value.size());
    user_fn(thread_value);
    value.assign(thread_value.data(), thread_value.size());
}

}  // namespace

MHD_Result http_request_impl::build_request_args(void* cls, MHD_ValueKind kind,
                                                 const char* key, const char* arg_value) {
    // Parameters needed to respect MHD interface, but not used in the implementation.
    std::ignore = kind;

    arguments_accumulator* aa = static_cast<arguments_accumulator*>(cls);

    // Security guard: reject requests that
    // exceed the per-request argument count or total byte budget. Both
    // limits prevent a crafted request with thousands of unique GET
    // arguments from exhausting the per-connection arena and the heap
    // upstream. Returning MHD_NO stops MHD's iteration over remaining
    // arguments immediately.
    std::string_view key_sv(key);
    std::string_view val_sv((arg_value != nullptr) ? arg_value : "");

    // Hoist the find so we use the iterator both for the count guard and
    // for the insert branch below -- eliminates the double lookup.
    auto& args = *aa->arguments;
    auto existing_it = args.find(key_sv);

    // Apply count limit: would this key add a new unique entry?
    const bool is_new_key = (existing_it == args.end());
    if (args.size() + (is_new_key ? 1u : 0u) > aa->max_args_count) {
        return MHD_NO;
    }

    // Apply byte limit: count key + value bytes accumulated so far.
    const std::size_t this_pair_bytes = key_sv.size() + val_sv.size();
    if (aa->accumulated_bytes + this_pair_bytes > aa->max_args_bytes) {
        return MHD_NO;
    }
    aa->accumulated_bytes += this_pair_bytes;

    // Arena-routed unescape: see unescape_in_arena() above for path details.
    // The returned pmr::string's data is owned by the arena and lives until
    // connection_state::reset_arena() runs (request completion), matching
    // the string_view lifetime contract.
    auto pmr_alloc = args.get_allocator();
    auto it = existing_it;
    if (it == args.end()) {
        std::pmr::vector<std::pmr::string> empty(pmr_alloc);
        auto inserted = args.emplace(
            std::pmr::string(key_sv.data(), key_sv.size(), pmr_alloc),
            std::move(empty));
        it = inserted.first;
    }

    // Allocate the destination pmr::string in the arena domain and
    // copy the raw input bytes into it. The unescape transformation
    // runs in-place on the arena-backed buffer.
    auto& arg_value_ref = it->second.emplace_back(val_sv.data(), val_sv.size());
    unescape_in_arena(arg_value_ref, aa->unescaper);
    return MHD_YES;
}

MHD_Result http_request_impl::build_request_querystring(void* cls, MHD_ValueKind kind,
                                                        const char* key_value, const char* arg_value) {
    // Parameters needed to respect MHD interface, but not used in the implementation.
    std::ignore = kind;

    // cls is a pmr::string* into impl_->querystring; growth
    // allocates from the per-connection arena.
    std::pmr::string* qs = static_cast<std::pmr::string*>(cls);

    std::string_view key = key_value;
    std::string_view value = ((arg_value == nullptr) ? "" : arg_value);

    // Limit to a single allocation.
    qs->reserve(qs->size() + key.size() + value.size() + 3);

    *qs += (qs->empty() ? "?" : "&");
    *qs += key;
    *qs += "=";
    *qs += value;

    return MHD_YES;
}

void http_request_impl::populate_args() const {
    if (args_populated) {
        return;
    }
    // Test-request path: connection_ is null, args already set directly.
    if (connection_ == nullptr) {
        args_populated = true;
        return;
    }
    arguments_accumulator aa;
    aa.unescaper = unescaper_;
    aa.arguments = &unescaped_args;
    // Pick up the per-connection args DoS limits from connection_state if
    // available (set by webserver_impl::connection_notify at STARTED).
    // Falls back to the compile-time defaults when the socket_context
    // isn't wired -- matches the heap-fallback behaviour for the arena.
    const MHD_ConnectionInfo* ci = MHD_get_connection_info(
        connection_, MHD_CONNECTION_INFO_SOCKET_CONTEXT);
    if (ci != nullptr && ci->socket_context != nullptr) {
        auto* cs = static_cast<connection_state*>(ci->socket_context);
        if (cs->max_args_count != 0) {
            aa.max_args_count = cs->max_args_count;
        }
        if (cs->max_args_bytes != 0) {
            aa.max_args_bytes = cs->max_args_bytes;
        }
    }
    MHD_get_connection_values(connection_, MHD_GET_ARGUMENT_KIND,
                              &http_request_impl::build_request_args,
                              reinterpret_cast<void*>(&aa));

    args_populated = true;
}

}  // namespace detail
}  // namespace httpserver
