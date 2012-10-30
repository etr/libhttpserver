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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef _HTTP_RESPONSE_HPP_
#define _HTTP_RESPONSE_HPP_
#include <map>
#include <utility>
#include <string>

struct MHD_Connection;

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
            SWITCH_PROTOCOL,
            LONG_POLLING_RECEIVE,
            LONG_POLLING_SEND
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
            bool reload_nonce = false,
            const std::vector<std::string>& topics = std::vector<std::string>(),
            int keepalive_secs = -1,
            const std::string keepalive_msg = "",
            const std::string send_topic = ""
        ):
            response_type(response_type),
            content(content),
            response_code(response_code),
            realm(realm),
            opaque(opaque),
            reload_nonce(reload_nonce),
            fp(-1),
            filename(content),
            topics(topics),
            keepalive_secs(keepalive_secs),
            keepalive_msg(keepalive_msg),
            send_topic(send_topic)
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
            footers(b.footers),
            topics(b.topics),
            keepalive_secs(b.keepalive_secs),
            keepalive_msg(b.keepalive_msg),
            send_topic(b.send_topic)
        {
        }
        virtual ~http_response()
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
        void get_content(std::string& result)
        {
            result = this->content;
        }
        /**
         * Method used to set the content of the response
         * @param content The content to set
        **/
        void set_content(const std::string& content)
        {
            this->content = content;
        }
        void grow_content(const std::string& content)
        {
            this->content.append(content);
        }
        void grow_content(const char* content, size_t size)
        {
            this->content.append(content, size);
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
        void get_header(const std::string& key, std::string& result)
        {
            result = this->headers[key];
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
        void get_footer(const std::string& key, std::string& result)
        {
            result = this->footers[key];
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
        size_t get_headers(std::vector<std::pair<std::string, std::string> >& result);
#ifndef SWIG
        size_t get_headers(std::map<std::string, std::string, header_comparator>& result);
#endif
        /**
         * Method used to get all footers passed with the request.
         * @return a map<string,string> containing all footers.
        **/
        const std::vector<std::pair<std::string, std::string> > get_footers();
        size_t get_footers(std::vector<std::pair<std::string, std::string> >& result);
#ifndef SWIG
        size_t get_footers(std::map<std::string, std::string, header_comparator>& result);
#endif
        /**
         * Method used to set all headers of the response.
         * @param headers The headers key-value map to set for the response.
        **/
        void set_headers(const std::map<std::string, std::string>& headers)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = headers.begin(); it != headers.end(); ++it)
                this->headers[it->first] = it->second;
        }
        /**
         * Method used to set all footers of the response.
         * @param footers The footers key-value map to set for the response.
        **/
        void set_footers(const std::map<std::string, std::string>& footers)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = footers.begin(); it != footers.end(); ++it)
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
        void get_realm(std::string& result) const
        {
            result = this->realm;
        }
        const std::string get_opaque() const
        {
            return this->opaque;
        }
        void get_opaque(std::string& result) const
        {
            result = this->opaque;
        }
        const bool need_nonce_reload() const
        {
            return this->reload_nonce;
        }
        int get_switch_callback() const
        {
            return 0;
        }
        size_t get_topics(std::vector<std::string>& topics) const
        {
            for(std::vector<std::string>::const_iterator it = this->topics.begin(); it != this->topics.end(); ++it)
                topics.push_back(*it);
            return topics.size();
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
        std::map<std::string, std::string, header_comparator> footers;
        std::vector<std::string> topics;
        int keepalive_secs;
        std::string keepalive_msg;
        std::string send_topic;
        struct MHD_Connection* underlying_connection;

        virtual void get_raw_response(MHD_Response** res, bool* found, webserver* ws = 0x0);

        friend class webserver;
        friend void clone_response(const http_response& hr, http_response** dhr);
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

        http_string_response(const http_response& b) : http_response(b) { }
};

class http_byte_response : public http_response
{
    public:
        http_byte_response
        (
            const char* content,
            size_t content_length,
            int response_code,
            const std::string& content_type = "text/plain"
        ): http_response(http_response::STRING_CONTENT, std::string(content, content_length), response_code, content_type) { }
};

class http_file_response : public http_response
{
    public:
        http_file_response
        (
            const std::string& filename,
            int response_code,
            const std::string& content_type = "text/plain"
        ) : http_response(http_response::FILE_CONTENT, filename, response_code, content_type)
        {
        }

        http_file_response(const http_response& b) : http_response(b) { }
    protected:
        virtual void get_raw_response(MHD_Response** res, bool* found, webserver* ws = 0x0);
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

        http_basic_auth_fail_response(const http_response& b) : http_response(b) { }
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

        http_digest_auth_fail_response(const http_response& b) : http_response(b) { }
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

        shoutCAST_response(const http_response& b) : http_response(b) { }
};

class switch_protocol_response : public http_response
{
    public:
        switch_protocol_response
        (
        )
        {
        }

        switch_protocol_response(const http_response& b) : http_response(b)
        { 
        }
    protected:
        virtual void get_raw_response(MHD_Response** res, bool* found, webserver* ws = 0x0) {}
};

class long_polling_receive_response : public http_response
{
    public:
        long_polling_receive_response
        (
            const std::string& content,
            int response_code,
            const std::string& content_type,
            const std::vector<std::string>& topics,
            int keepalive_secs = -1,
            std::string keepalive_msg = ""
        ) : http_response(http_response::LONG_POLLING_RECEIVE, content, response_code, content_type, "", "", false, topics, keepalive_secs, keepalive_msg)
        {
        }

        long_polling_receive_response(const http_response& b) : http_response(b) { }
    protected:
        virtual void get_raw_response(MHD_Response** res, bool* found, webserver* ws = 0x0);
    private:
        static ssize_t data_generator (void* cls, uint64_t pos, char* buf, size_t max);

        int connection_id;
        webserver* ws;
};

class long_polling_send_response : public http_response
{
    public:
        long_polling_send_response
        (
            const std::string& content,
            const std::string& topic
        ) : http_response(http_response::LONG_POLLING_SEND, content, 200, "", "", "", false, std::vector<std::string>(), -1, "", topic)
        {
        }

        long_polling_send_response(const http_response& b) : http_response(b) { }
    protected:
        virtual void get_raw_response(MHD_Response** res, bool* found, webserver* ws = 0x0);
};

void clone_response(http_response* hr, http_response** dhr);

};
#endif
