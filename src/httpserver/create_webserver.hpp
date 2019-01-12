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

#ifndef _CREATE_WEBSERVER_HPP_
#define _CREATE_WEBSERVER_HPP_

#include <stdlib.h>
#include "httpserver/http_utils.hpp"
#include "httpserver/http_response.hpp"

#define DEFAULT_WS_TIMEOUT 180
#define DEFAULT_WS_PORT 9898

namespace httpserver {

class webserver;
class http_request;

typedef const std::shared_ptr<http_response>(*render_ptr)(const http_request&);
typedef bool(*validator_ptr)(const std::string&);
typedef void(*unescaper_ptr)(std::string&);
typedef void(*log_access_ptr)(const std::string&);
typedef void(*log_error_ptr)(const std::string&);

class create_webserver
{
    public:
        create_webserver():
            _port(DEFAULT_WS_PORT),
            _start_method(http::http_utils::INTERNAL_SELECT),
            _max_threads(0),
            _max_connections(0),
            _memory_limit(0),
            _content_size_limit(static_cast<size_t>(-1)),
            _connection_timeout(DEFAULT_WS_TIMEOUT),
            _per_IP_connection_limit(0),
            _log_access(0x0),
            _log_error(0x0),
            _validator(0x0),
            _unescaper(0x0),
            _bind_address(0x0),
            _bind_socket(0),
            _max_thread_stack_size(0),
            _use_ssl(false),
            _use_ipv6(false),
            _debug(false),
            _pedantic(false),
            _https_mem_key(""),
            _https_mem_cert(""),
            _https_mem_trust(""),
            _https_priorities(""),
            _cred_type(http::http_utils::NONE),
            _digest_auth_random(""),
            _nonce_nc_size(0),
            _default_policy(http::http_utils::ACCEPT),
            _basic_auth_enabled(true),
            _digest_auth_enabled(true),
            _regex_checking(true),
            _ban_system_enabled(true),
            _post_process_enabled(true),
            _deferred_enabled(false),
            _single_resource(false),
            _not_found_resource(0x0),
            _method_not_allowed_resource(0x0),
            _internal_error_resource(0x0)
        {
        }

        explicit create_webserver(uint16_t port):
            _port(port),
            _start_method(http::http_utils::INTERNAL_SELECT),
            _max_threads(0),
            _max_connections(0),
            _memory_limit(0),
            _content_size_limit(static_cast<size_t>(-1)),
            _connection_timeout(DEFAULT_WS_TIMEOUT),
            _per_IP_connection_limit(0),
            _log_access(0x0),
            _log_error(0x0),
            _validator(0x0),
            _unescaper(0x0),
            _bind_address(0x0),
            _bind_socket(0),
            _max_thread_stack_size(0),
            _use_ssl(false),
            _use_ipv6(false),
            _debug(false),
            _pedantic(false),
            _https_mem_key(""),
            _https_mem_cert(""),
            _https_mem_trust(""),
            _https_priorities(""),
            _cred_type(http::http_utils::NONE),
            _digest_auth_random(""),
            _nonce_nc_size(0),
            _default_policy(http::http_utils::ACCEPT),
            _basic_auth_enabled(true),
            _digest_auth_enabled(true),
            _regex_checking(true),
            _ban_system_enabled(true),
            _post_process_enabled(true),
            _deferred_enabled(false),
            _single_resource(false),
            _not_found_resource(0x0),
            _method_not_allowed_resource(0x0),
            _internal_error_resource(0x0)
        {
        }

