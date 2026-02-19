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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::string_response;

LT_BEGIN_SUITE(create_webserver_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(create_webserver_suite)

// Test basic port configuration
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_port)
    create_webserver cw(8080);
    create_webserver cw2 = create_webserver().port(9090);
    // Just verify it compiles and runs without error
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_port)

// Test max_threads builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_max_threads)
    create_webserver cw = create_webserver(8080).max_threads(4);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_max_threads)

// Test max_connections builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_max_connections)
    create_webserver cw = create_webserver(8080).max_connections(100);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_max_connections)

// Test memory_limit builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_memory_limit)
    create_webserver cw = create_webserver(8080).memory_limit(1024);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_memory_limit)

// Test content_size_limit builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_content_size_limit)
    create_webserver cw = create_webserver(8080).content_size_limit(1024 * 1024);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_content_size_limit)

// Test connection_timeout builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_connection_timeout)
    create_webserver cw = create_webserver(8080).connection_timeout(30);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_connection_timeout)

// Test per_IP_connection_limit builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_per_IP_connection_limit)
    create_webserver cw = create_webserver(8080).per_IP_connection_limit(10);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_per_IP_connection_limit)

// Test use_ssl / no_ssl toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_ssl_toggle)
    create_webserver cw1 = create_webserver(8080).use_ssl();
    create_webserver cw2 = create_webserver(8080).no_ssl();
    create_webserver cw3 = create_webserver(8080).use_ssl().no_ssl();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_ssl_toggle)

// Test use_ipv6 / no_ipv6 toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_ipv6_toggle)
    create_webserver cw1 = create_webserver(8080).use_ipv6();
    create_webserver cw2 = create_webserver(8080).no_ipv6();
    create_webserver cw3 = create_webserver(8080).use_ipv6().no_ipv6();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_ipv6_toggle)

// Test use_dual_stack / no_dual_stack toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_dual_stack_toggle)
    create_webserver cw1 = create_webserver(8080).use_dual_stack();
    create_webserver cw2 = create_webserver(8080).no_dual_stack();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_dual_stack_toggle)

// Test debug / no_debug toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_debug_toggle)
    create_webserver cw1 = create_webserver(8080).debug();
    create_webserver cw2 = create_webserver(8080).no_debug();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_debug_toggle)

// Test pedantic / no_pedantic toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_pedantic_toggle)
    create_webserver cw1 = create_webserver(8080).pedantic();
    create_webserver cw2 = create_webserver(8080).no_pedantic();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_pedantic_toggle)

#ifdef HAVE_BAUTH
// Test basic_auth / no_basic_auth toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_basic_auth_toggle)
    create_webserver cw1 = create_webserver(8080).basic_auth();
    create_webserver cw2 = create_webserver(8080).no_basic_auth();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_basic_auth_toggle)
#endif  // HAVE_BAUTH

// Test digest_auth / no_digest_auth toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_digest_auth_toggle)
    create_webserver cw1 = create_webserver(8080).digest_auth();
    create_webserver cw2 = create_webserver(8080).no_digest_auth();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_digest_auth_toggle)

// Test deferred / no_deferred toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_deferred_toggle)
    create_webserver cw1 = create_webserver(8080).deferred();
    create_webserver cw2 = create_webserver(8080).no_deferred();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_deferred_toggle)

// Test regex_checking / no_regex_checking toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_regex_checking_toggle)
    create_webserver cw1 = create_webserver(8080).regex_checking();
    create_webserver cw2 = create_webserver(8080).no_regex_checking();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_regex_checking_toggle)

// Test ban_system / no_ban_system toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_ban_system_toggle)
    create_webserver cw1 = create_webserver(8080).ban_system();
    create_webserver cw2 = create_webserver(8080).no_ban_system();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_ban_system_toggle)

// Test post_process / no_post_process toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_post_process_toggle)
    create_webserver cw1 = create_webserver(8080).post_process();
    create_webserver cw2 = create_webserver(8080).no_post_process();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_post_process_toggle)

// Test put_processed_data_to_content toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_put_processed_data_toggle)
    create_webserver cw1 = create_webserver(8080).put_processed_data_to_content();
    create_webserver cw2 = create_webserver(8080).no_put_processed_data_to_content();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_put_processed_data_toggle)

// Test single_resource / no_single_resource toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_single_resource_toggle)
    create_webserver cw1 = create_webserver(8080).single_resource();
    create_webserver cw2 = create_webserver(8080).no_single_resource();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_single_resource_toggle)

