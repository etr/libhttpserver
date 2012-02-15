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
#ifndef _FRAMEWORK_WEBSERVER_HPP_
#define _FRAMEWORK_WEBSERVER_HPP_

#ifdef SWIG
%include "std_string.i"
%include "std_vector.i"
%include "exception.i"
%include "stl.i"
%include "std_map.i"

namespace std {
	%template(StringVector) vector<std::string>;
	%template(StringPair) pair<string, string>;
	%template(ArgVector) vector<std::pair<std::string, std::string> >;
}

#ifdef SWIGPHP
	%module(directors="1") libphpwebserver_framework
	%include "typemaps.i"
	%include "cpointer.i"
	%feature("director") Webserver;
	%feature("director") HttpRequest;
	%feature("director") HttpResponse;
	%feature("director") HttpResource;
	%feature("director") HttpEndpoint;
	%feature("director") HttpUtils;
#endif
	    
#ifdef SWIGJAVA
	%javaconst(1);
	%module(directors="1") libjavawebserver_framework
	%include "jstring.i"
	%include "typemaps.i"
	%include "cpointer.i"
	%feature("director") Webserver;
	%feature("director") HttpRequest;
	%feature("director") HttpResponse;
	%feature("director") HttpResource;
	%feature("director") HttpEndpoint;
	%feature("director") HttpUtils;
#endif

#ifdef SWIGPYTHON
	%module(directors="1", threads="1") libpythonwebserver_framework
	%nothread;
	%include "typemaps.i"
	%include "cpointer.i"
	%feature("director") Webserver;
	%feature("director") HttpRequest;
	%feature("director") HttpResponse;
	%feature("director") HttpResource;
	%feature("director") HttpEndpoint;
	%feature("director") HttpUtils;

	%typemap(out) string {
		$result = PyString_FromStringAndSize($1,$1.size);
	}

	%feature("director:except") {
		if ($error != NULL) {
			throw Swig::DirectorMethodException();
		}
	}

	%exception {
		try {
			$action
		} catch (const Swig::DirectorException& e) {
			PyEval_SaveThread();
			PyErr_SetString(PyExc_RuntimeError, e.getMessage());
		}
	};
#endif

%include "HttpUtils.hpp"

%template(SQMHeaders) std::map<std::string, std::string>;    

%extend std::map<std::string, std::string> {
	std::string getHeader(std::string key)  {
		std::map<std::string,std::string >::iterator i = self->find(key);
		if (i != self->end())
			return i->second;
		else
			return "";    
	}    
};

%exception {
	try {
	    $action
	} catch (const std::out_of_range& e) {
	#ifdef SWIGPYTHON
	    PyEval_SaveThread();
	#endif
	    SWIG_exception(SWIG_IndexError,const_cast<char*>(e.what()));
	} catch (const std::exception& e) {
	#ifdef SWIGPYTHON
	    PyEval_SaveThread();
	#endif
	    SWIG_exception(SWIG_RuntimeError, e.what());
	} catch (...) {
	#ifdef SWIGPYTHON
	    PyEval_SaveThread();
	#endif
	    SWIG_exception(SWIG_RuntimeError, "Generic SWIG Exception");
	}
}

%ignore policyCallback (void*, const struct sockaddr*, socklen_t);
%ignore error_log(void*, const char*, va_list);
%ignore access_log(Webserver*, std::string);
%ignore uri_log(void*, const char*);
%ignore unescaper_func(void*, struct MHD_Connection*, char*);
%ignore internal_unescaper(void*, struct MHD_Connection*, char*);

%{
#include "Webserver.hpp"
%}

#endif

#define NOT_FOUND_ERROR "{\"description\":\"NOT FOUND\"}"
#define METHOD_ERROR "{\"description\":\"METHOD NOT ALLOWED\"}"
#define NOT_METHOD_ERROR "{\"description\":\"METHOD NOT ACCEPTABLE\"}"
#define GENERIC_ERROR "{\"description\":\"INTERNAL ERROR\"}"

