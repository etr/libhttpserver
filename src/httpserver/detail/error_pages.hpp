/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// error_pages -- behavior service (DR-014, §4.11) that synthesises the
// 404 / 405 / 500 responses. A leaf in the dispatch DAG: it reads only
// the const config bag (the user not_found / method_not_allowed /
// internal_error handlers + expose_exception_messages) and mr->request;
// it owns no state, takes no locks, and touches no other collaborator.
//
// Internal header; only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "error_pages.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_ERROR_PAGES_HPP_
#define SRC_HTTPSERVER_DETAIL_ERROR_PAGES_HPP_

#include <string_view>

#include "httpserver/http_response.hpp"

namespace httpserver {

struct webserver_config;

namespace detail {

struct modded_request;

class error_pages {
 public:
    explicit error_pages(const webserver_config& config) noexcept
        : config_(config) {}

    error_pages(const error_pages&) = delete;
    error_pages& operator=(const error_pages&) = delete;
    error_pages(error_pages&&) = delete;
    error_pages& operator=(error_pages&&) = delete;
    ~error_pages() = default;

    // 404 body: the user not_found_handler if set, else the fixed
    // NOT_FOUND_ERROR string with http_not_found status.
    http_response not_found_page(modded_request* mr) const;

    // 405 body: the user method_not_allowed_handler if set, else the
    // fixed METHOD_ERROR string with http_method_not_allowed status. The
    // caller supplies the Allow header separately.
    http_response method_not_allowed_page(modded_request* mr) const;

    // 500 body. @p force_our=true returns the double-fault fallback: an
    // EMPTY-body 500 with @p msg ignored (used when the user handler
    // itself threw, or the belt-and-suspenders site after
    // get_raw_response_with_fallback fires). Otherwise invokes the user
    // internal_error_handler with @p msg if set; failing that, returns the
    // fixed "Internal Server Error" body (CWE-209) unless
    // expose_exception_messages is on, in which case @p msg becomes the
    // body. @p msg is still logged verbatim by the caller via
    // log_dispatch_error regardless of the body path taken.
    http_response internal_error_page(modded_request* mr,
                                      std::string_view msg,
                                      bool force_our = false) const;

    // Invoke internal_error_page(force_our=false) but contain a throwing
    // user internal_error_handler: on throw, log generically via
    // log_dispatch_error and return the hardcoded empty-body 500. No
    // exception escapes.
    http_response run_internal_error_handler_safely(modded_request* mr,
                                                    std::string_view msg) const;

 private:
    const webserver_config& config_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_ERROR_PAGES_HPP_
