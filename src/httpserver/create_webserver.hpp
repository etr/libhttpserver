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

#ifndef SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_
#define SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_

#include <stdlib.h>
#include <memory>
#include <functional>
#include <string>

#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"

#define DEFAULT_WS_TIMEOUT 180
#define DEFAULT_WS_PORT 9898

namespace httpserver {

class webserver;
class http_request;

typedef std::function<std::shared_ptr<http_response>(const http_request&)> render_ptr;
typedef std::function<bool(const std::string&)> validator_ptr;
typedef std::function<void(const std::string&)> log_access_ptr;
typedef std::function<void(const std::string&)> log_error_ptr;

class create_webserver {
 public:
     create_webserver() = default;
     create_webserver(const create_webserver& b) = default;
     create_webserver(create_webserver&& b) noexcept = default;
     create_webserver& operator=(const create_webserver& b) = default;
     create_webserver& operator=(create_webserver&& b) = default;

     explicit create_webserver(uint16_t port):
         _port(port) { }

     create_webserver& port(uint16_t port) {
         _port = port;
         return *this;
     }

     create_webserver& start_method(const http::http_utils::start_method_T& start_method) {
         _start_method = start_method;
         return *this;
     }

     create_webserver& max_threads(int max_threads) {
         _max_threads = max_threads;
         return *this;
     }

     create_webserver& max_connections(int max_connections) {
         _max_connections = max_connections;
         return *this;
     }

     create_webserver& memory_limit(int memory_limit) {
         _memory_limit = memory_limit;
         return *this;
     }

     create_webserver& content_size_limit(size_t content_size_limit) {
         _content_size_limit = content_size_limit;
         return *this;
     }

     create_webserver& connection_timeout(int connection_timeout) {
         _connection_timeout = connection_timeout;
         return *this;
     }

     create_webserver& per_IP_connection_limit(int per_IP_connection_limit) {
         _per_IP_connection_limit = per_IP_connection_limit;
         return *this;
     }

     create_webserver& log_access(log_access_ptr log_access) {
         _log_access = log_access;
         return *this;
     }

     create_webserver& log_error(log_error_ptr log_error) {
         _log_error = log_error;
         return *this;
     }

     create_webserver& validator(validator_ptr validator) {
         _validator = validator;
         return *this;
     }

     create_webserver& unescaper(unescaper_ptr unescaper) {
         _unescaper = unescaper;
         return *this;
     }

     create_webserver& bind_address(const struct sockaddr* bind_address) {
         _bind_address = bind_address;
         return *this;
     }

     create_webserver& bind_socket(int bind_socket) {
         _bind_socket = bind_socket;
         return *this;
     }

     create_webserver& max_thread_stack_size(int max_thread_stack_size) {
         _max_thread_stack_size = max_thread_stack_size;
         return *this;
     }

     create_webserver& use_ssl() {
         _use_ssl = true;
         return *this;
     }

     create_webserver& no_ssl() {
         _use_ssl = false;
         return *this;
     }

     create_webserver& use_ipv6() {
         _use_ipv6 = true;
         return *this;
     }

     create_webserver& no_ipv6() {
         _use_ipv6 = false;
         return *this;
     }

     create_webserver& use_dual_stack() {
         _use_dual_stack = true;
         return *this;
     }

     create_webserver& no_dual_stack() {
         _use_dual_stack = false;
         return *this;
     }

     create_webserver& debug() {
         _debug = true;
         return *this;
     }

     create_webserver& no_debug() {
         _debug = false;
         return *this;
     }

     create_webserver& pedantic() {
         _pedantic = true;
         return *this;
     }

     create_webserver& no_pedantic() {
         _pedantic = false;
         return *this;
     }

     create_webserver& https_mem_key(const std::string& https_mem_key) {
         _https_mem_key = http::load_file(https_mem_key);
         return *this;
     }

     create_webserver& https_mem_cert(const std::string& https_mem_cert) {
         _https_mem_cert = http::load_file(https_mem_cert);
         return *this;
     }

     create_webserver& https_mem_trust(const std::string& https_mem_trust) {
         _https_mem_trust = http::load_file(https_mem_trust);
         return *this;
     }

     create_webserver& raw_https_mem_key(const std::string& https_mem_key) {
         _https_mem_key = https_mem_key;
         return *this;
     }

     create_webserver& raw_https_mem_cert(const std::string& https_mem_cert) {
         _https_mem_cert = https_mem_cert;
         return *this;
     }

     create_webserver& raw_https_mem_trust(const std::string& https_mem_trust) {
         _https_mem_trust = https_mem_trust;
         return *this;
     }

     create_webserver& https_priorities(const std::string& https_priorities) {
         _https_priorities = https_priorities;
         return *this;
     }

     create_webserver& cred_type(const http::http_utils::cred_type_T& cred_type) {
         _cred_type = cred_type;
         return *this;
     }

     create_webserver& digest_auth_random(const std::string& digest_auth_random) {
         _digest_auth_random = digest_auth_random;
         return *this;
     }