#define DEFAULT_DROP_WS_PORT 9898
#define DEFAULT_DROP_WS_TIMEOUT 180

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <microhttpd.h>
#include <string.h>
#include <map>
#include <vector>
#include <string>
#include <utility>
#include <regex.h>
#include <memory>
//#include <boost/xpressive/xpressive.hpp>

#include "HttpUtils.hpp"

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
		HttpEndpoint(bool family = false);
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
        ~HttpEndpoint();
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
		 * Method used to get all pars defined inside an url.
		 * @return a vector of strings representing all found pars.
		**/
		const std::vector<std::string> get_url_pars() const;
		/**
		 * Method used to get all pieces of an url; considering an url splitted by '/'.
		 * @return a vector of strings representing all found pieces.
		**/
		const std::vector<std::string> get_url_pieces() const;
		/**
		 * Method used to get indexes of all parameters inside url
		 * @return a vector of int indicating all positions.
		**/
		const std::vector<int> get_chunk_positions() const;
	private:
		std::string url_complete;
		std::string url_modded;
		std::vector<std::string> url_pars;
		std::vector<std::string> url_pieces;
		std::vector<int> chunk_positions;
        regex_t re_url_modded;
//		boost::xpressive::sregex re_url_modded;
		bool family_url;
        bool reg_compiled;
};

