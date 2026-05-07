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

#include "httpserver/http_request.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <map>
#include <memory>
#include <memory_resource>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// TASK-016: pull in connection_state to read the per-connection arena
// out of MHD on impl construction. Both headers are gated by
// HTTPSERVER_COMPILATION so this stays internal.
#include "httpserver/detail/http_request_impl.hpp"
#include "httpserver/detail/webserver_impl.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"

#ifdef HAVE_GNUTLS
#include <gnutls/x509.h>

namespace {

// RAII wrapper for gnutls_x509_crt_t to ensure proper cleanup.
class scoped_x509_cert {
 public:
    scoped_x509_cert() : cert_(nullptr), valid_(false) {}

    ~scoped_x509_cert() {
        if (cert_ != nullptr) {
            gnutls_x509_crt_deinit(cert_);
        }
    }

    // Initialize from a TLS session's peer certificate
    // Returns true if certificate was successfully loaded
    bool init_from_session(gnutls_session_t session) {
        unsigned int list_size = 0;
        const gnutls_datum_t* cert_list = gnutls_certificate_get_peers(session, &list_size);

        if (cert_list == nullptr || list_size == 0) {
            return false;
        }

        if (gnutls_x509_crt_init(&cert_) != GNUTLS_E_SUCCESS) {
            cert_ = nullptr;
            return false;
        }

        if (gnutls_x509_crt_import(cert_, &cert_list[0], GNUTLS_X509_FMT_DER) != GNUTLS_E_SUCCESS) {
            gnutls_x509_crt_deinit(cert_);
            cert_ = nullptr;
            return false;
        }

        valid_ = true;
        return true;
    }

    bool is_valid() const { return valid_; }
    gnutls_x509_crt_t get() const { return cert_; }

    // Movable
    scoped_x509_cert(scoped_x509_cert&& other) noexcept
        : cert_(other.cert_), valid_(other.valid_) {
        other.cert_ = nullptr;
        other.valid_ = false;
    }
    scoped_x509_cert& operator=(scoped_x509_cert&& other) noexcept {
        if (this != &other) {
            if (cert_ != nullptr) gnutls_x509_crt_deinit(cert_);
            cert_ = other.cert_;
            valid_ = other.valid_;
            other.cert_ = nullptr;
            other.valid_ = false;
        }
        return *this;
    }

    // Non-copyable
    scoped_x509_cert(const scoped_x509_cert&) = delete;
    scoped_x509_cert& operator=(const scoped_x509_cert&) = delete;

 private:
    gnutls_x509_crt_t cert_;
    bool valid_;
};

}  // namespace
#endif  // HAVE_GNUTLS

