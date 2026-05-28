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
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::http_request;
using httpserver::http_response;

LT_BEGIN_SUITE(create_webserver_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(create_webserver_suite)

// Test basic port configuration
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_port)
    LT_CHECK_NOTHROW(create_webserver(8080));
    LT_CHECK_NOTHROW(create_webserver().port(9090));
LT_END_AUTO_TEST(builder_port)

// Test max_threads builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_max_threads)
    LT_CHECK_NOTHROW(create_webserver(8080).max_threads(4));
LT_END_AUTO_TEST(builder_max_threads)

// Test max_connections builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_max_connections)
    LT_CHECK_NOTHROW(create_webserver(8080).max_connections(100));
LT_END_AUTO_TEST(builder_max_connections)

// Test memory_limit builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_memory_limit)
    LT_CHECK_NOTHROW(create_webserver(8080).memory_limit(1024));
LT_END_AUTO_TEST(builder_memory_limit)

// Test content_size_limit builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_content_size_limit)
    LT_CHECK_NOTHROW(create_webserver(8080).content_size_limit(1024 * 1024));
LT_END_AUTO_TEST(builder_content_size_limit)

// content_size_limit(0): zero is a size_t (unsigned) and has no validation
// guard; the design decision is that zero is accepted (meaning no content
// will be buffered). Verify it does not throw.
LT_BEGIN_AUTO_TEST(create_webserver_suite, content_size_limit_zero_accepted)
    LT_CHECK_NOTHROW(create_webserver().content_size_limit(0));
LT_END_AUTO_TEST(content_size_limit_zero_accepted)

// Test connection_timeout builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_connection_timeout)
    LT_CHECK_NOTHROW(create_webserver(8080).connection_timeout(30));
LT_END_AUTO_TEST(builder_connection_timeout)

// Test per_IP_connection_limit builder method
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_per_IP_connection_limit)
    LT_CHECK_NOTHROW(create_webserver(8080).per_IP_connection_limit(10));
LT_END_AUTO_TEST(builder_per_IP_connection_limit)

// TASK-033 / PRD-CFG-REQ-001: every boolean flag setter takes a defaulted
// `bool enable = true`. The tests below exercise both (true) and (false)
// plus the default-arg form and confirm they all chain without throwing.
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_ssl_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).use_ssl());
    LT_CHECK_NOTHROW(create_webserver(8080).use_ssl(false));
    LT_CHECK_NOTHROW(create_webserver(8080).use_ssl(true).use_ssl(false));
LT_END_AUTO_TEST(builder_ssl_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_ipv6_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).use_ipv6());
    LT_CHECK_NOTHROW(create_webserver(8080).use_ipv6(false));
    LT_CHECK_NOTHROW(create_webserver(8080).use_ipv6(true).use_ipv6(false));
LT_END_AUTO_TEST(builder_ipv6_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_dual_stack_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).use_dual_stack());
    LT_CHECK_NOTHROW(create_webserver(8080).use_dual_stack(false));
LT_END_AUTO_TEST(builder_dual_stack_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_debug_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).debug());
    LT_CHECK_NOTHROW(create_webserver(8080).debug(false));
LT_END_AUTO_TEST(builder_debug_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_pedantic_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).pedantic());
    LT_CHECK_NOTHROW(create_webserver(8080).pedantic(false));
LT_END_AUTO_TEST(builder_pedantic_toggle)

#ifdef HAVE_BAUTH
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_basic_auth_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).basic_auth());
    LT_CHECK_NOTHROW(create_webserver(8080).basic_auth(false));
LT_END_AUTO_TEST(builder_basic_auth_toggle)
#endif  // HAVE_BAUTH

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_digest_auth_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).digest_auth());
    LT_CHECK_NOTHROW(create_webserver(8080).digest_auth(false));
LT_END_AUTO_TEST(builder_digest_auth_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_deferred_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).deferred());
    LT_CHECK_NOTHROW(create_webserver(8080).deferred(false));
