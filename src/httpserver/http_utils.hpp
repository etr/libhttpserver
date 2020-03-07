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

#ifndef _HTTPUTILS_H_
#define _HTTPUTILS_H_

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif
#include <microhttpd.h>
#include <algorithm>
#include <cctype>
#include <exception>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#define DEFAULT_MASK_VALUE 0xFFFF

namespace httpserver {

typedef void(*unescaper_ptr)(std::string&);

namespace http {

class http_utils
{
    public:

    enum cred_type_T
    {
        NONE = -1
#ifdef HAVE_GNUTLS
        ,CERTIFICATE = GNUTLS_CRD_CERTIFICATE,
        ANON = GNUTLS_CRD_ANON,
        SRP = GNUTLS_CRD_SRP,
        PSK = GNUTLS_CRD_PSK,
        IA = GNUTLS_CRD_IA
#endif
    };

    enum start_method_T
    {
#if defined(__MINGW32__) || defined(__CYGWIN32__)
    #ifdef ENABLE_POLL
        INTERNAL_SELECT = MHD_USE_SELECT_INTERNALLY | MHD_USE_POLL,
    #else
        INTERNAL_SELECT = MHD_USE_SELECT_INTERNALLY,
    #endif
#else
    #ifdef ENABLE_EPOLL
        INTERNAL_SELECT = MHD_USE_SELECT_INTERNALLY | MHD_USE_EPOLL | MHD_USE_EPOLL_TURBO,
    #else
        INTERNAL_SELECT = MHD_USE_SELECT_INTERNALLY,
    #endif
#endif
#ifdef ENABLE_POLL
        THREAD_PER_CONNECTION = MHD_USE_THREAD_PER_CONNECTION | MHD_USE_POLL
#else
        THREAD_PER_CONNECTION = MHD_USE_THREAD_PER_CONNECTION
#endif
    };

    enum policy_T
    {
        ACCEPT,
        REJECT
    };

    enum IP_version_T
    {
        IPV4 = 4, IPV6 = 16
    };

    static const short http_method_connect_code;
    static const short http_method_delete_code;
    static const short http_method_get_code;
    static const short http_method_head_code;
    static const short http_method_options_code;
    static const short http_method_post_code;
    static const short http_method_put_code;
    static const short http_method_trace_code;
    static const short http_method_patch_code;
    static const short http_method_unknown_code;

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
    static const int http_unordered_collection;
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

    /* See also: http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html */
    static const std::string http_header_accept;
    static const std::string http_header_accept_charset;
    static const std::string http_header_accept_encoding;
    static const std::string http_header_accept_language;
    static const std::string http_header_accept_ranges;
    static const std::string http_header_age;
    static const std::string http_header_allow;
    static const std::string http_header_authorization;
    static const std::string http_header_cache_control;
    static const std::string http_header_connection;
    static const std::string http_header_content_encoding;
    static const std::string http_header_content_language;
    static const std::string http_header_content_length;
    static const std::string http_header_content_location;
    static const std::string http_header_content_md5;
    static const std::string http_header_content_range;
    static const std::string http_header_content_type;
    static const std::string http_header_date;
    static const std::string http_header_etag;
    static const std::string http_header_expect;
    static const std::string http_header_expires;
    static const std::string http_header_from;
    static const std::string http_header_host;
    static const std::string http_header_if_match;
    static const std::string http_header_if_modified_since;
    static const std::string http_header_if_none_match;
    static const std::string http_header_if_range;
    static const std::string http_header_if_unmodified_since;
    static const std::string http_header_last_modified;
    static const std::string http_header_location;
    static const std::string http_header_max_forwards;
    static const std::string http_header_pragma;
    static const std::string http_header_proxy_authenticate;
    static const std::string http_header_proxy_authentication;
    static const std::string http_header_range;
    static const std::string http_header_referer;
    static const std::string http_header_retry_after;
    static const std::string http_header_server;
    static const std::string http_header_te;
    static const std::string http_header_trailer;
    static const std::string http_header_transfer_encoding;
    static const std::string http_header_upgrade;
    static const std::string http_header_user_agent;
    static const std::string http_header_vary;
    static const std::string http_header_via;
    static const std::string http_header_warning;
    static const std::string http_header_www_authenticate;