namespace httpserver {

const char http_request::EMPTY[] = "";

// (arguments_accumulator moved to http_request_impl.hpp so unit tests
// can drive build_request_args directly; see security-reviewer-iter1-2.)

// ============================================================================
// detail::http_request_impl method bodies
//
// Each body is the verbatim relocation of the v1 http_request method,
// with `cache->X` rewritten to `this->X`, `underlying_connection` to
// `connection_`, `unescaper` to `unescaper_`, `files` to `files_`, and
// `file_cleanup_callback` to `file_cleanup_callback_`.
// ============================================================================

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

#ifdef HAVE_GNUTLS
bool http_request_impl::has_tls_session() const {
    // Test-request path: connection_ is null, return the local flag.
    if (connection_ == nullptr) {
        return tls_enabled_local;
    }
    const MHD_ConnectionInfo* conninfo = MHD_get_connection_info(connection_, MHD_CONNECTION_INFO_GNUTLS_SESSION);
    return (conninfo != nullptr);
}

gnutls_session_t http_request_impl::get_tls_session() const {
    // TASK-019: the test-request path (connection_ == nullptr) has no
    // live MHD connection to query; return null so callers fall back
    // to the no-cert sentinels rather than UB-dereferencing in MHD.
    if (connection_ == nullptr) {
        return nullptr;
    }
    const MHD_ConnectionInfo* conninfo = MHD_get_connection_info(connection_, MHD_CONNECTION_INFO_GNUTLS_SESSION);

    if (conninfo == nullptr) {
        return nullptr;
    }

    return static_cast<gnutls_session_t>(conninfo->tls_session);
}

bool http_request_impl::has_client_certificate() const {
    if (!has_tls_session()) {
        return false;
    }

    // TASK-019: even when the test-request flag advertises a TLS
    // session, there is no live MHD connection from which to extract
    // peer certs. Bail out before calling into GnuTLS so the public
    // accessors honour the "no live cert means false / empty / -1"
    // sentinel contract.
    gnutls_session_t session = get_tls_session();
    if (session == nullptr) {
        return false;
    }
    unsigned int list_size = 0;
    const gnutls_datum_t* cert_list = gnutls_certificate_get_peers(session, &list_size);

    return (cert_list != nullptr && list_size > 0);
}

void http_request_impl::populate_all_cert_fields() const {
    if (client_cert_fields_cached) {
        return;
    }

    client_cert_fields_cached = true;

    gnutls_session_t session = nullptr;
    if (has_tls_session()) {
        session = get_tls_session();
    }

    scoped_x509_cert cert;
    if (session != nullptr) {
        cert.init_from_session(session);
    }

    if (!cert.is_valid()) {
        // Default values (empty strings and -1) are already set by the
        // impl member initializers; client_cert_verified defaults to false.
        return;
    }

    // Client certificate verification
    {
        unsigned int status = 0;
        if (gnutls_certificate_verify_peers2(session, &status) == GNUTLS_E_SUCCESS) {
            client_cert_verified = (status == 0);
        }
    }

    // Subject DN
    {
        size_t dn_size = 0;
        gnutls_x509_crt_get_dn(cert.get(), nullptr, &dn_size);
        std::string dn(dn_size, '\0');
        if (gnutls_x509_crt_get_dn(cert.get(), &dn[0], &dn_size) == GNUTLS_E_SUCCESS) {
            if (!dn.empty() && dn.back() == '\0') dn.pop_back();
            // pmr::string has no cross-allocator copy-assign, so we
            // assign through the .assign(ptr, len) overload.
            client_cert_dn.assign(dn.data(), dn.size());
        }
    }

    // Issuer DN
    {
        size_t dn_size = 0;
        gnutls_x509_crt_get_issuer_dn(cert.get(), nullptr, &dn_size);
        std::string dn(dn_size, '\0');
        if (gnutls_x509_crt_get_issuer_dn(cert.get(), &dn[0], &dn_size) == GNUTLS_E_SUCCESS) {
            if (!dn.empty() && dn.back() == '\0') dn.pop_back();
            client_cert_issuer_dn.assign(dn.data(), dn.size());
        }
    }

    // Common Name
    {
        size_t cn_size = 0;
        gnutls_x509_crt_get_dn_by_oid(cert.get(), GNUTLS_OID_X520_COMMON_NAME, 0, 0, nullptr, &cn_size);
        if (cn_size > 0) {
            std::string cn(cn_size, '\0');
            if (gnutls_x509_crt_get_dn_by_oid(cert.get(), GNUTLS_OID_X520_COMMON_NAME, 0, 0, &cn[0], &cn_size) == GNUTLS_E_SUCCESS) {
                if (!cn.empty() && cn.back() == '\0') cn.pop_back();
                client_cert_cn.assign(cn.data(), cn.size());
            }
        }
    }

    // SHA-256 fingerprint
    {
        unsigned char fingerprint[32];
        size_t fingerprint_size = sizeof(fingerprint);
        if (gnutls_x509_crt_get_fingerprint(cert.get(), GNUTLS_DIG_SHA256, fingerprint, &fingerprint_size) == GNUTLS_E_SUCCESS) {
            std::string hex_fingerprint;
            hex_fingerprint.reserve(fingerprint_size * 2);
            for (size_t i = 0; i < fingerprint_size; ++i) {
                char hex[3];
                snprintf(hex, sizeof(hex), "%02x", fingerprint[i]);
                hex_fingerprint += hex;
            }
            client_cert_fingerprint_sha256.assign(hex_fingerprint.data(),
                                                  hex_fingerprint.size());
        }
    }

    // Validity times. The GnuTLS API returns std::time_t; we cast to
    // int64_t so the public accessors can return a fixed-width type
    // regardless of how the local platform sizes time_t. ((time_t)-1
    // round-trips cleanly because both width and signedness are
    // preserved by the static_cast.)
    client_cert_not_before = static_cast<std::int64_t>(
        gnutls_x509_crt_get_activation_time(cert.get()));
    client_cert_not_after = static_cast<std::int64_t>(
        gnutls_x509_crt_get_expiration_time(cert.get()));
}
#endif  // HAVE_GNUTLS

}  // namespace detail