LT_END_AUTO_TEST(builder_deferred_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_regex_checking_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).regex_checking());
    LT_CHECK_NOTHROW(create_webserver(8080).regex_checking(false));
LT_END_AUTO_TEST(builder_regex_checking_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_ban_system_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).ban_system());
    LT_CHECK_NOTHROW(create_webserver(8080).ban_system(false));
LT_END_AUTO_TEST(builder_ban_system_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_post_process_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).post_process());
    LT_CHECK_NOTHROW(create_webserver(8080).post_process(false));
LT_END_AUTO_TEST(builder_post_process_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_put_processed_data_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).put_processed_data_to_content());
    LT_CHECK_NOTHROW(create_webserver(8080).put_processed_data_to_content(false));
LT_END_AUTO_TEST(builder_put_processed_data_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_single_resource_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).single_resource());
    LT_CHECK_NOTHROW(create_webserver(8080).single_resource(false));
LT_END_AUTO_TEST(builder_single_resource_toggle)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_random_filename_toggle)
    LT_CHECK_NOTHROW(create_webserver(8080).generate_random_filename_on_upload());
    LT_CHECK_NOTHROW(create_webserver(8080).generate_random_filename_on_upload(false));
LT_END_AUTO_TEST(builder_random_filename_toggle)

// TASK-033: setters renamed from no_listen_socket/no_thread_safety/no_alpn.
// Polarity is inverted at the API surface: listen_socket(true) means
// "have a listening socket", which maps to _no_listen_socket=false internally.
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_listen_socket_bool)
    LT_CHECK_NOTHROW(create_webserver(8080).listen_socket());
    LT_CHECK_NOTHROW(create_webserver(8080).listen_socket(false));
    LT_CHECK_NOTHROW(create_webserver(8080).listen_socket(false).listen_socket(true));
LT_END_AUTO_TEST(builder_listen_socket_bool)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_thread_safety_bool)
    LT_CHECK_NOTHROW(create_webserver(8080).thread_safety());
    LT_CHECK_NOTHROW(create_webserver(8080).thread_safety(false));
LT_END_AUTO_TEST(builder_thread_safety_bool)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_alpn_bool)
    LT_CHECK_NOTHROW(create_webserver(8080).alpn());
    LT_CHECK_NOTHROW(create_webserver(8080).alpn(false));
LT_END_AUTO_TEST(builder_alpn_bool)

// Widened positive-only flags: tcp_nodelay, turbo, suppress_date_header,
// sigpipe_handled_by_app all take bool enable = true (TASK-033).
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_turbo_bool)
    LT_CHECK_NOTHROW(create_webserver(8080).turbo());
    LT_CHECK_NOTHROW(create_webserver(8080).turbo(false));
LT_END_AUTO_TEST(builder_turbo_bool)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_suppress_date_header_bool)
    LT_CHECK_NOTHROW(create_webserver(8080).suppress_date_header());
    LT_CHECK_NOTHROW(create_webserver(8080).suppress_date_header(false));
LT_END_AUTO_TEST(builder_suppress_date_header_bool)

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_sigpipe_handled_by_app_bool)
    LT_CHECK_NOTHROW(create_webserver(8080).sigpipe_handled_by_app());
    LT_CHECK_NOTHROW(create_webserver(8080).sigpipe_handled_by_app(false));
LT_END_AUTO_TEST(builder_sigpipe_handled_by_app_bool)

// Test tcp_nodelay (widened to bool enable = true per TASK-033)
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_tcp_nodelay)
    LT_CHECK_NOTHROW(create_webserver(8080).tcp_nodelay());
    LT_CHECK_NOTHROW(create_webserver(8080).tcp_nodelay(false));
LT_END_AUTO_TEST(builder_tcp_nodelay)

// Test file_upload_target configurations
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_file_upload_target)
    LT_CHECK_NOTHROW(
        create_webserver(8080).file_upload_target(httpserver::FILE_UPLOAD_MEMORY_ONLY));
    LT_CHECK_NOTHROW(
        create_webserver(8080).file_upload_target(httpserver::FILE_UPLOAD_DISK_ONLY));
    LT_CHECK_NOTHROW(
        create_webserver(8080).file_upload_target(httpserver::FILE_UPLOAD_MEMORY_AND_DISK));
