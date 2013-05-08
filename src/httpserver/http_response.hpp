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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 
     USA
*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef _HTTP_RESPONSE_HPP_
#define _HTTP_RESPONSE_HPP_
#include <map>
#include <utility>
#include <string>
#include "httpserver/binders.hpp"

struct MHD_Connection;

namespace httpserver
{

class webserver;
class http_response;
struct cache_entry;

namespace http
{
    class header_comparator;
    class arg_comparator;
};

namespace details
{
    struct http_response_ptr;
    ssize_t cb(void*, uint64_t, char*, size_t);
};

using namespace http;

class bad_caching_attempt: public std::exception
{
    virtual const char* what() const throw()
    {
        return "You cannot pass ce = 0x0 without key!";
    }
};

/**
 * Class representing an abstraction for an Http Response. It is used from classes using these apis to send information through http protocol.
**/
class http_response 
{
    public:
        /**
         * Constructor used to build an http_response with a content and a response_code
         * @param content The content to set for the request. (if the response_type is FILE_CONTENT, it represents the path to the file to read from).
         * @param response_code The response code to set for the request.
         * @param response_type param indicating if the content have to be read from a string or from a file
        **/
        template <typename T>
        http_response
        (
            const T* response_type = 0x0,
            const std::string& content = "", 
            int response_code = 200,
            const std::string& content_type = "text/plain",
            bool autodelete = true,
            const std::string& realm = "",
            const std::string& opaque = "",
            bool reload_nonce = false,
            const std::vector<std::string>& topics = std::vector<std::string>(),
            int keepalive_secs = -1,
            const std::string keepalive_msg = "",
            const std::string send_topic = "",
            cache_entry* ce = 0x0
        ):
            content(content),
            response_code(response_code),
            autodelete(autodelete),
            realm(realm),
            opaque(opaque),
            reload_nonce(reload_nonce),
            fp(-1),
            filename(content),
            topics(topics),
            keepalive_secs(keepalive_secs),
            keepalive_msg(keepalive_msg),
            send_topic(send_topic),
            underlying_connection(0x0),
            ca(0x0),
            closure_data(0x0),
            ce(ce),
            get_raw_response(this, &http_response::get_raw_response_str),
            decorate_response(this, &http_response::decorate_response_str),
            enqueue_response(this, &http_response::enqueue_response_str),
            completed(false),
            ws(0x0),
            connection_id(0x0)
        {
            set_header(http_utils::http_header_content_type, content_type);
        }
        /**
         * Copy constructor
         * @param b The http_response object to copy attributes value from.
        **/
        http_response(const http_response& b):
            content(b.content),
            response_code(b.response_code),
            autodelete(b.autodelete),
            realm(b.realm),
            opaque(b.opaque),
            reload_nonce(b.reload_nonce),
            fp(b.fp),
            filename(b.filename),
            headers(b.headers),
            footers(b.footers),
            cookies(b.cookies),
            topics(b.topics),
            keepalive_secs(b.keepalive_secs),
            keepalive_msg(b.keepalive_msg),
            send_topic(b.send_topic),
            underlying_connection(b.underlying_connection),
            ca(0x0),
            closure_data(0x0),
            ce(b.ce),
            get_raw_response(b.get_raw_response),
            decorate_response(b.decorate_response),
            enqueue_response(b.enqueue_response),
            completed(b.completed),
            ws(b.ws),
            connection_id(b.connection_id)
        {
        }

        ~http_response();
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
        const std::string get_cookie(const std::string& key)
        {
            return this->cookies[key];
        }
        void get_cookie(const std::string& key, std::string& result)
        {
            result = this->cookies[key];
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
        void set_cookie(const std::string& key, const std::string& value)
        {
            this->cookies[key] = value;
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
        void remove_footer(const std::string& key)
        {
            this->footers.erase(key);
        }
        void remove_cookie(const std::string& key)
        {
            this->cookies.erase(key);
        }
        /**
         * Method used to get all headers passed with the request.
         * @return a map<string,string> containing all headers.
        **/
        size_t get_headers(
                std::map<std::string, std::string, header_comparator>& result
        );
        /**
         * Method used to get all footers passed with the request.
         * @return a map<string,string> containing all footers.
        **/
        size_t get_footers(
                std::map<std::string, std::string, header_comparator>& result
        );
        size_t get_cookies(
                std::map<std::string, std::string, header_comparator>& result
        );
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
        void set_cookies(const std::map<std::string, std::string>& cookies)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = cookies.begin(); it != cookies.end(); ++it)
                this->cookies[it->first] = it->second;
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
            typedef std::vector<std::string>::const_iterator topics_it;
            for(topics_it it=this->topics.begin();it != this->topics.end();++it)
                topics.push_back(*it);
            return topics.size();
        }
        void set_closure_action(void(*ca)(void*), void* closure_data = 0x0)
        {
            this->ca = ca;
            this->closure_data = closure_data;
        }
    protected:
        typedef details::binders::functor_two<MHD_Response**, 
                webserver*, void> get_raw_response_t;