// ============================================================================
// http_request: public-API forwarders + small outer-state setters.
// ============================================================================

namespace detail {

// Heap-deleter. The impl was allocated by std::make_unique (= operator
// new), so destruction goes through operator delete: the same as v1.
static void delete_impl_heap(http_request_impl* p) noexcept {
    delete p;
}

// Arena-deleter. The impl was placement-constructed inside a
// std::pmr::monotonic_buffer_resource. We must run its destructor (so
// every contained pmr::string/vector/map releases external resources
// like file_info disk handles) but MUST NOT call operator delete: the
// memory is owned by the arena and will be reclaimed wholesale by
// arena_.release() in webserver_impl::request_completed.
static void destroy_impl_arena(http_request_impl* p) noexcept {
    if (p != nullptr) {
        p->~http_request_impl();
    }
}

void http_request_impl_deleter::operator()(http_request_impl* p) const noexcept {
    if (fn != nullptr) {
        fn(p);
    }
}

}  // namespace detail

namespace {

// TASK-016: pick the right memory resource for an http_request_impl.
// On the live request path (a real MHD_Connection*), look up the
// per-connection arena set by webserver_impl::connection_notify and use
// it. If nothing is registered (test paths, very old MHD versions, or
// connection_notify hasn't fired yet for some reason), fall back to the
// default heap resource so behavior matches v1.
//
// performance-reviewer-iter1-4: the fallback is intentionally silent in
// production (to preserve v1 behaviour), but in debug builds we log a
// warning and increment a counter so integration tests can observe
// misconfiguration (e.g. MHD_OPTION_NOTIFY_CONNECTION not wired).
// Access the counter via httpserver::detail::arena_fallback_count().
static std::atomic<std::uint64_t> g_arena_fallback_count{0};

std::pmr::memory_resource* pick_resource(struct MHD_Connection* connection) {
    if (connection == nullptr) {
        return std::pmr::get_default_resource();
    }
    const MHD_ConnectionInfo* ci =
        MHD_get_connection_info(connection, MHD_CONNECTION_INFO_SOCKET_CONTEXT);
    if (ci == nullptr || ci->socket_context == nullptr) {
#ifndef NDEBUG
        ++g_arena_fallback_count;
        // Emit a single-line diagnostic so integration tests and CI logs
        // surface misconfiguration without crashing.
        fprintf(stderr,
                "[libhttpserver] WARN: connection %p has no arena "
                "socket_context; falling back to heap allocation "
                "(fallback count: %" PRIu64 ")\n",
                static_cast<void*>(connection),
                g_arena_fallback_count.load());
#endif
        return std::pmr::get_default_resource();
    }
    auto* cs = static_cast<httpserver::detail::connection_state*>(ci->socket_context);
    return &cs->arena_;
}

}  // namespace

