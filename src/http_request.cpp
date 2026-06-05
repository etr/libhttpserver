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

namespace httpserver {

const char http_request::EMPTY[] = "";

// (arguments_accumulator moved to http_request_impl.hpp so unit tests
// can drive build_request_args directly; see security-reviewer-iter1-2.)


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

// http_request_impl_deleter::operator() lives in
// src/detail/http_request_impl.cpp alongside the impl method bodies.

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
    // TASK-017: lazily populate the cache and return it by const&. All
    // cache-maintenance logic lives inside the impl class.
    // (code-quality-reviewer-iter1-4 / code-simplifier-iter1-8)
    impl_->ensure_path_pieces_cached(path);
    return impl_->path_pieces_cached_;
}

const std::string http_request::get_path_piece(int index) const {
    impl_->ensure_path_pieces_cached(path);
    if (static_cast<int>(impl_->path_pieces_cached_.size()) > index) {
        return impl_->path_pieces_cached_[index];
    }
    return EMPTY;
}


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

// TASK-064: structured-cookie accessor. Forwards to the impl's
// lazily-built cache (parses the request's `Cookie:` header once, then
// returns the same buffer on every subsequent call).
const std::vector<cookie>& http_request::get_cookies_parsed() const {
    impl_->ensure_cookies_parsed_cached();
    return impl_->cookies_parsed_cached_;
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

    // Return EMPTY directly: populate_args() has already iterated over all
    // MHD_GET_ARGUMENT_KIND values and stored them in unescaped_args.  If the
    // key is absent after that pass, MHD will also not have it, so calling
    // get_connection_value(key, MHD_GET_ARGUMENT_KIND) is redundant and
    // would bypass unescaping, returning a raw (non-unescaped) value.
    // (Items 4, 12, 18: code-simplifier / code-quality / performance.)
    return EMPTY;
}

const http::arg_view_map& http_request::get_args() const {
    // TASK-017: lazily populate the args view-map cache. All build logic
    // lives inside the impl class (code-simplifier-iter1-9).
    impl_->populate_args();
    impl_->ensure_args_view_cached();
    return impl_->args_view_cached_;
}

const std::map<std::string_view, std::string_view, http::arg_comparator>&
http_request::get_args_flat() const {
    impl_->populate_args();
    impl_->ensure_args_flat_view_cached();
    return impl_->args_flat_view_cached_;
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
    // impl_ is always non-null on live requests; the default constructor is
    // private and only callable by create_test_request, which constructs a
    // valid impl_ before build() returns. (Item 5.)
    return impl_->querystring;
}


