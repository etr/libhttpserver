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

#include "http_utils.hpp"
#include "http_request.hpp"
#include "string_utilities.hpp"
#include <iostream>

using namespace std;

namespace httpserver
{

const std::string http_request::EMPTY = "";

void http_request::set_method(const std::string& method)
{
    this->method = string_utilities::to_upper_copy(method);
}

bool http_request::check_digest_auth(
        const std::string& realm,
        const std::string& password,
        int nonce_timeout,
        bool& reload_nonce
)
{
    digest_auth_parse();

    int val = MHD_digest_auth_check(
            underlying_connection,
            realm.c_str(),
            digested_user.c_str(),
            password.c_str(),
            nonce_timeout
    );

    if(val == MHD_INVALID_NONCE)
    {
        reload_nonce = true;
        return false;
    }
    else if(val == MHD_NO)
    {
        reload_nonce = false;
        return false;
    }
    reload_nonce = false;
    return true;
}


const std::string& http_request::get_header(const std::string& key)
{
    check_or_fill_headers();

    std::map<std::string, std::string>::const_iterator it = this->headers.find(key);
    if(it != this->headers.end())
    {
        return it->second;
    }
    else
    {
        return EMPTY;
    }
}

const std::map<std::string, std::string, http::header_comparator>& http_request::get_headers()
{
    check_or_fill_headers();

    return this->headers;
}

const std::string& http_request::get_footer(const std::string& key)
{
    check_or_fill_footers();

    std::map<std::string, std::string>::const_iterator it = this->footers.find(key);
    if(it != this->footers.end())
    {
        return it->second;
    }
    else
    {
        return EMPTY;
    }
}

const std::map<std::string, std::string, http::header_comparator>& http_request::get_footers()
{
    check_or_fill_footers();

    return this->footers;
}

const std::string& http_request::get_cookie(const std::string& key)
{
    check_or_fill_cookies();

    std::map<std::string, std::string>::const_iterator it = this->cookies.find(key);
    if(it != this->cookies.end())
    {
        return it->second;
    }
    else
    {
        return EMPTY;
    }
}

const std::map<std::string, std::string, http::header_comparator>& http_request::get_cookies()
{
    check_or_fill_cookies();

    return this->cookies;
}

const std::string& http_request::get_arg(const std::string& key)
{
    check_or_fill_args();

    std::map<std::string, std::string>::const_iterator it = this->args.find(key);
    if(it != this->args.end())
    {
        return it->second;
    }
    else
    {
        return EMPTY;
    }
}

const std::map<std::string, std::string, http::arg_comparator>& http_request::get_args()
{
    check_or_fill_args();

    return this->args;
}

const std::string& http_request::get_querystring()
{
    check_or_fill_args();

    return this->querystring;
}

std::ostream &operator<< (std::ostream &os, http_request &r)
{
    os << r.get_method() << " Request [user:\"" << r.get_user() << "\" pass:\"" << r.get_pass() << "\"] path:\""
       << r.get_path() << "\"" << std::endl;

    http::dump_header_map(os,"Headers",r.get_headers());
    http::dump_header_map(os,"Footers",r.get_footers());
    http::dump_header_map(os,"Cookies",r.get_cookies());
    http::dump_arg_map(os,"Query Args",r.get_args());

    os << "    Version [ " << r.get_version() << " ] Requestor [ " << r.get_requestor()
       << " ] Port [ " << r.get_requestor_port() << " ]" << std::endl;

    return os;
}

void http_request::check_or_fill_headers()
{
    if (this->headers_loaded) return;

    MHD_get_connection_values (
        this->underlying_connection,
        MHD_HEADER_KIND,
        &build_request_header,
        (void*) this
    );

    this->headers_loaded = true;
}

void http_request::check_or_fill_cookies()
{
    if (this->cookies_loaded) return;

    MHD_get_connection_values (
        this->underlying_connection,
        MHD_COOKIE_KIND,
        &build_request_cookie,
        (void*) this
    );

    this->cookies_loaded = true;
}

void http_request::check_or_fill_footers()
{
    if (this->footers_loaded) return;

    MHD_get_connection_values (
        this->underlying_connection,
        MHD_FOOTER_KIND,
        &build_request_footer,
        (void*) this
    );

    this->footers_loaded = true;
}

void http_request::check_or_fill_args()
{
    if (this->args_loaded) return;

    MHD_get_connection_values (
        this->underlying_connection,
        MHD_GET_ARGUMENT_KIND,
        &build_request_args,
        (void*) this
    );

    this->args_loaded = true;
}

int http_request::build_request_header(
        void *cls,
        enum MHD_ValueKind kind,
        const char *key,
        const char *value
)
{
    http_request* dhr = static_cast<http_request*>(cls);
    dhr->set_header(key, value);
    return MHD_YES;
}

int http_request::build_request_cookie (
        void *cls,
        enum MHD_ValueKind kind,
        const char *key,
        const char *value
)
{
    http_request* dhr = static_cast<http_request*>(cls);
    dhr->set_cookie(key, value);
    return MHD_YES;
}

int http_request::build_request_footer(
        void *cls,
        enum MHD_ValueKind kind,
        const char *key,
        const char *value
)
{
    http_request* dhr = static_cast<http_request*>(cls);
    dhr->set_footer(key, value);
    return MHD_YES;
}

int http_request::build_request_args(
        void *cls,
        enum MHD_ValueKind kind,
        const char *key,
        const char *arg_value
)
{
    http_request* dhr = static_cast<http_request*>(cls);
    std::string value = ((arg_value == NULL) ? "" : arg_value);
    {
        char buf[std::string(key).size() + value.size() + 3];
        if(dhr->querystring == "")
        {
            snprintf(buf, sizeof buf, "?%s=%s", key, value.c_str());
            dhr->querystring = buf;
        }
        else
        {
            snprintf(buf, sizeof buf, "&%s=%s", key, value.c_str());
            dhr->querystring += string(buf);
        }
    }
    http::base_unescaper(value, dhr->unescaper);
    dhr->set_arg(key, value);
    return MHD_YES;
}

const std::string& http_request::get_user()
{
    basic_auth_parse();
    return this->user;
}

const std::string& http_request::get_pass()
{
    basic_auth_parse();
    return this->pass;
}

void http_request::basic_auth_parse()
{
    if (this->basic_auth_loaded) return;

    char* username = 0x0;
    char* password = 0x0;
    username = MHD_basic_auth_get_username_password(underlying_connection, &password);

    if (username != 0x0)
    {
        this->user = username;
        free(username);
    }
    if (password != 0x0)
    {
        this->pass = password;
        free(password);
    }

    this->basic_auth_loaded = true;
}

const std::string& http_request::get_digested_user()
{
    digest_auth_parse();
    return this->digested_user;
}

void http_request::digest_auth_parse()
{
    if (this->digest_auth_loaded) return;

    char* digested_user_c = 0x0;
    digested_user_c = MHD_digest_auth_get_username(underlying_connection);

    if (digested_user_c != 0x0)
    {
        this->digested_user = digested_user_c;
        free(digested_user_c);
    }

    this->digest_auth_loaded = true;
}

const std::string& http_request::get_requestor()
{
    parse_requestor_info();
    return this->requestor;
}

unsigned short http_request::get_requestor_port()
{
    parse_requestor_info();
    return this->requestor_port;
}

void http_request::parse_requestor_info()
{
    if (this->requestor_loaded) return;

    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(
            underlying_connection,
            MHD_CONNECTION_INFO_CLIENT_ADDRESS
    );

    std::string ip_str = http::get_ip_str(conninfo->client_addr);
    this->set_requestor(ip_str);
    this->set_requestor_port(http::get_port(conninfo->client_addr));
}

}