/**
 * Class representing an abstraction for an Http Request. It is used from classes using these apis to receive information through http protocol.
**/
class HttpRequest 
{
	public:
		/**
		 * Default constructor of the class. It is a specific responsibility of apis to initialize this type of objects.
		**/
		HttpRequest();
		/**
		 * Copy constructor.
		 * @param b HttpRequest b to copy attributes from.
		**/
		HttpRequest(const HttpRequest& b);
		/**
		 * Method used to get the username eventually passed through basic authentication.
		 * @return string representation of the username.
		**/
		const std::string getUser() const;
		/**
		 * Method used to get the password eventually passed through basic authentication.
		 * @return string representation of the password.
		**/
		const std::string getPass() const;
		/**
		 * Method used to get the path requested
		 * @return string representing the path requested.
		**/
		const std::string getPath() const;
		/**
		 * Method used to get all pieces of the path requested; considering an url splitted by '/'.
		 * @return a vector of strings containing all pieces
		**/
		const std::vector<std::string> getPathPieces() const;
		/**
		 * Method used to obtain the size of path in terms of pieces; considering an url splitted by '/'.
		 * @return an integer representing the number of pieces
		**/
		int getPathPiecesSize() const;
		/**
		 * Method used to obtain a specified piece of the path; considering an url splitted by '/'.
		 * @return the selected piece in form of string
		**/
		const std::string getPathPiece(int index) const;
		/**
		 * Method used to get the METHOD used to make the request.
		 * @return string representing the method.
		**/
		const std::string getMethod() const;
		/**
		 * Method used to get all headers passed with the request.
		 * @return a vector<pair<string,string> > containing all headers.
		**/
		const std::vector<std::pair<std::string, std::string> > getHeaders() const;
		/**
		 * Method used to get all footers passed with the request.
		 * @return a vector<pair<string,string> > containing all footers.
		**/
		const std::vector<std::pair<std::string, std::string> > getFooters() const;
        /**
         * Method used to get all cookies passed with the request.
         * @return a vector<pair<string, string> > containing all cookies.
        **/
        const std::vector<std::pair<std::string, std::string> > getCookies() const;
		/**
		 * Method used to get all parameters passed with the request. Usually parameters are passed with DELETE or GET methods.
		 * @return a map<string,string> containing all parameters.
		**/
		const std::vector<std::pair<std::string, std::string> > getArgs() const;
		/**
		 * Method used to get a specific header passed with the request.
		 * @param key the specific header to get the value from
		 * @return the value of the header.
		**/
		const std::string getHeader(const std::string& key) const;
		/**
		 * Method used to get a specific footer passed with the request.
		 * @param key the specific footer to get the value from
		 * @return the value of the footer.
		**/
		const std::string getFooter(const std::string& key) const;
		/**
		 * Method used to get a specific argument passed with the request.
		 * @param ket the specific argument to get the value from
		 * @return the value of the arg.
		**/
		const std::string getArg(const std::string& key) const;
		/**
		 * Method used to get the content of the request.
		 * @return the content in string representation
		**/
		const std::string getContent() const;
		/**
		 * Method used to get the version of the request.
		 * @return the version in string representation
		**/
		const std::string getVersion() const;
		/**
		 * Method used to get the requestor.
		 * @return the requestor
		**/
		const std::string getRequestor() const;
		/**
		 * Method used to get the requestor port used.
		 * @return the requestor port
		**/
		short getRequestorPort() const;
		/**
		 * Method used to set an header value by key.
		 * @param key The name identifying the header
		 * @param value The value assumed by the header
		**/
		void setHeader(const std::string& key, const std::string& value);
		/**
		 * Method used to set a footer value by key.
		 * @param key The name identifying the footer
		 * @param value The value assumed by the footer
		**/
		void setFooter(const std::string& key, const std::string& value);
		/**
		 * Method used to set a cookie value by key.
		 * @param key The name identifying the cookie
		 * @param value The value assumed by the cookie
		**/
        void setCookie(const std::string& key, const std::string& value);
		/**
		 * Method used to set an argument value by key.
		 * @param key The name identifying the argument
		 * @param value The value assumed by the argument
		**/
		void setArg(const std::string& key, const std::string& value);
		/**
		 * Method used to set an argument value by key.
		 * @param key The name identifying the argument
		 * @param value The value assumed by the argument
		 * @param size The size in number of char of the value parameter.
		**/
		void setArg(const char* key, const char* value, size_t size);
		/**
		 * Method used to set the content of the request
		 * @param content The content to set.
		**/
		void setContent(const std::string& content);
		/**
		 * Method used to append content to the request preserving the previous inserted content
		 * @param content The content to append.
		 * @param size The size of the data to append.
		**/
		void growContent(const char* content, size_t size);
		/**
		 * Method used to set the path requested.
		 * @param path The path searched by the request.
		**/
		void setPath(const std::string& path);
		/**
		 * Method used to set the request METHOD
		 * @param method The method to set for the request
		**/
		void setMethod(const std::string& method);
		/**
		 * Method used to set the request http version (ie http 1.1)
		 * @param version The version to set in form of string
		**/
		void setVersion(const std::string& version);
		/**
		 * Method used to set the requestor
		 * @param requestor The requestor to set
		**/
		void setRequestor(const std::string& requestor);
		/**
		 * Method used to set the requestor port
		 * @param requestor The requestor port to set
		**/
		void setRequestorPort(short requestor);
		/**
		 * Method used to remove an header previously inserted
		 * @param key The key identifying the header to remove.
		**/
		void removeHeader(const std::string& key);
		/**
		 * Method used to set all headers of the request.
		 * @param headers The headers key-value map to set for the request.
		**/
		void setHeaders(const std::map<std::string, std::string>& headers);
		/**
		 * Method used to set all footers of the request.
		 * @param footers The footers key-value map to set for the request.
		**/
		void setFooters(const std::map<std::string, std::string>& footers);
		/**
		 * Method used to set all cookies of the request.
		 * @param cookies The cookies key-value map to set for the request.
		**/
		void setCookies(const std::map<std::string, std::string>& cookies);
		/**
		 * Method used to set all arguments of the request.
		 * @param args The args key-value map to set for the request.
		**/
		void setArgs(const std::map<std::string, std::string>& args);
		/**
		 * Method used to set the username of the request.
		 * @param user The username to set.
		**/
		void setUser(const std::string& user);
		/**
		 * Method used to set the password of the request.
		 * @param pass The password to set.
		**/
		void setPass(const std::string& pass);
	private:
		friend class Webserver;
		std::string user;
		std::string pass;
		std::string path;
		std::string method;
		std::vector<std::string> post_path;
		std::map<std::string, std::string, HeaderComparator> headers;
		std::map<std::string, std::string, HeaderComparator> footers;
		std::map<std::string, std::string, HeaderComparator> cookies;
		std::map<std::string, std::string, ArgComparator> args;
		std::string content;
		std::string version;
		std::string requestor;
		short requestorPort;
};

