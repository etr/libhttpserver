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

// detail::http_request_impl method bodies (non-TLS section). The TLS /
// client-cert section lives in src/detail/http_request_impl_tls.cpp;
// the public-API forwarders + ctor live in src/http_request.cpp; the
// auth-surface public-API forwarders live in src/http_request_auth.cpp.

#include "httpserver/http_request.hpp"

#include <algorithm>
#include <map>
#include <memory_resource>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <microhttpd.h>

#include "httpserver/detail/http_request_impl.hpp"
#include "httpserver/http_utils.hpp"

namespace httpserver {

namespace detail {

std::string_view http_request_impl::get_connection_value(std::string_view key, MHD_ValueKind kind) const {
    // Test-request path: connection_ is null, fall back to local storage.
    if (connection_ == nullptr) {
        const auto* map = [&]() -> const http::header_map* {
            switch (kind) {
                case MHD_HEADER_KIND: return &headers_local;
                case MHD_FOOTER_KIND: return &footers_local;
                case MHD_COOKIE_KIND: return &cookies_local;
                default:             return nullptr;
            }
        }();
        if (map != nullptr) {
            auto it = map->find(std::string(key));
            if (it != map->end()) return it->second;
        }
        return http_request::EMPTY;
    }

    const char* header_c = MHD_lookup_connection_value(connection_, kind, key.data());

    if (header_c == nullptr) return http_request::EMPTY;

    return header_c;
}

MHD_Result http_request_impl::build_request_header(void* cls, MHD_ValueKind kind,
                                                   const char* key, const char* value) {
    // Parameters needed to respect MHD interface, but not used in the implementation.
    std::ignore = kind;

    http::header_view_map* dhr = static_cast<http::header_view_map*>(cls);
    (*dhr)[key] = value;
    return MHD_YES;
}

const http::header_view_map& http_request_impl::ensure_headerlike_cache(MHD_ValueKind kind) const {
    // Pick the cache slot and build-flag matching `kind`. We resolve them
    // up front so the cold (build) and warm (return) paths share a single
    // reference without re-switching.
    http::header_view_map* cache = nullptr;
    bool* built = nullptr;
    const http::header_map* local_fallback = nullptr;
    switch (kind) {
        case MHD_HEADER_KIND:
            cache = &headers_cached_;
            built = &headers_cache_built_;
            local_fallback = &headers_local;
            break;
        case MHD_FOOTER_KIND:
            cache = &footers_cached_;
            built = &footers_cache_built_;
            local_fallback = &footers_local;
            break;
        case MHD_COOKIE_KIND:
            cache = &cookies_cached_;
            built = &cookies_cache_built_;
            local_fallback = &cookies_local;
            break;
        default:
            // Unsupported kind: hand back the headers cache (kept empty)
            // as a safe fallback; the public API never reaches here.
            cache = &headers_cached_;
            built = &headers_cache_built_;
            local_fallback = &headers_local;
            break;
    }

    if (*built) {
        return *cache;
    }

    // Test-request path: connection_ is null, build the view map from
    // local owning storage (the create_test_request builder populated it).
    if (connection_ == nullptr) {
        if (local_fallback != nullptr) {
            for (const auto& [k, v] : *local_fallback) {
                (*cache)[k] = v;
            }
        }
        *built = true;
        return *cache;
    }

    // Live-request path: ask MHD to enumerate values for this kind into
    // the cache. The string_view keys/values alias MHD-owned storage that
    // outlives the request handler.
    MHD_get_connection_values(connection_, kind, &http_request_impl::build_request_header,
                              reinterpret_cast<void*>(cache));
    *built = true;
    return *cache;
}

MHD_Result http_request_impl::build_request_args(void* cls, MHD_ValueKind kind,
                                                 const char* key, const char* arg_value) {
    // Parameters needed to respect MHD interface, but not used in the implementation.
    std::ignore = kind;

    arguments_accumulator* aa = static_cast<arguments_accumulator*>(cls);

    // Security guard (security-reviewer-iter1-2): reject requests that
    // exceed the per-request argument count or total byte budget. Both
    // limits prevent a crafted request with thousands of unique GET
    // arguments from exhausting the per-connection arena and the heap
    // upstream. Returning MHD_NO stops MHD's iteration over remaining
    // arguments immediately.
    std::string_view key_sv(key);
    std::string_view val_sv((arg_value != nullptr) ? arg_value : "");

    // Apply count limit: check how many unique keys exist so far.
    auto& args = *aa->arguments;
    const std::size_t new_unique =
        (args.find(key_sv) == args.end()) ? 1u : 0u;
    if (args.size() + new_unique > aa->max_args_count) {
        return MHD_NO;
    }

    // Apply byte limit: count key + value bytes accumulated so far.
    const std::size_t this_pair_bytes = key_sv.size() + val_sv.size();
    if (aa->accumulated_bytes + this_pair_bytes > aa->max_args_bytes) {
        return MHD_NO;
    }
    aa->accumulated_bytes += this_pair_bytes;

    // Unescape into a temporary std::string (the C-style unescaper is
    // string-typed). The unescape itself touches the global heap if the
    // key/value spill out of std::string's small-buffer; tracked by
    // TASK-018 (move the unescape onto the arena too).
    std::string value(val_sv);
    http::base_unescaper(&value, aa->unescaper);

    // Look up via heterogeneous string_view (no allocation), insert the
    // key as pmr::string in the map's allocator domain on miss. The
    // value vector is allocator-constructed in place via the same
    // allocator (scoped propagation gives nested pmr::strings the
    // right allocator too).
    auto pmr_alloc = args.get_allocator();
    auto it = args.find(key_sv);
    if (it == args.end()) {
        std::pmr::vector<std::pmr::string> empty(pmr_alloc);
        auto inserted = args.emplace(
            std::pmr::string(key_sv.data(), key_sv.size(), pmr_alloc),
            std::move(empty));
        it = inserted.first;
    }
    // emplace_back into a pmr::vector<pmr::string>: use (ptr, size); the
    // outer vector's allocator-propagating construct wires the inner
    // pmr::string's allocator automatically. Passing the allocator
    // ourselves leads to double-injection via uses-allocator construction.
    it->second.emplace_back(value.data(), value.size());
    return MHD_YES;
}

MHD_Result http_request_impl::build_request_querystring(void* cls, MHD_ValueKind kind,
                                                        const char* key_value, const char* arg_value) {
    // Parameters needed to respect MHD interface, but not used in the implementation.
    std::ignore = kind;

    // TASK-016: cls is a pmr::string* into impl_->querystring; growth
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
    MHD_get_connection_values(connection_, MHD_GET_ARGUMENT_KIND,
                              &http_request_impl::build_request_args,
                              reinterpret_cast<void*>(&aa));

    args_populated = true;
}

void http_request_impl::ensure_path_pieces_cached(std::string_view path) const {
    if (path_pieces_cached) {
        return;
    }
    // tokenize_url returns std::vector<std::string> (default-allocator).
    // Copy element-wise into the pmr-backed cache so the stored strings
    // live on the arena, not the heap.
    auto tokens = http::http_utils::tokenize_url(std::string(path));
    path_pieces.clear();
    path_pieces.reserve(tokens.size());
    for (auto& t : tokens) {
        // Vector's allocator-propagating construct wires the inner
        // pmr::string's allocator automatically.
        path_pieces.emplace_back(t.data(), t.size());
    }
    path_pieces_cached = true;
}

namespace {

// Helper: look up `key` via heterogeneous string_view (no alloc), insert
// a pmr::string key + an empty vector if missing, then append `value`.
// All allocations use the map's allocator (the per-connection arena).
inline auto& find_or_insert_arg(
    std::pmr::map<std::pmr::string, std::pmr::vector<std::pmr::string>,
                  http::arg_comparator>& args,
    std::string_view key) {
    auto pmr_alloc = args.get_allocator();
    auto it = args.find(key);
    if (it == args.end()) {
        std::pmr::vector<std::pmr::string> empty(pmr_alloc);
        auto inserted = args.emplace(
            std::pmr::string(key.data(), key.size(), pmr_alloc),
            std::move(empty));
        it = inserted.first;
    }
    return it->second;
}

inline void append_arg(
    std::pmr::map<std::pmr::string, std::pmr::vector<std::pmr::string>,
                  http::arg_comparator>& args,
    std::string_view key, std::string_view value) {
    auto& vec = find_or_insert_arg(args, key);
    // emplace_back forwards (ptr, size) to pmr::string's (ptr, size, alloc)
    // ctor; the trailing allocator is supplied by the vector's
    // allocator-propagating construct.
    vec.emplace_back(value.data(), value.size());
}

}  // namespace

void http_request_impl::set_arg(const std::string& key, const std::string& value,
                                std::size_t content_size_limit) {
    append_arg(unescaped_args, key,
               std::string_view(value).substr(
                   0, std::min(value.size(), content_size_limit)));
}

void http_request_impl::set_arg(const char* key, const char* value, std::size_t size,
                                std::size_t content_size_limit) {
    append_arg(unescaped_args, key,
               std::string_view(value, std::min(size, content_size_limit)));
}

void http_request_impl::set_arg_flat(const std::string& key, const std::string& value,
                                     std::size_t content_size_limit) {
    auto& vec = find_or_insert_arg(unescaped_args, key);
    vec.clear();
    const auto bounded_size = std::min(value.size(), content_size_limit);
    vec.emplace_back(value.data(), bounded_size);
}

void http_request_impl::set_args(const std::map<std::string, std::string>& args,
                                 std::size_t content_size_limit) {
    for (auto const& [key, value] : args) {
        append_arg(unescaped_args, key,
                   std::string_view(value).substr(
                       0, std::min(value.size(), content_size_limit)));
    }
}

void http_request_impl::grow_last_arg(const std::string& key, const std::string& value) {
    auto& vec = find_or_insert_arg(unescaped_args, key);
    if (!vec.empty()) {
        vec.back() += value;
    } else {
        vec.emplace_back(value.data(), value.size());
    }
}

#ifdef HAVE_BAUTH
void http_request_impl::fetch_user_pass() const {
    // Test-request path: connection_ is null, credentials already set.
    if (connection_ == nullptr) {
        return;
    }
    struct MHD_BasicAuthInfo* info = MHD_basic_auth_get_username_password3(connection_);

    if (info != nullptr) {
        username.assign(info->username, info->username_len);
        if (info->password != nullptr) {
            password.assign(info->password, info->password_len);
        }
        MHD_free(info);
    }
}
#endif  // HAVE_BAUTH

void http_request_impl_deleter::operator()(http_request_impl* p) const noexcept {
    if (fn != nullptr) {
        fn(p);
    }
}

}  // namespace detail
}  // namespace httpserver
