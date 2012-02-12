/*
     This file is part of libhttpserver
     Copyright (C) 2011 Sebastiano Merlino

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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "HttpUtils.hpp"
#include "string_utilities.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
//#include <boost/xpressive/xpressive.hpp>

/* See also: http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html */
const int HttpUtils::http_continue = MHD_HTTP_CONTINUE;
const int HttpUtils::http_switching_protocol = MHD_HTTP_SWITCHING_PROTOCOLS;
const int HttpUtils::http_processing = MHD_HTTP_PROCESSING;

const int HttpUtils::http_ok = MHD_HTTP_OK;
const int HttpUtils::http_created = MHD_HTTP_CREATED;
const int HttpUtils::http_accepted = MHD_HTTP_ACCEPTED;
const int HttpUtils::http_non_authoritative_information = MHD_HTTP_NON_AUTHORITATIVE_INFORMATION;
const int HttpUtils::http_no_content = MHD_HTTP_NO_CONTENT;
const int HttpUtils::http_reset_content = MHD_HTTP_RESET_CONTENT;
const int HttpUtils::http_partial_content = MHD_HTTP_PARTIAL_CONTENT;
const int HttpUtils::http_multi_status = MHD_HTTP_MULTI_STATUS;

const int HttpUtils::http_multiple_choices = MHD_HTTP_MULTIPLE_CHOICES;
const int HttpUtils::http_moved_permanently = MHD_HTTP_MOVED_PERMANENTLY;
const int HttpUtils::http_found = MHD_HTTP_FOUND;
const int HttpUtils::http_see_other = MHD_HTTP_SEE_OTHER;
const int HttpUtils::http_not_modified = MHD_HTTP_NOT_MODIFIED;
const int HttpUtils::http_use_proxy = MHD_HTTP_USE_PROXY;
const int HttpUtils::http_switch_proxy = MHD_HTTP_SWITCH_PROXY;
const int HttpUtils::http_temporary_redirect = MHD_HTTP_TEMPORARY_REDIRECT;

const int HttpUtils::http_bad_request = MHD_HTTP_BAD_REQUEST;
const int HttpUtils::http_unauthorized = MHD_HTTP_UNAUTHORIZED;
const int HttpUtils::http_payment_required = MHD_HTTP_PAYMENT_REQUIRED;
const int HttpUtils::http_forbidden = MHD_HTTP_FORBIDDEN;
const int HttpUtils::http_not_found = MHD_HTTP_NOT_FOUND;
const int HttpUtils::http_method_not_allowed = MHD_HTTP_METHOD_NOT_ALLOWED;
const int HttpUtils::http_method_not_acceptable = MHD_HTTP_METHOD_NOT_ACCEPTABLE;
const int HttpUtils::http_proxy_authentication_required = MHD_HTTP_PROXY_AUTHENTICATION_REQUIRED;
const int HttpUtils::http_request_timeout = MHD_HTTP_REQUEST_TIMEOUT;
const int HttpUtils::http_conflict = MHD_HTTP_CONFLICT;
const int HttpUtils::http_gone = MHD_HTTP_GONE;
const int HttpUtils::http_length_required = MHD_HTTP_LENGTH_REQUIRED;
const int HttpUtils::http_precondition_failed = MHD_HTTP_PRECONDITION_FAILED;
const int HttpUtils::http_request_entity_too_large = MHD_HTTP_REQUEST_ENTITY_TOO_LARGE;
const int HttpUtils::http_request_uri_too_long = MHD_HTTP_REQUEST_URI_TOO_LONG;
const int HttpUtils::http_unsupported_media_type = MHD_HTTP_UNSUPPORTED_MEDIA_TYPE;
const int HttpUtils::http_requested_range_not_satisfiable = MHD_HTTP_REQUESTED_RANGE_NOT_SATISFIABLE;
const int HttpUtils::http_expectation_failed = MHD_HTTP_EXPECTATION_FAILED;
const int HttpUtils::http_unprocessable_entity = MHD_HTTP_UNPROCESSABLE_ENTITY;
const int HttpUtils::http_locked = MHD_HTTP_LOCKED;
const int HttpUtils::http_failed_dependency = MHD_HTTP_FAILED_DEPENDENCY;
const int HttpUtils::http_unordered_collection = MHD_HTTP_UNORDERED_COLLECTION;
const int HttpUtils::http_upgrade_required = MHD_HTTP_UPGRADE_REQUIRED;
const int HttpUtils::http_retry_with = MHD_HTTP_RETRY_WITH;