        typedef details::binders::functor_one<MHD_Response*,
                void> decorate_response_t;

        typedef details::binders::functor_two<MHD_Connection*,
                MHD_Response*, int> enqueue_response_t;

        std::string content;
        int response_code;
        bool autodelete;
        std::string realm;
        std::string opaque;
        bool reload_nonce;
        int fp;
        std::string filename;
        std::map<std::string, std::string, header_comparator> headers;
        std::map<std::string, std::string, header_comparator> footers;
        std::map<std::string, std::string, header_comparator> cookies;
        std::vector<std::string> topics;
        int keepalive_secs;
        std::string keepalive_msg;
        std::string send_topic;
        struct MHD_Connection* underlying_connection;
        void(*ca)(void*);
        void* closure_data;
        cache_entry* ce;

        const get_raw_response_t get_raw_response;
        const decorate_response_t decorate_response;
        const enqueue_response_t enqueue_response;

        bool completed;

        webserver* ws;
        struct httpserver_ska connection_id;

        void get_raw_response_str(MHD_Response** res, webserver* ws = 0x0);
        void get_raw_response_file(MHD_Response** res, webserver* ws = 0x0);
        void get_raw_response_switch_r(MHD_Response** res, webserver* ws = 0x0);
        
        void get_raw_response_lp_receive(MHD_Response** res,
                webserver* ws = 0x0);
        
        void get_raw_response_lp_send(MHD_Response** res, webserver* ws = 0x0);
        void get_raw_response_cache(MHD_Response** res, webserver* ws = 0x0);
        void get_raw_response_deferred(MHD_Response** res, webserver* ws = 0x0);
        void decorate_response_str(MHD_Response* res);
        void decorate_response_cache(MHD_Response* res);
        void decorate_response_deferred(MHD_Response* res);
        int enqueue_response_str(MHD_Connection* connection, MHD_Response* res);

        int enqueue_response_basic(MHD_Connection* connection,
                MHD_Response* res
        );

        int enqueue_response_digest(MHD_Connection* connection,
                MHD_Response* res
        );

        friend class webserver;
        friend struct details::http_response_ptr;

        friend void clone_response(const http_response& hr,
                http_response** dhr
        );

