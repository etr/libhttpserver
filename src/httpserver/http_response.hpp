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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef _HTTP_RESPONSE_HPP_
#define _HTTP_RESPONSE_HPP_
#include <map>
#include <utility>
#include <string>
#include <iosfwd>
#include <stdint.h>
#include <vector>

#include "httpserver/binders.hpp"

struct MHD_Connection;

namespace httpserver {

class webserver;
class http_response_builder;

namespace http {
    class header_comparator;
    class arg_comparator;
} // namespace http

namespace details {
    struct http_response_ptr;
    ssize_t cb(void*, uint64_t, char*, size_t);
    struct cache_entry;
} // namespace details

class bad_caching_attempt: public std::exception
{
    virtual const char* what() const throw()
    {
        return "You cannot pass ce = 0x0 without key!";
    }
};

typedef ssize_t(*cycle_callback_ptr)(char*, size_t);

/**
 * Class representing an abstraction for an Http Response. It is used from classes using these apis to send information through http protocol.
**/
class http_response
{
    public:

        http_response(const http_response_builder& builder);

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
            ce(b.ce),
            cycle_callback(b.cycle_callback),
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
         * Method used to get all headers passed with the request.
         * @return a map<string,string> containing all headers.
        **/
        size_t get_headers(
                std::map<std::string, std::string, http::header_comparator>& result
        ) const;

        /**
         * Method used to get all footers passed with the request.
         * @return a map<string,string> containing all footers.
        **/
        size_t get_footers(
                std::map<std::string, std::string, http::header_comparator>& result
        ) const;

        size_t get_cookies(
                std::map<std::string, std::string, http::header_comparator>& result
        ) const;

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

        bool is_autodelete() const
        {
            return autodelete;
        }

        size_t get_topics(std::vector<std::string>& topics) const
        {
            typedef std::vector<std::string>::const_iterator topics_it;
            for(topics_it it=this->topics.begin();it != this->topics.end();++it)
                topics.push_back(*it);
            return topics.size();
        }
    protected:
        typedef details::binders::functor_two<MHD_Response**, webserver*, void> get_raw_response_t;

        typedef details::binders::functor_one<MHD_Response*, void> decorate_response_t;

        typedef details::binders::functor_two<MHD_Connection* ,MHD_Response*, int> enqueue_response_t;

        std::string content;
        int response_code;
        bool autodelete;
        std::string realm;
        std::string opaque;
        bool reload_nonce;
        int fp;
        std::string filename;
        std::map<std::string, std::string, http::header_comparator> headers;
        std::map<std::string, std::string, http::header_comparator> footers;
        std::map<std::string, std::string, http::header_comparator> cookies;
        std::vector<std::string> topics;
        int keepalive_secs;
        std::string keepalive_msg;
        std::string send_topic;
        struct MHD_Connection* underlying_connection;
        details::cache_entry* ce;
        cycle_callback_ptr cycle_callback;

        const get_raw_response_t get_raw_response;
        const decorate_response_t decorate_response;
        const enqueue_response_t enqueue_response;

        bool completed;

        webserver* ws;
        MHD_Connection* connection_id;

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
        friend class http_response_builder;
        friend void clone_response(const http_response& hr, http_response** dhr);
        friend ssize_t details::cb(void* cls, uint64_t pos, char* buf, size_t max);
        friend std::ostream &operator<< (std::ostream &os, const http_response &r);
    private:
        http_response& operator=(const http_response& b);

        static ssize_t data_generator (void* cls, uint64_t pos, char* buf, size_t max);
};

std::ostream &operator<< (std::ostream &os, const http_response &r);

} // namespace httpserver
#endif
