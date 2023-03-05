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

#ifndef SRC_HTTPSERVER_HTTP_UTILS_HPP_
#define SRC_HTTPSERVER_HTTP_UTILS_HPP_

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif

// needed to force Vista as a bare minimum to have inet_ntop (libmicro defines
// this to include XP support as a lower version).
#if defined(__MINGW32__) || defined(__CYGWIN32__)
#define _WINDOWS
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x600
#endif

// needed to have the fd_set definition ahead of microhttpd.h import
#if defined(__CYGWIN__)
#include <sys/select.h>
#endif

#include <microhttpd.h>
#include <stddef.h>

#if !defined(__MINGW32__)
#include <sys/socket.h>
#endif

#include <algorithm>
#include <cctype>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "httpserver/http_arg_value.hpp"

#define DEFAULT_MASK_VALUE 0xFFFF

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

enum file_upload_target_T {
     FILE_UPLOAD_MEMORY_ONLY,
     FILE_UPLOAD_DISK_ONLY,
     FILE_UPLOAD_MEMORY_AND_DISK,
};

typedef void(*unescaper_ptr)(std::string&);

namespace http {

struct generateFilenameException : public std::exception {
 public:
     explicit generateFilenameException(const std::string& message) noexcept : error_message(message) {
     }

     const char* what() const noexcept {
          return this->error_message.c_str();
     }

 private:
     std::string error_message;
};

class http_utils {
 public:
     enum cred_type_T {
         NONE = -1
#ifdef HAVE_GNUTLS
         , CERTIFICATE = GNUTLS_CRD_CERTIFICATE,
         ANON = GNUTLS_CRD_ANON,
         SRP = GNUTLS_CRD_SRP,
         PSK = GNUTLS_CRD_PSK,
         IA = GNUTLS_CRD_IA
#endif
     };

     enum start_method_T {
         INTERNAL_SELECT = MHD_USE_SELECT_INTERNALLY | MHD_USE_AUTO,
         THREAD_PER_CONNECTION = MHD_USE_THREAD_PER_CONNECTION | MHD_USE_AUTO
     };

     enum policy_T {
         ACCEPT,
         REJECT
     };

     enum IP_version_T {
         IPV4 = 4,
         IPV6 = 16
     };

     static const uint16_t http_method_connect_code;
     static const uint16_t http_method_delete_code;
     static const uint16_t http_method_get_code;
     static const uint16_t http_method_head_code;
     static const uint16_t http_method_options_code;
     static const uint16_t http_method_post_code;
     static const uint16_t http_method_put_code;
     static const uint16_t http_method_trace_code;
     static const uint16_t http_method_patch_code;
     static const uint16_t http_method_unknown_code;

     static const int http_continue;
     static const int http_switching_protocol;
     static const int http_processing;

     static const int http_ok;
     static const int http_created;
     static const int http_accepted;
     static const int http_non_authoritative_information;
     static const int http_no_content;
     static const int http_reset_content;
     static const int http_partial_content;
     static const int http_multi_status;

     static const int http_multiple_choices;
     static const int http_moved_permanently;
     static const int http_found;
     static const int http_see_other;
     static const int http_not_modified;
     static const int http_use_proxy;
     static const int http_switch_proxy;
     static const int http_temporary_redirect;

     static const int http_bad_request;
     static const int http_unauthorized;
     static const int http_payment_required;
     static const int http_forbidden;
     static const int http_not_found;
     static const int http_method_not_allowed;
     static const int http_method_not_acceptable;
     static const int http_proxy_authentication_required;
     static const int http_request_timeout;
     static const int http_conflict;
     static const int http_gone;
     static const int http_length_required;
     static const int http_precondition_failed;
     static const int http_request_entity_too_large;
     static const int http_request_uri_too_long;
     static const int http_unsupported_media_type;
     static const int http_requested_range_not_satisfiable;
     static const int http_expectation_failed;
     static const int http_unprocessable_entity;
     static const int http_locked;
     static const int http_failed_dependency;
     static const int http_upgrade_required;
     static const int http_retry_with;

     static const int http_internal_server_error;
     static const int http_not_implemented;
     static const int http_bad_gateway;
     static const int http_service_unavailable;
     static const int http_gateway_timeout;
     static const int http_version_not_supported;
     static const int http_variant_also_negotiated;
     static const int http_insufficient_storage;
     static const int http_bandwidth_limit_exceeded;
     static const int http_not_extended;

     static const int shoutcast_response;

     // See also: http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html
     static const char* http_header_accept;
     static const char* http_header_accept_charset;
     static const char* http_header_accept_encoding;
     static const char* http_header_accept_language;
     static const char* http_header_accept_ranges;
     static const char* http_header_age;
     static const char* http_header_allow;
     static const char* http_header_authorization;
     static const char* http_header_cache_control;
     static const char* http_header_connection;
     static const char* http_header_content_encoding;
     static const char* http_header_content_language;
     static const char* http_header_content_length;
     static const char* http_header_content_location;
     static const char* http_header_content_md5;
     static const char* http_header_content_range;
     static const char* http_header_content_type;
     static const char* http_header_date;
     static const char* http_header_etag;
     static const char* http_header_expect;
     static const char* http_header_expires;
     static const char* http_header_from;
     static const char* http_header_host;
     static const char* http_header_if_match;
     static const char* http_header_if_modified_since;
     static const char* http_header_if_none_match;
     static const char* http_header_if_range;
     static const char* http_header_if_unmodified_since;
     static const char* http_header_last_modified;
     static const char* http_header_location;
     static const char* http_header_max_forwards;
     static const char* http_header_pragma;
     static const char* http_header_proxy_authenticate;
     static const char* http_header_proxy_authentication;
     static const char* http_header_range;
     static const char* http_header_referer;
     static const char* http_header_retry_after;
     static const char* http_header_server;
     static const char* http_header_te;
     static const char* http_header_trailer;
     static const char* http_header_transfer_encoding;
     static const char* http_header_upgrade;
     static const char* http_header_user_agent;
     static const char* http_header_vary;
     static const char* http_header_via;
     static const char* http_header_warning;
     static const char* http_header_www_authenticate;