std::string_view http_request::get_requestor() const {
    // Single consolidated early-return covers both the cache-hit path
    // (requestor_ip_cached == true after first call on a live connection)
    // and the test-request path (connection_ == nullptr, requestor_ip was
    // set directly by create_test_request). Using a dedicated boolean instead
    // of checking requestor_ip.empty() is consistent with the args_populated /
    // path_pieces_cached / user_pass_fetched pattern and is robust if the
    // connection layer ever returns an empty IP string.
    // (code-simplifier-iter1 findings #7 + #8/#9 / major+minor review items)
    if (impl_->requestor_ip_cached || impl_->connection_ == nullptr) {
        return impl_->requestor_ip;
    }

    const MHD_ConnectionInfo* conninfo = MHD_get_connection_info(impl_->connection_, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    if (conninfo == nullptr) {
        return EMPTY;
    }

    auto ip = http::get_ip_str(conninfo->client_addr);
    impl_->requestor_ip.assign(ip.data(), ip.size());
    impl_->requestor_ip_cached = true;
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

void http_request::set_expose_credentials_in_logs(bool v) {
    impl_->expose_credentials_in_logs_ = v;
}

namespace {

constexpr std::string_view kRedacted = "<redacted>";

// Case-insensitive equality on ASCII bytes, reusing http_header_toupper
// so the casing rule stays in lock-step with http::header_comparator
// (which is what header_view_map orders keys by).
bool iequal_ascii(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(),
        [](char x, char y) {
            return http::http_header_toupper(x) == http::http_header_toupper(y);
        });
}

// TASK-057: Authorization-class header names whose values carry
// credential material (Basic / Digest / Bearer payloads). Matched
// case-insensitively against the keys in the Headers / Footers maps
// so that the redaction policy is robust to header-case variation.
bool is_authorization_header_key(std::string_view key) noexcept {
    constexpr std::string_view kAuth = "Authorization";
    constexpr std::string_view kProxyAuth = "Proxy-Authorization";
    return iequal_ascii(key, kAuth) || iequal_ascii(key, kProxyAuth);
}

// TASK-057: shared redaction-aware dumper for the Headers, Footers, and
// Cookies maps. ValueFor lets the caller decide per-entry whether to emit
// the original value or the fixed redaction token, so the stream-output
// boilerplate (empty-guard, prefix line, key/quote framing) lives in
// exactly one place and cannot drift between sections. The prefix type
// matches http::dump_header_map so all three helpers share one function-
// pointer signature in operator<<.
template <typename ValueFor>
void dump_map_redacted(std::ostream& os, const std::string& prefix,
                       const http::header_view_map& map,
                       ValueFor value_for) {
    if (map.empty()) return;
    os << "    " << prefix << " [";
    for (const auto& kv : map) {
        os << kv.first << ":\"" << value_for(kv) << "\" ";
    }
    os << "]" << std::endl;
}

// TASK-057: emit a Headers/Footers map with Authorization-class header
// values replaced by the fixed redaction token. Mirrors the wire shape
// of http::dump_header_map(header_view_map) for non-authorization
// entries so the on-the-wire diagnostic format is unchanged.
void dump_header_map_redacted(std::ostream& os, const std::string& prefix,
                              const http::header_view_map& map) {
    dump_map_redacted(os, prefix, map, [](const auto& kv) -> std::string_view {
        return is_authorization_header_key(kv.first) ? kRedacted : kv.second;
    });
}

// TASK-057: cookie values are credential material by default (session
// IDs, CSRF tokens, JWTs); redaction is unconditional on the keys
// (which remain visible for log triage). On HAVE_BAUTH-off / HAVE_DAUTH-off
// builds the pass and digested-user surfaces are absent by construction,
// so the redaction policy only meaningfully acts on Authorization headers
// and cookies on those configurations.
void dump_cookie_map_redacted(std::ostream& os, const std::string& prefix,
                              const http::header_view_map& map) {
    dump_map_redacted(os, prefix, map,
                      [](const auto&) -> std::string_view { return kRedacted; });
}

}  // namespace

std::ostream& operator<< (std::ostream& os, const http_request& r) {
    const bool expose = r.impl_->expose_credentials_in_logs_;
    using header_dumper = void(*)(std::ostream&, const std::string&, const http::header_view_map&);
    const header_dumper dump_headers = expose
        ? static_cast<header_dumper>(&http::dump_header_map)
        : &dump_header_map_redacted;
    const header_dumper dump_cookies = expose
        ? static_cast<header_dumper>(&http::dump_header_map)
        : &dump_cookie_map_redacted;

    const std::string_view pass_out = expose
        ? std::string_view(r.get_pass())
        : kRedacted;
    os << r.get_method() << " Request ["
       << "user:\"" << r.get_user() << "\" "
       << "pass:\"" << pass_out << "\""
       << "] path:\"" << r.get_path() << "\"" << std::endl;

    dump_headers(os, "Headers", r.get_headers());
    dump_headers(os, "Footers", r.get_footers());
    dump_cookies(os, "Cookies", r.get_cookies());
    http::dump_arg_map(os, "Query Args", r.get_args());

    os << "    Version [ " << r.get_version() << " ] Requestor [ " << r.get_requestor()
       << " ] Port [ " << r.get_requestor_port() << " ]" << std::endl;

    return os;
}

}  // namespace httpserver
