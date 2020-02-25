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

#ifndef _HTTP_RESPONSE_HPP_
#define _HTTP_RESPONSE_HPP_
#include <map>
#include <utility>
#include <string>
#include <iosfwd>
#include <stdint.h>
#include <vector>

#include "httpserver/http_utils.hpp"

struct MHD_Connection;
struct MHD_Response;

namespace httpserver
{

/**
 * Class representing an abstraction for an Http Response. It is used from classes using these apis to send information through http protocol.
**/
class http_response
{
    public:
        http_response():
            response_code(-1)
        {
        }

        explicit http_response(int response_code, const std::string& content_type):
            response_code(response_code)
        {
            headers[http::http_utils::http_header_content_type] = content_type;
        }

        /**
         * Copy constructor
         * @param b The http_response object to copy attributes value from.
        **/
        http_response(const http_response& b):
            response_code(b.response_code),
            headers(b.headers),
            footers(b.footers),
            cookies(b.cookies)
        {
        }

        http_response(http_response&& other) noexcept:
            response_code(other.response_code),
            headers(std::move(other.headers)),
            footers(std::move(other.footers)),
            cookies(std::move(other.cookies))
        {
        }

        http_response& operator=(const http_response& b)
        {
            if (this == &b) return *this;

            this->response_code = b.response_code;
            this->headers = b.headers;
            this->footers = b.footers;
            this->cookies = b.cookies;

            return *this;
        }

        http_response& operator=(http_response&& b)
        {
            if (this == &b) return *this;

            this->response_code = b.response_code;
            this->headers = std::move(b.headers);
            this->footers = std::move(b.footers);
            this->cookies = std::move(b.cookies);

            return *this;
        }

        virtual ~http_response()
        {
        }

        /**
         * Method used to get a specified header defined for the response
         * @param key The header identification
         * @return a string representing the value assumed by the header
        **/
        const std::string& get_header(const std::string& key)
        {
            return headers[key];
        }

        /**
         * Method used to get a specified footer defined for the response
         * @param key The footer identification
         * @return a string representing the value assumed by the footer
        **/
        const std::string& get_footer(const std::string& key)
        {
            return footers[key];
        }

        const std::string& get_cookie(const std::string& key)
        {
            return cookies[key];
        }

        /**
         * Method used to get all headers passed with the request.
         * @return a map<string,string> containing all headers.
        **/
        const std::map<std::string, std::string, http::header_comparator>& get_headers() const
        {
            return headers;
        }

        /**
         * Method used to get all footers passed with the request.
         * @return a map<string,string> containing all footers.
        **/
        const std::map<std::string, std::string, http::header_comparator>& get_footers() const
        {
            return footers;
        }

        const std::map<std::string, std::string, http::header_comparator>& get_cookies() const
        {
            return cookies;
        }

        /**
         * Method used to get the response code from the response
         * @return The response code
        **/
        int get_response_code() const
        {
            return response_code;
        }

        void with_header(const std::string& key, const std::string& value)
        {
            headers[key] = value;
        }

        void with_footer(const std::string& key, const std::string& value)
        {
            footers[key] = value;
        }

        void with_cookie(const std::string& key, const std::string& value)
        {
            cookies[key] = value;
        }

        void shoutCAST();

        virtual MHD_Response* get_raw_response();
        virtual void decorate_response(MHD_Response* response);
        virtual int enqueue_response(MHD_Connection* connection, MHD_Response* response);

    protected:
        std::string content;
        int response_code;

        std::map<std::string, std::string, http::header_comparator> headers;
        std::map<std::string, std::string, http::header_comparator> footers;
        std::map<std::string, std::string, http::header_comparator> cookies;

    	friend std::ostream &operator<< (std::ostream &os, const http_response &r);
};

std::ostream &operator<< (std::ostream &os, const http_response &r);

};
#endif
