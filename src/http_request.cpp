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
    // TASK-034: get_user/get_pass are unconditional; they return empty
    // on HAVE_BAUTH-off builds, so the dump prints two empty quoted
    // strings in that case (harmless).
    os << "user:\"" << r.get_user() << "\" pass:\"" << r.get_pass() << "\"";
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