     create_webserver& nonce_nc_size(int nonce_nc_size) {
         _nonce_nc_size = nonce_nc_size;
         return *this;
     }

     create_webserver& default_policy(const http::http_utils::policy_T& default_policy) {
         _default_policy = default_policy;
         return *this;
     }

     create_webserver& basic_auth() {
         _basic_auth_enabled = true;
         return *this;
     }

     create_webserver& no_basic_auth() {
         _basic_auth_enabled = false;
         return *this;
     }

     create_webserver& digest_auth() {
         _digest_auth_enabled = true;
         return *this;
     }

     create_webserver& no_digest_auth() {
         _digest_auth_enabled = false;
         return *this;
     }

     create_webserver& deferred() {
         _deferred_enabled = true;
         return *this;
     }

     create_webserver& no_deferred() {
         _deferred_enabled = false;
         return *this;
     }

     create_webserver& regex_checking() {
         _regex_checking = true;
         return *this;
     }

     create_webserver& no_regex_checking() {
         _regex_checking = false;
         return *this;
     }

     create_webserver& ban_system() {
         _ban_system_enabled = true;
         return *this;
     }

     create_webserver& no_ban_system() {
         _ban_system_enabled = false;
         return *this;
     }

     create_webserver& post_process() {
         _post_process_enabled = true;
         return *this;
     }

     create_webserver& no_post_process() {
         _post_process_enabled = false;
         return *this;
     }

     create_webserver& no_put_processed_data_to_content() {
         _put_processed_data_to_content = false;
         return *this;
     }

     create_webserver& put_processed_data_to_content() {
         _put_processed_data_to_content = true;
         return *this;
     }

     create_webserver& file_upload_target(const file_upload_target_T& file_upload_target) {
         _file_upload_target = file_upload_target;
         return *this;
     }

     create_webserver& file_upload_dir(const std::string& file_upload_dir) {
         _file_upload_dir = file_upload_dir;
         return *this;
     }

     create_webserver& no_generate_random_filename_on_upload() {
         _generate_random_filename_on_upload = false;
         return *this;
     }

     create_webserver& generate_random_filename_on_upload() {
         _generate_random_filename_on_upload = true;
         return *this;
     }

     create_webserver& single_resource() {
         _single_resource = true;
         return *this;
     }

     create_webserver& no_single_resource() {
         _single_resource = false;
         return *this;
     }

     create_webserver& tcp_nodelay() {
         _tcp_nodelay = true;
         return *this;
     }

     create_webserver& not_found_resource(render_ptr not_found_resource) {
         _not_found_resource = not_found_resource;
         return *this;
     }

     create_webserver& method_not_allowed_resource(render_ptr method_not_allowed_resource) {
         _method_not_allowed_resource = method_not_allowed_resource;
         return *this;
     }

     create_webserver& internal_error_resource(render_ptr internal_error_resource) {
         _internal_error_resource = internal_error_resource;
         return *this;
     }

 private:
     uint16_t _port = DEFAULT_WS_PORT;
     http::http_utils::start_method_T _start_method = http::http_utils::INTERNAL_SELECT;
     int _max_threads = 0;
     int _max_connections = 0;
     int _memory_limit = 0;
     size_t _content_size_limit = static_cast<size_t>(-1);
     int _connection_timeout = DEFAULT_WS_TIMEOUT;
     int _per_IP_connection_limit = 0;
     log_access_ptr _log_access = nullptr;
     log_error_ptr _log_error = nullptr;
     validator_ptr _validator = nullptr;
     unescaper_ptr _unescaper = nullptr;
     const struct sockaddr* _bind_address = nullptr;
     int _bind_socket = 0;
     int _max_thread_stack_size = 0;
     bool _use_ssl = false;
     bool _use_ipv6 = false;
     bool _use_dual_stack = false;
     bool _debug = false;
     bool _pedantic = false;
     std::string _https_mem_key = "";
     std::string _https_mem_cert = "";
     std::string _https_mem_trust = "";
     std::string _https_priorities = "";
     http::http_utils::cred_type_T _cred_type = http::http_utils::NONE;
     std::string _digest_auth_random = "";
     int _nonce_nc_size = 0;
     http::http_utils::policy_T _default_policy = http::http_utils::ACCEPT;
     bool _basic_auth_enabled = true;
     bool _digest_auth_enabled = true;
     bool _regex_checking = true;
     bool _ban_system_enabled = true;
     bool _post_process_enabled = true;
     bool _put_processed_data_to_content = true;
     file_upload_target_T _file_upload_target = FILE_UPLOAD_MEMORY_ONLY;
     std::string _file_upload_dir = "/tmp";
     bool _generate_random_filename_on_upload = false;
     bool _deferred_enabled = false;
     bool _single_resource = false;
     bool _tcp_nodelay = false;
     render_ptr _not_found_resource = nullptr;
     render_ptr _method_not_allowed_resource = nullptr;
     render_ptr _internal_error_resource = nullptr;

     friend class webserver;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_