LT_END_AUTO_TEST(builder_file_upload_target)

// Test file_upload_dir (non-empty path must not throw)
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_file_upload_dir)
    LT_CHECK_NOTHROW(create_webserver(8080).file_upload_dir("/tmp/uploads"));
LT_END_AUTO_TEST(builder_file_upload_dir)

// Test not_found_handler
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_not_found_handler)
    auto not_found_handler = [](const http_request&) {
        return http_response::string("Custom 404").with_status(404);
    };
    LT_CHECK_NOTHROW(create_webserver(8080).not_found_handler(not_found_handler));
LT_END_AUTO_TEST(builder_not_found_handler)

// Test method_not_allowed_handler
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_method_not_allowed_handler)
    auto method_not_allowed_handler = [](const http_request&) {
        return http_response::string("Custom 405").with_status(405);
    };
    LT_CHECK_NOTHROW(
        create_webserver(8080).method_not_allowed_handler(method_not_allowed_handler));
LT_END_AUTO_TEST(builder_method_not_allowed_handler)

// Test internal_error_handler
// TASK-031: signature widened to (request, message) per DR-009 §5.2.
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_internal_error_handler)
    auto internal_error_handler = [](const http_request&, std::string_view) {
        return http_response::string("Custom 500").with_status(500);
    };
    LT_CHECK_NOTHROW(create_webserver(8080).internal_error_handler(internal_error_handler));
LT_END_AUTO_TEST(builder_internal_error_handler)

// Test start_method configurations
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_start_method)
    LT_CHECK_NOTHROW(
        create_webserver(8080).start_method(httpserver::http::http_utils::INTERNAL_SELECT));
    LT_CHECK_NOTHROW(
        create_webserver(8080).start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION));
LT_END_AUTO_TEST(builder_start_method)

// Test default_policy configurations
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_default_policy)
    LT_CHECK_NOTHROW(
        create_webserver(8080).default_policy(httpserver::http::http_utils::ACCEPT));
    LT_CHECK_NOTHROW(
        create_webserver(8080).default_policy(httpserver::http::http_utils::REJECT));
LT_END_AUTO_TEST(builder_default_policy)

// Test cred_type configuration
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_cred_type)
    LT_CHECK_NOTHROW(
        create_webserver(8080).cred_type(httpserver::http::http_utils::NONE));
LT_END_AUTO_TEST(builder_cred_type)

// Test nonce_nc_size
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_nonce_nc_size)
    LT_CHECK_NOTHROW(create_webserver(8080).nonce_nc_size(10));
LT_END_AUTO_TEST(builder_nonce_nc_size)

// Test digest_auth_random
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_digest_auth_random)
    LT_CHECK_NOTHROW(create_webserver(8080).digest_auth_random("random_seed_string"));
LT_END_AUTO_TEST(builder_digest_auth_random)

// Test https_priorities
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_https_priorities)
    LT_CHECK_NOTHROW(create_webserver(8080).https_priorities("NORMAL:-MD5"));
LT_END_AUTO_TEST(builder_https_priorities)

// Test raw_https_mem_key
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_raw_https_mem_key)
    LT_CHECK_NOTHROW(create_webserver(8080).raw_https_mem_key("raw key content"));
LT_END_AUTO_TEST(builder_raw_https_mem_key)

// Test raw_https_mem_cert
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_raw_https_mem_cert)
    LT_CHECK_NOTHROW(create_webserver(8080).raw_https_mem_cert("raw cert content"));
LT_END_AUTO_TEST(builder_raw_https_mem_cert)

// Test raw_https_mem_trust
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_raw_https_mem_trust)
    LT_CHECK_NOTHROW(create_webserver(8080).raw_https_mem_trust("raw trust content"));
LT_END_AUTO_TEST(builder_raw_https_mem_trust)