http_request::http_request(struct MHD_Connection* underlying_connection, unescaper_ptr unescaper)
    : impl_(nullptr, detail::http_request_impl_deleter{nullptr}) {
    auto* res = pick_resource(underlying_connection);
    if (res == std::pmr::get_default_resource()) {
        // Heap-fallback: matches v1 lifetime exactly; deleter frees via
        // operator delete.
        impl_.reset(new detail::http_request_impl(underlying_connection, unescaper));
        impl_.get_deleter().fn = &detail::delete_impl_heap;
    } else {
        // Arena-backed: allocate and construct via polymorphic_allocator
        // so the impl's pmr-aware members propagate the arena allocator.
        // Reclamation is by destructor only; arena_.release() in
        // webserver_impl::request_completed reclaims the bytes.
        std::pmr::polymorphic_allocator<detail::http_request_impl> alloc(res);
        auto* p = alloc.new_object<detail::http_request_impl>(
            underlying_connection, unescaper, std::pmr::polymorphic_allocator<>(res));
        impl_.reset(p);
        impl_.get_deleter().fn = &detail::destroy_impl_arena;
    }

    // TASK-018: assemble the querystring eagerly on the live-MHD path so
    // the public reader can be `noexcept`. On the test-request path
    // (connection_ == nullptr) the create_test_request builder is the
    // sole writer of impl_->querystring; leave it untouched here.
    // Allocations during assembly land on the per-connection arena (or
    // the heap fallback) and may throw -- that's permitted during
    // construction.
    if (impl_->connection_ != nullptr) {
        MHD_get_connection_values(
            impl_->connection_, MHD_GET_ARGUMENT_KIND,
            &detail::http_request_impl::build_request_querystring,
            reinterpret_cast<void*>(&impl_->querystring));
    }
}

http_request::~http_request() {
    if (impl_) {
        for (const auto& [key, by_filename] : impl_->files_) {
            for (const auto& [fname, finfo] : by_filename) {
                bool should_delete = true;
                if (impl_->file_cleanup_callback_ != nullptr) {
                    try {
                        should_delete = impl_->file_cleanup_callback_(key, fname, finfo);
                    } catch (...) {
                        // If callback throws, default to deleting the file.
                        should_delete = true;
                    }
                }
                if (should_delete) {
                    // C++17 has std::filesystem::remove()
                    remove(finfo.get_file_system_file_name().c_str());
                }
            }
        }
    }
}

void http_request::set_method(const std::string& method) {
    this->method = method;
}

const std::vector<std::string>& http_request::get_path_pieces() const {
    // TASK-017: lazily populate the public-typed mirror cache from the
    // (already-built) pmr-backed `path_pieces` and return it by const&.
    // Two caches in lockstep -- the pmr one stays arena-friendly for any
    // future internal consumer; the public one is what the API exposes.
    impl_->ensure_path_pieces_cached(path);
    if (!impl_->path_pieces_public_built_) {
        impl_->path_pieces_public_.clear();
        impl_->path_pieces_public_.reserve(impl_->path_pieces.size());
        for (const auto& p : impl_->path_pieces) {
            impl_->path_pieces_public_.emplace_back(p.data(), p.size());
        }
        impl_->path_pieces_public_built_ = true;
    }
    return impl_->path_pieces_public_;
}

const std::string http_request::get_path_piece(int index) const {
    impl_->ensure_path_pieces_cached(path);
    if (static_cast<int>(impl_->path_pieces.size()) > index) {
        const auto& p = impl_->path_pieces[index];
        return std::string(p.data(), p.size());
    }
    return EMPTY;
}

#ifdef HAVE_DAUTH
http::http_utils::digest_auth_result http_request::check_digest_auth(
    const std::string& realm,
    const std::string& password,
    unsigned int nonce_timeout,
    uint32_t max_nc,
    http::http_utils::digest_algorithm algo) const {
    std::string_view digested_user = get_digested_user();

    enum MHD_DigestAuthResult result = MHD_digest_auth_check3(
        impl_->connection_,
        realm.c_str(),
        digested_user.data(),
        password.c_str(),
        nonce_timeout,
        max_nc,
        MHD_DIGEST_AUTH_MULT_QOP_ANY_NON_INT,
        static_cast<MHD_DigestAuthMultiAlgo3>(algo));

    return static_cast<http::http_utils::digest_auth_result>(result);
}

