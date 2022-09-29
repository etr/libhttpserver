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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_HTTP_RESOURCE_HPP_
#define SRC_HTTPSERVER_HTTP_RESOURCE_HPP_

#ifdef DEBUG
#include <iostream>
#endif

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace httpserver { class http_request; }
namespace httpserver { class http_response; }

namespace httpserver {

namespace details { std::shared_ptr<http_response> empty_render(const http_request& r); }

void resource_init(std::map<std::string, bool>* res);

/**
 * Class representing a callable http resource.
**/
class http_resource {
 public:
     /**
      * Class destructor
     **/
     virtual ~http_resource() = default;

     /**
      * Method used to answer to a generic request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render(const http_request& req) {
         return details::empty_render(req);
     }

     /**
      * Method used to answer to a GET request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_GET(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a POST request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_POST(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a PUT request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_PUT(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a HEAD request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_HEAD(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a DELETE request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_DELETE(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a TRACE request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_TRACE(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a OPTIONS request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_OPTIONS(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a PATCH request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_PATCH(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a CONNECT request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_CONNECT(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to set if a specific method is allowed or not on this request
      * @param method method to set permission on
      * @param allowed boolean indicating if the method is allowed or not
     **/
     void set_allowing(const std::string& method, bool allowed) {
         if (method_state.count(method)) {
             method_state[method] = allowed;
         }
     }

     /**
      * Method used to implicitly allow all methods
     **/
     void allow_all() {
         std::map<std::string, bool>::iterator it;
         for (it=method_state.begin(); it != method_state.end(); ++it) {
             method_state[(*it).first] = true;
         }
     }

     /**
      * Method used to implicitly disallow all methods
     **/
     void disallow_all() {
         std::map<std::string, bool>::iterator it;
         for (it=method_state.begin(); it != method_state.end(); ++it) {
             method_state[(*it).first] = false;
         }
     }

     /**
      * Method used to discover if an http method is allowed or not for this resource
      * @param method Method to discover allowings
      * @return true if the method is allowed
     **/
     bool is_allowed(const std::string& method) {
         if (method_state.count(method)) {
             return method_state[method];
         } else {
#ifdef DEBUG
             std::map<std::string, bool>::iterator it;
             for (it = method_state.begin(); it != method_state.end(); ++it) {
                 std::cout << (*it).first << " -> " << (*it).second << std::endl;
             }
#endif  // DEBUG
             return false;
         }
     }

     /**
      * Method used to return a list of currently allowed HTTP methods for this resource
      * @return vector of strings
     **/
     std::vector<std::string> get_allowed_methods() {
         std::vector<std::string> allowed_methods;

         for (auto it = method_state.cbegin(); it != method_state.cend(); ++it) {
             if ( (*it).second ) {
                 allowed_methods.push_back((*it).first);
             }
         }

         return allowed_methods;
     }

 protected:
     /**
      * Constructor of the class
     **/
     http_resource() {
         resource_init(&method_state);
     }

     /**
      * Copy constructor
     **/
     http_resource(const http_resource& b) = default;
     http_resource(http_resource&& b) noexcept = default;
     http_resource& operator=(const http_resource& b) = default;
     http_resource& operator=(http_resource&& b) = default;

 private:
     friend class webserver;
     friend void resource_init(std::map<std::string, bool>* res);
     std::map<std::string, bool> method_state;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_RESOURCE_HPP_