// Test bind_socket
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_bind_socket)
    LT_CHECK_NOTHROW(create_webserver(8080).bind_socket(0));
LT_END_AUTO_TEST(builder_bind_socket)

// Test max_thread_stack_size
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_max_thread_stack_size)
    LT_CHECK_NOTHROW(create_webserver(8080).max_thread_stack_size(4 * 1024 * 1024));
LT_END_AUTO_TEST(builder_max_thread_stack_size)

// Test log_access callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_log_access)
    auto log_access_handler = [](const std::string& log_msg) {
        (void)log_msg;
    };
    LT_CHECK_NOTHROW(create_webserver(8080).log_access(log_access_handler));
LT_END_AUTO_TEST(builder_log_access)

// Test log_error callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_log_error)
    auto log_error_handler = [](const std::string& log_msg) {
        (void)log_msg;
    };
    LT_CHECK_NOTHROW(create_webserver(8080).log_error(log_error_handler));
LT_END_AUTO_TEST(builder_log_error)

// Test validator callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_validator)
    auto validator_handler = [](const std::string& url) {
        (void)url;
        return true;
    };
    LT_CHECK_NOTHROW(create_webserver(8080).validator(validator_handler));
LT_END_AUTO_TEST(builder_validator)

// Test unescaper callback (signature: void(*)(std::string&))
void test_unescaper(std::string& s) {
    // Simple passthrough unescaper
    (void)s;
}

LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_unescaper)
    LT_CHECK_NOTHROW(create_webserver(8080).unescaper(test_unescaper));
LT_END_AUTO_TEST(builder_unescaper)

// Test auth_handler callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_auth_handler)
    auto auth_handler = [](const http_request&) {
        return std::shared_ptr<http_response>(nullptr);
    };
    LT_CHECK_NOTHROW(create_webserver(8080).auth_handler(auth_handler));
LT_END_AUTO_TEST(builder_auth_handler)

// Test auth_skip_paths
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_auth_skip_paths)
    std::vector<std::string> skip_paths = {"/public", "/health", "/static/*"};
    LT_CHECK_NOTHROW(create_webserver(8080).auth_skip_paths(skip_paths));
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
    LT_CHECK_NOTHROW(create_webserver(8080).file_cleanup_callback(cleanup_handler));
LT_END_AUTO_TEST(builder_file_cleanup_callback)

// Test PSK cred handler callback
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_psk_cred_handler)
    auto psk_handler = [](const std::string& identity) {
        (void)identity;
        return std::string("psk_key");
    };
    LT_CHECK_NOTHROW(create_webserver(8080).psk_cred_handler(psk_handler));
LT_END_AUTO_TEST(builder_psk_cred_handler)

// Test copy constructor: copied object must be independently functional.
// Verify by checking that chaining still produces a valid builder.
LT_BEGIN_AUTO_TEST(create_webserver_suite, copy_constructor)
    create_webserver cw1 = create_webserver(8080)
        .max_threads(4)
        .max_connections(100)
        .debug();
    // Copy must not throw.
    create_webserver cw2(cw1);
    // Copied builder can be further configured without throwing.
    LT_CHECK_NOTHROW(cw2.max_threads(8));
LT_END_AUTO_TEST(copy_constructor)

// Test move constructor: the moved-from builder transfers its state.
LT_BEGIN_AUTO_TEST(create_webserver_suite, move_constructor)
    create_webserver cw1 = create_webserver(8080).max_threads(4);
    // Move must not throw.
    create_webserver cw2(std::move(cw1));
    LT_CHECK_NOTHROW(cw2.max_threads(8));
LT_END_AUTO_TEST(move_constructor)

// Test assignment operator: assignment produces an independently usable copy.
LT_BEGIN_AUTO_TEST(create_webserver_suite, assignment_operator)
    create_webserver cw1 = create_webserver(8080).max_threads(4);
    create_webserver cw2 = create_webserver(9090);
    LT_CHECK_NOTHROW(cw2 = cw1);
    // After assignment, cw2 can be further configured without throwing.
    LT_CHECK_NOTHROW(cw2.max_threads(8));
