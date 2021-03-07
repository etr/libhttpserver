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
    std::map<std::string, std::string, http::arg_comparator>* arguments;
};

void http_request::set_method(const std::string& method) {
    this->method = string_utilities::to_upper_copy(method);
}

bool http_request::check_digest_auth(const std::string& realm, const std::string& password, int nonce_timeout, bool& reload_nonce) const {
    std::string digested_user = get_digested_user();

    int val = MHD_digest_auth_check(underlying_connection, realm.c_str(), digested_user.c_str(), password.c_str(), nonce_timeout);

    if (val == MHD_INVALID_NONCE) {
        reload_nonce = true;
        return false;
    } else if (val == MHD_NO) {
        reload_nonce = false;
        return false;
    }
    reload_nonce = false;
    return true;
}

const std::string http_request::get_connection_value(const std::string& key, enum MHD_ValueKind kind) const {
    const char* header_c = MHD_lookup_connection_value(underlying_connection, kind, key.c_str());

    if (header_c == NULL) return EMPTY;

    return header_c;
}

MHD_Result http_request::build_request_header(void *cls, enum MHD_ValueKind kind, const char *key, const char *value) {
    std::map<std::string, std::string, http::header_comparator>* dhr = static_cast<std::map<std::string, std::string, http::header_comparator>*>(cls);
    (*dhr)[key] = value;
    return MHD_YES;
}

const std::map<std::string, std::string, http::header_comparator> http_request::get_headerlike_values(enum MHD_ValueKind kind) const {
    std::map<std::string, std::string, http::header_comparator> headers;

    MHD_get_connection_values(underlying_connection, kind, &build_request_header, reinterpret_cast<void*>(&headers));

    return headers;
}

const std::string http_request::get_header(const std::string& key) const {
    return get_connection_value(key, MHD_HEADER_KIND);
}

const std::map<std::string, std::string, http::header_comparator> http_request::get_headers() const {
    return get_headerlike_values(MHD_HEADER_KIND);
}

const std::string http_request::get_footer(const std::string& key) const {
    return get_connection_value(key, MHD_FOOTER_KIND);
}

const std::map<std::string, std::string, http::header_comparator> http_request::get_footers() const {
    return get_headerlike_values(MHD_FOOTER_KIND);
}

const std::string http_request::get_cookie(const std::string& key) const {
    return get_connection_value(key, MHD_COOKIE_KIND);
}

const std::map<std::string, std::string, http::header_comparator> http_request::get_cookies() const {
    return get_headerlike_values(MHD_COOKIE_KIND);
}

const std::string http_request::get_arg(const std::string& key) const {
    std::map<std::string, std::string>::const_iterator it = args.find(key);

    if (it != args.end()) {
        return it->second;
    }

    return get_connection_value(key, MHD_GET_ARGUMENT_KIND);
}

const std::map<std::string, std::string, http::arg_comparator> http_request::get_args() const {
    std::map<std::string, std::string, http::arg_comparator> arguments;
    arguments.insert(args.begin(), args.end());

    arguments_accumulator aa;
    aa.unescaper = unescaper;
    aa.arguments = &arguments;

    MHD_get_connection_values(underlying_connection, MHD_GET_ARGUMENT_KIND, &build_request_args, reinterpret_cast<void*>(&aa));

    return arguments;
}

const std::string http_request::get_querystring() const {
    std::string querystring = "";

    MHD_get_connection_values(underlying_connection, MHD_GET_ARGUMENT_KIND, &build_request_querystring, reinterpret_cast<void*>(&querystring));

    return querystring;
}

MHD_Result http_request::build_request_args(void *cls, enum MHD_ValueKind kind, const char *key, const char *arg_value) {
    arguments_accumulator* aa = static_cast<arguments_accumulator*>(cls);
    std::string value = ((arg_value == NULL) ? "" : arg_value);

    http::base_unescaper(&value, aa->unescaper);
    (*aa->arguments)[key] = value;
    return MHD_YES;
}

MHD_Result http_request::build_request_querystring(void *cls, enum MHD_ValueKind kind, const char *key, const char *arg_value) {
    std::string* querystring = static_cast<std::string*>(cls);
    std::string value = ((arg_value == NULL) ? "" : arg_value);
    {
        int buffer_size = std::string(key).size() + value.size() + 3;
        char* buf = new char[buffer_size];
        if (*querystring == "") {
            snprintf(buf, buffer_size, "?%s=%s", key, value.c_str());
            *querystring = buf;
        } else {
            snprintf(buf, buffer_size, "&%s=%s", key, value.c_str());
            *querystring += std::string(buf);
        }
    }

    return MHD_YES;
}

const std::string http_request::get_user() const {
    char* username = 0x0;
    char* password = 0x0;

    username = MHD_basic_auth_get_username_password(underlying_connection, &password);
    if (password != 0x0) free(password);

    std::string user;
    if (username != 0x0) user = username;

    free(username);

    return user;
}

const std::string http_request::get_pass() const {
    char* username = 0x0;
    char* password = 0x0;

    username = MHD_basic_auth_get_username_password(underlying_connection, &password);
    if (username != 0x0) free(username);

    std::string pass;
    if (password != 0x0) pass = password;

    free(password);

    return pass;
}

const std::string http_request::get_digested_user() const {
    char* digested_user_c = 0x0;
    digested_user_c = MHD_digest_auth_get_username(underlying_connection);

    std::string digested_user = EMPTY;
    if (digested_user_c != 0x0) {
        digested_user = digested_user_c;
        free(digested_user_c);
    }

    return digested_user;
}

const std::string http_request::get_requestor() const {
    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(underlying_connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    return http::get_ip_str(conninfo->client_addr);
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

}  // namespace httpserver