http::http_utils::digest_auth_result http_request::check_digest_auth_digest(
    const std::string& realm,
    const void* userdigest,
    size_t userdigest_size,
    unsigned int nonce_timeout,
    uint32_t max_nc,
    http::http_utils::digest_algorithm algo) const {
    std::string_view digested_user = get_digested_user();

    enum MHD_DigestAuthResult result = MHD_digest_auth_check_digest3(
        impl_->connection_,
        realm.c_str(),
        digested_user.data(),
        userdigest,
        userdigest_size,
        nonce_timeout,
        max_nc,
        MHD_DIGEST_AUTH_MULT_QOP_ANY_NON_INT,
        static_cast<MHD_DigestAuthMultiAlgo3>(algo));

    return static_cast<http::http_utils::digest_auth_result>(result);
}
#endif  // HAVE_DAUTH

std::string_view http_request::get_header(std::string_view key) const {
    return impl_->get_connection_value(key, MHD_HEADER_KIND);
}

const http::header_view_map& http_request::get_headers() const {
    return impl_->ensure_headerlike_cache(MHD_HEADER_KIND);
}

std::string_view http_request::get_footer(std::string_view key) const {
    return impl_->get_connection_value(key, MHD_FOOTER_KIND);
}

const http::header_view_map& http_request::get_footers() const {
    return impl_->ensure_headerlike_cache(MHD_FOOTER_KIND);
}

std::string_view http_request::get_cookie(std::string_view key) const {
    return impl_->get_connection_value(key, MHD_COOKIE_KIND);
}

const http::header_view_map& http_request::get_cookies() const {
    return impl_->ensure_headerlike_cache(MHD_COOKIE_KIND);
}

http_arg_value http_request::get_arg(std::string_view key) const {
    impl_->populate_args();

    auto it = impl_->unescaped_args.find(key);
    if (it != impl_->unescaped_args.end()) {
        http_arg_value arg;
        arg.values.reserve(it->second.size());
        for (const auto& value : it->second) {
            arg.values.push_back(value);
        }
        return arg;
    }
    return http_arg_value();
}

std::string_view http_request::get_arg_flat(std::string_view key) const {
    impl_->populate_args();

    auto const it = impl_->unescaped_args.find(key);

    if (it != impl_->unescaped_args.end()) {
        return it->second[0];
    }

    return impl_->get_connection_value(key, MHD_GET_ARGUMENT_KIND);
}

const http::arg_view_map& http_request::get_args() const {
    // TASK-017: lazily populate the args view-map cache from the pmr-backed
    // `unescaped_args` (which is itself populated lazily by populate_args()).
    impl_->populate_args();
    if (!impl_->args_view_cache_built_) {
        impl_->args_view_cached_.clear();
        for (const auto& [key, value] : impl_->unescaped_args) {
            // The string_view keys/values alias the pmr-backed strings owned
            // by `unescaped_args` -- same lifetime as the request.
            auto& arg_values = impl_->args_view_cached_[
                std::string_view(key.data(), key.size())];
            arg_values.values.reserve(value.size());
            for (const auto& v : value) {
                arg_values.values.emplace_back(v.data(), v.size());
            }
        }
        impl_->args_view_cache_built_ = true;
    }
    return impl_->args_view_cached_;
}

const std::map<std::string_view, std::string_view, http::arg_comparator> http_request::get_args_flat() const {
    impl_->populate_args();
    std::map<std::string_view, std::string_view, http::arg_comparator> ret;
    for (const auto& [key, val] : impl_->unescaped_args) {
        ret[key] = val[0];
    }
    return ret;
}