    static const std::string http_version_1_0;
    static const std::string http_version_1_1;

    static const std::string http_method_connect;
    static const std::string http_method_delete;
    static const std::string http_method_head;
    static const std::string http_method_get;
    static const std::string http_method_options;
    static const std::string http_method_post;
    static const std::string http_method_put;
    static const std::string http_method_trace;
    static const std::string http_method_patch;

    static const std::string http_post_encoding_form_urlencoded;
    static const std::string http_post_encoding_multipart_formdata;

    static const std::string text_plain;

    static std::vector<std::string> tokenize_url(const std::string&,
        const char separator = '/'
    );
    static std::string standardize_url(const std::string&);
};

#define COMPARATOR(x, y, op) \
    { \
        size_t l1 = (x).size();\
        size_t l2 = (y).size();\
        if (l1 < l2) return true;\
        if (l1 > l2) return false;\
        \
        for (size_t n = 0; n < l1; n++)\
        {\
            int xc = op((x)[n]);\
            int yc = op((y)[n]);\
            if (xc < yc) return true;\
            if (xc > yc) return false;\
        }\
        return false;\
    }

class header_comparator {
    public:
        /**
         * Operator used to compare strings.
         * @param first string
         * @param second string
        **/
        bool operator()(const std::string& x,const std::string& y) const
        {
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
        /**
         * Operator used to compare strings.
         * @param first string
         * @param second string
        **/
        bool operator()(const std::string& x,const std::string& y) const
        {
#ifdef CASE_INSENSITIVE
            COMPARATOR(x, y, std::toupper);
#else
            COMPARATOR(x, y, );
#endif
        }
};

struct ip_representation
{
    http_utils::IP_version_T ip_version;
    unsigned short pieces[16];
    unsigned short mask;

    ip_representation(http_utils::IP_version_T ip_version) :
        ip_version(ip_version)
    {
        mask = DEFAULT_MASK_VALUE;
        std::fill(pieces, pieces + 16, 0);
    }

    ip_representation(const std::string& ip);
    ip_representation(const struct sockaddr* ip);

    bool operator <(const ip_representation& b) const;
    int weight() const
    {
        //variable-precision SWAR algorithm
        unsigned short x = mask;
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
std::string get_ip_str(const struct sockaddr *sa, socklen_t maxlen = 0);

std::string get_ip_str_new(const struct sockaddr* sa, socklen_t maxlen = 0);
/**
 * Method used to get a port from a sockaddr
 * @param sa The sockaddr object to find the port from
 * @return short representing the port
**/
unsigned short get_port(const struct sockaddr* sa);

/**
 * Method to output the contents of a headers map to a std::ostream
 * @param os The ostream
 * @param prefix Prefix to identify the map
 * @param map
**/
void dump_header_map(std::ostream &os, const std::string &prefix,
                     const std::map<std::string,std::string,header_comparator> &map);

/**
 * Method to output the contents of an arguments map to a std::ostream
 * @param os The ostream
 * @param prefix Prefix to identify the map
 * @param map
**/
void dump_arg_map(std::ostream &os, const std::string &prefix,
                     const std::map<std::string,std::string,arg_comparator> &map);

/**
 * Process escape sequences ('+'=space, %HH) Updates val in place; the
 * result should be UTF-8 encoded and cannot be larger than the input.
 * The result must also still be 0-terminated.
 *
 * @param val the string to unescape
 * @return length of the resulting val (strlen(val) maybe
 *  shorter afterwards due to elimination of escape sequences)
 */
size_t http_unescape (std::string& val);

const std::string load_file (const std::string& filename);

size_t base_unescaper(std::string&, unescaper_ptr unescaper);

};
};
#endif