     static const char* http_version_1_0;
     static const char* http_version_1_1;

     static const char* http_method_connect;
     static const char* http_method_delete;
     static const char* http_method_head;
     static const char* http_method_get;
     static const char* http_method_options;
     static const char* http_method_post;
     static const char* http_method_put;
     static const char* http_method_trace;
     static const char* http_method_patch;

     static const char* http_post_encoding_form_urlencoded;
     static const char* http_post_encoding_multipart_formdata;

     static const char* application_octet_stream;
     static const char* text_plain;

     static const char* upload_filename_template;
     static const char path_separator;

     static std::vector<std::string> tokenize_url(const std::string&, const char separator = '/');
     static std::string standardize_url(const std::string&);

     static const std::string generate_random_upload_filename(const std::string& directory);
};

#define COMPARATOR(x, y, op) { \
    size_t l1 = (x).size(); \
    size_t l2 = (y).size(); \
    if (l1 < l2) return true; \
    if (l1 > l2) return false; \
    \
    for (size_t n = 0; n < l1; n++) { \
        int xc = op((x)[n]); \
        int yc = op((y)[n]); \
        if (xc < yc) return true; \
        if (xc > yc) return false; \
    } \
    return false; \
}

class header_comparator {
 public:
     /**
      * Operator used to compare strings.
      * @param first string
      * @param second string
     **/
     bool operator()(std::string_view x, std::string_view y) const {
         COMPARATOR(x, y, std::toupper);
     }
     bool operator()(const std::string& x, const std::string& y) const {
         COMPARATOR(x, y, std::toupper);
     }
};

/**
 * Operator Class that is used to compare two strings. The comparison can be sensitive or insensitive.
 * The default comparison is case sensitive. To obtain insensitive comparison you have to pass in
 * compilation phase the flag CASE_INSENSITIVE to the preprocessor.
**/
class arg_comparator {
 public:
     using is_transparent = std::true_type;
     /**
      * Operator used to compare strings.
      * @param first string
      * @param second string
     **/
     bool operator()(std::string_view x, std::string_view y) const {
#ifdef CASE_INSENSITIVE
         COMPARATOR(x, y, std::toupper);
#else
         COMPARATOR(x, y,);  // NOLINT(whitespace/comma)
#endif
     }
     bool operator()(const std::string& x, const std::string& y) const {
        return operator()(std::string_view(x), std::string_view(y));
     }
     bool operator()(std::string_view x, const std::string& y) const {
        return operator()(x, std::string_view(y));
     }
     bool operator()(const std::string& x, std::string_view y) const {
        return operator()(std::string_view(x), std::string(y));
     }
};

using header_map = std::map<std::string, std::string, http::header_comparator>;
using header_view_map = std::map<std::string_view, std::string_view, http::header_comparator>;
using arg_map = std::map<std::string, http_arg_value, http::arg_comparator>;
using arg_view_map = std::map<std::string_view, http_arg_value, http::arg_comparator>;


struct ip_representation {
    http_utils::IP_version_T ip_version;
    uint16_t pieces[16];
    uint16_t mask;

    explicit ip_representation(http_utils::IP_version_T ip_version) :
        ip_version(ip_version) {
            mask = DEFAULT_MASK_VALUE;
            std::fill(pieces, pieces + 16, 0);
    }

    explicit ip_representation(const std::string& ip);
    explicit ip_representation(const struct sockaddr* ip);

    bool operator<(const ip_representation& b) const;

    int weight() const {
        // variable-precision SWAR algorithm
        uint16_t x = mask;
        x = x - ((x >> 1) & 0x55555555);
        x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
        return (((x + (x >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
    }
};

/**
 * Method used to get an ip in form of string from a sockaddr structure
 * @param sa The sockaddr object to find the ip address from
 * @param maxlen Maxlen of the address (automatically discovered if not passed)
 * @return string containing the ip address
**/
std::string get_ip_str(const struct sockaddr *sa);

/**
 * Method used to get a port from a sockaddr
 * @param sa The sockaddr object to find the port from
 * @return short representing the port
**/
uint16_t get_port(const struct sockaddr* sa);

/**
 * Method to output the contents of a headers map to a std::ostream
 * @param os The ostream
 * @param prefix Prefix to identify the map
 * @param map
**/
void dump_header_map(std::ostream& os, const std::string& prefix, const http::header_view_map& map);

/**
 * Method to output the contents of an arguments map to a std::ostream
 * @param os The ostream
 * @param prefix Prefix to identify the map
 * @param map
**/
void dump_arg_map(std::ostream& os, const std::string& prefix, const http::arg_view_map& map);

/**
 * Process escape sequences ('+'=space, %HH) Updates val in place; the
 * result should be UTF-8 encoded and cannot be larger than the input.
 * The result must also still be 0-terminated.
 *
 * @param val the string to unescape
 * @return length of the resulting val (strlen(val) maybe
 *  shorter afterwards due to elimination of escape sequences)
 */
size_t http_unescape(std::string* val);

const std::string load_file(const std::string& filename);

size_t base_unescaper(std::string*, unescaper_ptr unescaper);

}  // namespace http
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_UTILS_HPP_

