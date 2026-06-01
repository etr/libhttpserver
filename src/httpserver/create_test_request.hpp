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

#ifndef SRC_HTTPSERVER_CREATE_TEST_REQUEST_HPP_
#define SRC_HTTPSERVER_CREATE_TEST_REQUEST_HPP_

#include <map>
#include <string>
#include <vector>

#include "httpserver/http_request.hpp"
#include "httpserver/http_utils.hpp"

namespace httpserver {

class create_test_request {
 public:
    create_test_request() = default;

    create_test_request& method(const std::string& method) {
        _method = method;
        return *this;
    }

    create_test_request& path(const std::string& path) {
        _path = path;
        return *this;
    }

    create_test_request& version(const std::string& version) {
        _version = version;
        return *this;
    }

    create_test_request& content(const std::string& content) {
        _content = content;
        return *this;
    }

    create_test_request& header(const std::string& key, const std::string& value) {
        _headers[key] = value;
        return *this;
    }

    create_test_request& footer(const std::string& key, const std::string& value) {
        _footers[key] = value;
        return *this;
    }

    create_test_request& cookie(const std::string& key, const std::string& value) {
        _cookies[key] = value;
        return *this;
    }

    create_test_request& arg(const std::string& key, const std::string& value) {
        _args[key].push_back(value);
        return *this;
    }

    create_test_request& querystring(const std::string& querystring) {
        _querystring = querystring;
        return *this;
    }

    // TASK-034: setters are unconditional. On HAVE_BAUTH-off /
    // HAVE_DAUTH-off builds the values are silently dropped on .build()
    // (matching the sentinel-empty contract of the corresponding
    // http_request accessors).
    create_test_request& user(const std::string& user) {
        _user = user;
        return *this;
    }

    create_test_request& pass(const std::string& pass) {
        _pass = pass;
        return *this;
    }

    create_test_request& digested_user(const std::string& digested_user) {
        _digested_user = digested_user;
        return *this;
    }

    create_test_request& requestor(const std::string& requestor) {
        _requestor = requestor;
        return *this;
    }

    create_test_request& requestor_port(uint16_t port) {
        _requestor_port = port;
        return *this;
    }

    // TASK-034: setter is unconditional. On HAVE_GNUTLS-off builds the
    // value is silently dropped (has_tls_session() returns false
    // regardless — TASK-019 sentinel contract).
    create_test_request& tls_enabled(bool enabled = true) {
        _tls_enabled = enabled;
        return *this;
    }

    // TASK-057: opt out of the default credential-redaction policy in
    // http_request::operator<<. Mirrors the webserver-side builder
    // setter @ref create_webserver::expose_credentials_in_logs for the
    // unit-test scope (no webserver construction).
    create_test_request& expose_credentials_in_logs(bool enable = true) {
        _expose_credentials_in_logs = enable;
        return *this;
    }

    http_request build();

 private:
    std::string _method = "GET";
    std::string _path = "/";
    std::string _version = "HTTP/1.1";
    std::string _content;
    http::header_map _headers;
    http::header_map _footers;
    http::header_map _cookies;
    std::map<std::string, std::vector<std::string>, http::arg_comparator> _args;
    std::string _querystring;
    // TASK-034: fields stored unconditionally. On HAVE_*-off builds the
    // values are populated by the setters but silently dropped during
    // .build() propagation, matching the sentinel-empty contract on
    // the http_request accessor side.
    std::string _user;
    std::string _pass;
    std::string _digested_user;
    std::string _requestor = "127.0.0.1";
    uint16_t _requestor_port = 0;
    bool _tls_enabled = false;
    // TASK-057: default false (secure-by-default). When true, build()
    // sets http_request_impl::expose_credentials_in_logs_ so the
    // diagnostic dump streams the v1 verbose form.
    bool _expose_credentials_in_logs = false;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_CREATE_TEST_REQUEST_HPP_
