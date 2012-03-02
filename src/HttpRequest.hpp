#ifndef _http_request_hpp_
#define _http_request_hpp_

#include <map>
#include <vector>
#include <string>
#include "HttpUtils.hpp"

namespace httpserver 
{

class Webserver;

using namespace http;
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

};
#endif
