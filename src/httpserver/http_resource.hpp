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

#ifndef _http_resource_hpp_
#define _http_resource_hpp_
#include <map>
#include <string>

#ifdef DEBUG
#include <iostream>
#endif

namespace httpserver
{

class webserver;
class http_request;
class http_response;

/**
 * Class representing a callable http resource.
**/

void resource_init(std::map<std::string, bool>& res);

template<typename CHILD>
class http_resource 
{
    public:
        /**
         * Class destructor
        **/
        ~http_resource()
        {
        }
        /**
         * Method used to answer to a generic request
         * @param req Request passed through http
         * @return A http_response object
        **/
        void render(const http_request& r, http_response** res)
        {
            static_cast<CHILD*>(this)->render(r, res);
        }
        /**
         * Method used to answer to a GET request
         * @param req Request passed through http
         * @return A http_response object
        **/
        void render_GET(const http_request& req, http_response** res)
        {
            static_cast<CHILD*>(this)->render_GET(req, res);
        }
        /**
         * Method used to answer to a POST request
         * @param req Request passed through http
         * @return A http_response object
        **/
        void render_POST(const http_request& req, http_response** res)
        {
            static_cast<CHILD*>(this)->render_POST(req, res);
        }
        /**
         * Method used to answer to a PUT request
         * @param req Request passed through http
         * @return A http_response object
        **/
        void render_PUT(const http_request& req, http_response** res)
        {
            static_cast<CHILD*>(this)->render_PUT(req, res);
        }
        /**
         * Method used to answer to a HEAD request
         * @param req Request passed through http
         * @return A http_response object
        **/
        void render_HEAD(const http_request& req, http_response** res)
        {
            static_cast<CHILD*>(this)->render_HEAD(req, res);
        }
        /**
         * Method used to answer to a DELETE request
         * @param req Request passed through http
         * @return A http_response object
        **/
        void render_DELETE(const http_request& req, http_response** res)
        {
            static_cast<CHILD*>(this)->render_DELETE(req, res);
        }
        /**
         * Method used to answer to a TRACE request
         * @param req Request passed through http
         * @return A http_response object
        **/
        void render_TRACE(const http_request& req, http_response** res)
        {
            static_cast<CHILD*>(this)->render_TRACE(req, res);
        }
        /**
         * Method used to answer to a OPTIONS request
         * @param req Request passed through http
         * @return A http_response object
        **/
        void render_OPTIONS(const http_request& req, http_response** res)
        {
            static_cast<CHILD*>(this)->render_OPTIONS(req, res);
        }
        /**
         * Method used to answer to a CONNECT request
         * @param req Request passed through http
         * @return A http_response object
        **/
        void render_CONNECT(const http_request& req, http_response** res)
        {
            static_cast<CHILD*>(this)->render_CONNECT(req, res);
        }
        /**
         * Method used to set if a specific method is allowed or not on this request
         * @param method method to set permission on
         * @param allowed boolean indicating if the method is allowed or not
        **/
        void set_allowing(const std::string& method, bool allowed)
        {
            if(this->allowed_methods.count(method)) 
            {
                this->allowed_methods[method] = allowed;
            }
        }
        /**
         * Method used to implicitly allow all methods
        **/
        void allow_all()
        {
            std::map<std::string,bool>::iterator it;
            for ( it=this->allowed_methods.begin() ; it != this->allowed_methods.end(); ++it )
                this->allowed_methods[(*it).first] = true;
        }
        /**
         * Method used to implicitly disallow all methods
        **/
        void disallow_all()
        {
            std::map<std::string,bool>::iterator it;
            for ( it=this->allowed_methods.begin() ; it != this->allowed_methods.end(); ++it )
                this->allowed_methods[(*it).first] = false;
        }
        /**
         * Method used to discover if an http method is allowed or not for this resource
         * @param method Method to discover allowings
         * @return true if the method is allowed
        **/
        bool is_allowed(const std::string& method)
        {
            if(this->allowed_methods.count(method))
            {
                return this->allowed_methods[method];
            }
            else
            {
#ifdef DEBUG
                std::map<std::string, bool>::iterator it;
                for(it = allowed_methods.begin(); it != allowed_methods.end(); ++it)
                {
                    std::cout << (*it).first << " -> " << (*it).second << std::endl;
                }
#endif //DEBUG
                return false;
            }
        }
    protected:
        /**
         * Constructor of the class
        **/
        http_resource()
        {
            resource_init(allowed_methods);
        }
        /**
         * Copy constructor
        **/
        http_resource(const http_resource& b) : allowed_methods(b.allowed_methods) { }

        http_resource& operator = (const http_resource& b)
        {
            allowed_methods = b.allowed_methods;
            return (*this);
        }

    private:
        friend class webserver;
        friend void resource_init(std::map<std::string, bool>& res);
        std::map<std::string, bool> allowed_methods;
};

};
#endif