/**
 * Class representing an abstraction for an Http Response. It is used from classes using these apis to send information through http protocol.
**/
class HttpResponse 
{
	public:
		/**
		 * Enumeration indicating whether the response content is got from a string or from a file
		**/
		enum ResponseType_T 
		{
			STRING_CONTENT = 0,
			FILE_CONTENT
		};
		
		/**
		 * Default constructor
		**/
		HttpResponse();
		/**
		 * Constructor used to build an HttpResponse with a content and a responseCode
		 * @param content The content to set for the request. (if the responseType is FILE_CONTENT, it represents the path to the file to read from).
		 * @param responseCode The response code to set for the request.
		 * @param responseType param indicating if the content have to be read from a string or from a file
		**/
		HttpResponse
		(
			const std::string& content, 
			int responseCode,
			const std::string& contentType = "application/json",
			const HttpResponse::ResponseType_T& responseType = HttpResponse::STRING_CONTENT
		);
		void HttpResponseInit
		(
			const std::string& content, 
			int responseCode,
			const std::string& contentType = "application/json",
			const HttpResponse::ResponseType_T& responseType = HttpResponse::STRING_CONTENT
		);
		/**
		 * Copy constructor
		 * @param b The HttpResponse object to copy attributes value from.
		**/
		HttpResponse(const HttpResponse& b);
		/**
		 * Method used to get the content from the response.
		 * @return the content in string form
		**/
		const std::string getContent();
		/**
		 * Method used to set the content of the response
		 * @param content The content to set
		**/
		void setContent(const std::string& content);
		/**
		 * Method used to get a specified header defined for the response
		 * @param key The header identification
		 * @return a string representing the value assumed by the header
		**/
		const std::string getHeader(const std::string& key);
		/**
		 * Method used to get a specified footer defined for the response
		 * @param key The footer identification
		 * @return a string representing the value assumed by the footer
		**/
		const std::string getFooter(const std::string& key);
		/**
		 * Method used to set an header value by key.
		 * @param key The name identifying the header
		 * @param value The value assumed by the header
		**/
		void setHeader(const std::string& key, const std::string& value);
		/**
		 * Method used to set a footer value by key.
		 * @param key The name identifying the footer
		 * @param value The value assumed by the footer
		**/
		void setFooter(const std::string& key, const std::string& value);
        /**
         * Method used to set a cookie value by key.
         * @param key The name identifying the cookie
         * @param value The value assumed by the cookie
        **/
        void setCookie(const std::string& key, const std::string& value);
		/**
		 * Method used to set the content type for the request. This is a shortcut of setting the corresponding header.
		 * @param contentType the content type to use for the request
		**/
		void setContentType(const std::string& contentType);
		/**
		 * Method used to remove previously defined header
		 * @param key The header to remove
		**/
		void removeHeader(const std::string& key);
		/**
		 * Method used to get all headers passed with the request.
		 * @return a map<string,string> containing all headers.
		**/
		const std::vector<std::pair<std::string, std::string> > getHeaders();
		/**
		 * Method used to get all footers passed with the request.
		 * @return a map<string,string> containing all footers.
		**/
		const std::vector<std::pair<std::string, std::string> > getFooters();
		/**
		 * Method used to get all cookies passed with the request.
		 * @return a map<string,string> containing all cookies.
		**/
        const std::vector<std::pair<std::string, std::string> > getCookies();
		/**
		 * Method used to set all headers of the response.
		 * @param headers The headers key-value map to set for the response.
		**/
		void setHeaders(const std::map<std::string, std::string>& headers);
		/**
		 * Method used to set all footers of the response.
		 * @param footers The footers key-value map to set for the response.
		**/
		void setFooters(const std::map<std::string, std::string>& footers);
		/**
		 * Method used to set all cookies of the response.
		 * @param cookies The cookies key-value map to set for the response.
		**/
        void setCookies(const std::map<std::string, std::string>& cookies);
		/**
		 * Method used to set the response code of the response
		 * @param responseCode the response code to set
		**/
		void setResponseCode(int responseCode);
		/**
		 * Method used to get the response code from the response
		 * @return The response code
		**/
		int getResponseCode();
	private:
		friend class Webserver;
		std::string content;
		ResponseType_T responseType;
		int responseCode;
		std::map<std::string, std::string, HeaderComparator> headers;
		std::map<std::string, std::string, ArgComparator> footers;
		std::map<std::string, std::string, HeaderComparator> cookies;
		int fp;
		std::string filename;
};

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
		void setAllowing(const std::string& method, bool allowed);
		/**
		 * Method used to implicitly allow all methods
		**/
		void allowAll();
		/**
		 * Method used to implicitly disallow all methods
		**/
		void disallowAll();
		/**
		 * Method used to discover if an http method is allowed or not for this resource
		 * @param method Method to discover allowings
		 * @return true if the method is allowed
		**/
		bool isAllowed(const std::string& method);
	private:
		friend class Webserver;
		std::map<std::string, bool> allowedMethods;
};