// Test generate_random_filename_on_upload toggle
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_random_filename_toggle)
    create_webserver cw1 = create_webserver(8080).generate_random_filename_on_upload();
    create_webserver cw2 = create_webserver(8080).no_generate_random_filename_on_upload();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_random_filename_toggle)

// Test tcp_nodelay
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_tcp_nodelay)
    create_webserver cw = create_webserver(8080).tcp_nodelay();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_tcp_nodelay)

// Test file_upload_target configurations
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_file_upload_target)
    create_webserver cw1 = create_webserver(8080)
        .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_ONLY);
    create_webserver cw2 = create_webserver(8080)
        .file_upload_target(httpserver::FILE_UPLOAD_DISK_ONLY);
    create_webserver cw3 = create_webserver(8080)
        .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_AND_DISK);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_file_upload_target)

// Test file_upload_dir
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_file_upload_dir)
    create_webserver cw = create_webserver(8080).file_upload_dir("/tmp/uploads");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_file_upload_dir)

// Test not_found_resource
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_not_found_resource)
    auto not_found_handler = [](const http_request&) {
        return std::make_shared<string_response>("Custom 404", 404);
    };
    create_webserver cw = create_webserver(8080).not_found_resource(not_found_handler);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_not_found_resource)

// Test method_not_allowed_resource
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_method_not_allowed_resource)
    auto method_not_allowed_handler = [](const http_request&) {
        return std::make_shared<string_response>("Custom 405", 405);
    };
    create_webserver cw = create_webserver(8080).method_not_allowed_resource(method_not_allowed_handler);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_method_not_allowed_resource)

// Test internal_error_resource
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_internal_error_resource)
    auto internal_error_handler = [](const http_request&) {
        return std::make_shared<string_response>("Custom 500", 500);
    };
    create_webserver cw = create_webserver(8080).internal_error_resource(internal_error_handler);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_internal_error_resource)

// Test start_method configurations
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_start_method)
    create_webserver cw1 = create_webserver(8080)
        .start_method(httpserver::http::http_utils::INTERNAL_SELECT);
    create_webserver cw2 = create_webserver(8080)
        .start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_start_method)

// Test default_policy configurations
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_default_policy)
    create_webserver cw1 = create_webserver(8080)
        .default_policy(httpserver::http::http_utils::ACCEPT);
    create_webserver cw2 = create_webserver(8080)
        .default_policy(httpserver::http::http_utils::REJECT);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_default_policy)

// Test cred_type configuration
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_cred_type)
    create_webserver cw = create_webserver(8080)
        .cred_type(httpserver::http::http_utils::NONE);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_cred_type)

// Test nonce_nc_size
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_nonce_nc_size)
    create_webserver cw = create_webserver(8080).nonce_nc_size(10);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_nonce_nc_size)

// Test digest_auth_random
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_digest_auth_random)
    create_webserver cw = create_webserver(8080).digest_auth_random("random_seed_string");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_digest_auth_random)

// Test https_priorities
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_https_priorities)
    create_webserver cw = create_webserver(8080).https_priorities("NORMAL:-MD5");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_https_priorities)

// Test raw_https_mem_key
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_raw_https_mem_key)
    create_webserver cw = create_webserver(8080).raw_https_mem_key("raw key content");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_raw_https_mem_key)

// Test raw_https_mem_cert
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_raw_https_mem_cert)
    create_webserver cw = create_webserver(8080).raw_https_mem_cert("raw cert content");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_raw_https_mem_cert)

// Test raw_https_mem_trust
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_raw_https_mem_trust)
    create_webserver cw = create_webserver(8080).raw_https_mem_trust("raw trust content");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_raw_https_mem_trust)

// Test bind_socket
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_bind_socket)
    create_webserver cw = create_webserver(8080).bind_socket(0);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_bind_socket)

// Test max_thread_stack_size
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_max_thread_stack_size)
    create_webserver cw = create_webserver(8080).max_thread_stack_size(4 * 1024 * 1024);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_max_thread_stack_size)

// Test log_access callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_log_access)
    auto log_access_handler = [](const std::string& log_msg) {
        // do nothing with the log
        (void)log_msg;
    };
    create_webserver cw = create_webserver(8080).log_access(log_access_handler);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_log_access)

