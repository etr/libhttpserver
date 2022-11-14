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

#include "httpserver/http_utils.hpp"

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <share.h>
#else  // WIN32 check
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif  // WIN32 check

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "httpserver/string_utilities.hpp"

#pragma GCC diagnostic ignored "-Warray-bounds"
#define CHECK_BIT(var, pos) ((var) & (1 << (pos)))
#define SET_BIT(var, pos) ((var) |= 1 << (pos))
#define CLEAR_BIT(var, pos) ((var) &= ~(1 << (pos)))

#if defined (__CYGWIN__)

#if !defined (NI_MAXHOST)
#define NI_MAXHOST 1025
#endif  // NI_MAXHOST

#ifndef __u_char_defined
typedef unsigned char u_char;
#define __u_char_defined
#endif  // __u_char_defined

#endif  // CYGWIN

// libmicrohttpd deprecated some definitions with v0.9.74, and introduced new ones
#if MHD_VERSION < 0x00097314
#define MHD_HTTP_CONTENT_TOO_LARGE      MHD_HTTP_PAYLOAD_TOO_LARGE
#define MHD_HTTP_UNPROCESSABLE_CONTENT  MHD_HTTP_UNPROCESSABLE_ENTITY
#endif

namespace httpserver {
namespace http {

// See also: http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html

const int http_utils::http_continue = MHD_HTTP_CONTINUE;
const int http_utils::http_switching_protocol = MHD_HTTP_SWITCHING_PROTOCOLS;
const int http_utils::http_processing = MHD_HTTP_PROCESSING;

const int http_utils::http_ok = MHD_HTTP_OK;
const int http_utils::http_created = MHD_HTTP_CREATED;
const int http_utils::http_accepted = MHD_HTTP_ACCEPTED;
const int http_utils::http_non_authoritative_information = MHD_HTTP_NON_AUTHORITATIVE_INFORMATION;
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
const int http_utils::http_proxy_authentication_required = MHD_HTTP_PROXY_AUTHENTICATION_REQUIRED;
const int http_utils::http_request_timeout = MHD_HTTP_REQUEST_TIMEOUT;
const int http_utils::http_conflict = MHD_HTTP_CONFLICT;
const int http_utils::http_gone = MHD_HTTP_GONE;
const int http_utils::http_length_required = MHD_HTTP_LENGTH_REQUIRED;
const int http_utils::http_precondition_failed = MHD_HTTP_PRECONDITION_FAILED;
const int http_utils::http_request_entity_too_large = MHD_HTTP_CONTENT_TOO_LARGE;
const int http_utils::http_request_uri_too_long = MHD_HTTP_URI_TOO_LONG;
const int http_utils::http_unsupported_media_type = MHD_HTTP_UNSUPPORTED_MEDIA_TYPE;
const int http_utils::http_requested_range_not_satisfiable = MHD_HTTP_RANGE_NOT_SATISFIABLE;
const int http_utils::http_expectation_failed = MHD_HTTP_EXPECTATION_FAILED;
const int http_utils::http_unprocessable_entity = MHD_HTTP_UNPROCESSABLE_CONTENT;
const int http_utils::http_locked = MHD_HTTP_LOCKED;
const int http_utils::http_failed_dependency = MHD_HTTP_FAILED_DEPENDENCY;
const int http_utils::http_upgrade_required = MHD_HTTP_UPGRADE_REQUIRED;
const int http_utils::http_retry_with = MHD_HTTP_RETRY_WITH;

const int http_utils::http_internal_server_error = MHD_HTTP_INTERNAL_SERVER_ERROR;
const int http_utils::http_not_implemented = MHD_HTTP_NOT_IMPLEMENTED;
const int http_utils::http_bad_gateway = MHD_HTTP_BAD_GATEWAY;
const int http_utils::http_service_unavailable = MHD_HTTP_SERVICE_UNAVAILABLE;
const int http_utils::http_gateway_timeout = MHD_HTTP_GATEWAY_TIMEOUT;
const int http_utils::http_version_not_supported = MHD_HTTP_HTTP_VERSION_NOT_SUPPORTED;
const int http_utils::http_variant_also_negotiated = MHD_HTTP_VARIANT_ALSO_NEGOTIATES;
const int http_utils::http_insufficient_storage = MHD_HTTP_INSUFFICIENT_STORAGE;
const int http_utils::http_bandwidth_limit_exceeded = MHD_HTTP_BANDWIDTH_LIMIT_EXCEEDED;
const int http_utils::http_not_extended = MHD_HTTP_NOT_EXTENDED;

const int http_utils::shoutcast_response = MHD_ICY_FLAG;

const char* http_utils::http_header_accept = MHD_HTTP_HEADER_ACCEPT;
const char* http_utils::http_header_accept_charset = MHD_HTTP_HEADER_ACCEPT_CHARSET;
const char* http_utils::http_header_accept_encoding = MHD_HTTP_HEADER_ACCEPT_ENCODING;
const char* http_utils::http_header_accept_language = MHD_HTTP_HEADER_ACCEPT_LANGUAGE;
const char* http_utils::http_header_accept_ranges = MHD_HTTP_HEADER_ACCEPT_RANGES;
const char* http_utils::http_header_age = MHD_HTTP_HEADER_AGE;
const char* http_utils::http_header_allow = MHD_HTTP_HEADER_ALLOW;
const char* http_utils::http_header_authorization = MHD_HTTP_HEADER_AUTHORIZATION;
const char* http_utils::http_header_cache_control = MHD_HTTP_HEADER_CACHE_CONTROL;
const char* http_utils::http_header_connection = MHD_HTTP_HEADER_CONNECTION;
const char* http_utils::http_header_content_encoding = MHD_HTTP_HEADER_CONTENT_ENCODING;
const char* http_utils::http_header_content_language = MHD_HTTP_HEADER_CONTENT_LANGUAGE;
const char* http_utils::http_header_content_length = MHD_HTTP_HEADER_CONTENT_LENGTH;
const char* http_utils::http_header_content_location = MHD_HTTP_HEADER_CONTENT_LOCATION;
const char* http_utils::http_header_content_md5 = MHD_HTTP_HEADER_CONTENT_MD5;
const char* http_utils::http_header_content_range = MHD_HTTP_HEADER_CONTENT_RANGE;
const char* http_utils::http_header_content_type = MHD_HTTP_HEADER_CONTENT_TYPE;
const char* http_utils::http_header_date = MHD_HTTP_HEADER_DATE;
const char* http_utils::http_header_etag = MHD_HTTP_HEADER_ETAG;
const char* http_utils::http_header_expect = MHD_HTTP_HEADER_EXPECT;
const char* http_utils::http_header_expires = MHD_HTTP_HEADER_EXPIRES;
const char* http_utils::http_header_from = MHD_HTTP_HEADER_FROM;
const char* http_utils::http_header_host = MHD_HTTP_HEADER_HOST;
const char* http_utils::http_header_if_match = MHD_HTTP_HEADER_IF_MATCH;
const char* http_utils::http_header_if_modified_since = MHD_HTTP_HEADER_IF_MODIFIED_SINCE;
const char* http_utils::http_header_if_none_match = MHD_HTTP_HEADER_IF_NONE_MATCH;
const char* http_utils::http_header_if_range = MHD_HTTP_HEADER_IF_RANGE;
const char* http_utils::http_header_if_unmodified_since = MHD_HTTP_HEADER_IF_UNMODIFIED_SINCE;
const char* http_utils::http_header_last_modified = MHD_HTTP_HEADER_LAST_MODIFIED;
const char* http_utils::http_header_location = MHD_HTTP_HEADER_LOCATION;
const char* http_utils::http_header_max_forwards = MHD_HTTP_HEADER_MAX_FORWARDS;
const char* http_utils::http_header_pragma = MHD_HTTP_HEADER_PRAGMA;
const char* http_utils::http_header_proxy_authenticate = MHD_HTTP_HEADER_PROXY_AUTHENTICATE;
const char* http_utils::http_header_proxy_authentication = MHD_HTTP_HEADER_PROXY_AUTHORIZATION;
const char* http_utils::http_header_range = MHD_HTTP_HEADER_RANGE;
const char* http_utils::http_header_referer = MHD_HTTP_HEADER_REFERER;
const char* http_utils::http_header_retry_after = MHD_HTTP_HEADER_RETRY_AFTER;
const char* http_utils::http_header_server = MHD_HTTP_HEADER_SERVER;
const char* http_utils::http_header_te = MHD_HTTP_HEADER_TE;
const char* http_utils::http_header_trailer = MHD_HTTP_HEADER_TRAILER;
const char* http_utils::http_header_transfer_encoding = MHD_HTTP_HEADER_TRANSFER_ENCODING;
const char* http_utils::http_header_upgrade = MHD_HTTP_HEADER_UPGRADE;
const char* http_utils::http_header_user_agent = MHD_HTTP_HEADER_USER_AGENT;
const char* http_utils::http_header_vary = MHD_HTTP_HEADER_VARY;
const char* http_utils::http_header_via = MHD_HTTP_HEADER_VIA;
const char* http_utils::http_header_warning = MHD_HTTP_HEADER_WARNING;
const char* http_utils::http_header_www_authenticate = MHD_HTTP_HEADER_WWW_AUTHENTICATE;

const char* http_utils::http_version_1_0 = MHD_HTTP_VERSION_1_0;
const char* http_utils::http_version_1_1 = MHD_HTTP_VERSION_1_1;

const char* http_utils::http_method_connect = MHD_HTTP_METHOD_CONNECT;
const char* http_utils::http_method_delete = MHD_HTTP_METHOD_DELETE;
const char* http_utils::http_method_get = MHD_HTTP_METHOD_GET;
const char* http_utils::http_method_head = MHD_HTTP_METHOD_HEAD;
const char* http_utils::http_method_options = MHD_HTTP_METHOD_OPTIONS;
const char* http_utils::http_method_post = MHD_HTTP_METHOD_POST;
const char* http_utils::http_method_put = MHD_HTTP_METHOD_PUT;
const char* http_utils::http_method_trace = MHD_HTTP_METHOD_TRACE;
const char* http_utils::http_method_patch = MHD_HTTP_METHOD_PATCH;

const char* http_utils::http_post_encoding_form_urlencoded = MHD_HTTP_POST_ENCODING_FORM_URLENCODED;
const char* http_utils::http_post_encoding_multipart_formdata = MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA;

const char* http_utils::application_octet_stream = "application/octet-stream";
const char* http_utils::text_plain = "text/plain";

const char* http_utils::upload_filename_template = "libhttpserver.XXXXXX";

#if defined(_WIN32)
    const char http_utils::path_separator = '\\';
#else  // _WIN32
    const char http_utils::path_separator = '/';
#endif  // _WIN32

std::vector<std::string> http_utils::tokenize_url(const std::string& str, const char separator) {
    return string_utilities::string_split(str, separator);
}

std::string http_utils::standardize_url(const std::string& url) {
    std::string n_url = url;

    std::string::iterator new_end = std::unique(n_url.begin(), n_url.end(), [](char a, char b) { return (a == b) && (a == '/'); });
    n_url.erase(new_end, n_url.end());

    std::string::size_type n_url_length = n_url.length();

    std::string result;

    if (n_url_length > 1 && n_url[n_url_length - 1] == '/') {
        result = n_url.substr(0, n_url_length - 1);
    } else {
        result = n_url;
    }

    return result;
}

const std::string http_utils::generate_random_upload_filename(const std::string& directory) {
    std::string filename = directory + http_utils::path_separator + http_utils::upload_filename_template;
    char *template_filename = strdup(filename.c_str());
    int fd = 0;

#if defined(_WIN32)
    // only function for win32 which creates unique filenames and can handle a given template including a path
    // all other functions like tmpnam() always create filenames in the 'temp' directory
    if (0 != _mktemp_s(template_filename, filename.size() + 1)) {
        free(template_filename);
        throw generateFilenameException("Failed to create unique filename");
    }

    // as no existing file should be overwritten the operation should fail if the file already exists
    // fstream or ofstream classes don't feature such an option
    // with the function _sopen_s this can be achieved by setting the flag _O_EXCL
    if (0 != _sopen_s(&fd, template_filename, _O_CREAT | _O_EXCL | _O_NOINHERIT, _SH_DENYNO, _S_IREAD | _S_IWRITE)) {
        free(template_filename);
        throw generateFilenameException("Failed to create file");
    }
    if (fd == -1) {
        free(template_filename);
        throw generateFilenameException("File descriptor after successful _sopen_s is -1");
    }
    _close(fd);
#else  // _WIN32
    fd = mkstemp(template_filename);

    if (fd == -1) {
        free(template_filename);
        throw generateFilenameException("Failed to create unique file");
    }
    close(fd);
#endif  // _WIN32
    std::string ret_filename = template_filename;
    free(template_filename);
    return ret_filename;
}

std::string get_ip_str(const struct sockaddr *sa) {
    if (!sa) throw std::invalid_argument("socket pointer is null");

    char to_ret[NI_MAXHOST];
    if (AF_INET6 == sa->sa_family) {
        inet_ntop(AF_INET6, &((reinterpret_cast<const sockaddr_in6*>(sa))->sin6_addr), to_ret, INET6_ADDRSTRLEN);
        return to_ret;
    } else if (AF_INET == sa->sa_family) {
        inet_ntop(AF_INET, &((reinterpret_cast<const sockaddr_in*>(sa))->sin_addr), to_ret, INET_ADDRSTRLEN);
        return to_ret;
    } else {
        throw std::invalid_argument("IP family must be either AF_INET or AF_INET6");
    }
}

uint16_t get_port(const struct sockaddr* sa) {
    if (!sa) throw std::invalid_argument("socket pointer is null");

    if (sa->sa_family == AF_INET) {
        return (reinterpret_cast<const struct sockaddr_in*>(sa))->sin_port;
    } else if (sa->sa_family == AF_INET6) {
        return (reinterpret_cast<const struct sockaddr_in6*>(sa))->sin6_port;
    } else {
        throw std::invalid_argument("IP family must be either AF_INET or AF_INET6");
    }
}

size_t http_unescape(std::string* val) {
    if (val->empty()) return 0;

    unsigned int rpos = 0;
    unsigned int wpos = 0;

    unsigned int num;
    unsigned int size = val->size();

    while (rpos < size && (*val)[rpos] != '\0') {
        switch ((*val)[rpos]) {
            case '+':
                (*val)[wpos] = ' ';
                wpos++;
                rpos++;
                break;
            case '%':
                if (size > rpos + 2 && ((1 == sscanf(val->substr(rpos + 1, 2).c_str(), "%2x", &num)) || (1 == sscanf(val->substr(rpos + 1, 2).c_str(), "%2X", &num)))) {
                    (*val)[wpos] = (unsigned char) num;
                    wpos++;
                    rpos += 3;
                    break;
                }
            // intentional fall through!
            default:
                (*val)[wpos] = (*val)[rpos];
                wpos++;
                rpos++;
        }
    }
    (*val)[wpos] = '\0';  // add 0-terminator
    val->resize(wpos);
    return wpos;  // = strlen(val)
}

ip_representation::ip_representation(const struct sockaddr* ip) {
    std::fill(pieces, pieces + 16, 0);
    if (ip->sa_family == AF_INET) {
        ip_version = http_utils::IPV4;
        const in_addr* sin_addr_pt = &((reinterpret_cast<const struct sockaddr_in*>(ip))->sin_addr);
        for (int i = 0; i < 4; i++) {
            pieces[12 + i] = (reinterpret_cast<const u_char*>(sin_addr_pt))[i];
        }
    } else {
        ip_version = http_utils::IPV6;
        const in6_addr* sin_addr6_pt = &((reinterpret_cast<const struct sockaddr_in6*>(ip))->sin6_addr);
        for (int i = 0; i < 16; i++) {
            pieces[i] = (reinterpret_cast<const u_char*>(sin_addr6_pt))[i];
        }
    }
    mask = DEFAULT_MASK_VALUE;
}

ip_representation::ip_representation(const std::string& ip) {
    std::vector<std::string> parts;
    mask = DEFAULT_MASK_VALUE;
    std::fill(pieces, pieces + 16, 0);
    if (ip.find(':') != std::string::npos) {  // IPV6
        ip_version = http_utils::IPV6;
        parts = string_utilities::string_split(ip, ':', false);
        if (parts.size() > 8) {
            throw std::invalid_argument("IP is badly formatted. Max 8 parts in IPV6.");
        }

        unsigned int omitted = 8 - (parts.size() - 1);
        if (omitted != 0) {
            int empty_count = 0;
            for (unsigned int i = 0; i < parts.size(); i++) {
                if (parts[i].size() == 0) empty_count++;
            }

            if (empty_count > 1) {
                if (parts[parts.size() - 1].find(".") != std::string::npos) omitted -= 1;

                if (empty_count == 2 && parts[0] == "" && parts[1] == "") {
                    omitted += 1;
                    parts = std::vector<std::string>(parts.begin() + 1, parts.end());
                } else {
                    throw std::invalid_argument("IP is badly formatted. Cannot have more than one omitted segment in IPV6.");
                }
            }
        }

        int y = 0;
        for (unsigned int i = 0; i < parts.size(); i++) {
            if (parts[i] != "*") {
                if (parts[i].size() == 0) {
                    for (unsigned int omitted_idx = 0; omitted_idx < omitted; omitted_idx++) {
                        pieces[y] = 0;
                        pieces[y+1] = 0;
                        y += 2;
                    }

                    continue;
                }

                if (parts[i].size() < 4) {
                    std::stringstream ss;
                    ss << std::setfill('0') << std::setw(4) << parts[i];
                    parts[i] = ss.str();
                }

                if (parts[i].size() == 4) {
                    pieces[y] = strtol((parts[i].substr(0, 2)).c_str(), nullptr, 16);
                    pieces[y+1] = strtol((parts[i].substr(2, 2)).c_str(), nullptr, 16);

                    y += 2;
                } else {
                    if (parts[i].find('.') != std::string::npos) {
                        if (y != 12) {
                            throw std::invalid_argument("IP is badly formatted. Missing parts before nested IPV4.");
                        }

                        if (i != parts.size() - 1) {
                            throw std::invalid_argument("IP is badly formatted. Nested IPV4 should be at the end");
                        }

                        std::vector<std::string> subparts = string_utilities::string_split(parts[i], '.');
                        if (subparts.size() == 4) {
                            for (unsigned int k = 0; k < 10; k++) {
                                if (pieces[k] != 0) throw std::invalid_argument("IP is badly formatted. Nested IPV4 can be preceded only by 0 (and, optionally, two 255 octects)");
                            }

                            if ((pieces[10] != 0 && pieces[10] != 255) || (pieces[11] != 0 && pieces[11] != 255)) {
                                throw std::invalid_argument("IP is badly formatted. Nested IPV4 can be preceded only by 0 (and, optionally, two 255 octects)");
                            }

                            for (unsigned int ii = 0; ii < subparts.size(); ii++) {
                                if (subparts[ii] != "*") {
                                    pieces[y+ii] = strtol(subparts[ii].c_str(), nullptr, 10);
                                    if (pieces[y+ii] > 255) throw std::invalid_argument("IP is badly formatted. 255 is max value for ip part.");
                                } else {
                                    CLEAR_BIT(mask, y+ii);
                                }
                            }
                        } else {
                            throw std::invalid_argument("IP is badly formatted. Nested IPV4 can have max 4 parts.");
                        }
                    } else {
                        throw std::invalid_argument("IP is badly formatted. IPV6 parts can have max 4 characters (or nest an IPV4)");
                    }
                }
            } else {
                CLEAR_BIT(mask, y);
                CLEAR_BIT(mask, y+1);
                y+=2;
            }
        }
    } else {  // IPV4
        ip_version = http_utils::IPV4;
        parts = string_utilities::string_split(ip, '.');
        if (parts.size() == 4) {
            for (unsigned int i = 0; i < parts.size(); i++) {
                if (parts[i] != "*") {
                    pieces[12+i] = strtol(parts[i].c_str(), nullptr, 10);
                    if (pieces[12+i] > 255) throw std::invalid_argument("IP is badly formatted. 255 is max value for ip part.");
                } else {
                    CLEAR_BIT(mask, 12+i);
                }
            }
        } else {
            throw std::invalid_argument("IP is badly formatted. Max 4 parts in IPV4.");
        }
    }
}

bool ip_representation::operator <(const ip_representation& b) const {
    int64_t this_score = 0;
    int64_t b_score = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 10 || i == 11) continue;