/**
 * Delegate class used to wrap callbacks dedicated to logging.
**/
class LoggingDelegate
{
	public:
		/**
		 * Delegate constructor.
		**/
		LoggingDelegate();
		/**
		 * Destructor of the class
		**/
		virtual ~LoggingDelegate();
		/**
		 * Method used to log access to the webserver.
		 * @param s string to log
		**/
		virtual void log_access(const std::string& s) const;
		/**
		 * Method used to log errors on the webserver.
		 * @param s string to log
		**/
		virtual void log_error(const std::string& s) const;
};

/**
 * Delegate class used to validate requests before serve it
**/
class RequestValidator
{
	public:
		/**
		 * Delegate constructor
		**/
		RequestValidator();
		/**
		 * Destructor of the class
		**/
		virtual ~RequestValidator();
		/**
		 * Method used to validate a request. The validation method is entirely based upon the requestor address.
		 * @param address The requestor address
		 * @return true if the request is considered to be valid, false otherwise.
		**/
		virtual bool validate(const std::string& address) const;
};

/**
 * Delegate class used to unescape requests uri before serving it.
**/
class Unescaper
{
	public:
		/**
		 * Delegate constructor
		**/
		Unescaper();
		/**
		 * Destructor of the class
		**/
		virtual ~Unescaper();
		/**
		 * Method used to unescape the uri.
		 * @param s pointer to the uri string representation to unescape.
		**/
		virtual void unescape(char* s) const;
};

