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
#ifndef _HTTP_RESPONSE_HPP_
#define _HTTP_RESPONSE_HPP_
#include <map>
#include <utility>
#include <string>

namespace httpserver
{

class webserver;

namespace http
{
    class header_comparator;
    class arg_comparator;
};

using namespace http;

/**
 * Class representing an abstraction for an Http Response. It is used from classes using these apis to send information through http protocol.
**/
class http_response 
{
    public:
        /**
         * Enumeration indicating whether the response content is got from a string or from a file
        **/
        enum response_type_T 
        {
            STRING_CONTENT = 0,
            FILE_CONTENT,
            SHOUTCAST_CONTENT,
            DIGEST_AUTH_FAIL,
            BASIC_AUTH_FAIL,
            SWITCH_PROTOCOL
        };

        /**
         * Constructor used to build an http_response with a content and a response_code
         * @param content The content to set for the request. (if the response_type is FILE_CONTENT, it represents the path to the file to read from).
         * @param response_code The response code to set for the request.
         * @param response_type param indicating if the content have to be read from a string or from a file
        **/
        http_response
        (
            const http_response::response_type_T& response_type = http_response::STRING_CONTENT,
            const std::string& content = "", 
            int response_code = 200,
            const std::string& content_type = "text/plain",
            const std::string& realm = "",
            const std::string& opaque = "",
            bool reload_nonce = false
        ):
            response_type(response_type),
            content(content),
            response_code(response_code),
            realm(realm),
            opaque(opaque),
            reload_nonce(reload_nonce),
            fp(-1),
            filename(content)
        {
            set_header(http_utils::http_header_content_type, content_type);
        }
        /**
         * Copy constructor
         * @param b The http_response object to copy attributes value from.
        **/
        http_response(const http_response& b):
            response_type(b.response_type),
            content(b.content),
            response_code(b.response_code),
            realm(b.realm),
            opaque(b.opaque),
            reload_nonce(b.reload_nonce),
            fp(b.fp),
            filename(b.filename),
            headers(b.headers),
            footers(b.footers)
        {
        }
        /**
         * Method used to get the content from the response.
         * @return the content in string form
        **/
        const std::string get_content()
        {
            return this->content;
        }
        /**
         * Method used to set the content of the response
         * @param content The content to set
        **/
        void set_content(const std::string& content)
        {
            this->content = content;
        }
        /**
         * Method used to get a specified header defined for the response
         * @param key The header identification
         * @return a string representing the value assumed by the header
        **/
        const std::string get_header(const std::string& key)
        {
            return this->headers[key];
        }
        /**
         * Method used to get a specified footer defined for the response
         * @param key The footer identification
         * @return a string representing the value assumed by the footer
        **/
        const std::string get_footer(const std::string& key)
        {
            return this->footers[key];
        }
        /**
         * Method used to set an header value by key.
         * @param key The name identifying the header
         * @param value The value assumed by the header
        **/
        void set_header(const std::string& key, const std::string& value)
        {
            this->headers[key] = value;
        }
        /**
         * Method used to set a footer value by key.
         * @param key The name identifying the footer
         * @param value The value assumed by the footer
        **/
        void set_footer(const std::string& key, const std::string& value)
        {
            this->footers[key] = value;
        }
        /**
         * Method used to set the content type for the request. This is a shortcut of setting the corresponding header.
         * @param content_type the content type to use for the request
        **/
        void set_content_type(const std::string& content_type)
        {
            this->headers[http_utils::http_header_content_type] = content_type;
        }
        /**
         * Method used to remove previously defined header
         * @param key The header to remove
        **/
        void remove_header(const std::string& key)
        {
            this->headers.erase(key);
        }
        /**
         * Method used to get all headers passed with the request.
         * @return a map<string,string> containing all headers.
        **/
        const std::vector<std::pair<std::string, std::string> > get_headers();
        /**
         * Method used to get all footers passed with the request.
         * @return a map<string,string> containing all footers.
        **/
        const std::vector<std::pair<std::string, std::string> > get_footers();
        /**
         * Method used to set all headers of the response.
         * @param headers The headers key-value map to set for the response.
        **/
        void set_headers(const std::map<std::string, std::string>& headers)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = headers.begin(); it != headers.end(); it ++)
                this->headers[it->first] = it->second;
        }
        /**
         * Method used to set all footers of the response.
         * @param footers The footers key-value map to set for the response.
        **/
        void set_footers(const std::map<std::string, std::string>& footers)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = footers.begin(); it != footers.end(); it ++)
                this->footers[it->first] = it->second;
        }
        /**
         * Method used to set the response code of the response
         * @param response_code the response code to set
        **/
        void set_response_code(int response_code)
        {
            this->response_code = response_code;
        }
        /**
         * Method used to get the response code from the response
         * @return The response code
        **/
        int get_response_code()
        {
            return this->response_code;
        }
        const std::string get_realm() const
        {
            return this->realm;
        }
        const std::string get_opaque() const
        {
            return this->opaque;
        }
        const bool need_nonce_reload() const
        {
            return this->reload_nonce;
        }
        int get_switch_callback() const
        {
            return 0;
        }
    protected:
        response_type_T response_type;
        std::string content;
        int response_code;
        std::string realm;
        std::string opaque;
        bool reload_nonce;
        int fp;
        std::string filename;
        std::map<std::string, std::string, header_comparator> headers;
        std::map<std::string, std::string, arg_comparator> footers;
        friend class webserver;
};

class http_string_response : public http_response
{
    public:
        http_string_response
        (
            const std::string& content,
            int response_code,
            const std::string& content_type = "text/plain"
        ): http_response(http_response::STRING_CONTENT, content, response_code, content_type) { }

        http_string_response(const http_string_response& b) : http_response(b) { }
};

class http_file_response : public http_response
{
    public:
        http_file_response
        (
            const std::string& filename, 
            int response_code,
            const std::string& content_type = "text/plain"
        );

        http_file_response(const http_file_response& b) : http_response(b) { }
};

class http_basic_auth_fail_response : public http_response
{
    public:
        http_basic_auth_fail_response
        (
            const std::string& content,
            int response_code,
            const std::string& content_type = "text/plain",
            const std::string& realm = "",
            const http_response::response_type_T& response_type = http_response::BASIC_AUTH_FAIL
        ) : http_response(http_response::BASIC_AUTH_FAIL, content, response_code, content_type, realm) { }

        http_basic_auth_fail_response(const http_basic_auth_fail_response& b) : http_response(b) { }
};

class http_digest_auth_fail_response : public http_response
{
    public:
        http_digest_auth_fail_response
        (
            const std::string& content,
            int response_code,
            const std::string& content_type = "text/plain",
            const std::string& realm = "",
            const std::string& opaque = "",
            bool reload_nonce = false
        ) : http_response(http_response::DIGEST_AUTH_FAIL, content, response_code, content_type, realm, opaque, reload_nonce)
        { 
        }

        http_digest_auth_fail_response(const http_digest_auth_fail_response& b) : http_response(b) { }
};

class shoutCAST_response : public http_response
{
    public:
        shoutCAST_response
        (
            const std::string& content,
            int response_code,
            const std::string& content_type = "text/plain"
        );

        shoutCAST_response(const shoutCAST_response& b) : http_response(b) { }
};

class switch_protocol_response : public http_response
{
    public:
        switch_protocol_response
        (
        );

        switch_protocol_response(const switch_protocol_response& b) : http_response(b) { }
};

};
#endif