http::file_info& http_request::get_or_create_file_info(const std::string& key, const std::string& upload_file_name) {
    return impl_->files_[key][upload_file_name];
}

const std::map<std::string, std::map<std::string, http::file_info>>& http_request::get_files() const noexcept {
    return impl_->files_;
}

std::string_view http_request::get_querystring() const noexcept {
    // TASK-018: querystring is assembled eagerly during construction (live
    // MHD path) or set directly by create_test_request (test path), so the
    // reader is a trivial member access -- genuinely noexcept.
    return impl_->querystring;
}

#ifdef HAVE_BAUTH
std::string_view http_request::get_user() const {
    if (!impl_->username.empty()) {
        return impl_->username;
    }
    impl_->fetch_user_pass();
    return impl_->username;
}

std::string_view http_request::get_pass() const {
    if (!impl_->password.empty()) {
        return impl_->password;
    }
    impl_->fetch_user_pass();
    return impl_->password;
}
#endif  // HAVE_BAUTH

#ifdef HAVE_DAUTH
std::string_view http_request::get_digested_user() const {
    if (!impl_->digested_user.empty()) {
        return impl_->digested_user;
    }

    // Test-request path: connection_ is null, digested_user already set.
    if (impl_->connection_ == nullptr) {
        return impl_->digested_user;
    }

    struct MHD_DigestAuthUsernameInfo* info = MHD_digest_auth_get_username3(impl_->connection_);

    impl_->digested_user = EMPTY;
    if (info != nullptr) {
        if (info->username != nullptr) {
            impl_->digested_user.assign(info->username, info->username_len);
        }
        MHD_free(info);
    }

    return impl_->digested_user;
}
#endif  // HAVE_DAUTH

// ----------------------------------------------------------------------
// TASK-019: high-level GnuTLS accessors. Public surface is unconditional
// (same symbols visible whether HAVE_GNUTLS is on or off at build time);
// only the body of each method dispatches on HAVE_GNUTLS. Sentinel
// returns when GnuTLS is disabled match the architecture spec §7:
// empty for the four string_view accessors, false for the three
// predicates, -1 for the two time accessors.
//
// The five accessors documented as `noexcept` (the three predicates and
// the two times) wrap populate_all_cert_fields() in try/catch so that a
// hypothetical std::bad_alloc from the cache-fill path doesn't violate
// the noexcept commitment; on throw we return the documented sentinel.
// The four string_view accessors deliberately omit `noexcept` so the
// allocator failure mode is observable (and they don't need a
// try/catch).
// ----------------------------------------------------------------------
bool http_request::has_tls_session() const noexcept {
#ifdef HAVE_GNUTLS
    return impl_->has_tls_session();
#else
    return false;
#endif
}

bool http_request::has_client_certificate() const noexcept {
#ifdef HAVE_GNUTLS
    return impl_->has_client_certificate();
#else
    return false;
#endif
}

std::string_view http_request::get_client_cert_dn() const {
#ifdef HAVE_GNUTLS
    impl_->populate_all_cert_fields();
    return std::string_view(impl_->client_cert_dn.data(),
                            impl_->client_cert_dn.size());
#else
    return std::string_view{};
#endif
}

std::string_view http_request::get_client_cert_issuer_dn() const {
#ifdef HAVE_GNUTLS
    impl_->populate_all_cert_fields();
    return std::string_view(impl_->client_cert_issuer_dn.data(),
                            impl_->client_cert_issuer_dn.size());
#else
    return std::string_view{};
#endif
}

std::string_view http_request::get_client_cert_cn() const {
#ifdef HAVE_GNUTLS
    impl_->populate_all_cert_fields();
    return std::string_view(impl_->client_cert_cn.data(),
                            impl_->client_cert_cn.size());
#else
    return std::string_view{};
#endif
}

