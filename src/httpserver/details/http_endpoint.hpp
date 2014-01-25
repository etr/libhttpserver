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

#ifndef _HTTP_ENDPOINT_HPP_
#define _HTTP_ENDPOINT_HPP_

#include <vector>
#include <utility>
#include <regex.h>
#include <string>

namespace httpserver
{

class webserver;

namespace details
{

struct http_resource_mirror;

/**
 * Exception class throwed when a bad formatted http url is used
**/
class bad_http_endpoint : public std::exception
{
    /**
     * Method used to see error details
     * @return a const char* containing the error message
    **/
    virtual const char* what() const throw()
    {
        return "Bad url format!";
    }
};

/**
 * Class representing an Http Endpoint. It is an abstraction used by the APIs.
**/
class http_endpoint 
{
    private:
        /**
         * Copy constructor. It is useful expecially to copy regex_t structure that contains dinamically allocated data.
         * @param h The http_endpoint to copy
        **/
        http_endpoint(const http_endpoint& h);
        /**
         * Class Destructor
        **/
        ~http_endpoint(); //if inlined it causes problems during ruby wrapper compiling
        /**
         * Operator overload for "less than operator". It is used to order endpoints in maps.
         * @param b The http_endpoint to compare to
         * @return boolean indicating if this is less than b.
        **/
        bool operator <(const http_endpoint& b) const;
        /**
         * Operator overload for "assignment operator". It is used to copy endpoints to existing objects.
         * Is is functional expecially to copy regex_t structure that contains dinamically allocated data.
         * @param h The http_endpoint to copy
         * @return a reference to the http_endpoint obtained
        **/
        http_endpoint& operator =(const http_endpoint& h);
        /**
         * Method indicating if this endpoint 'matches' with the one passed. A passed endpoint matches a registered endpoint if
         * the regex represented by the registered endpoint matches the passed one.
         * @param url The endpoint to match
         * @return true if the passed endpoint matches this.
        **/
        bool match(const http_endpoint& url) const;
        /**
         * Method used to get the complete endpoint url
         * @return a string representing the url
        **/
        const std::string get_url_complete() const
        {
            return this->url_complete;
        }
        /**
         * Method used to get the complete endpoint url
         * @param result a string reference that will be filled with the url
        **/
        void get_url_complete(std::string& result) const
        {
            result = this->url_complete;
        }
        /**
         * Method used to find the size of the complete endpoint url
         * @return the size
        **/
        size_t get_url_complete_size() const
        {
            return this->url_complete.size();
        }
        /**
         * Method used to get all pars defined inside an url.
         * @return a vector of strings representing all found pars.
        **/
        const std::vector<std::string> get_url_pars() const
        {
            return this->url_pars;
        }
        size_t get_url_pars(std::vector<std::string>& result) const
        {
            result = this->url_pars;
            return result.size();
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
         * Method used to get all pieces of an url; considering an url splittet by '/'.
         * @param result a vector of strings to fill with url pieces.
         * @return the size of the vector in output
        **/
        size_t get_url_pieces(std::vector<std::string>& result) const
        {
            result = this->url_pieces;
            return result.size();
        }
        /**
         * Method used to get the number of pieces the url is composed of
         * @return the number of pieces
        **/
        size_t get_url_pieces_num() const
        {
            return this->url_pieces.size();
        }
        /**
         * Method used to get indexes of all parameters inside url
         * @return a vector of int indicating all positions.
        **/
        const std::vector<int> get_chunk_positions() const
        {
            return this->chunk_positions;
        }
        /**
         * Method used to get indexes of all parameters inside url
         * @param result a vector to fill with ints indicating chunk positions
         * @return the size of the vector filled
        **/
        size_t get_chunk_positions(std::vector<int>& result) const
        {
            result = this->chunk_positions;
            return result.size();
        }
        /**
         * Default constructor of the class.
         * @param family boolean that indicates if the endpoint is a family endpoint.
         *                A family endpoint is an endpoint that identifies a root and all its child like the same resource.
         *                For example, if I identify "/path/" like a family endpoint and I associate to it the resource "A", also
         *                "/path/to/res/" is automatically associated to resource "A".
        **/
        http_endpoint(bool family = false):
            url_complete("/"),
            url_modded("/"),
            family_url(family),
            reg_compiled(false)
        {
        }
        /**
         * Constructor of the class http_endpoint. It is used to initialize an http_endpoint starting from a string form URL.
         * @param url The string representation of the endpoint. All endpoints are in the form "/path/to/resource".
         * @param family boolean that indicates if the endpoint is a family endpoint.
         *                A family endpoint is an endpoint that identifies a root and all its child like the same resource.
         *                For example, if I identify "/path/" like a family endpoint and I associate to it the resource "A", also
         *                "/path/to/res/" is automatically associated to resource "A". Default is false.
         * @param registration boolean that indicates to the system if this is an endpoint that need to be registered to a webserver
         *                     or it is simply an endpoint to be used for comparisons. Default is false.
         * @param use_regex boolean that indicates if regexes are checked or not. Default is true.
        **/
        http_endpoint(const std::string& url,
                bool family = false,
                bool registration = false,
                bool use_regex = true
        );
        /**
         * The complete url extracted
        **/
        std::string url_complete;
        /**
         * The url standardized in order to use standard comparisons or regexes
        **/
        std::string url_modded;
        /**
         * Vector containing parameters extracted from url
        **/
        std::vector<std::string> url_pars;
        /**
         * Pieces the url can be splitted into (consider '/' as separator)
        **/
        std::vector<std::string> url_pieces;
        /**
         * Position of url pieces representing parameters
        **/
        std::vector<int> chunk_positions;
        /**
         * Regex used in comparisons
        **/
        regex_t re_url_modded;
        /**
         * Boolean indicating wheter the endpoint represents a family
        **/
        bool family_url;
        /**
         * Boolean indicating if the regex is compiled
        **/
        bool reg_compiled;
        friend class httpserver::webserver;
        friend void _register_resource(
                webserver*,
                const std::string&,
                details::http_resource_mirror&,
                bool
        );
        template<typename, typename> friend struct std::pair;
        template<typename> friend struct std::less;
};

};

};
#endif