LT_END_AUTO_TEST(assignment_operator)

// Test move assignment operator.
LT_BEGIN_AUTO_TEST(create_webserver_suite, move_assignment_operator)
    create_webserver cw1 = create_webserver(8080).max_threads(4);
    create_webserver cw2 = create_webserver(9090);
    LT_CHECK_NOTHROW(cw2 = std::move(cw1));
    LT_CHECK_NOTHROW(cw2.max_threads(8));
LT_END_AUTO_TEST(move_assignment_operator)

// Test method chaining with many options: all setters in a chain must not throw.
LT_BEGIN_AUTO_TEST(create_webserver_suite, method_chaining)
    LT_CHECK_NOTHROW(create_webserver(8080)
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
        .tcp_nodelay());
LT_END_AUTO_TEST(method_chaining)

// Test default constructor: default-constructed builder must not throw.
LT_BEGIN_AUTO_TEST(create_webserver_suite, default_constructor)
    create_webserver cw;
    LT_CHECK_NOTHROW(cw.max_threads(0));
LT_END_AUTO_TEST(default_constructor)

// Test https_mem_key (loads from file path)
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_https_mem_key_file)
    // Use the test key file that exists in the test directory
    LT_CHECK_NOTHROW(create_webserver(8080).https_mem_key("../test/key.pem"));
LT_END_AUTO_TEST(builder_https_mem_key_file)

// Test https_mem_cert (loads from file path)
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_https_mem_cert_file)
    // Use the test cert file that exists in the test directory
    LT_CHECK_NOTHROW(create_webserver(8080).https_mem_cert("../test/cert.pem"));
LT_END_AUTO_TEST(builder_https_mem_cert_file)

// Test https_mem_trust (loads from file path)
LT_BEGIN_AUTO_TEST(create_webserver_suite, builder_https_mem_trust_file)
    // Use the test CA file that exists in the test directory
    LT_CHECK_NOTHROW(create_webserver(8080).https_mem_trust("../test/test_root_ca.pem"));
LT_END_AUTO_TEST(builder_https_mem_trust_file)

// Helper: assert that `op()` throws std::invalid_argument whose what()
// contains `needle`. Used by the numeric-validation tests below.
namespace {
template <typename Fn>
bool throws_invalid_argument_with(Fn&& op, const std::string& needle) {
    try {
        op();
    } catch (const std::invalid_argument& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    } catch (...) {
        return false;
    }
    return false;
}
}  // namespace

// TASK-033: check_non_negative includes both the param name and the offending
// value in the message (e.g. "max_threads: -1 must be >= 0"). Both substrings
// are verified so a regression stripping the value is caught by the test.
LT_BEGIN_AUTO_TEST(create_webserver_suite, max_threads_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().max_threads(-1); }, "max_threads"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().max_threads(-1); }, "-1"));
LT_END_AUTO_TEST(max_threads_negative_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, max_connections_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().max_connections(-1); }, "max_connections"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().max_connections(-1); }, "-1"));
LT_END_AUTO_TEST(max_connections_negative_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, memory_limit_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().memory_limit(-1); }, "memory_limit"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().memory_limit(-1); }, "-1"));
LT_END_AUTO_TEST(memory_limit_negative_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, connection_timeout_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().connection_timeout(-1); }, "connection_timeout"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().connection_timeout(-1); }, "-1"));
LT_END_AUTO_TEST(connection_timeout_negative_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, per_IP_connection_limit_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().per_IP_connection_limit(-1); }, "per_IP_connection_limit"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().per_IP_connection_limit(-1); }, "-1"));
LT_END_AUTO_TEST(per_IP_connection_limit_negative_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, max_thread_stack_size_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().max_thread_stack_size(-1); }, "max_thread_stack_size"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().max_thread_stack_size(-1); }, "-1"));
LT_END_AUTO_TEST(max_thread_stack_size_negative_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, nonce_nc_size_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().nonce_nc_size(-1); }, "nonce_nc_size"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().nonce_nc_size(-1); }, "-1"));
LT_END_AUTO_TEST(nonce_nc_size_negative_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, listen_backlog_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().listen_backlog(-1); }, "listen_backlog"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().listen_backlog(-1); }, "-1"));
LT_END_AUTO_TEST(listen_backlog_negative_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, address_reuse_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().address_reuse(-1); }, "address_reuse"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().address_reuse(-1); }, "-1"));
LT_END_AUTO_TEST(address_reuse_negative_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, tcp_fastopen_queue_size_negative_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().tcp_fastopen_queue_size(-1); }, "tcp_fastopen_queue_size"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().tcp_fastopen_queue_size(-1); }, "-1"));
LT_END_AUTO_TEST(tcp_fastopen_queue_size_negative_throws)

