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
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"

namespace httpserver {

const char http_request::EMPTY[] = "";

struct arguments_accumulator {
    unescaper_ptr unescaper;
    http::arg_map* arguments;
};

void http_request::set_method(const std::string& method) {
    this->method = string_utilities::to_upper_copy(method);
}

bool http_request::check_digest_auth(const std::string& realm, const std::string& password, int nonce_timeout, bool* reload_nonce) const {
    std::string_view digested_user = get_digested_user();

    int val = MHD_digest_auth_check(underlying_connection, realm.c_str(), digested_user.data(), password.c_str(), nonce_timeout);

    if (val == MHD_INVALID_NONCE) {
        *reload_nonce = true;
        return false;
    } else if (val == MHD_NO) {
        *reload_nonce = false;
        return false;
    }
    *reload_nonce = false;
    return true;
}

std::string_view http_request::get_connection_value(std::string_view key, enum MHD_ValueKind kind) const {
    const char* header_c = MHD_lookup_connection_value(underlying_connection, kind, key.data());

    if (header_c == nullptr) return EMPTY;

    return header_c;
}

MHD_Result http_request::build_request_header(void *cls, enum MHD_ValueKind kind, const char *key, const char *value) {
    // Parameters needed to respect MHD interface, but not used in the implementation.
    std::ignore = kind;

    http::header_view_map* dhr = static_cast<http::header_view_map*>(cls);
    (*dhr)[key] = value;
    return MHD_YES;
}

const http::header_view_map http_request::get_headerlike_values(enum MHD_ValueKind kind) const {
    http::header_view_map headers;

    MHD_get_connection_values(underlying_connection, kind, &build_request_header, reinterpret_cast<void*>(&headers));

    return headers;
}

std::string_view http_request::get_header(std::string_view key) const {
    return get_connection_value(key, MHD_HEADER_KIND);
}

const http::header_view_map http_request::get_headers() const {
    return get_headerlike_values(MHD_HEADER_KIND);
}

std::string_view http_request::get_footer(std::string_view key) const {
    return get_connection_value(key, MHD_FOOTER_KIND);
}

const http::header_view_map http_request::get_footers() const {
    return get_headerlike_values(MHD_FOOTER_KIND);
}

std::string_view http_request::get_cookie(std::string_view key) const {
    return get_connection_value(key, MHD_COOKIE_KIND);
}

const http::header_view_map http_request::get_cookies() const {
    return get_headerlike_values(MHD_COOKIE_KIND);
}

std::string_view http_request::get_arg(std::string_view key) const {
    std::map<std::string, std::string>::const_iterator it = cache->unescaped_args.find(std::string(key));

    if (it != cache->unescaped_args.end()) {
        return it->second;
    }

    return get_connection_value(key, MHD_GET_ARGUMENT_KIND);
}

const http::arg_view_map http_request::get_args() const {
    http::arg_view_map arguments;

    if (!cache->unescaped_args.empty()) {
        arguments.insert(cache->unescaped_args.begin(), cache->unescaped_args.end());
        return arguments;
    }

    arguments_accumulator aa;
    aa.unescaper = unescaper;
    aa.arguments = &cache->unescaped_args;

    MHD_get_connection_values(underlying_connection, MHD_GET_ARGUMENT_KIND, &build_request_args, reinterpret_cast<void*>(&aa));

    arguments.insert(cache->unescaped_args.begin(), cache->unescaped_args.end());

    return arguments;
}

http::file_info& http_request::get_or_create_file_info(const std::string& key, const std::string& upload_file_name) {
    return files[key][upload_file_name];
}

std::string_view http_request::get_querystring() const {
    if (!cache->querystring.empty()) {
        return cache->querystring;
    }

    MHD_get_connection_values(underlying_connection, MHD_GET_ARGUMENT_KIND, &build_request_querystring, reinterpret_cast<void*>(&cache->querystring));

    return cache->querystring;
}

MHD_Result http_request::build_request_args(void *cls, enum MHD_ValueKind kind, const char *key, const char *arg_value) {
    // Parameters needed to respect MHD interface, but not used in the implementation.
    std::ignore = kind;

    arguments_accumulator* aa = static_cast<arguments_accumulator*>(cls);
    std::string value = ((arg_value == nullptr) ? "" : arg_value);

    http::base_unescaper(&value, aa->unescaper);
    (*aa->arguments)[key] = value;
    return MHD_YES;
}

MHD_Result http_request::build_request_querystring(void *cls, enum MHD_ValueKind kind, const char *key, const char *arg_value) {
    // Parameters needed to respect MHD interface, but not used in the implementation.
    std::ignore = kind;

    std::string* querystring = static_cast<std::string*>(cls);
    std::string value = ((arg_value == nullptr) ? "" : arg_value);

    int buffer_size = std::string(key).size() + value.size() + 3;
    char* buf = new char[buffer_size];
    if (*querystring == "") {
        snprintf(buf, buffer_size, "?%s=%s", key, value.c_str());
        *querystring = std::string(buf);
    } else {
        snprintf(buf, buffer_size, "&%s=%s", key, value.c_str());
        *querystring += std::string(buf);
    }

    delete[] buf;

    return MHD_YES;
}

void http_request::fetch_user_pass() const {
    char* password = nullptr;
    auto* username = MHD_basic_auth_get_username_password(underlying_connection, &password);

    if (username != nullptr) {
        cache->username = username;
        MHD_free(username);
    }
    if (password != nullptr) {
        cache->password = password;
        MHD_free(password);
    }
}

std::string_view http_request::get_user() const {
    if (!cache->username.empty()) {
        return cache->username;
    }
    fetch_user_pass();
    return cache->username;
}

std::string_view http_request::get_pass() const {
    if (!cache->password.empty()) {
        return cache->password;
    }
    fetch_user_pass();
    return cache->password;
}

std::string_view http_request::get_digested_user() const {
    if (!cache->digested_user.empty()) {
        return cache->digested_user;
    }

    char* digested_user_c = MHD_digest_auth_get_username(underlying_connection);

    cache->digested_user = EMPTY;
    if (digested_user_c != nullptr) {
        cache->digested_user = digested_user_c;
        free(digested_user_c);
    }

    return cache->digested_user;
}

#ifdef HAVE_GNUTLS
bool http_request::has_tls_session() const {
    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(underlying_connection, MHD_CONNECTION_INFO_GNUTLS_SESSION);
    return (conninfo != nullptr);
}

gnutls_session_t http_request::get_tls_session() const {
    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(underlying_connection, MHD_CONNECTION_INFO_GNUTLS_SESSION);

    return static_cast<gnutls_session_t>(conninfo->tls_session);
}
#endif  // HAVE_GNUTLS

std::string_view http_request::get_requestor() const {
    if (!cache->requestor_ip.empty()) {
        return cache->requestor_ip;
    }

    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(underlying_connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    cache->requestor_ip = http::get_ip_str(conninfo->client_addr);
    return cache->requestor_ip;
}

uint16_t http_request::get_requestor_port() const {
    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(underlying_connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    return http::get_port(conninfo->client_addr);
}

std::ostream &operator<< (std::ostream &os, const http_request &r) {
    os << r.get_method() << " Request [user:\"" << r.get_user() << "\" pass:\"" << r.get_pass() << "\"] path:\""
       << r.get_path() << "\"" << std::endl;

    http::dump_header_map(os, "Headers", r.get_headers());
    http::dump_header_map(os, "Footers", r.get_footers());
    http::dump_header_map(os, "Cookies", r.get_cookies());
    http::dump_arg_map(os, "Query Args", r.get_args());

    os << "    Version [ " << r.get_version() << " ] Requestor [ " << r.get_requestor()
       << " ] Port [ " << r.get_requestor_port() << " ]" << std::endl;

    return os;
}

http_request::~http_request() {
    for ( const auto &file_key : this->get_files() ) {
        for ( const auto &files : file_key.second ) {
            // C++17 has std::filesystem::remove()
            remove(files.second.get_file_system_file_name().c_str());
        }
    }
}

}  // namespace httpserver
