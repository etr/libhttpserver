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

    create_test_request& user(const std::string& user) {
        _user = user;
        return *this;
    }

    create_test_request& pass(const std::string& pass) {
        _pass = pass;
        return *this;
    }

#ifdef HAVE_DAUTH
    create_test_request& digested_user(const std::string& digested_user) {
        _digested_user = digested_user;
        return *this;
    }
#endif  // HAVE_DAUTH

    create_test_request& requestor(const std::string& requestor) {
        _requestor = requestor;
        return *this;
    }

    create_test_request& requestor_port(uint16_t port) {
        _requestor_port = port;
        return *this;
    }

#ifdef HAVE_GNUTLS
    create_test_request& tls_enabled(bool enabled = true) {
        _tls_enabled = enabled;
        return *this;
    }
#endif  // HAVE_GNUTLS

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
    std::string _user;
    std::string _pass;
#ifdef HAVE_DAUTH
    std::string _digested_user;
#endif  // HAVE_DAUTH
    std::string _requestor = "127.0.0.1";
    uint16_t _requestor_port = 0;
#ifdef HAVE_GNUTLS
    bool _tls_enabled = false;
#endif  // HAVE_GNUTLS
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_CREATE_TEST_REQUEST_HPP_
