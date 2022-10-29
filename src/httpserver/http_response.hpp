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

#ifndef SRC_HTTPSERVER_HTTP_RESPONSE_HPP_
#define SRC_HTTPSERVER_HTTP_RESPONSE_HPP_

#include <iosfwd>
#include <map>
#include <string>
#include "httpserver/http_utils.hpp"

struct MHD_Connection;
struct MHD_Response;

namespace httpserver {

/**
 * Class representing an abstraction for an Http Response. It is used from classes using these apis to send information through http protocol.
**/
class http_response {
 public:
     http_response() = default;

     explicit http_response(int response_code, const std::string& content_type):
         response_code(response_code) {
             headers[http::http_utils::http_header_content_type] = content_type;
     }

     /**
      * Copy constructor
      * @param b The http_response object to copy attributes value from.
     **/
     http_response(const http_response& b) = default;
     http_response(http_response&& b) noexcept = default;

     http_response& operator=(const http_response& b) = default;
     http_response& operator=(http_response&& b) noexcept = default;

     virtual ~http_response() = default;

     /**
      * Method used to get a specified header defined for the response
      * @param key The header identification
      * @return a string representing the value assumed by the header
     **/
     const std::string& get_header(const std::string& key) {
         return headers[key];
     }

     /**
      * Method used to get a specified footer defined for the response
      * @param key The footer identification
      * @return a string representing the value assumed by the footer
     **/
     const std::string& get_footer(const std::string& key) {
         return footers[key];
     }

     const std::string& get_cookie(const std::string& key) {
         return cookies[key];
     }

     /**
      * Method used to get all headers passed with the request.
      * @return a map<string,string> containing all headers.
     **/
     const std::map<std::string, std::string, http::header_comparator>& get_headers() const {
         return headers;
     }

     /**
      * Method used to get all footers passed with the request.
      * @return a map<string,string> containing all footers.
     **/
     const std::map<std::string, std::string, http::header_comparator>& get_footers() const {
         return footers;
     }

     const std::map<std::string, std::string, http::header_comparator>& get_cookies() const {
         return cookies;
     }

     /**
      * Method used to get the response code from the response
      * @return The response code
     **/
     int get_response_code() const {
         return response_code;
     }

     void with_header(const std::string& key, const std::string& value) {
         headers[key] = value;
     }

     void with_footer(const std::string& key, const std::string& value) {
         footers[key] = value;
     }

     void with_cookie(const std::string& key, const std::string& value) {
         cookies[key] = value;
     }

     void shoutCAST();

     virtual MHD_Response* get_raw_response();
     virtual void decorate_response(MHD_Response* response);
     virtual int enqueue_response(MHD_Connection* connection, MHD_Response* response);

 private:
     int response_code = -1;

     http::header_map headers;
     http::header_map footers;
     http::header_map cookies;

 protected:
     friend std::ostream &operator<< (std::ostream &os, const http_response &r);
};

std::ostream &operator<<(std::ostream &os, const http_response &r);

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_RESPONSE_HPP_
