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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

// http_request_getters.hpp — data-accessor surface of class http_request.
//
// This header carries member-function DECLARATIONS only; it is meant to
// be included from WITHIN the body of `class http_request` defined in
// httpserver/http_request.hpp. Including it in any other context
// produces a compile error (see the inner gate below). It groups the
// container getters (get_headers / get_footers / get_cookies /
// get_cookies_parsed / get_args / get_args_flat / get_files) and the
// single-key getters (get_header / get_cookie / get_footer / get_arg /
// get_arg_flat) plus get_or_create_file_info.
//
// Keeping the surface in its own file lets the http_request.hpp class
// definition stay under the project-wide per-file line-count ceiling
// (FILE_LOC_MAX in scripts/check-file-size.sh) without splitting the
// public class across translation units. The out-of-line definitions
// for every declaration here live in src/http_request.cpp.
#ifndef SRC_HTTPSERVER_HTTP_REQUEST_GETTERS_HPP_
#define SRC_HTTPSERVER_HTTP_REQUEST_GETTERS_HPP_

#ifndef SRC_HTTPSERVER_HTTP_REQUEST_HPP_INSIDE_CLASS_
#error "httpserver/http_request_getters.hpp must be included from inside the http_request class body in <httpserver/http_request.hpp>."
#endif

     /**
      * Method used to get all headers passed with the request.
      * @return a const reference to a map<string_view, string_view>
      *         containing all headers. The reference (and the views it
      *         holds) remain valid until the http_request is destroyed.
      * @note The string_view keys and values inside the returned map are
      *       only valid within the handler call frame. Copy to std::string
      *       if a longer lifetime is required.
      *       (security-reviewer-iter1-18 / CWE-672)
     **/
     [[nodiscard]] const http::header_view_map& get_headers() const;

     /**
      * Method used to get all footers passed with the request.
      * @return a const reference to a map<string_view, string_view>
      *         containing all footers. The reference (and the views it
      *         holds) remain valid until the http_request is destroyed.
      * @note The string_view keys and values inside the returned map are
      *       only valid within the handler call frame. Copy to std::string
      *       if a longer lifetime is required.
      *       (security-reviewer-iter1-18 / CWE-672)
     **/
     [[nodiscard]] const http::header_view_map& get_footers() const;

     /**
      * Method used to get all cookies passed with the request.
      * @return a const reference to a map<string_view, string_view>
      *         containing all cookies. The reference (and the views it
      *         holds) remain valid until the http_request is destroyed.
      * @note The string_view keys and values inside the returned map are
      *       only valid within the handler call frame. Copy to std::string
      *       if a longer lifetime is required.
      *       (security-reviewer-iter1-18 / CWE-672)
     **/
     [[nodiscard]] const http::header_view_map& get_cookies() const;

     /**
      * TASK-064: structured cookie accessor. Returns the in-order list
      * of cookies parsed from the request's `Cookie:` header per RFC
      * 6265 §5.4. Each entry carries `name` and `value`; request
      * cookies do not carry attributes per the spec, so domain/path/
      * etc. are left default-constructed.
      *
      * Lifetime: the returned reference and the strings it holds remain
      * valid until the http_request is destroyed (typically when the
      * handler returns). Backed by a per-request lazy cache (TASK-016 /
      * TASK-017 arena pattern): the first call parses the Cookie header
      * and builds the vector; subsequent calls are O(1) and reuse the
      * same buffer.
      *
      * Byte-transparent: no percent-decoding is performed. Callers that
      * need decoded values must decode themselves.
     **/
     [[nodiscard]] const std::vector<cookie>& get_cookies_parsed() const;  // NOLINT(build/include_what_you_use)

     /**
      * Method used to get all args passed with the request.
      * @return a const reference to the args map. The reference (and the
      *         string_view keys/values it holds) remain valid until the
      *         http_request is destroyed.
      * @note The string_view keys and values inside the returned map are
      *       only valid within the handler call frame. Copy to std::string
      *       if a longer lifetime is required.
      *       (security-reviewer-iter1-18 / CWE-672)
     **/
     [[nodiscard]] const http::arg_view_map& get_args() const;

     /**
      * Method used to get all args passed with the request. If any key has multiple
      * values, one value is chosen and returned (the first).
      * @return a const reference to a cached "first value per key" view map.
      *         The reference (and the string_view keys/values it holds) remain
      *         valid until the http_request is destroyed.
      * @note (Item 22) The returned views carry the same CWE-416 dangling risk as
      *       get_arg_flat() and the other affected getters listed in the class-level
      *       string_view lifetime contract block above. Copy to std::string if a
      *       longer lifetime is required.
      * @note (Item 19 / Item 24 / PRD-REQ-REQ-001) Previously returned by value;
      *       now returns const& backed by a lazily-populated cache, so repeat
      *       calls are O(1) and zero-allocating.
     **/
     [[nodiscard]] const std::map<std::string_view, std::string_view, http::arg_comparator>& get_args_flat() const;

     /**
      * Method to get or create a file info struct in the map if the provided filename is already in the map
      * return the exiting file info struct, otherwise create one in the map and return it.
      * @param key the multipart form field name (top-level map key).
      * @param upload_file_name the file name the user uploaded (identifier for the inner map entry).
      * @result a http::file_info
     **/
     http::file_info& get_or_create_file_info(const std::string& key, const std::string& upload_file_name);

     /**
      * Method used to get all files passed with the request.
      * @return a const reference to a map<std::string, map<std::string,
      *         http::file_info>> containing all files. The reference
      *         remains valid until the http_request is destroyed.
      * @note Copying the returned map copies file metadata only (paths,
      *       sizes, MIME types). The actual on-disk temporary files are
      *       NOT copied; they remain subject to cleanup when the
      *       http_request destructor runs unless the file_cleanup_callback
      *       suppresses deletion.
      *       (architecture-alignment-checker-iter1-2 / CWE-672)
     **/
     [[nodiscard]] const std::map<std::string, std::map<std::string, http::file_info>>& get_files() const noexcept;  // NOLINT(build/include_what_you_use)

     /**
      * Method used to get a specific header passed with the request.
      * @param key the specific header to get the value from
      * @return the value of the header.
      * @note The returned view is only valid within the handler's call frame.
     **/
     std::string_view get_header(std::string_view key) const;

     /**
      * Method used to get a specific cookie passed with the request.
      * @param key the specific cookie to get the value from
      * @return the value of the cookie.
      * @note The returned view is only valid within the handler's call frame.
     **/
     std::string_view get_cookie(std::string_view key) const;

     /**
      * Method used to get a specific footer passed with the request.
      * @param key the specific footer to get the value from
      * @return the value of the footer.
      * @note The returned view is only valid within the handler's call frame.
     **/
     std::string_view get_footer(std::string_view key) const;

     /**
      * Method used to get a specific argument passed with the request.
      * @param key the specific argument to get the value from
      * @return the value(s) of the arg.
      * @note The `std::string_view` values inside the returned `http_arg_value`
      *       alias the request's arena storage and carry the same lifetime
      *       restriction as the standalone view accessors: do not store them
      *       past the handler invocation. (Item 25: spec-alignment-checker.)
     **/
     http_arg_value get_arg(std::string_view key) const;

     /**
      * Method used to get a specific argument passed with the request.
      * If the arg key has more than one value, only one is returned.
      * @param key the specific argument to get the value from
      * @return the value of the arg.
      * @note The returned view is only valid within the handler's call frame.
      *       Copy into std::string if the value must outlast the handler.
     **/
     [[nodiscard]] std::string_view get_arg_flat(std::string_view key) const;

#endif  // SRC_HTTPSERVER_HTTP_REQUEST_GETTERS_HPP_
