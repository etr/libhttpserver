/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__MINGW32__) || defined(__CYGWIN32__)
#define _WINDOWS
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x600
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#endif
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iostream>
#include "string_utilities.hpp"
#include "http_utils.hpp"

#pragma GCC diagnostic ignored "-Warray-bounds"
#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))
#define SET_BIT(var,pos) ((var) |= 1 << (pos))
#define CLEAR_BIT(var,pos) ((var) &= ~(1<<(pos)))

using namespace std;

namespace httpserver {
namespace http {

/* See also: http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html */

const int http_utils::http_continue = MHD_HTTP_CONTINUE;
const int http_utils::http_switching_protocol = MHD_HTTP_SWITCHING_PROTOCOLS;
const int http_utils::http_processing = MHD_HTTP_PROCESSING;

const int http_utils::http_ok = MHD_HTTP_OK;
const int http_utils::http_created = MHD_HTTP_CREATED;
const int http_utils::http_accepted = MHD_HTTP_ACCEPTED;
const int http_utils::http_non_authoritative_information =
    MHD_HTTP_NON_AUTHORITATIVE_INFORMATION;
const int http_utils::http_no_content = MHD_HTTP_NO_CONTENT;
const int http_utils::http_reset_content = MHD_HTTP_RESET_CONTENT;
const int http_utils::http_partial_content = MHD_HTTP_PARTIAL_CONTENT;
const int http_utils::http_multi_status = MHD_HTTP_MULTI_STATUS;

const int http_utils::http_multiple_choices = MHD_HTTP_MULTIPLE_CHOICES;
const int http_utils::http_moved_permanently = MHD_HTTP_MOVED_PERMANENTLY;
const int http_utils::http_found = MHD_HTTP_FOUND;
const int http_utils::http_see_other = MHD_HTTP_SEE_OTHER;
const int http_utils::http_not_modified = MHD_HTTP_NOT_MODIFIED;
const int http_utils::http_use_proxy = MHD_HTTP_USE_PROXY;
const int http_utils::http_switch_proxy = MHD_HTTP_SWITCH_PROXY;
const int http_utils::http_temporary_redirect = MHD_HTTP_TEMPORARY_REDIRECT;

const int http_utils::http_bad_request = MHD_HTTP_BAD_REQUEST;
const int http_utils::http_unauthorized = MHD_HTTP_UNAUTHORIZED;
const int http_utils::http_payment_required = MHD_HTTP_PAYMENT_REQUIRED;
const int http_utils::http_forbidden = MHD_HTTP_FORBIDDEN;
const int http_utils::http_not_found = MHD_HTTP_NOT_FOUND;
const int http_utils::http_method_not_allowed = MHD_HTTP_METHOD_NOT_ALLOWED;
const int http_utils::http_method_not_acceptable = MHD_HTTP_NOT_ACCEPTABLE;
const int http_utils::http_proxy_authentication_required =
    MHD_HTTP_PROXY_AUTHENTICATION_REQUIRED;
const int http_utils::http_request_timeout = MHD_HTTP_REQUEST_TIMEOUT;
const int http_utils::http_conflict = MHD_HTTP_CONFLICT;
const int http_utils::http_gone = MHD_HTTP_GONE;
const int http_utils::http_length_required = MHD_HTTP_LENGTH_REQUIRED;
const int http_utils::http_precondition_failed = MHD_HTTP_PRECONDITION_FAILED;
const int http_utils::http_request_entity_too_large =
    MHD_HTTP_REQUEST_ENTITY_TOO_LARGE;
const int http_utils::http_request_uri_too_long = MHD_HTTP_REQUEST_URI_TOO_LONG;
const int http_utils::http_unsupported_media_type =
    MHD_HTTP_UNSUPPORTED_MEDIA_TYPE;
const int http_utils::http_requested_range_not_satisfiable =
    MHD_HTTP_REQUESTED_RANGE_NOT_SATISFIABLE;
const int http_utils::http_expectation_failed = MHD_HTTP_EXPECTATION_FAILED;
const int http_utils::http_unprocessable_entity = MHD_HTTP_UNPROCESSABLE_ENTITY;
const int http_utils::http_locked = MHD_HTTP_LOCKED;
const int http_utils::http_failed_dependency = MHD_HTTP_FAILED_DEPENDENCY;
const int http_utils::http_unordered_collection = MHD_HTTP_UNORDERED_COLLECTION;
const int http_utils::http_upgrade_required = MHD_HTTP_UPGRADE_REQUIRED;
const int http_utils::http_retry_with = MHD_HTTP_RETRY_WITH;

const int http_utils::http_internal_server_error =
    MHD_HTTP_INTERNAL_SERVER_ERROR;
const int http_utils::http_not_implemented = MHD_HTTP_NOT_IMPLEMENTED;
const int http_utils::http_bad_gateway = MHD_HTTP_BAD_GATEWAY;
const int http_utils::http_service_unavailable = MHD_HTTP_SERVICE_UNAVAILABLE;
const int http_utils::http_gateway_timeout = MHD_HTTP_GATEWAY_TIMEOUT;
const int http_utils::http_version_not_supported =
    MHD_HTTP_HTTP_VERSION_NOT_SUPPORTED;
const int http_utils::http_variant_also_negotiated =
    MHD_HTTP_VARIANT_ALSO_NEGOTIATES;
const int http_utils::http_insufficient_storage = MHD_HTTP_INSUFFICIENT_STORAGE;
const int http_utils::http_bandwidth_limit_exceeded =
    MHD_HTTP_BANDWIDTH_LIMIT_EXCEEDED;
const int http_utils::http_not_extended = MHD_HTTP_NOT_EXTENDED;

const int http_utils::shoutcast_response = MHD_ICY_FLAG;

const std::string http_utils::http_header_accept = MHD_HTTP_HEADER_ACCEPT;
const std::string http_utils::http_header_accept_charset =
    MHD_HTTP_HEADER_ACCEPT_CHARSET;
const std::string http_utils::http_header_accept_encoding =
    MHD_HTTP_HEADER_ACCEPT_ENCODING;
const std::string http_utils::http_header_accept_language =
    MHD_HTTP_HEADER_ACCEPT_LANGUAGE;
const std::string http_utils::http_header_accept_ranges =
    MHD_HTTP_HEADER_ACCEPT_RANGES;
const std::string http_utils::http_header_age = MHD_HTTP_HEADER_AGE;
const std::string http_utils::http_header_allow = MHD_HTTP_HEADER_ALLOW;
const std::string http_utils::http_header_authorization =
    MHD_HTTP_HEADER_AUTHORIZATION;
const std::string http_utils::http_header_cache_control =
    MHD_HTTP_HEADER_CACHE_CONTROL;
const std::string http_utils::http_header_connection =
    MHD_HTTP_HEADER_CONNECTION;
const std::string http_utils::http_header_content_encoding =
    MHD_HTTP_HEADER_CONTENT_ENCODING;
const std::string http_utils::http_header_content_language =
    MHD_HTTP_HEADER_CONTENT_LANGUAGE;
const std::string http_utils::http_header_content_length =
    MHD_HTTP_HEADER_CONTENT_LENGTH;
const std::string http_utils::http_header_content_location =
    MHD_HTTP_HEADER_CONTENT_LOCATION;
const std::string http_utils::http_header_content_md5 =
    MHD_HTTP_HEADER_CONTENT_MD5;
const std::string http_utils::http_header_content_range =
    MHD_HTTP_HEADER_CONTENT_RANGE;
const std::string http_utils::http_header_content_type =
    MHD_HTTP_HEADER_CONTENT_TYPE;
const std::string http_utils::http_header_date = MHD_HTTP_HEADER_DATE;
const std::string http_utils::http_header_etag = MHD_HTTP_HEADER_ETAG;
const std::string http_utils::http_header_expect = MHD_HTTP_HEADER_EXPECT;
const std::string http_utils::http_header_expires = MHD_HTTP_HEADER_EXPIRES;
const std::string http_utils::http_header_from = MHD_HTTP_HEADER_FROM;
const std::string http_utils::http_header_host = MHD_HTTP_HEADER_HOST;
const std::string http_utils::http_header_if_match = MHD_HTTP_HEADER_IF_MATCH;
const std::string http_utils::http_header_if_modified_since =
    MHD_HTTP_HEADER_IF_MODIFIED_SINCE;
const std::string http_utils::http_header_if_none_match =
    MHD_HTTP_HEADER_IF_NONE_MATCH;
const std::string http_utils::http_header_if_range = MHD_HTTP_HEADER_IF_RANGE;
const std::string http_utils::http_header_if_unmodified_since =
    MHD_HTTP_HEADER_IF_UNMODIFIED_SINCE;
const std::string http_utils::http_header_last_modified =
    MHD_HTTP_HEADER_LAST_MODIFIED;
const std::string http_utils::http_header_location = MHD_HTTP_HEADER_LOCATION;
const std::string http_utils::http_header_max_forwards =
    MHD_HTTP_HEADER_MAX_FORWARDS;
const std::string http_utils::http_header_pragma = MHD_HTTP_HEADER_PRAGMA;
const std::string http_utils::http_header_proxy_authenticate =
    MHD_HTTP_HEADER_PROXY_AUTHENTICATE;
const std::string http_utils::http_header_proxy_authentication =
    MHD_HTTP_HEADER_PROXY_AUTHORIZATION;
const std::string http_utils::http_header_range = MHD_HTTP_HEADER_RANGE;
const std::string http_utils::http_header_referer = MHD_HTTP_HEADER_REFERER;
const std::string http_utils::http_header_retry_after =
    MHD_HTTP_HEADER_RETRY_AFTER;
const std::string http_utils::http_header_server = MHD_HTTP_HEADER_SERVER;
const std::string http_utils::http_header_te = MHD_HTTP_HEADER_TE;
const std::string http_utils::http_header_trailer = MHD_HTTP_HEADER_TRAILER;
const std::string http_utils::http_header_transfer_encoding =
    MHD_HTTP_HEADER_TRANSFER_ENCODING;
const std::string http_utils::http_header_upgrade = MHD_HTTP_HEADER_UPGRADE;
const std::string http_utils::http_header_user_agent =
    MHD_HTTP_HEADER_USER_AGENT;
const std::string http_utils::http_header_vary = MHD_HTTP_HEADER_VARY;
const std::string http_utils::http_header_via = MHD_HTTP_HEADER_VIA;
const std::string http_utils::http_header_warning = MHD_HTTP_HEADER_WARNING;
const std::string http_utils::http_header_www_authenticate =
    MHD_HTTP_HEADER_WWW_AUTHENTICATE;

const std::string http_utils::http_version_1_0 = MHD_HTTP_VERSION_1_0;
const std::string http_utils::http_version_1_1 = MHD_HTTP_VERSION_1_1;

const std::string http_utils::http_method_connect = MHD_HTTP_METHOD_CONNECT;
const std::string http_utils::http_method_delete = MHD_HTTP_METHOD_DELETE;
const std::string http_utils::http_method_get = MHD_HTTP_METHOD_GET;
const std::string http_utils::http_method_head = MHD_HTTP_METHOD_HEAD;
const std::string http_utils::http_method_options = MHD_HTTP_METHOD_OPTIONS;
const std::string http_utils::http_method_post = MHD_HTTP_METHOD_POST;
const std::string http_utils::http_method_put = MHD_HTTP_METHOD_PUT;
const std::string http_utils::http_method_trace = MHD_HTTP_METHOD_TRACE;

const std::string http_utils::http_post_encoding_form_urlencoded =
    MHD_HTTP_POST_ENCODING_FORM_URLENCODED;
const std::string http_utils::http_post_encoding_multipart_formdata =
    MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA;


size_t http_utils::tokenize_url(
        const std::string& str,
        std::vector<std::string>& result,
        const char separator
)
{
    string_utilities::string_split(str, result, separator);
    return result.size();
}

void http_utils::standardize_url(const std::string& url, std::string& result)
{
    std::string n_url;
    string_utilities::regex_replace(url, "(\\/)+", "/", n_url);
    std::string::size_type n_url_length = n_url.length();

    if (n_url_length > 1 && n_url[n_url_length - 1] == '/')
    {
        result = n_url.substr(0, n_url_length - 1);
    }
    else
    {
        result = n_url;
    }
}

void get_ip_str(
    const struct sockaddr *sa,
    std::string& result,
    socklen_t maxlen
)
{
    if(sa)
    {
        char to_ret[NI_MAXHOST];
        getnameinfo(sa, sizeof (struct sockaddr), to_ret, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
        result = to_ret;
    }
}

std::string get_ip_str_new(
    const struct sockaddr* sa,
    socklen_t maxlen
)
{
    std::string to_ret;
    get_ip_str(sa, to_ret, maxlen);
    return to_ret;
}

short get_port(const struct sockaddr* sa)
{
    if(sa)
    {
        switch(sa->sa_family)
        {
            case AF_INET:
                return ((struct sockaddr_in *)sa)->sin_port;
            case AF_INET6:
                return ((struct sockaddr_in *)sa)->sin_port;
            default:
                return 0;
        }
    }
    return 0;
}

size_t http_unescape (char *val)
{
    char *rpos = val;
    char *wpos = val;
    unsigned int num;

    while ('\0' != *rpos)
    {
        switch (*rpos)
        {
            case '+':
                *wpos = ' ';
                wpos++;
                rpos++;
                break;
            case '%':
                if ( (1 == sscanf (&rpos[1],
                    "%2x", &num)) ||
                    (1 == sscanf (&rpos[1],
                    "%2X", &num))
                )
                {
                    *wpos = (unsigned char) num;
                    wpos++;
                    rpos += 3;
                    break;
                }
            /* intentional fall through! */
            default:
                *wpos = *rpos;
                wpos++;
                rpos++;
        }
    }
    *wpos = '\0'; /* add 0-terminator */
    return wpos - val; /* = strlen(val) */
}

ip_representation::ip_representation(const struct sockaddr* ip)
{
    std::fill(pieces, pieces + 16, 0);
    if(ip->sa_family == AF_INET)
    {
        ip_version = http_utils::IPV4;
        for(int i=0;i<4;i++)
        {
            pieces[12+i]=((u_char*)&(((struct sockaddr_in *)ip)->sin_addr))[i];
        }
    }
    else
    {
        ip_version = http_utils::IPV6;
        for(int i=0;i<32;i+=2)
        {
            pieces[i/2] =
                ((u_char*)&(((struct sockaddr_in6 *)ip)->sin6_addr))[i] +
                16 * ((u_char*)&(((struct sockaddr_in6 *)ip)->sin6_addr))[i+1];
        }
    }
    mask = DEFAULT_MASK_VALUE;
}

ip_representation::ip_representation(const std::string& ip)
{
    std::vector<std::string> parts;
    mask = DEFAULT_MASK_VALUE;
    std::fill(pieces, pieces + 16, 0);
    if(ip.find(':') != std::string::npos) //IPV6
    {
        ip_version = http_utils::IPV6;
        string_utilities::string_split(ip, parts, ':', false);
        int y = 0;
        for(unsigned int i = 0; i < parts.size(); i++)
        {
            if(parts[i] != "*" && parts[i] != "")
            {
                if(parts[i].size() < 4)
                {
                    stringstream ss;
                    ss << setfill('0') << setw(4) << parts[i];
                    parts[i] = ss.str();
                }
                if(parts[i].size() == 4)
                {
                    pieces[y] = strtol((parts[i].substr(0,2)).c_str(),NULL,16);
                    pieces[y+1] = strtol(
                            (parts[i].substr(2,2)).c_str(),
                            NULL,
                            16
                    );
                    y += 2;
                }
                else
                {
                    if(y != 12)
                    {
                        throw bad_ip_format_exception();
                    }
                    if(parts[i].find('.') != std::string::npos)
                    {
                        vector<string> subparts;
                        string_utilities::string_split(parts[i], subparts, '.');
                        if(subparts.size() == 4)
                        {
                            for(unsigned int ii = 0; ii < subparts.size(); ii++)
                            {
                                if(subparts[ii] != "*")
                                {
                                    pieces[y+ii] = strtol(
                                            subparts[ii].c_str(),
                                            NULL,
                                            10
                                    );
                                }
                                else
                                {
                                    CLEAR_BIT(mask, y+11);
                                }
                                y++;
                            }
                        }
                        else
                        {
                            throw bad_ip_format_exception();
                        }
                    }
                    else
                    {
                        throw bad_ip_format_exception();
                    }
                }
            }
            else if(parts[i] == "*")
            {
                CLEAR_BIT(mask, y);
                y++;
            }
            else
            {
                if(parts.size() <= 8)
                {
                    int covered_pieces = 1 + (8 - parts.size());
                    if(parts[parts.size() - 1].find('.') != std::string::npos)
                    {
                        covered_pieces -= 2;
                    }
                    for(int k = 0; k < covered_pieces; k++)
                    {
                        pieces[y] = 0;
                        y++;
                    }
                }
                else
                {
                    throw bad_ip_format_exception();
                }
            }
        }
    }
    else //IPV4
    {
        ip_version = http_utils::IPV4;
        string_utilities::string_split(ip, parts, '.');
        if(parts.size() == 4)
        {
            for(unsigned int i = 0; i < parts.size(); i++)
            {
                if(parts[i] != "*")
                {
                    pieces[12+i] = strtol(parts[i].c_str(), NULL, 10);
                }
                else
                {
                    CLEAR_BIT(mask, 12+i);
                }
            }
        }
        else
        {
            throw bad_ip_format_exception();
        }
    }
}

bool ip_representation::operator <(const ip_representation& b) const
{
    int VAL = 16;
    if(this->ip_version == http_utils::IPV4 && this->ip_version == b.ip_version)
    {
        VAL = this->ip_version;
    }
    for(int i = 16 - VAL; i < 16; i++)
    {
        if(CHECK_BIT(this->mask,i) &&
                CHECK_BIT(b.mask,i) &&
                this->pieces[i] < b.pieces[i]
        )
            return true;
    }
    return false;
}

size_t load_file (const char* filename, char** content)
{
    ifstream fp(filename, ios::in | ios::binary | ios::ate);
    if(fp.is_open())
    {
        int size = fp.tellg();
        *content = (char*) malloc(size * sizeof(char) + 1);
        fp.seekg(0, ios::beg);
        fp.read(*content, size);
        content[size] = '\0';
        fp.close();
        return size;
    }
    else
        throw file_access_exception();
}

char* load_file (const char *filename)
{
    char* content = NULL;
    load_file(filename, &content);
    return content;
}

void dump_header_map(std::ostream &os, const std::string &prefix,
                     const std::map<std::string,std::string,header_comparator> &map)
{
    std::map<std::string,std::string,header_comparator>::const_iterator it = map.begin();
    std::map<std::string,std::string,header_comparator>::const_iterator end = map.end();

    if (map.size()) {
        os << "    " << prefix << " [";
        for (; it != end; ++it) {
            os << (*it).first << ":\"" << (*it).second << "\" ";
        }
        os << "]" << std::endl;
    }
}

void dump_arg_map(std::ostream &os, const std::string &prefix,
                  const std::map<std::string,std::string,arg_comparator> &map)
{
    std::map<std::string,std::string,arg_comparator>::const_iterator it = map.begin();
    std::map<std::string,std::string,arg_comparator>::const_iterator end = map.end();

    if (map.size()) {
        os << "    " << prefix << " [";
        for (; it != end; ++it) {
            os << (*it).first << ":\"" << (*it).second << "\" ";
        }
        os << "]" << std::endl;
    }
}


};
};
