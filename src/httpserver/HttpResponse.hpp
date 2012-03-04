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
#ifndef _http_response_hpp_
#define _http_response_hpp_
#include <map>
#include <utility>
#include <string>

namespace httpserver
{

class Webserver;

namespace http
{
    class HeaderComparator;
    class ArgComparator;
};

using namespace http;

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
		int fp;
		std::string filename;
};

};
#endif
