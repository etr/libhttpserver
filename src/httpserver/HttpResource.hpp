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

namespace httpserver
{

class Webserver;
class HttpRequest;
class HttpResponse;

/**
 * Class representing a callable http resource.
**/
class HttpResource 
{
    public:
        /**
         * Constructor of the class
        **/
        HttpResource();
        /**
         * Class destructor
        **/
        virtual ~HttpResource();
        /**
         * Method used to answer to a generic request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse render(const HttpRequest& req);
        /**
         * Method used to return a 404 error from the server
         * @return A HttpResponse object containing a 404 error content and responseCode
        **/
        virtual HttpResponse render_404();
        /**
         * Method used to return a 500 error from the server
         * @return A HttpResponse object containing a 500 error content and responseCode
        **/
        virtual HttpResponse render_500();
        /**
         * Method used to return a 405 error from the server
         * @return A HttpResponse object containing a 405 error content and responseCode
        **/
        virtual HttpResponse render_405();
        /**
         * Method used to answer to a GET request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse render_GET(const HttpRequest& req);
        /**
         * Method used to answer to a POST request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse render_POST(const HttpRequest& req);
        /**
         * Method used to answer to a PUT request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse render_PUT(const HttpRequest& req);
        /**
         * Method used to answer to a HEAD request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse render_HEAD(const HttpRequest& req);
        /**
         * Method used to answer to a DELETE request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse render_DELETE(const HttpRequest& req);
        /**
         * Method used to answer to a TRACE request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse render_TRACE(const HttpRequest& req);
        /**
         * Method used to answer to a OPTIONS request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse render_OPTIONS(const HttpRequest& req);
        /**
         * Method used to answer to a CONNECT request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse render_CONNECT(const HttpRequest& req);
        /**
         * Method used to route the request to the correct object method according to the METHOD in the request
         * @param req Request passed through http
         * @return A HttpResponse object
        **/
        virtual HttpResponse routeRequest(const HttpRequest& req);
        /**
         * Method used to set if a specific method is allowed or not on this request
         * @param method method to set permission on
         * @param allowed boolean indicating if the method is allowed or not
        **/
        void setAllowing(const std::string& method, bool allowed)
        {
            if(this->allowedMethods.count(method)) 
            {
                this->allowedMethods[method] = allowed;
            }
        }
        /**
         * Method used to implicitly allow all methods
        **/
        void allowAll()
        {
            std::map<std::string,bool>::iterator it;
            for ( it=this->allowedMethods.begin() ; it != this->allowedMethods.end(); it++ )
                this->allowedMethods[(*it).first] = true;
        }
        /**
         * Method used to implicitly disallow all methods
        **/
        void disallowAll()
        {
            std::map<std::string,bool>::iterator it;
            for ( it=this->allowedMethods.begin() ; it != this->allowedMethods.end(); it++ )
                this->allowedMethods[(*it).first] = false;
        }
        /**
         * Method used to discover if an http method is allowed or not for this resource
         * @param method Method to discover allowings
         * @return true if the method is allowed
        **/
        bool isAllowed(const std::string& method)
        {
            if(this->allowedMethods.count(method))
            {
                return this->allowedMethods[method];
            }
            else
            {
                return false;
            }
        }
    private:
        friend class Webserver;
        std::map<std::string, bool> allowedMethods;
};

};
#endif
