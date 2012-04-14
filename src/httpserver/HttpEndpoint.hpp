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
#ifndef _http_endpoint_hpp_
#define _http_endpoint_hpp_

#include <vector>
#include <regex.h>
#include <string>

namespace httpserver
{

class Webserver;

/**
 * Class representing an Http Endpoint. It is an abstraction used by the APIs.
**/
class HttpEndpoint 
{
    public:
        /**
         * Default constructor of the class.
         * @param family boolean that indicates if the endpoint is a family endpoint.
         *                A family endpoint is an endpoint that identifies a root and all its child like the same resource.
         *                For example, if I identify "/path/" like a family endpoint and I associate to it the resource "A", also
         *                "/path/to/res/" is automatically associated to resource "A".
        **/
        HttpEndpoint(bool family = false):
            url_complete("/"),
            url_modded("/"),
            family_url(family),
            reg_compiled(false)
        {
        }
        /**
         * Constructor of the class HttpEndpoint. It is used to initialize an HttpEndpoint starting from a string form URL.
         * @param url The string representation of the endpoint. All endpoints are in the form "/path/to/resource".
         * @param family boolean that indicates if the endpoint is a family endpoint.
         *                A family endpoint is an endpoint that identifies a root and all its child like the same resource.
         *                For example, if I identify "/path/" like a family endpoint and I associate to it the resource "A", also
         *                "/path/to/res/" is automatically associated to resource "A".
         * @param registration boolean that indicates to the system if this is an endpoint that need to be registered to a webserver
         *                     or it is simply an endpoint to be used for comparisons.
        **/
        HttpEndpoint(const std::string& url, bool family = false, bool registration = false);
        /**
         * Copy constructor. It is useful expecially to copy regex_t structure that contains dinamically allocated data.
         * @param h The HttpEndpoint to copy
        **/
        HttpEndpoint(const HttpEndpoint& h);
        /**
         * Destructor of the class. Essentially it frees the regex dinamically allocated pattern
        **/
        ~HttpEndpoint()
        {
            
            if(reg_compiled)
            {
                regfree(&(this->re_url_modded));
            }
            
        }
        /**
         * Operator overload for "less than operator". It is used to order endpoints in maps.
         * @param b The HttpEndpoint to compare to
         * @return boolean indicating if this is less than b.
        **/
        bool operator <(const HttpEndpoint& b) const;
        /**
         * Operator overload for "assignment operator". It is used to copy endpoints to existing objects.
         * Is is functional expecially to copy regex_t structure that contains dinamically allocated data.
         * @param h The HttpEndpoint to copy
         * @return a reference to the HttpEndpoint obtained
        **/
        HttpEndpoint& operator =(const HttpEndpoint& h);
        /**
         * Method indicating if this endpoint 'matches' with the one passed. A passed endpoint matches a registered endpoint if
         * the regex represented by the registered endpoint matches the passed one.
         * @param url The endpoint to match
         * @return true if the passed endpoint matches this.
        **/
        bool match(const HttpEndpoint& url) const;
        /**
         * Method used to get the complete endpoint url
         * @return a string representing the url
        **/
        const std::string get_url_complete() const
        {
            return this->url_complete;
        }
        /**
         * Method used to get all pars defined inside an url.
         * @return a vector of strings representing all found pars.
        **/
        const std::vector<std::string> get_url_pars() const
        {
            return this->url_pars;
        }
        /**
         * Method used to get all pieces of an url; considering an url splitted by '/'.
         * @return a vector of strings representing all found pieces.
        **/
        const std::vector<std::string> get_url_pieces() const
        {
            return this->url_pieces;
        }
        /**
         * Method used to get indexes of all parameters inside url
         * @return a vector of int indicating all positions.
        **/
        const std::vector<int> get_chunk_positions() const
        {
            return this->chunk_positions;
        }
    private:
        std::string url_complete;
        std::string url_modded;
        std::vector<std::string> url_pars;
        std::vector<std::string> url_pieces;
        std::vector<int> chunk_positions;
        regex_t re_url_modded;
//      boost::xpressive::sregex re_url_modded;
        bool family_url;
        bool reg_compiled;
};

};
#endif