        if (CHECK_BIT(mask, i) && CHECK_BIT(b.mask, i)) {
            this_score += (16 - i) * pieces[i];
            b_score += (16 - i) * b.pieces[i];
        }
    }

    if (this_score == b_score &&
            ((pieces[10] == 0x00 || pieces[10] == 0xFF) && (b.pieces[10] == 0x00 || b.pieces[10] == 0xFF)) &&
            ((pieces[11] == 0x00 || pieces[11] == 0xFF) && (b.pieces[11] == 0x00 || b.pieces[11] == 0xFF))) {
        return false;
    }

    for (int i = 10; i < 12; i++) {
        if (CHECK_BIT(mask, i) && CHECK_BIT(b.mask, i)) {
            this_score += (16 - i) * pieces[i];
            b_score += (16 - i) * b.pieces[i];
        }
    }

    return this_score < b_score;
}

const std::string load_file(const std::string& filename) {
    std::ifstream fp(filename.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
    if (fp.is_open()) {
        std::string content;

        fp.seekg(0, fp.end);
        content.reserve(fp.tellg());
        fp.seekg(0, fp.beg);

        content.assign((std::istreambuf_iterator<char>(fp)), std::istreambuf_iterator<char>());
        return content;
    } else {
        throw std::invalid_argument("Unable to open file");
    }
}

template<typename map_t>
void dump_map(std::ostream& os, const std::string& prefix, const map_t& map) {
    auto it = map.begin();
    auto end = map.end();

    if (map.size()) {
        os << "    " << prefix << " [";
        for (; it != end; ++it) {
            os << (*it).first << ":\"" << (*it).second << "\" ";
        }
        os << "]" << std::endl;
    }
}

void dump_header_map(std::ostream& os, const std::string& prefix, const http::header_view_map &map) {
    dump_map<http::header_view_map>(os, prefix, map);
}

void dump_arg_map(std::ostream& os, const std::string& prefix, const http::arg_view_map &map) {
    dump_map<http::arg_view_map>(os, prefix, map);
}

size_t base_unescaper(std::string* s, unescaper_ptr unescaper) {
    if ((*s)[0] == 0) return 0;

    if (unescaper != nullptr) {
        unescaper(*s);
        return s->size();
    }

    return http_unescape(s);
}

}  // namespace http
}  // namespace httpserver
