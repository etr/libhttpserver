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

// Map a MHD_ValueKind to the corresponding test-request local storage map.
// Returns nullptr for kinds that have no local-storage counterpart (e.g.
// MHD_GET_ARGUMENT_KIND). Called from both get_connection_value() and
// ensure_headerlike_cache() to keep the kind→map dispatch in one place.
// Adding a new kind (e.g. MHD_POSTDATA_KIND) only requires updating this
// function. (code-simplifier-iter1 findings #3/#4 / major review items)
const http::header_map* http_request_impl::local_map_for(MHD_ValueKind kind) const noexcept {
    switch (kind) {
        case MHD_HEADER_KIND: return &headers_local;
        case MHD_FOOTER_KIND: return &footers_local;
        case MHD_COOKIE_KIND: return &cookies_local;
        default:              return nullptr;
    }
}

std::string_view http_request_impl::get_connection_value(std::string_view key, MHD_ValueKind kind) const {
    // Test-request path: connection_ is null, fall back to local storage.
    if (connection_ == nullptr) {
        const auto* map = local_map_for(kind);
        if (map != nullptr) {
            // header_comparator is_transparent: find(string_view) works without
            // constructing a temporary std::string. (performance-reviewer-iter1-30)
            auto it = map->find(key);
            if (it != map->end()) return it->second;
        }
        return http_request::EMPTY;
    }

    // std::string_view is not guaranteed null-terminated, but
    // MHD_lookup_connection_value requires a C-string. Construct a temporary
    // null-terminated string from the view to avoid an OOB read when callers
    // pass a non-terminated substring view. (Item 20: security-reviewer.)
    const std::string key_str(key);
    const char* header_c = MHD_lookup_connection_value(connection_, kind, key_str.c_str());

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
    // reference without re-switching. local_map_for() provides the
    // local-storage fallback pointer without duplicating the switch.
    // (code-simplifier-iter1 findings #3/#4 / major review items)
    http::header_view_map* cache = nullptr;
    bool* built = nullptr;
    switch (kind) {
        case MHD_HEADER_KIND:
            cache = &headers_cached_;
            built = &headers_cache_built_;
            break;
        case MHD_FOOTER_KIND:
            cache = &footers_cached_;
            built = &footers_cache_built_;
            break;
        case MHD_COOKIE_KIND:
            cache = &cookies_cached_;
            built = &cookies_cache_built_;
            break;
        default:
            // The public API (get_headers / get_footers / get_cookies) only
            // passes valid kinds; this branch is unreachable through all
            // current callers. An assertion here makes any future misuse fail
            // loudly rather than silently returning stale header data
            // (security-reviewer-iter1-16 / code-quality-reviewer-iter1-3).
            assert(false && "ensure_headerlike_cache: unsupported MHD_ValueKind");
            // Fallback for release builds: return headers cache to avoid UB.
            cache = &headers_cached_;
            built = &headers_cache_built_;
            break;
    }
    const http::header_map* local_fallback = local_map_for(kind);

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


void http_request_impl::ensure_args_flat_view_cached() const {
    // Build the "first value per key" view map from unescaped_args. Keys
    // and values are string_views aliasing the pmr-backed storage owned
    // by unescaped_args -- same lifetime as the request.
    if (args_flat_view_cache_built_) {
        return;
    }
    args_flat_view_cached_.clear();
    for (const auto& [key, values] : unescaped_args) {
        if (values.empty()) {
            continue;
        }
        args_flat_view_cached_.emplace(
            std::string_view(key.data(), key.size()),
            std::string_view(values[0].data(), values[0].size()));
    }
    args_flat_view_cache_built_ = true;
}

void http_request_impl::ensure_args_view_cached() const {
    // Populate the arg view-map cache from unescaped_args. Must be called
    // after populate_args(). Building the cache here (inside the impl class)
    // keeps all cache-maintenance code in one place, analogous to
    // ensure_headerlike_cache() / ensure_path_pieces_cached().
    // (code-simplifier-iter1-9)
    if (args_view_cache_built_) {
        return;
    }
    args_view_cached_.clear();
    for (const auto& [key, value] : unescaped_args) {
        // The string_view keys/values alias the pmr-backed strings owned
        // by `unescaped_args` -- same lifetime as the request.
        auto& arg_values = args_view_cached_[
            std::string_view(key.data(), key.size())];
        arg_values.values.reserve(value.size());
        for (const auto& v : value) {
            arg_values.values.emplace_back(v.data(), v.size());
        }
    }
    args_view_cache_built_ = true;
}

void http_request_impl::ensure_path_pieces_cached(std::string_view path) const {
    if (path_pieces_cache_built_) {
        return;
    }
    // tokenize_url returns std::vector<std::string>; move the tokens
    // straight into the cache (single heap-backed vector, returned by
    // const& from http_request::get_path_pieces()).
    path_pieces_cached_ = http::http_utils::tokenize_url(std::string(path));
    path_pieces_cache_built_ = true;
}

void http_request_impl::ensure_cookies_parsed_cached() const {
    if (cookies_parsed_cache_built_) {
        return;
    }
    // Test-request path: walk the local owning storage and synthesize
    // a structured cookie per entry. Attributes are left default --
    // request-side cookies carry name+value only per RFC 6265 §5.4.
    if (connection_ == nullptr) {
        cookies_parsed_cached_.reserve(cookies_local.size());
        for (const auto& [k, v] : cookies_local) {
            // Bypass cookie::with_*'s strict validators: the test
            // builder writes arbitrary user-controlled bytes (the
            // common case is fine, but the validators reject ';' / '='
            // / whitespace in names which the v1 test API permitted).
            // http_request_impl is a friend of cookie (see cookie.hpp),
            // so name_/value_ are assigned directly rather than
            // round-tripping through a concatenated "k=v" string and
            // cookie::parse_cookie_header -- that round trip would also
            // mis-split a synthetic key containing '='.
            cookie c;
            c.name_ = k;
            c.value_ = v;
            cookies_parsed_cached_.push_back(std::move(c));
        }
        cookies_parsed_cache_built_ = true;
        return;
    }

    // Live-request path: read the raw `Cookie:` header from MHD and
    // hand it to cookie::parse_cookie_header. Going through the
    // dedicated parser (rather than walking MHD_COOKIE_KIND) means
    // browsers' quoted-value, byte-transparent, and skip-malformed
    // semantics all live in one tested code path.
    const char* raw = MHD_lookup_connection_value(
        connection_, MHD_HEADER_KIND, "Cookie");
    if (raw != nullptr) {
        cookies_parsed_cached_ = cookie::parse_cookie_header(raw);
    }
    cookies_parsed_cache_built_ = true;
}

namespace {

// Type alias to avoid repeating the verbose map type in every helper
// signature and call site. (code-quality-reviewer-iter1-11)
using args_map_t = std::pmr::map<std::pmr::string, std::pmr::vector<std::pmr::string>, http::arg_comparator>;  // NOLINT(whitespace/line_length)

// Helper: look up `key` via heterogeneous string_view (no alloc), insert
// a pmr::string key + an empty vector if missing, then append `value`.
// All allocations use the map's allocator (the per-connection arena).
inline std::pmr::vector<std::pmr::string>& find_or_insert_arg(
    args_map_t& args,
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
    args_map_t& args,
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
    // Invalidate the args_view_cached_ so a subsequent get_args() call
    // rebuilds the view map from the updated unescaped_args.
    // (security-reviewer-iter1-17: prevent stale cache after post-build mutation)
    args_view_cache_built_ = false;
    append_arg(unescaped_args, key,
               std::string_view(value).substr(
                   0, std::min(value.size(), content_size_limit)));
}

void http_request_impl::set_arg(const char* key, const char* value, std::size_t size,
                                std::size_t content_size_limit) {
    // Invalidate the args_view_cached_ so a subsequent get_args() call
    // rebuilds the view map from the updated unescaped_args.
    // (security-reviewer-iter1-17: prevent stale cache after post-build mutation)
    args_view_cache_built_ = false;
    append_arg(unescaped_args, key,
               std::string_view(value, std::min(size, content_size_limit)));
}

void http_request_impl::set_arg_flat(const std::string& key, const std::string& value,
                                     std::size_t content_size_limit) {
    // Invalidate the args_view_cached_ so a subsequent get_args() call
    // rebuilds the view map from the updated unescaped_args.
    // (security-reviewer-iter1-17: prevent stale cache after post-build mutation)
    args_view_cache_built_ = false;
    auto& vec = find_or_insert_arg(unescaped_args, key);
    vec.clear();
    const auto bounded_size = std::min(value.size(), content_size_limit);
    vec.emplace_back(value.data(), bounded_size);
}

void http_request_impl::set_args(const std::map<std::string, std::string>& args,
                                 std::size_t content_size_limit) {
    // Invalidate the args_view_cached_ so a subsequent get_args() call
    // rebuilds the view map from the updated unescaped_args.
    // (security-reviewer-iter1-17: prevent stale cache after post-build mutation)
    args_view_cache_built_ = false;
    for (auto const& [key, value] : args) {
        append_arg(unescaped_args, key,
                   std::string_view(value).substr(
                       0, std::min(value.size(), content_size_limit)));
    }
}

void http_request_impl::grow_last_arg(const std::string& key, const std::string& value) {
    // Invalidate the args_view_cached_ so a subsequent get_args() call
    // rebuilds the view map from the updated unescaped_args.
    // (security-reviewer-iter1-17: prevent stale cache after post-build mutation)
    args_view_cache_built_ = false;
    auto& vec = find_or_insert_arg(unescaped_args, key);
    if (!vec.empty()) {
        vec.back() += value;
    } else {
        vec.emplace_back(value.data(), value.size());
    }
}

#ifdef HAVE_BAUTH
void http_request_impl::fetch_user_pass() const {
    // Test-request path: connection_ is null, credentials already set
    // directly by create_test_request; nothing to fetch.
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
    // Mark as fetched so subsequent get_user()/get_pass() calls skip the
    // MHD round-trip -- even when the request carries no auth header and
    // the credential strings remain empty after this call.
    // (code-simplifier-iter1 finding #6 / major review item)
    user_pass_fetched = true;
}
#endif  // HAVE_BAUTH

void http_request_impl_deleter::operator()(http_request_impl* p) const noexcept {
    if (fn != nullptr) {
        fn(p);
    }
}

}  // namespace detail
}  // namespace httpserver