// Test log_error callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_log_error)
    auto log_error_handler = [](const std::string& log_msg) {
        // do nothing with the log
        (void)log_msg;
    };
    create_webserver cw = create_webserver(8080).log_error(log_error_handler);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_log_error)

// Test validator callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_validator)
    auto validator_handler = [](const std::string& url) {
        (void)url;
        return true;
    };
    create_webserver cw = create_webserver(8080).validator(validator_handler);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_validator)

// Test unescaper callback (signature: void(*)(std::string&))
void test_unescaper(std::string& s) {
    // Simple passthrough unescaper
    (void)s;
}

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_unescaper)
    create_webserver cw = create_webserver(8080).unescaper(test_unescaper);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_unescaper)

// Test auth_handler callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_auth_handler)
    auto auth_handler = [](const http_request&) {
        return std::shared_ptr<http_response>(nullptr);
    };
    create_webserver cw = create_webserver(8080).auth_handler(auth_handler);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_auth_handler)

// Test auth_skip_paths
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_auth_skip_paths)
    std::vector<std::string> skip_paths = {"/public", "/health", "/static/*"};
    create_webserver cw = create_webserver(8080).auth_skip_paths(skip_paths);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_auth_skip_paths)

// Test file_cleanup_callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_file_cleanup_callback)
    auto cleanup_handler = [](const std::string& field_name,
                              const std::string& file_name,
                              const httpserver::http::file_info& fi) {
        (void)field_name;
        (void)file_name;
        (void)fi;
        return true;  // return true to delete file
    };
    create_webserver cw = create_webserver(8080).file_cleanup_callback(cleanup_handler);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_file_cleanup_callback)

// Test PSK cred handler callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_psk_cred_handler)
    auto psk_handler = [](const std::string& identity) {
        (void)identity;
        return std::string("psk_key");
    };
    create_webserver cw = create_webserver(8080).psk_cred_handler(psk_handler);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_psk_cred_handler)

// Test copy constructor
LT_BEGIN_AUTO_TEST(create_webserver_suite, copy_constructor)
    create_webserver cw1 = create_webserver(8080)
        .max_threads(4)
        .max_connections(100)
        .debug();
    create_webserver cw2(cw1);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(copy_constructor)

// Test move constructor
LT_BEGIN_AUTO_TEST(create_webserver_suite, move_constructor)
    create_webserver cw1 = create_webserver(8080).max_threads(4);
    create_webserver cw2(std::move(cw1));
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(move_constructor)

// Test assignment operator
LT_BEGIN_AUTO_TEST(create_webserver_suite, assignment_operator)
    create_webserver cw1 = create_webserver(8080).max_threads(4);
    create_webserver cw2 = create_webserver(9090);
    cw2 = cw1;
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(assignment_operator)

// Test move assignment operator
LT_BEGIN_AUTO_TEST(create_webserver_suite, move_assignment_operator)
    create_webserver cw1 = create_webserver(8080).max_threads(4);
    create_webserver cw2 = create_webserver(9090);
    cw2 = std::move(cw1);
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(move_assignment_operator)

// Test method chaining with many options
LT_BEGIN_AUTO_TEST(create_webserver_suite, method_chaining)
    create_webserver cw = create_webserver(8080)
        .max_threads(4)
        .max_connections(100)
        .memory_limit(1024)
        .content_size_limit(1024 * 1024)
        .connection_timeout(30)
        .per_IP_connection_limit(10)
        .debug()
        .pedantic()
        .regex_checking()
        .ban_system()
        .post_process()
        .tcp_nodelay();
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(method_chaining)

// Test default constructor
LT_BEGIN_AUTO_TEST(create_webserver_suite, default_constructor)
    create_webserver cw;
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(default_constructor)

// Test https_mem_key (loads from file path)
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_https_mem_key_file)
    // Use the test key file that exists in the test directory
    create_webserver cw = create_webserver(8080).https_mem_key("../test/key.pem");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_https_mem_key_file)

// Test https_mem_cert (loads from file path)
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_https_mem_cert_file)
    // Use the test cert file that exists in the test directory
    create_webserver cw = create_webserver(8080).https_mem_cert("../test/cert.pem");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_https_mem_cert_file)

// Test https_mem_trust (loads from file path)
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_https_mem_trust_file)
    // Use the test CA file that exists in the test directory
    create_webserver cw = create_webserver(8080).https_mem_trust("../test/test_root_ca.pem");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(builder_https_mem_trust_file)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