// connection_memory_increment takes size_t (unsigned) — no negative path.
// Zero is allowed (library default) and a typical 4096-byte increment
// must be accepted without throwing.
LT_BEGIN_AUTO_TEST(create_webserver_suite, connection_memory_increment_zero_accepted)
    LT_CHECK_NOTHROW(create_webserver().connection_memory_increment(0));
LT_END_AUTO_TEST(connection_memory_increment_zero_accepted)

LT_BEGIN_AUTO_TEST(create_webserver_suite, connection_memory_increment_typical_accepted)
    LT_CHECK_NOTHROW(create_webserver().connection_memory_increment(4096));
LT_END_AUTO_TEST(connection_memory_increment_typical_accepted)

LT_BEGIN_AUTO_TEST(create_webserver_suite, client_discipline_level_below_minus_one_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().client_discipline_level(-2); }, "client_discipline_level"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().client_discipline_level(-2); }, "-2"));
LT_END_AUTO_TEST(client_discipline_level_below_minus_one_throws)

LT_BEGIN_AUTO_TEST(create_webserver_suite, client_discipline_level_minus_one_ok)
    LT_CHECK_NOTHROW(create_webserver().client_discipline_level(-1));
LT_END_AUTO_TEST(client_discipline_level_minus_one_ok)

LT_BEGIN_AUTO_TEST(create_webserver_suite, file_upload_dir_empty_throws)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().file_upload_dir(""); }, "file_upload_dir"));
LT_END_AUTO_TEST(file_upload_dir_empty_throws)

// TASK-033: bind_address(string) now prefixes the message with "bind_address".
LT_BEGIN_AUTO_TEST(create_webserver_suite, bind_address_invalid_ip_throws_with_param_name)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().bind_address(std::string("not.an.ip")); }, "bind_address"));
LT_END_AUTO_TEST(bind_address_invalid_ip_throws_with_param_name)

// Low boundary: port(0) is valid (OS-assigned ephemeral port); pin both the
// int-setter and the uint16_t constructor path at the maximum boundary.
LT_BEGIN_AUTO_TEST(create_webserver_suite, port_min_valid_boundary_accepted)
    LT_CHECK_NOTHROW(create_webserver().port(0));
LT_END_AUTO_TEST(port_min_valid_boundary_accepted)

// High boundary: port(65535) is the maximum valid value; both the int-setter
// and the uint16_t constructor overload must accept it without throwing.
LT_BEGIN_AUTO_TEST(create_webserver_suite, port_max_valid_boundary_accepted)
    LT_CHECK_NOTHROW(create_webserver().port(65535));
    LT_CHECK_NOTHROW(create_webserver(static_cast<std::uint16_t>(65535)));
LT_END_AUTO_TEST(port_max_valid_boundary_accepted)

// One above max: port(65536) must throw; message must name "port" and "65536".
LT_BEGIN_AUTO_TEST(create_webserver_suite, port_one_above_max_rejected)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().port(65536); }, "port"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().port(65536); }, "65536"));
LT_END_AUTO_TEST(port_one_above_max_rejected)

// Negative port must throw; message must name "port" and the offending value.
LT_BEGIN_AUTO_TEST(create_webserver_suite, port_negative_rejected)
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().port(-1); }, "port"));
    LT_CHECK(throws_invalid_argument_with(
        []{ create_webserver().port(-1); }, "-1"));