        create_webserver& port(uint16_t port) { _port = port; return *this; }
        create_webserver& start_method(
                const http::http_utils::start_method_T& start_method
        )
        {
            _start_method = start_method; return *this;
        }
        create_webserver& max_threads(int max_threads)
        {
            _max_threads = max_threads; return *this;
        }
        create_webserver& max_connections(int max_connections)
        {
            _max_connections = max_connections; return *this;
        }
        create_webserver& memory_limit(int memory_limit)
        {
            _memory_limit = memory_limit; return *this;
        }
        create_webserver& content_size_limit(size_t content_size_limit)
        {
            _content_size_limit = content_size_limit; return *this;
        }
        create_webserver& connection_timeout(int connection_timeout)
        {
            _connection_timeout = connection_timeout; return *this;
        }
        create_webserver& per_IP_connection_limit(int per_IP_connection_limit)
        {
            _per_IP_connection_limit = per_IP_connection_limit; return *this;
        }
        create_webserver& log_access(log_access_ptr log_access)
        {
            _log_access = log_access; return *this;
        }
        create_webserver& log_error(log_error_ptr log_error)
        {
            _log_error = log_error; return *this;
        }
        create_webserver& validator(validator_ptr validator)
        {
            _validator = validator; return *this;
        }
        create_webserver& unescaper(unescaper_ptr unescaper)
        {
            _unescaper = unescaper; return *this;
        }
        create_webserver& bind_address(const struct sockaddr* bind_address)
        {
            _bind_address = bind_address; return *this;
        }
        create_webserver& bind_socket(int bind_socket)
        {
            _bind_socket = bind_socket; return *this;
        }
        create_webserver& max_thread_stack_size(int max_thread_stack_size)
        {
            _max_thread_stack_size = max_thread_stack_size; return *this;
        }
        create_webserver& use_ssl() { _use_ssl = true; return *this; }
        create_webserver& no_ssl() { _use_ssl = false; return *this; }
        create_webserver& use_ipv6() { _use_ipv6 = true; return *this; }
        create_webserver& no_ipv6() { _use_ipv6 = false; return *this; }
        create_webserver& debug() { _debug = true; return *this; }
        create_webserver& no_debug() { _debug = false; return *this; }
        create_webserver& pedantic() { _pedantic = true; return *this; }
        create_webserver& no_pedantic() { _pedantic = false; return *this; }
        create_webserver& https_mem_key(const std::string& https_mem_key)
        {
            _https_mem_key = http::load_file(https_mem_key);
            return *this;
        }
        create_webserver& https_mem_cert(const std::string& https_mem_cert)
        {
            _https_mem_cert = http::load_file(https_mem_cert);
            return *this;
        }
        create_webserver& https_mem_trust(const std::string& https_mem_trust)
        {
            _https_mem_trust = http::load_file(https_mem_trust);
            return *this;
        }
        create_webserver& raw_https_mem_key(const std::string& https_mem_key)
        {
            _https_mem_key = https_mem_key; return *this;
        }
        create_webserver& raw_https_mem_cert(const std::string& https_mem_cert)
        {
            _https_mem_cert = https_mem_cert; return *this;
        }
        create_webserver& raw_https_mem_trust(
                const std::string& https_mem_trust
        )
        {
            _https_mem_trust = https_mem_trust; return *this;
        }
        create_webserver& https_priorities(const std::string& https_priorities)
        {
            _https_priorities = https_priorities; return *this;
        }
        create_webserver& cred_type(const http::http_utils::cred_type_T& cred_type)
        {
            _cred_type = cred_type; return *this;
        }
        create_webserver& digest_auth_random(
                const std::string& digest_auth_random
        )
        {
            _digest_auth_random = digest_auth_random; return *this;
        }
        create_webserver& nonce_nc_size(int nonce_nc_size)
        {
            _nonce_nc_size = nonce_nc_size; return *this;
        }
        create_webserver& default_policy(
                const http::http_utils::policy_T& default_policy
        )
        {
            _default_policy = default_policy; return *this;
        }
        create_webserver& basic_auth()
        {
            _basic_auth_enabled = true; return *this;
        }
        create_webserver& no_basic_auth()
        {
            _basic_auth_enabled = false; return *this;
        }
        create_webserver& digest_auth()
        {
            _digest_auth_enabled = true; return *this;
        }
        create_webserver& no_digest_auth()
        {
            _digest_auth_enabled = false; return *this;
        }
        create_webserver& deferred()
        {
            _deferred_enabled = true; return *this;
        }
        create_webserver& no_deferred()
        {
            _deferred_enabled = false; return *this;
        }
        create_webserver& regex_checking()
        {
            _regex_checking = true; return *this;
        }
        create_webserver& no_regex_checking()
        {
            _regex_checking = false; return *this;
        }
        create_webserver& ban_system()
        {
            _ban_system_enabled = true; return *this;
        }
        create_webserver& no_ban_system()
        {
            _ban_system_enabled = false; return *this;
        }
        create_webserver& post_process()
        {
            _post_process_enabled = true; return *this;
        }
        create_webserver& no_post_process()
        {
            _post_process_enabled = false; return *this;
        }
        create_webserver& single_resource()
        {
            _single_resource = true; return *this;
        }
        create_webserver& not_found_resource(render_ptr not_found_resource)
        {
            _not_found_resource = not_found_resource; return *this;
        }
        create_webserver& method_not_allowed_resource(
                render_ptr method_not_allowed_resource
        )
        {
            _method_not_allowed_resource = method_not_allowed_resource;
            return *this;
        }
        create_webserver& internal_error_resource(
                render_ptr internal_error_resource
        )
        {
            _internal_error_resource = internal_error_resource; return *this;
        }

    private:
        uint16_t _port;
        http::http_utils::start_method_T _start_method;
        int _max_threads;
        int _max_connections;
        int _memory_limit;
        size_t _content_size_limit;
        int _connection_timeout;
        int _per_IP_connection_limit;
        log_access_ptr _log_access;
        log_error_ptr _log_error;
        validator_ptr _validator;
        unescaper_ptr _unescaper;
        const struct sockaddr* _bind_address;
        int _bind_socket;
        int _max_thread_stack_size;
        bool _use_ssl;
        bool _use_ipv6;
        bool _debug;
        bool _pedantic;
        std::string _https_mem_key;
        std::string _https_mem_cert;
        std::string _https_mem_trust;
        std::string _https_priorities;
        http::http_utils::cred_type_T _cred_type;
        std::string _digest_auth_random;
        int _nonce_nc_size;
        http::http_utils::policy_T _default_policy;
        bool _basic_auth_enabled;
        bool _digest_auth_enabled;
        bool _regex_checking;
        bool _ban_system_enabled;
        bool _post_process_enabled;
        bool _deferred_enabled;
        bool _single_resource;
        render_ptr _not_found_resource;
        render_ptr _method_not_allowed_resource;
        render_ptr _internal_error_resource;

        friend class webserver;
};

} //httpserver

#endif //_CREATE_WEBSERVER_HPP_