/**
 * Class representing the webserver. Main class of the apis.
**/
class Webserver 
{
	public:
		/**
		 * Constructor of the class.
		 * @param port Integer representing the port the webserver is listening on.
		 * @param maxThreads max number of serving threads (0 -> infty)
		 * @param maxConnections max number of allowed connections (0 -> infty).
		 * @param memoryLimit max memory allocated to serve requests (0 -> infty).
		 * @param perIPConnectionLimit max number of connections allowed for a single IP (0 -> infty).
		 * @param logDelegate LoggingDelegate object used to log
		 * @param validator RequestValidator object used to validate requestors
		 * @param unescaper Unescaper object used to unescape urls.
		 * @param maxThreadStackSize max dimesion of request stack
		 * @param httpsMemKey used with https. Private key used for requests.
		 * @param httpsMemCert used with https. Certificate used for requests.
		 * @param httpsMemTrust used with https. CA Certificate used to trust request certificates.
		 * @param httpsPriorities used with https. Determinates the SSL/TLS version to be used.
		 * @param credType used with https. Daemon credential type. Can be NONE, certificate or anonymous.
		 * @param digestAuthRandom used with https. Digest authentication nonce's seed.
		 * @param nonceNcSize used with https. Size of an array of nonce and nonce counter map.
		**/
		Webserver
		(
			const int port = DEFAULT_DROP_WS_PORT, 
			const int maxThreads = 0, 
			const int maxConnections = 0,
			const int memoryLimit = 0,
			const int connectionTimeout = 0,
			const int perIPConnectionLimit = 0,
			const LoggingDelegate* logDelegate = 0x0,
			const RequestValidator* validator = 0x0,
			const Unescaper* unescaper = 0x0,
			const int maxThreadStackSize = 0,
			const std::string& httpsMemKey = "",
			const std::string& httpsMemCert = "",
			const std::string& httpsMemTrust = "",
			const std::string& httpsPriorities = "",
			const HttpUtils::CredType_T credType= HttpUtils::NONE,
			const std::string digestAuthRandom = "", //IT'S CORRECT TO PASS THIS PARAMETER BY VALUE
			const int nonceNcSize = 0
		);
		/**
		 * Destructor of the class
		**/
		~Webserver();
#ifdef SWIG
		%thread;
#endif
		/**
		 * Method used to start the webserver.
		 * This method can be blocking or not.
		 * @param blocking param indicating if the method is blocking or not
		 * @return a boolean indicating if the webserver is running or not.
		**/
		bool start(bool blocking = false);
#ifdef SWIG
		%nothread;
#endif
		/**
		 * Method used to stop the webserver.
		 * @return true if the webserver is stopped.
		**/
		bool stop();
		/**
		 * Method used to evaluate if the server is running or not.
		 * @return true if the webserver is running
		**/
		bool isRunning();
		/**
		 * Method used to registrate a resource to the webserver.
		 * @param resource The url pointing to the resource. This url could be also parametrized in the form /path/to/url/{par1}/and/{par2}
		 *                 or a regular expression.
		 * @param http_resource HttpResource pointer to register.
		 * @param family boolean indicating whether the resource is registered for the endpoint and its child or not.
		**/
		void registerResource(const std::string& resource, HttpResource* http_resource, bool family = false);
		/**
		 * Method used to kill the webserver waiting for it to terminate
		**/
		void sweetKill();
	private:
		bool running;
		int port;
		int maxThreads;
		int maxConnections;
		int memoryLimit;
		int connectionTimeout;
		int perIPConnectionLimit;
		int maxThreadStackSize;
		int nonceNcSize;
		const LoggingDelegate* logDelegate;
		const RequestValidator* validator;
		const Unescaper* unescaper;
		std::string httpsMemKey;
		std::string httpsMemCert;
		std::string httpsMemTrust;
		std::string httpsPriorities;
		std::string digestAuthRandom;
		HttpUtils::CredType_T credType;

		std::map<HttpEndpoint, HttpResource* > registeredResources;
		struct MHD_Daemon *daemon;
		static int not_found_page 
		(
			const void *cls,
			struct MHD_Connection *connection
		);
		static int method_not_acceptable_page 
		(
			const void *cls,
			struct MHD_Connection *connection
		);
		static void requestCompleted(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe);
		static int buildRequestHeader (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
		static int buildRequestFooter (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
		static int buildRequestCookie (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
		static int buildRequestArgs (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
		static int answerToConnection
		(
			void* cls, MHD_Connection* connection,
			const char* url, const char* method,
			const char* version, const char* upload_data,
			size_t* upload_data_size, void** con_cls
		);
		static int post_iterator 
		(
			void *cls,
			enum MHD_ValueKind kind,
			const char *key,
			const char *filename,
			const char *content_type,
			const char *transfer_encoding,
			const char *data, uint64_t off, size_t size
		);

		friend int policyCallback (void *cls, const struct sockaddr* addr, socklen_t addrlen);
		friend void error_log(void* cls, const char* fmt, va_list ap);
		friend void access_log(Webserver* cls, std::string uri);
		friend void* uri_log(void* cls, const char* uri);
		friend size_t unescaper_func(void * cls, struct MHD_Connection *c, char *s);
		friend size_t internal_unescaper(void * cls, struct MHD_Connection *c, char *s);
};

struct ModdedRequest
{
	struct MHD_PostProcessor *pp;
	std::string* completeUri;
	HttpRequest *dhr;
	bool second;
};

#endif //_FRAMEWORK_WEBSERVER_HPP__
