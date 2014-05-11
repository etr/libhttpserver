/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014 Sebastiano Merlino

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

#ifndef _HTTP_RESPONSE_BUILDER_HPP_
#define _HTTP_RESPONSE_BUILDER_HPP_
#include <map>
#include <string>
#include "httpserver/http_response.hpp"

struct MHD_Connection;

namespace httpserver
{

class webserver;

namespace http
{
    class header_comparator;
};

namespace details
{
    struct cache_entry;
};

using namespace http;

struct byte_string
{
    public:
        byte_string(const char* content_hook, size_t content_length):
            content_hook(content_hook),
            content_length(content_length)
        {
        }

        const char* get_content_hook() const
        {
            return content_hook;
        }

        size_t get_content_length() const
        {
            return content_length;
        }

    private:
        const char* content_hook;
        size_t content_length;
};

class http_response_builder
{
    public:
        explicit http_response_builder(
            const std::string& content_hook,
            int response_code = 200,
            const std::string& content_type = "text/plain",
            bool autodelete = true
        ):
            _content_hook(content_hook),
            _response_code(response_code),
            _autodelete(autodelete),
            _realm(""),
            _opaque(""),
            _reload_nonce(false),
            _headers(std::map<std::string, std::string, header_comparator>()),
            _footers(std::map<std::string, std::string, header_comparator>()),
            _cookies(std::map<std::string, std::string, header_comparator>()),
            _topics(std::vector<std::string>()),
            _keepalive_secs(-1),
            _keepalive_msg(""),
            _send_topic(""),
            _ce(0x0),
            _get_raw_response(&http_response::get_raw_response_str),
            _decorate_response(&http_response::decorate_response_str),
            _enqueue_response(&http_response::enqueue_response_str)
        {
            _headers[http_utils::http_header_content_type] = content_type;
        }

        http_response_builder(
            const byte_string& content_hook,
            int response_code = 200,
            const std::string& content_type = "text/plain",
            bool autodelete = true
        ):
            _content_hook(std::string(content_hook.get_content_hook(), content_hook.get_content_length())),
            _response_code(response_code),
            _autodelete(autodelete),
            _realm(""),
            _opaque(""),
            _reload_nonce(false),
            _headers(std::map<std::string, std::string, header_comparator>()),
            _footers(std::map<std::string, std::string, header_comparator>()),
            _cookies(std::map<std::string, std::string, header_comparator>()),
            _topics(std::vector<std::string>()),
            _keepalive_secs(-1),
            _keepalive_msg(""),
            _send_topic(""),
            _ce(0x0),
            _get_raw_response(&http_response::get_raw_response_str),
            _decorate_response(&http_response::decorate_response_str),
            _enqueue_response(&http_response::enqueue_response_str)
        {
            _headers[http_utils::http_header_content_type] = content_type;
        }

        http_response_builder(const http_response_builder& b):
            _content_hook(b._content_hook),
            _response_code(b._response_code),
            _autodelete(b._autodelete),
            _realm(b._realm),
            _opaque(b._opaque),
            _reload_nonce(b._reload_nonce),
            _headers(b._headers),
            _footers(b._footers),
            _cookies(b._cookies),
            _topics(b._topics),
            _keepalive_secs(b._keepalive_secs),
            _keepalive_msg(b._keepalive_msg),
            _send_topic(b._send_topic),
            _ce(b._ce),
            _get_raw_response(b._get_raw_response),
            _decorate_response(b._decorate_response),
            _enqueue_response(b._enqueue_response)
        {
        }

        http_response_builder& operator=(const http_response_builder& b)
        {
            _content_hook = b._content_hook;
            _response_code = b._response_code;
            _autodelete = b._autodelete;
            _realm = b._realm;
            _opaque = b._opaque;
            _reload_nonce = b._reload_nonce;
            _headers = b._headers;
            _footers = b._footers;
            _cookies = b._cookies;
            _topics = b._topics;
            _keepalive_secs = b._keepalive_secs;
            _keepalive_msg = b._keepalive_msg;
            _send_topic = b._send_topic;
            _ce = b._ce;
            _get_raw_response = b._get_raw_response;
            _decorate_response = b._decorate_response;
            _enqueue_response = b._enqueue_response;
            return *this;
        }

        ~http_response_builder()
        {
        }

        http_response_builder& string_response()
        {
            return *this;
        }

        http_response_builder& file_response()
        {
            _get_raw_response = &http_response::get_raw_response_file;
            return *this;
        }

        http_response_builder& basic_auth_fail_response(const std::string& realm = "")
        {
            _realm = realm;
            _enqueue_response = &http_response::enqueue_response_basic;
            return *this;
        }

        http_response_builder& digest_auth_fail_response(
            const std::string& realm = "",
            const std::string& opaque = "",
            bool reload_nonce = false
        )
        {
            _realm = realm;
            _opaque = opaque;
            _reload_nonce = reload_nonce;
            _enqueue_response = &http_response::enqueue_response_digest;
            return *this;
        }

        http_response_builder& long_polling_receive_response(
            const std::vector<std::string>& topics,
            int keepalive_secs = -1,
            std::string keepalive_msg = ""
        )
        {
            _topics = topics;
            _keepalive_secs = keepalive_secs;
            _keepalive_msg = keepalive_msg;
            _get_raw_response = &http_response::get_raw_response_lp_receive;
            return *this;
        }

        http_response_builder& long_polling_send_response(const std::string& send_topic)
        {
            _send_topic = send_topic;
            _get_raw_response = &http_response::get_raw_response_lp_send;
            return *this;
        }

        http_response_builder& cache_response()
        {
            _get_raw_response = &http_response::get_raw_response_cache;
            _decorate_response = &http_response::decorate_response_cache;
            return *this;
        }

        http_response_builder& deferred_response(cycle_callback_ptr cycle_callback)
        {
            _cycle_callback = cycle_callback;
            _get_raw_response = &http_response::get_raw_response_deferred;
            _decorate_response = &http_response::decorate_response_deferred;
            return *this;
        }

        http_response_builder& shoutCAST_response()
        {
            _response_code |= http_utils::shoutcast_response;
            return *this;
        }

        http_response_builder& with_header(const std::string& key, const std::string& value)
        {
            _headers[key] = value; return *this;
        }

        http_response_builder& with_footer(const std::string& key, const std::string& value)
        {
            _footers[key] = value; return *this;
        }

        http_response_builder& with_cookie(const std::string& key, const std::string& value)
        {
            _cookies[key] = value; return *this;
        }

    private:
        std::string _content_hook;
        int _response_code;
        bool _autodelete;
        std::string _realm;
        std::string _opaque;
        bool _reload_nonce;
        std::map<std::string, std::string, header_comparator> _headers;
        std::map<std::string, std::string, header_comparator> _footers;
        std::map<std::string, std::string, header_comparator> _cookies;
        std::vector<std::string> _topics;
        int _keepalive_secs;
        std::string _keepalive_msg;
        std::string _send_topic;
        cycle_callback_ptr _cycle_callback;
        details::cache_entry* _ce;

        void (http_response::*_get_raw_response)(MHD_Response**, webserver*);
        void (http_response::*_decorate_response)(MHD_Response*);
        int (http_response::*_enqueue_response)(MHD_Connection*, MHD_Response*);

        http_response_builder& cache_response(details::cache_entry* ce)
        {
            _ce = ce;
            _get_raw_response = &http_response::get_raw_response_cache;
            _decorate_response = &http_response::decorate_response_cache;
            return *this;
        }

        friend struct http_response;
};

};
#endif //_HTTP_RESPONSE_BUILDER_HPP_