std::string_view http_request::get_client_cert_fingerprint_sha256() const {
#ifdef HAVE_GNUTLS
    impl_->populate_all_cert_fields();
    return std::string_view(impl_->client_cert_fingerprint_sha256.data(),
                            impl_->client_cert_fingerprint_sha256.size());
#else
    return std::string_view{};
#endif
}

bool http_request::is_client_cert_verified() const noexcept {
#ifdef HAVE_GNUTLS
    try {
        impl_->populate_all_cert_fields();
        return impl_->client_cert_verified;
    } catch (...) {
        return false;
    }
#else
    return false;
#endif
}

std::int64_t http_request::get_client_cert_not_before() const noexcept {
#ifdef HAVE_GNUTLS
    try {
        impl_->populate_all_cert_fields();
        return impl_->client_cert_not_before;
    } catch (...) {
        return -1;
    }
#else
    return -1;
#endif
}

std::int64_t http_request::get_client_cert_not_after() const noexcept {
#ifdef HAVE_GNUTLS
    try {
        impl_->populate_all_cert_fields();
        return impl_->client_cert_not_after;
    } catch (...) {
        return -1;
    }
#else
    return -1;
#endif
}

std::string_view http_request::get_requestor() const {
    if (!impl_->requestor_ip.empty()) {
        return impl_->requestor_ip;
    }

    // Test-request path: connection_ is null, requestor_ip already set.
    if (impl_->connection_ == nullptr) {
        return impl_->requestor_ip;
    }

    const MHD_ConnectionInfo* conninfo = MHD_get_connection_info(impl_->connection_, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    if (conninfo == nullptr) {
        return EMPTY;
    }

    auto ip = http::get_ip_str(conninfo->client_addr);
    impl_->requestor_ip.assign(ip.data(), ip.size());
    return impl_->requestor_ip;
}

uint16_t http_request::get_requestor_port() const {
    // Test-request path: connection_ is null, use local port.
    if (impl_->connection_ == nullptr) {
        return impl_->requestor_port_local;
    }

    const MHD_ConnectionInfo* conninfo = MHD_get_connection_info(impl_->connection_, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    if (conninfo == nullptr) {
        return 0;
    }

    return http::get_port(conninfo->client_addr);
}

// ----- Private setters used by webserver_impl dispatch. ---------------------

void http_request::set_arg(const std::string& key, const std::string& value) {
    impl_->set_arg(key, value, content_size_limit);
}

void http_request::set_arg(const char* key, const char* value, size_t size) {
    impl_->set_arg(key, value, size, content_size_limit);
}

void http_request::set_arg_flat(const std::string& key, const std::string& value) {
    impl_->set_arg_flat(key, value, content_size_limit);
}

void http_request::set_args(const std::map<std::string, std::string>& args) {
    impl_->set_args(args, content_size_limit);
}

void http_request::grow_last_arg(const std::string& key, const std::string& value) {
    impl_->grow_last_arg(key, value);
}

void http_request::set_file_cleanup_callback(file_cleanup_callback_ptr callback) {
    impl_->file_cleanup_callback_ = callback;
}

std::ostream& operator<< (std::ostream& os, const http_request& r) {
    os << r.get_method() << " Request [";
#ifdef HAVE_BAUTH
    os << "user:\"" << r.get_user() << "\" pass:\"" << r.get_pass() << "\"";
#endif  // HAVE_BAUTH
    os << "] path:\"" << r.get_path() << "\"" << std::endl;

    http::dump_header_map(os, "Headers", r.get_headers());
    http::dump_header_map(os, "Footers", r.get_footers());
    http::dump_header_map(os, "Cookies", r.get_cookies());
    http::dump_arg_map(os, "Query Args", r.get_args());

    os << "    Version [ " << r.get_version() << " ] Requestor [ " << r.get_requestor()
       << " ] Port [ " << r.get_requestor_port() << " ]" << std::endl;

    return os;
}

}  // namespace httpserver