LT_END_AUTO_TEST(port_negative_rejected)

// TASK-034 cycle C: on a HAVE_GNUTLS-off build, calling
// webserver(create_webserver{...}.use_ssl(true)) must throw
// feature_unavailable whose what() names both the feature and the flag.
// The setter itself must still chain without throwing (consistent with
// the existing builder_ssl_toggle test above).
#ifndef HAVE_GNUTLS
LT_BEGIN_AUTO_TEST(create_webserver_suite,
                   use_ssl_true_throws_feature_unavailable_when_no_tls)
    bool caught = false;
    std::string msg;
    try {
        httpserver::webserver ws{
            create_webserver(8080).use_ssl(true)
        };
        (void)ws;
    } catch (const httpserver::feature_unavailable& e) {
        caught = true;
        msg = e.what();
    }
    LT_CHECK(caught);
    LT_CHECK(msg.find("tls") != std::string::npos);
    LT_CHECK(msg.find("HAVE_GNUTLS") != std::string::npos);
LT_END_AUTO_TEST(use_ssl_true_throws_feature_unavailable_when_no_tls)

LT_BEGIN_AUTO_TEST(create_webserver_suite,
                   use_ssl_false_does_not_throw_when_no_tls)
    LT_CHECK_NOTHROW(httpserver::webserver{create_webserver(8081).use_ssl(false)});
LT_END_AUTO_TEST(use_ssl_false_does_not_throw_when_no_tls)

LT_BEGIN_AUTO_TEST(create_webserver_suite,
                   use_ssl_setter_does_not_throw_when_no_tls)
    // The setter is fluent; only construction validates.
    LT_CHECK_NOTHROW(create_webserver(8082).use_ssl(true).use_ssl(false));
LT_END_AUTO_TEST(use_ssl_setter_does_not_throw_when_no_tls)
#endif  // !HAVE_GNUTLS

#ifndef HAVE_BAUTH
LT_BEGIN_AUTO_TEST(create_webserver_suite,
                   basic_auth_true_throws_feature_unavailable_when_no_bauth)
    bool caught = false;
    std::string msg;
    try {
        httpserver::webserver ws{
            create_webserver(8083).basic_auth(true)
        };
        (void)ws;
    } catch (const httpserver::feature_unavailable& e) {
        caught = true;
        msg = e.what();
    }
    LT_CHECK(caught);
    LT_CHECK(msg.find("basic_auth") != std::string::npos);
    LT_CHECK(msg.find("HAVE_BAUTH") != std::string::npos);
LT_END_AUTO_TEST(basic_auth_true_throws_feature_unavailable_when_no_bauth)

LT_BEGIN_AUTO_TEST(create_webserver_suite,
                   basic_auth_false_does_not_throw_when_no_bauth)
    LT_CHECK_NOTHROW(httpserver::webserver{create_webserver(8084).basic_auth(false)});
LT_END_AUTO_TEST(basic_auth_false_does_not_throw_when_no_bauth)
#endif  // !HAVE_BAUTH

// TASK-034 cleanup: on HAVE_BAUTH-on builds, constructing a webserver with
// basic_auth(true) must succeed (the guard fires only on HAVE_BAUTH-off).
// Guards the inverse of basic_auth_true_throws_feature_unavailable_when_no_bauth.
#ifdef HAVE_BAUTH
LT_BEGIN_AUTO_TEST(create_webserver_suite,
                   basic_auth_true_succeeds_when_bauth_available)
    // Construction must not throw; listen_socket(false) avoids binding a port.
    bool threw = false;
    try {
        httpserver::webserver ws{create_webserver(8085).basic_auth(true).listen_socket(false)};
        (void)ws;
    } catch (...) {
        threw = true;
    }
    LT_CHECK(!threw);
LT_END_AUTO_TEST(basic_auth_true_succeeds_when_bauth_available)
#endif  // HAVE_BAUTH

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
