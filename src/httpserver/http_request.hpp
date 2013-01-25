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

#ifndef _HTTP_REQUEST_HPP_
#define _HTTP_REQUEST_HPP_

#include <map>
#include <vector>
#include <string>
#include <utility>

struct MHD_Connection;

namespace httpserver 
{

class webserver;

namespace http
{
    class header_comparator;
    class arg_comparator;
};

using namespace http;

/**
 * Class representing an abstraction for an Http Request. It is used from classes using these apis to receive information through http protocol.
**/
class http_request 
{
    public:

        /**
         * Method used to get the username eventually passed through basic authentication.
         * @return string representation of the username.
        **/
        const std::string get_user() const
        {
            return this->user;
        }
        /**
         * Method used to get the username eventually passed through basic authentication.
         * @param result string that will be filled with the username
        **/
        void get_user(std::string& result) const
        {
            result = this->user;
        }
        /**
         * Method used to get the username extracted from a digest authentication
         * @return the username
        **/
        const std::string get_digested_user() const
        {
            return this->digested_user;
        }
        /**
         * Method used to get the username extracted from a digest authentication
         * @param result string that will be filled with the username
        **/
        void get_digested_user(std::string& result) const
        {
            result = this->digested_user;
        }
        /**
         * Method used to get the password eventually passed through basic authentication.
         * @return string representation of the password.
        **/
        const std::string get_pass() const
        {
            return this->pass;
        }
        /**
         * Method used to get the password eventually passed through basic authentication.
         * @param result string that will be filled with the password.
        **/
        void get_pass(std::string& result) const
        {
            result = this->pass;
        }
        /**
         * Method used to get the path requested
         * @return string representing the path requested.
        **/
        const std::string get_path() const
        {
            return this->path;
        }
        /**
         * Method used to get the path requested
         * @param result string that will be filled with the path.
        **/
        void get_path(std::string& result) const
        {
            result = this->path;
        }
        /**
         * Method used to get all pieces of the path requested; considering an url splitted by '/'.
         * @return a vector of strings containing all pieces
        **/
        const std::vector<std::string> get_path_pieces() const
        {
            return this->post_path;
        }
        /**
         * Method used to get all pieces of the path requested; considering an url splitted by '/'.
         * @param result vector of strings containing the path
         * @return the size of the vector filled
        **/
        size_t get_path_pieces(std::vector<std::string>& result) const
        {
            result = this->post_path;
            return result.size();
        }
        /**
         * Method used to obtain the size of path in terms of pieces; considering an url splitted by '/'.
         * @return an integer representing the number of pieces
        **/
        size_t get_path_pieces_size() const
        {
            return this->post_path.size();
        }
        /**
         * Method used to obtain a specified piece of the path; considering an url splitted by '/'.
         * @param index the index of the piece selected
         * @return the selected piece in form of string
        **/
        const std::string get_path_piece(int index) const
        {
            if(((int)(this->post_path.size())) > index)
                return this->post_path[index];
            return "";
        }
        /**
         * Method used to obtain a specified piece of the path; considering an url splitted by '/'.
         * @param index the index of the piece selected
         * @param result a string that will be filled with the piece found
         * @return the length of the piece found
        **/
        size_t get_path_piece(int index, std::string& result) const
        {
            if(((int)(this->post_path.size())) > index)
            {
                result = this->post_path[index];
                return result.size();
            }
            else
            {
                result = "";
                return result.size();
            }
        }
        /**
         * Method used to get the METHOD used to make the request.
         * @return string representing the method.
        **/
        const std::string get_method() const
        {
            return this->method;
        }
        /**
         * Method used to get the METHOD used to make the request.
         * @param result string that will be filled with the method.
        **/
        void get_method(std::string& result) const
        {
            result = this->method;
        }
        /**
         * Method used to get all headers passed with the request.
         * @param result a map<string, string> > that will be filled with all headers
         * @result the size of the map
        **/
        size_t get_headers(std::map<std::string, std::string, header_comparator>& result) const;
        /**
         * Method used to get all footers passed with the request.
         * @param result a map<string, string> > that will be filled with all footers
         * @result the size of the map
        **/
        size_t get_footers(std::map<std::string, std::string, header_comparator>& result) const;
        /**
         * Method used to get all cookies passed with the request.
         * @param result a map<string, string> > that will be filled with all cookies
         * @result the size of the map
        **/
        size_t get_cookies(std::map<std::string, std::string, header_comparator>& result) const;
        /**
         * Method used to get all args passed with the request.
         * @param result a map<string, string> > that will be filled with all args
         * @result the size of the map
        **/
        size_t get_args(std::map<std::string, std::string, arg_comparator>& result) const;
        /**
         * Method used to get a specific header passed with the request.
         * @param key the specific header to get the value from
         * @return the value of the header.
        **/
        const std::string get_header(const std::string& key) const
        {
            std::map<std::string, std::string>::const_iterator it = 
                this->headers.find(key);
            if(it != this->headers.end())
                return it->second;
            else
                return "";
        }
        void get_header(const std::string& key, std::string& result) const
        {
            std::map<std::string, std::string>::const_iterator it = 
                this->headers.find(key);
            if(it != this->headers.end())
                result = it->second;
            else
                result = "";
        }
        const std::string get_cookie(const std::string& key) const
        {
            std::map<std::string, std::string>::const_iterator it =
                this->cookies.find(key);
            if(it != this->cookies.end())
                return it->second;
            else
                return "";
        }
        void get_cookie(const std::string& key, std::string& result) const
        {
            std::map<std::string, std::string>::const_iterator it =
                this->cookies.find(key);
            if(it != this->cookies.end())
                result = it->second;
            else
                result = "";
        }
        /**
         * Method used to get a specific footer passed with the request.
         * @param key the specific footer to get the value from
         * @return the value of the footer.
        **/
        const std::string get_footer(const std::string& key) const
        {
            std::map<std::string, std::string>::const_iterator it =
                this->footers.find(key);
            if(it != this->footers.end())
                return it->second;
            else
                return "";
        }
        void get_footer(const std::string& key, std::string& result) const
        {
            std::map<std::string, std::string>::const_iterator it =
                this->footers.find(key);
            if(it != this->footers.end())
                result = it->second;
            else
                result = "";
        }
        /**
         * Method used to get a specific argument passed with the request.
         * @param ket the specific argument to get the value from
         * @return the value of the arg.
        **/
        const std::string get_arg(const std::string& key) const
        {
            std::map<std::string, std::string>::const_iterator it =
                this->args.find(key);
            if(it != this->args.end())
                return it->second;
            else
                return "";
        }
        void get_arg(const std::string& key, std::string& result) const
        {
            std::map<std::string, std::string>::const_iterator it =
                this->args.find(key);
            if(it != this->args.end())
                result = it->second;
            else
                result = "";
        }
        /**
         * Method used to get the content of the request.
         * @return the content in string representation
        **/
        const std::string get_content() const
        {
            return this->content;
        }
        void get_content(std::string& result) const
        {
            result = this->content;
        }
        const std::string get_querystring() const
        {
            return this->querystring;
        }
        void get_querystring(std::string& result) const
        {
            result = this->querystring;
        }
        /**
         * Method used to get the version of the request.
         * @return the version in string representation
        **/
        const std::string get_version() const
        {
            return this->version;
        }
        void get_version(std::string& result) const
        {
            result = this->version;
        }
        /**
         * Method used to get the requestor.
         * @return the requestor
        **/
        const std::string get_requestor() const
        {
            return this->requestor;
        }
        void get_requestor(std::string& result) const
        {
            result = this->requestor;
        }
        /**
         * Method used to get the requestor port used.
         * @return the requestor port
        **/
        short get_requestor_port() const
        {
            return this->requestor_port;
        }
        bool check_digest_auth(const std::string& realm,
                const std::string& password,
                int nonce_timeout, bool& reload_nonce
        ) const;
    private:
        /**
         * Default constructor of the class. It is a specific responsibility of apis to initialize this type of objects.
        **/
        http_request():
            content("")
        {
        }
        /**
         * Copy constructor.
         * @param b http_request b to copy attributes from.
        **/
        http_request(const http_request& b):
            user(b.user),
            pass(b.pass),
            path(b.path),
            digested_user(b.digested_user),
            method(b.method),
            post_path(b.post_path),
            headers(b.headers),
            footers(b.footers),
            cookies(b.cookies),
            args(b.args),
            querystring(b.querystring),
            content(b.content),
            version(b.version),
            requestor(b.requestor),
            underlying_connection(b.underlying_connection)
        {
        }
        std::string user;
        std::string pass;
        std::string path;
        std::string digested_user;
        std::string method;
        std::vector<std::string> post_path;
        std::map<std::string, std::string, header_comparator> headers;
        std::map<std::string, std::string, header_comparator> footers;
        std::map<std::string, std::string, header_comparator> cookies;
        std::map<std::string, std::string, arg_comparator> args;
        std::string querystring;
        std::string content;
        std::string version;
        std::string requestor;