const int HttpUtils::http_internal_server_error = MHD_HTTP_INTERNAL_SERVER_ERROR;
const int HttpUtils::http_not_implemented = MHD_HTTP_NOT_IMPLEMENTED;
const int HttpUtils::http_bad_gateway = MHD_HTTP_BAD_GATEWAY;
const int HttpUtils::http_service_unavailable = MHD_HTTP_SERVICE_UNAVAILABLE;
const int HttpUtils::http_gateway_timeout = MHD_HTTP_GATEWAY_TIMEOUT;
const int HttpUtils::http_version_not_supported = MHD_HTTP_HTTP_VERSION_NOT_SUPPORTED;
const int HttpUtils::http_variant_also_negotiated = MHD_HTTP_VARIANT_ALSO_NEGOTIATES;
const int HttpUtils::http_insufficient_storage = MHD_HTTP_INSUFFICIENT_STORAGE;
const int HttpUtils::http_bandwidth_limit_exceeded = MHD_HTTP_BANDWIDTH_LIMIT_EXCEEDED;
const int HttpUtils::http_not_extended = MHD_HTTP_NOT_EXTENDED;


const std::string HttpUtils::http_header_accept = MHD_HTTP_HEADER_ACCEPT;
const std::string HttpUtils::http_header_accept_charset = MHD_HTTP_HEADER_ACCEPT_CHARSET;
const std::string HttpUtils::http_header_accept_encoding = MHD_HTTP_HEADER_ACCEPT_ENCODING;
const std::string HttpUtils::http_header_accept_language = MHD_HTTP_HEADER_ACCEPT_LANGUAGE;
const std::string HttpUtils::http_header_accept_ranges = MHD_HTTP_HEADER_ACCEPT_RANGES;
const std::string HttpUtils::http_header_age = MHD_HTTP_HEADER_AGE;
const std::string HttpUtils::http_header_allow = MHD_HTTP_HEADER_ALLOW;
const std::string HttpUtils::http_header_authorization = MHD_HTTP_HEADER_AUTHORIZATION;
const std::string HttpUtils::http_header_cache_control = MHD_HTTP_HEADER_CACHE_CONTROL;
const std::string HttpUtils::http_header_connection = MHD_HTTP_HEADER_CONNECTION;
const std::string HttpUtils::http_header_content_encoding = MHD_HTTP_HEADER_CONTENT_ENCODING;
const std::string HttpUtils::http_header_content_language = MHD_HTTP_HEADER_CONTENT_LANGUAGE;
const std::string HttpUtils::http_header_content_length = MHD_HTTP_HEADER_CONTENT_LENGTH;
const std::string HttpUtils::http_header_content_location = MHD_HTTP_HEADER_CONTENT_LOCATION;
const std::string HttpUtils::http_header_content_md5 = MHD_HTTP_HEADER_CONTENT_MD5;
const std::string HttpUtils::http_header_content_range = MHD_HTTP_HEADER_CONTENT_RANGE;
const std::string HttpUtils::http_header_content_type = MHD_HTTP_HEADER_CONTENT_TYPE;
const std::string HttpUtils::http_header_date = MHD_HTTP_HEADER_DATE;
const std::string HttpUtils::http_header_etag = MHD_HTTP_HEADER_ETAG;
const std::string HttpUtils::http_header_expect = MHD_HTTP_HEADER_EXPECT;
const std::string HttpUtils::http_header_expires = MHD_HTTP_HEADER_EXPIRES;
const std::string HttpUtils::http_header_from = MHD_HTTP_HEADER_FROM;
const std::string HttpUtils::http_header_host = MHD_HTTP_HEADER_HOST;
const std::string HttpUtils::http_header_if_match = MHD_HTTP_HEADER_IF_MATCH;
const std::string HttpUtils::http_header_if_modified_since = MHD_HTTP_HEADER_IF_MODIFIED_SINCE;
const std::string HttpUtils::http_header_if_none_match = MHD_HTTP_HEADER_IF_NONE_MATCH;
const std::string HttpUtils::http_header_if_range = MHD_HTTP_HEADER_IF_RANGE;
const std::string HttpUtils::http_header_if_unmodified_since = MHD_HTTP_HEADER_IF_UNMODIFIED_SINCE;
const std::string HttpUtils::http_header_last_modified = MHD_HTTP_HEADER_LAST_MODIFIED;
const std::string HttpUtils::http_header_location = MHD_HTTP_HEADER_LOCATION;
const std::string HttpUtils::http_header_max_forwards = MHD_HTTP_HEADER_MAX_FORWARDS;
const std::string HttpUtils::http_header_pragma = MHD_HTTP_HEADER_PRAGMA;
const std::string HttpUtils::http_header_proxy_authenticate = MHD_HTTP_HEADER_PROXY_AUTHENTICATE;
const std::string HttpUtils::http_header_proxy_authentication = MHD_HTTP_HEADER_PROXY_AUTHORIZATION;
const std::string HttpUtils::http_header_range = MHD_HTTP_HEADER_RANGE;
const std::string HttpUtils::http_header_referer = MHD_HTTP_HEADER_REFERER;
const std::string HttpUtils::http_header_retry_after = MHD_HTTP_HEADER_RETRY_AFTER;
const std::string HttpUtils::http_header_server = MHD_HTTP_HEADER_SERVER;
const std::string HttpUtils::http_header_te = MHD_HTTP_HEADER_TE;
const std::string HttpUtils::http_header_trailer = MHD_HTTP_HEADER_TRAILER;
const std::string HttpUtils::http_header_transfer_encoding = MHD_HTTP_HEADER_TRANSFER_ENCODING;
const std::string HttpUtils::http_header_upgrade = MHD_HTTP_HEADER_UPGRADE;
const std::string HttpUtils::http_header_user_agent = MHD_HTTP_HEADER_USER_AGENT;
const std::string HttpUtils::http_header_vary = MHD_HTTP_HEADER_VARY;
const std::string HttpUtils::http_header_via = MHD_HTTP_HEADER_VIA;
const std::string HttpUtils::http_header_warning = MHD_HTTP_HEADER_WARNING;
const std::string HttpUtils::http_header_www_authenticate = MHD_HTTP_HEADER_WWW_AUTHENTICATE;

