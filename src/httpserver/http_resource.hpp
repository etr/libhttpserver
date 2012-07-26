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
class http_resource 
{
    public:
        /**
         * Class destructor
        **/
        virtual ~http_resource();
        /**
         * Method used to answer to a generic request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response render(const http_request& req);
        /**
         * Method used to return a 404 error from the server
         * @return A http_response object containing a 404 error content and responseCode
        **/
        virtual http_response render_404();
        /**
         * Method used to return a 500 error from the server
         * @return A http_response object containing a 500 error content and responseCode
        **/
        virtual http_response render_500();
        /**
         * Method used to return a 405 error from the server
         * @return A http_response object containing a 405 error content and responseCode
        **/
        virtual http_response render_405();
        /**
         * Method used to answer to a GET request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response render_GET(const http_request& req);
        /**
         * Method used to answer to a POST request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response render_POST(const http_request& req);
        /**
         * Method used to answer to a PUT request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response render_PUT(const http_request& req);
        /**
         * Method used to answer to a HEAD request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response render_HEAD(const http_request& req);
        /**
         * Method used to answer to a DELETE request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response render_DELETE(const http_request& req);
        /**
         * Method used to answer to a TRACE request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response render_TRACE(const http_request& req);
        /**
         * Method used to answer to a OPTIONS request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response render_OPTIONS(const http_request& req);
        /**
         * Method used to answer to a CONNECT request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response render_CONNECT(const http_request& req);
        /**
         * Method used to route the request to the correct object method according to the METHOD in the request
         * @param req Request passed through http
         * @return A http_response object
        **/
        virtual http_response route_request(const http_request& req);
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
        http_resource();
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
        std::map<std::string, bool> allowed_methods;
};

};
#endif