        short requestor_port;
        struct MHD_Connection* underlying_connection;

        void set_underlying_connection(struct MHD_Connection* conn)
        {
            this->underlying_connection = conn;
        }
        /**
         * Method used to set an header value by key.
         * @param key The name identifying the header
         * @param value The value assumed by the header
        **/
        void set_header(const std::string& key, const std::string& value)
        {
            this->headers[key] = value;
        }
        /**
         * Method used to set a footer value by key.
         * @param key The name identifying the footer
         * @param value The value assumed by the footer
        **/
        void set_footer(const std::string& key, const std::string& value)
        {
            this->footers[key] = value;
        }
        /**
         * Method used to set a cookie value by key.
         * @param key The name identifying the cookie
         * @param value The value assumed by the cookie
        **/
        void set_cookie(const std::string& key, const std::string& value)
        {
            this->cookies[key] = value;
        }
        /**
         * Method used to set an argument value by key.
         * @param key The name identifying the argument
         * @param value The value assumed by the argument
        **/
        void set_arg(const std::string& key, const std::string& value)
        {
            this->args[key] = value;
        }
        /**
         * Method used to set an argument value by key.
         * @param key The name identifying the argument
         * @param value The value assumed by the argument
         * @param size The size in number of char of the value parameter.
        **/
        void set_arg(const char* key, const char* value, size_t size)
        {
            this->args[key] = std::string(value, size);
        }
        /**
         * Method used to set the content of the request
         * @param content The content to set.
        **/
        void set_content(const std::string& content)
        {
            this->content = content;
        }
        /**
         * Method used to append content to the request preserving the previous inserted content
         * @param content The content to append.
         * @param size The size of the data to append.
        **/
        void grow_content(const char* content, size_t size)
        {
            this->content.append(content, size);
        }
        /**
         * Method used to set the path requested.
         * @param path The path searched by the request.
        **/
        void set_path(const std::string& path)
        {
            this->path = path;
            std::vector<std::string> complete_path;
            http_utils::tokenize_url(this->path, complete_path);
            for(unsigned int i = 0; i < complete_path.size(); i++) 
            {
                this->post_path.push_back(complete_path[i]);
            }
        }
        /**
         * Method used to set the request METHOD
         * @param method The method to set for the request
        **/
        void set_method(const std::string& method);
        /**
         * Method used to set the request http version (ie http 1.1)
         * @param version The version to set in form of string
        **/
        void set_version(const std::string& version)
        {
            this->version = version;
        }
        /**
         * Method used to set the requestor
         * @param requestor The requestor to set
        **/
        void set_requestor(const std::string& requestor)
        {
            this->requestor = requestor;
        }
        /**
         * Method used to set the requestor port
         * @param requestor The requestor port to set
        **/
        void set_requestor_port(short requestor)
        {
            this->requestor_port = requestor_port;
        }
        /**
         * Method used to remove an header previously inserted
         * @param key The key identifying the header to remove.
        **/
        void remove_header(const std::string& key)
        {
            this->headers.erase(key);
        }
        /**
         * Method used to set all headers of the request.
         * @param headers The headers key-value map to set for the request.
        **/
        void set_headers(const std::map<std::string, std::string>& headers)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = headers.begin(); it != headers.end(); ++it)
                this->headers[it->first] = it->second;
        }
        /**
         * Method used to set all footers of the request.
         * @param footers The footers key-value map to set for the request.
        **/
        void set_footers(const std::map<std::string, std::string>& footers)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = footers.begin(); it != footers.end(); ++it)
                this->footers[it->first] = it->second;
        }
        /**
         * Method used to set all cookies of the request.
         * @param cookies The cookies key-value map to set for the request.
        **/
        void set_cookies(const std::map<std::string, std::string>& cookies)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = cookies.begin(); it != cookies.end(); ++it)
                this->cookies[it->first] = it->second;
        }
        /**
         * Method used to set all arguments of the request.
         * @param args The args key-value map to set for the request.
        **/
        void set_args(const std::map<std::string, std::string>& args)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = args.begin(); it != args.end(); ++it)
                this->args[it->first] = it->second;
        }
        /**
         * Method used to set the username of the request.
         * @param user The username to set.
        **/
        void set_user(const std::string& user)
        {
            this->user = user;
        }
        void set_digested_user(const std::string& user)
        {
            this->digested_user = digested_user;
        }
        /**
         * Method used to set the password of the request.
         * @param pass The password to set.
        **/
        void set_pass(const std::string& pass)
        {
            this->pass = pass;
        }

        friend class webserver;
};

};
#endif
