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
#ifndef _http_request_hpp_
#define _http_request_hpp_

#include <map>
#include <vector>
#include <string>
#include <utility>

struct MHD_Connection;

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
 * Class representing an abstraction for an Http Request. It is used from classes using these apis to receive information through http protocol.
**/
class HttpRequest 
{
    public:
        /**
         * Default constructor of the class. It is a specific responsibility of apis to initialize this type of objects.
        **/
        HttpRequest():
            content("")
        {
        }
        /**
         * Copy constructor.
         * @param b HttpRequest b to copy attributes from.
        **/
        HttpRequest(const HttpRequest& b):
            user(b.user),
            pass(b.pass),
            path(b.path),
            method(b.method),
            post_path(b.post_path),
            headers(b.headers),
            args(b.args),
            content(b.content)
        {
        }
        /**
         * Method used to get the username eventually passed through basic authentication.
         * @return string representation of the username.
        **/
        const std::string getUser() const
        {
            return this->user;
        }
        const std::string getDigestedUser() const
        {
            return this->digestedUser;
        }
        /**
         * Method used to get the password eventually passed through basic authentication.
         * @return string representation of the password.
        **/
        const std::string getPass() const
        {
            return this->pass;
        }
        /**
         * Method used to get the path requested
         * @return string representing the path requested.
        **/
        const std::string getPath() const
        {
            return this->path;
        }
        /**
         * Method used to get all pieces of the path requested; considering an url splitted by '/'.
         * @return a vector of strings containing all pieces
        **/
        const std::vector<std::string> getPathPieces() const
        {
            return this->post_path;
        }
        /**
         * Method used to obtain the size of path in terms of pieces; considering an url splitted by '/'.
         * @return an integer representing the number of pieces
        **/
        int getPathPiecesSize() const
        {
            return this->post_path.size();
        }
        /**
         * Method used to obtain a specified piece of the path; considering an url splitted by '/'.
         * @return the selected piece in form of string
        **/
        const std::string getPathPiece(int index) const
        {
            if(((int)(this->post_path.size())) > index)
                return this->post_path[index];
            return "";
        }
        /**
         * Method used to get the METHOD used to make the request.
         * @return string representing the method.
        **/
        const std::string getMethod() const
        {
            return this->method;
        }
        /**
         * Method used to get all headers passed with the request.
         * @return a vector<pair<string,string> > containing all headers.
        **/
        const std::vector<std::pair<std::string, std::string> > getHeaders() const
        {
            std::vector<std::pair<std::string, std::string> > toRet;
            std::map<std::string, std::string, HeaderComparator>::const_iterator it;
            for(it = headers.begin(); it != headers.end(); it++)
#ifdef USE_CPP_ZEROX
                toRet.push_back(std::make_pair((*it).first,(*it).second));
#else
                toRet.push_back(std::make_pair<std::string, std::string>((*it).first,(*it).second));
#endif
            return toRet;
        }
        /**
         * Method used to get all footers passed with the request.
         * @return a vector<pair<string,string> > containing all footers.
        **/
        const std::vector<std::pair<std::string, std::string> > getFooters() const
        {
            std::vector<std::pair<std::string, std::string> > toRet;
            std::map<std::string, std::string, HeaderComparator>::const_iterator it;
            for(it = footers.begin(); it != footers.end(); it++)
#ifdef USE_CPP_ZEROX
                toRet.push_back(std::make_pair((*it).first,(*it).second));
#else
                toRet.push_back(std::make_pair<std::string, std::string>((*it).first,(*it).second));
#endif
            return toRet;
        }
        /**
         * Method used to get all cookies passed with the request.
         * @return a vector<pair<string, string> > containing all cookies.
        **/
        const std::vector<std::pair<std::string, std::string> > getCookies() const
        {
            std::vector<std::pair<std::string, std::string> > toRet;
            std::map<std::string, std::string, HeaderComparator>::const_iterator it;
            for(it = cookies.begin(); it != cookies.end(); it++)
#ifdef USE_CPP_ZEROX
                toRet.push_back(std::make_pair((*it).first,(*it).second));
#else
                toRet.push_back(std::make_pair<std::string, std::string>((*it).first,(*it).second));
#endif
            return toRet;
        }
        /**
         * Method used to get all parameters passed with the request. Usually parameters are passed with DELETE or GET methods.
         * @return a map<string,string> containing all parameters.
        **/
        const std::vector<std::pair<std::string, std::string> > getArgs() const
        {
            std::vector<std::pair<std::string, std::string> > toRet;
            std::map<std::string, std::string, ArgComparator>::const_iterator it;
            for(it = args.begin(); it != args.end(); it++)
#ifdef USE_CPP_ZEROX
                toRet.push_back(std::make_pair((*it).first,(*it).second));
#else
                toRet.push_back(std::make_pair<std::string, std::string>((*it).first,(*it).second));
#endif
            return toRet;
        }
        /**
         * Method used to get a specific header passed with the request.
         * @param key the specific header to get the value from
         * @return the value of the header.
        **/
        const std::string getHeader(const std::string& key) const
        {
            //string new_key = boost::to_lower_copy(key);
            std::map<std::string, std::string>::const_iterator it = this->headers.find(key);
            if(it != this->headers.end())
                return it->second;
            else
                return "";
        }
        /**
         * Method used to get a specific footer passed with the request.
         * @param key the specific footer to get the value from
         * @return the value of the footer.
        **/
        const std::string getFooter(const std::string& key) const
        {
            //string new_key = boost::to_lower_copy(key);
            std::map<std::string, std::string>::const_iterator it = this->footers.find(key);
            if(it != this->footers.end())
                return it->second;
            else
                return "";
        }
        /**
         * Method used to get a specific argument passed with the request.
         * @param ket the specific argument to get the value from
         * @return the value of the arg.
        **/
        const std::string getArg(const std::string& key) const
        {
            //string new_key = string_utilities::to_lower_copy(key);
            std::map<std::string, std::string>::const_iterator it = this->args.find(key);
            if(it != this->args.end())
                return it->second;
            else
                return "";
        }
        /**
         * Method used to get the content of the request.
         * @return the content in string representation
        **/
        const std::string getContent() const
        {
            return this->content;
        }
        /**
         * Method used to get the version of the request.
         * @return the version in string representation
        **/
        const std::string getVersion() const
        {
            return version;
        }
        /**
         * Method used to get the requestor.
         * @return the requestor
        **/
        const std::string getRequestor() const
        {
            return this->requestor;
        }
        /**
         * Method used to get the requestor port used.
         * @return the requestor port
        **/
        short getRequestorPort() const
        {
            return this->requestorPort;
        }
        /**
         * Method used to set an header value by key.
         * @param key The name identifying the header
         * @param value The value assumed by the header
        **/
        void setHeader(const std::string& key, const std::string& value)
        {
            this->headers[key] = value;
        }
        /**
         * Method used to set a footer value by key.
         * @param key The name identifying the footer
         * @param value The value assumed by the footer
        **/
        void setFooter(const std::string& key, const std::string& value)
        {
            this->footers[key] = value;
        }
        /**
         * Method used to set a cookie value by key.
         * @param key The name identifying the cookie
         * @param value The value assumed by the cookie
        **/
        void setCookie(const std::string& key, const std::string& value)
        {
            this->cookies[key] = value;
        }
        /**
         * Method used to set an argument value by key.
         * @param key The name identifying the argument
         * @param value The value assumed by the argument
        **/
        void setArg(const std::string& key, const std::string& value)
        {
            this->args[key] = value;
        }
        /**
         * Method used to set an argument value by key.
         * @param key The name identifying the argument
         * @param value The value assumed by the argument
         * @param size The size in number of char of the value parameter.
        **/
        void setArg(const char* key, const char* value, size_t size)
        {
            this->args[key] = std::string(value, size);
        }
        /**
         * Method used to set the content of the request
         * @param content The content to set.
        **/
        void setContent(const std::string& content)
        {
            this->content = content;
        }
        /**
         * Method used to append content to the request preserving the previous inserted content
         * @param content The content to append.
         * @param size The size of the data to append.
        **/
        void growContent(const char* content, size_t size)
        {
            this->content += std::string(content, size);
        }
        /**
         * Method used to set the path requested.
         * @param path The path searched by the request.
        **/
        void setPath(const std::string& path)
        {
            //this->path = boost::to_lower_copy(path);
            this->path = path;
            std::vector<std::string> complete_path = HttpUtils::tokenizeUrl(this->path);
            for(unsigned int i = 0; i < complete_path.size(); i++) 
            {
                this->post_path.push_back(complete_path[i]);
            }
        }
        /**
         * Method used to set the request METHOD
         * @param method The method to set for the request
        **/
        void setMethod(const std::string& method);
        /**
         * Method used to set the request http version (ie http 1.1)
         * @param version The version to set in form of string
        **/
        void setVersion(const std::string& version)
        {
            this->version = version;
        }
        /**
         * Method used to set the requestor
         * @param requestor The requestor to set
        **/
        void setRequestor(const std::string& requestor)
        {
            this->requestor = requestor;
        }
        /**
         * Method used to set the requestor port
         * @param requestor The requestor port to set
        **/
        void setRequestorPort(short requestor)
        {
            this->requestorPort = requestorPort;
        }
        /**
         * Method used to remove an header previously inserted
         * @param key The key identifying the header to remove.
        **/
        void removeHeader(const std::string& key)
        {
            this->headers.erase(key);
        }
        /**
         * Method used to set all headers of the request.
         * @param headers The headers key-value map to set for the request.
        **/
        void setHeaders(const std::map<std::string, std::string>& headers)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = headers.begin(); it != headers.end(); it++)
                this->headers[it->first] = it->second;
        }
        /**
         * Method used to set all footers of the request.
         * @param footers The footers key-value map to set for the request.
        **/
        void setFooters(const std::map<std::string, std::string>& footers)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = footers.begin(); it != footers.end(); it++)
                this->footers[it->first] = it->second;
        }
        /**
         * Method used to set all cookies of the request.
         * @param cookies The cookies key-value map to set for the request.
        **/
        void setCookies(const std::map<std::string, std::string>& cookies)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = cookies.begin(); it != cookies.end(); it++)
                this->cookies[it->first] = it->second;
        }
        /**
         * Method used to set all arguments of the request.
         * @param args The args key-value map to set for the request.
        **/
        void setArgs(const std::map<std::string, std::string>& args)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = args.begin(); it != args.end(); it++)
                this->args[it->first] = it->second;
        }
        /**
         * Method used to set the username of the request.
         * @param user The username to set.
        **/
        void setUser(const std::string& user)
        {
            this->user = user;
        }
        void setDigestedUser(const std::string& user)
        {
            this->digestedUser = digestedUser;
        }
        /**
         * Method used to set the password of the request.
         * @param pass The password to set.
        **/
        void setPass(const std::string& pass)
        {
            this->pass = pass;
        }
        bool checkDigestAuth(const std::string& realm, const std::string& password, int nonce_timeout, bool& reloadNonce) const;
    private:
        friend class Webserver;
        std::string user;
        std::string pass;
        std::string path;
        std::string digestedUser;
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
        struct MHD_Connection* underlying_connection;

        void set_underlying_connection(struct MHD_Connection* conn)
        {
            this->underlying_connection = conn;
        }
};

};
#endif