        friend class cache_response;
        friend class deferred_response;
    private:
        http_response& operator=(const http_response& b);
};

class http_file_response;
class http_basic_auth_fail_response;
class http_digest_auth_fail_response;
class switch_protocol_response;
class long_polling_receive_response;
class long_polling_send_response;
class cache_response;
class deferred_response;

#define SPECIALIZE_RESPONSE_FOR(TYPE, S1, S2, S3) \
template <> \
inline http_response::http_response<TYPE> \
( \
    const TYPE* response_type, \
    const std::string& content, \
    int response_code,\
    const std::string& content_type,\
    bool autodelete,\
    const std::string& realm,\
    const std::string& opaque,\
    bool reload_nonce,\
    const std::vector<std::string>& topics,\
    int keepalive_secs,\
    const std::string keepalive_msg,\
    const std::string send_topic,\
    cache_entry* ce\
):\
    content(content),\
    response_code(response_code),\
    autodelete(autodelete),\
    realm(realm),\
    opaque(opaque),\
    reload_nonce(reload_nonce),\
    fp(-1),\
    filename(content),\
    topics(topics),\
    keepalive_secs(keepalive_secs),\
    keepalive_msg(keepalive_msg),\
    send_topic(send_topic),\
    underlying_connection(0x0),\
    ca(0x0),\
    closure_data(0x0),\
    ce(ce),\
    get_raw_response(this, &http_response::get_raw_response_## S1),\
    decorate_response(this, &http_response::decorate_response_## S2),\
    enqueue_response(this, &http_response::enqueue_response_## S3),\
    completed(false),\
    ws(0x0),\
    connection_id(0x0)\
{\
    set_header(http_utils::http_header_content_type, content_type);\
}

SPECIALIZE_RESPONSE_FOR(http_file_response, file, str, str);
SPECIALIZE_RESPONSE_FOR(http_basic_auth_fail_response, str, str, basic);
SPECIALIZE_RESPONSE_FOR(http_digest_auth_fail_response, str, str, digest);
SPECIALIZE_RESPONSE_FOR(switch_protocol_response, switch_r, str, str);
SPECIALIZE_RESPONSE_FOR(long_polling_receive_response, lp_receive, str, str);
SPECIALIZE_RESPONSE_FOR(long_polling_send_response, lp_send, str, str);
SPECIALIZE_RESPONSE_FOR(cache_response, cache, cache, str);
SPECIALIZE_RESPONSE_FOR(deferred_response, deferred, deferred, str);

class http_string_response : public http_response
{
    public:
        http_string_response
        (
            const std::string& content,
            int response_code,
            const std::string& content_type = "text/plain",
            bool autodelete = true
        ): http_response(this, content, response_code, content_type, autodelete)
        {
        }

        http_string_response(const http_response& b) : http_response(b) { }
    private:
        friend class webserver;
};

class http_byte_response : public http_response
{
    public:
        http_byte_response
        (
            const char* content,
            size_t content_length,
            int response_code,
            const std::string& content_type = "text/plain",
            bool autodelete = true
        ): http_response(
            this, std::string(content, content_length), 
            response_code, content_type, autodelete) 
        {
        }
    private:
        friend class webserver;
};

class http_file_response : public http_response
{
    public:
        http_file_response
        (
            const std::string& filename,
            int response_code,
            const std::string& content_type = "text/plain",
            bool autodelete = true
        ) : http_response(this,filename,response_code,content_type,autodelete)
        {
        }

        http_file_response(const http_response& b) : http_response(b) { }
    private:
        friend class webserver;
};

class http_basic_auth_fail_response : public http_response
{
    public:
        http_basic_auth_fail_response
        (
            const std::string& content,
            int response_code,
            const std::string& content_type = "text/plain",
            bool autodelete = true,
            const std::string& realm = ""
        ) : http_response(this, content, response_code, 
            content_type, autodelete, realm)
        {
        }

        http_basic_auth_fail_response(const http_response& b) :
            http_response(b) { }
    private:
        friend class webserver;
};

class http_digest_auth_fail_response : public http_response
{
    public:
        http_digest_auth_fail_response
        (
            const std::string& content,
            int response_code,
            const std::string& content_type = "text/plain",
            bool autodelete = true,
            const std::string& realm = "",
            const std::string& opaque = "",
            bool reload_nonce = false
        ) : http_response(this, content, response_code,
            content_type, autodelete, realm, opaque, reload_nonce)
        { 
        }

        http_digest_auth_fail_response(const http_response& b) :
            http_response(b) { }
    private:
        friend class webserver;
};

class shoutCAST_response : public http_response
{
    public:
        shoutCAST_response
        (
            const std::string& content,
            int response_code,
            const std::string& content_type = "text/plain",
            bool autodelete = true
        );

        shoutCAST_response(const http_response& b) : http_response(b) { }
    private:
        friend class webserver;
};

class switch_protocol_response : public http_response
{
    public:
        switch_protocol_response() : http_response(this)
        {
        }

        switch_protocol_response(const http_response& b) : http_response(b)
        { 
        }
    private:
        friend class webserver;
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
            bool autodelete = true,
            int keepalive_secs = -1,
            std::string keepalive_msg = ""
        ) : http_response(this, content, response_code, content_type,
                autodelete, "", "", false, topics, keepalive_secs, keepalive_msg
            )
        {
        }

        long_polling_receive_response(const http_response& b) : http_response(b)
        {
        }

        static ssize_t data_generator (void* cls, uint64_t pos,
                char* buf, size_t max
        );

        friend class webserver;
};

class long_polling_send_response : public http_response
{
    public:
        long_polling_send_response
        (
            const std::string& content,
            const std::string& topic,
            bool autodelete = true
        ) : http_response(this, content, 200, "", autodelete, "", "", false,
                std::vector<std::string>(), -1, "", topic
            )
        {
        }

        long_polling_send_response(const http_response& b) : http_response(b)
        {
        }
    private:
        friend class webserver;
};

class cache_response : public http_response
{
    public:
        cache_response
        (
            const std::string& key
        ) : http_response(this, key, 200, "", true, "", "", false,
                std::vector<std::string>(), -1, "", "", 0x0
            )
        {
        }
        cache_response
        (
            cache_entry* ce
        ) : http_response(this, "", 200, "", true, "", "", false,
                std::vector<std::string>(), -1, "", "", ce
            )
        {
            if(ce == 0x0)
                throw bad_caching_attempt();
        }

        cache_response(const http_response& b) : http_response(b) { }
        ~cache_response();
    protected:
        friend class webserver;
};

class deferred_response : public http_response
{
    public:
        deferred_response
        (
        ) : http_response(this)
        {
        }
        deferred_response(const http_response& b) : http_response(b) { }
        virtual ssize_t cycle_callback(const std::string& buf);
    private:
        friend class webserver;
        friend ssize_t details::cb(void*, uint64_t, char*, size_t); 
};

};
#endif