const std::string HttpUtils::http_version_1_0 = MHD_HTTP_VERSION_1_0;
const std::string HttpUtils::http_version_1_1 = MHD_HTTP_VERSION_1_1;

const std::string HttpUtils::http_method_connect = MHD_HTTP_METHOD_CONNECT;
const std::string HttpUtils::http_method_delete = MHD_HTTP_METHOD_DELETE;
const std::string HttpUtils::http_method_get = MHD_HTTP_METHOD_GET;
const std::string HttpUtils::http_method_head = MHD_HTTP_METHOD_HEAD;
const std::string HttpUtils::http_method_options = MHD_HTTP_METHOD_OPTIONS;
const std::string HttpUtils::http_method_post = MHD_HTTP_METHOD_POST;
const std::string HttpUtils::http_method_put = MHD_HTTP_METHOD_PUT;
const std::string HttpUtils::http_method_trace = MHD_HTTP_METHOD_TRACE;

const std::string HttpUtils::http_post_encoding_form_urlencoded = MHD_HTTP_POST_ENCODING_FORM_URLENCODED;
const std::string HttpUtils::http_post_encoding_multipart_formdata = MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA;


const std::vector<std::string> HttpUtils::tokenizeUrl(const std::string& str, const char separator)
{
    return string_utilities::string_split(str, separator);
}

std::string HttpUtils::standardizeUrl(const std::string& url)
{
//    boost::xpressive::sregex re = boost::xpressive::sregex::compile( "(\\/)+", boost::xpressive::regex_constants::icase );
//    std::string n_url = regex_replace( url, re, "/" );
    std::string n_url = string_utilities::regex_replace(url, "(\\/)+", "/");
    if(n_url[n_url.size() - 1] == '/')
    {
        return n_url.substr(0, n_url.size() -1);
    }
    else
    {
        return n_url;
    }
}

std::string generate_random_uuid()
{
	uuid_t out;
	uuid_generate(out);
	std::string ret;
	ret.assign((const char*)out, 16);
	char unparsed[37];
	uuid_unparse((const unsigned char*)ret.c_str(), unparsed);
	return unparsed;
}

std::string get_ip_str(const struct sockaddr *sa, socklen_t maxlen)
{
	char* to_ret;
	switch(sa->sa_family) 
	{
		case AF_INET:
			if(maxlen == 0)
				maxlen = INET_ADDRSTRLEN;
			to_ret = (char*)malloc(maxlen*sizeof(char));
			inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), to_ret, maxlen);
			break;

		case AF_INET6:
			if(maxlen == 0)
				maxlen = INET6_ADDRSTRLEN;
			to_ret = (char*)malloc(maxlen*sizeof(char));
			inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr), to_ret, maxlen);
			break;
		default:
			to_ret = (char*)malloc(11*sizeof(char));
			strncpy(to_ret, "Unknown AF", 11);
			return NULL;
	}
	std::string res(to_ret);
	free(to_ret);
	return std::string(res);
}

short get_port(const struct sockaddr* sa)
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

//std::string HttpUtils::base64_decode(const std::string& aPlain) {
    //unsigned char buf[512];
    //memset(buf, '\0', 512);
    //BIO* mbio=BIO_new(BIO_s_mem());
    //BIO* b64bio=BIO_new(BIO_f_base64());
    //BIO_set_flags(b64bio,BIO_FLAGS_BASE64_NO_NL);
    //BIO_puts(mbio,aPlain.c_str());
    //BIO* bio=BIO_push(b64bio,mbio);
    //std::string aOut;
    //int i=BIO_read(bio, (void*)buf, BIO_ctrl_pending(bio));
    //if (i>0) {
        //aOut = std::string((char*)buf);
    //}
    //BIO_free_all(bio);
    //return aOut;
//}
