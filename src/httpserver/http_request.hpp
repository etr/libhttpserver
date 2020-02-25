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
#include <iosfwd>

struct MHD_Connection;

namespace httpserver
{

class webserver;

namespace http
{
    class header_comparator;
    class arg_comparator;
};

/**
 * Class representing an abstraction for an Http Request. It is used from classes using these apis to receive information through http protocol.
**/
class http_request
{
    public:
        static const std::string EMPTY;

        /**
         * Method used to get the username eventually passed through basic authentication.
         * @return string representation of the username.
        **/
        const std::string get_user() const;

        /**
         * Method used to get the username extracted from a digest authentication
         * @return the username
        **/
        const std::string get_digested_user() const;

        /**
         * Method used to get the password eventually passed through basic authentication.
         * @return string representation of the password.
        **/
        const std::string get_pass() const;

        /**
         * Method used to get the path requested
         * @return string representing the path requested.
        **/
        const std::string& get_path() const
        {
            return path;
        }

        /**
         * Method used to get all pieces of the path requested; considering an url splitted by '/'.
         * @return a vector of strings containing all pieces
        **/
        const std::vector<std::string> get_path_pieces() const
        {
            return http::http_utils::tokenize_url(path);
        }

        /**
         * Method used to obtain a specified piece of the path; considering an url splitted by '/'.
         * @param index the index of the piece selected
         * @return the selected piece in form of string
        **/
        const std::string get_path_piece(int index) const
        {
            std::vector<std::string> post_path = get_path_pieces();
            if(((int)(post_path.size())) > index)
                return post_path[index];
            return EMPTY;
        }

        /**
         * Method used to get the METHOD used to make the request.
         * @return string representing the method.
        **/
        const std::string& get_method() const
        {
            return method;
        }

        /**
         * Method used to get all headers passed with the request.
         * @param result a map<string, string> > that will be filled with all headers
         * @result the size of the map
        **/
        const std::map<std::string, std::string, http::header_comparator> get_headers() const;

        /**
         * Method used to get all footers passed with the request.
         * @param result a map<string, string> > that will be filled with all footers
         * @result the size of the map
        **/
        const std::map<std::string, std::string, http::header_comparator> get_footers() const;

        /**
         * Method used to get all cookies passed with the request.
         * @param result a map<string, string> > that will be filled with all cookies
         * @result the size of the map
        **/
        const std::map<std::string, std::string, http::header_comparator> get_cookies() const;

        /**
         * Method used to get all args passed with the request.
         * @param result a map<string, string> > that will be filled with all args
         * @result the size of the map
        **/
        const std::map<std::string, std::string, http::arg_comparator> get_args() const;

        /**
         * Method used to get a specific header passed with the request.
         * @param key the specific header to get the value from
         * @return the value of the header.
        **/
        const std::string get_header(const std::string& key) const;

        const std::string get_cookie(const std::string& key) const;

        /**
         * Method used to get a specific footer passed with the request.
         * @param key the specific footer to get the value from
         * @return the value of the footer.
        **/
        const std::string get_footer(const std::string& key) const;

        /**
         * Method used to get a specific argument passed with the request.
         * @param ket the specific argument to get the value from
         * @return the value of the arg.
        **/
        const std::string get_arg(const std::string& key) const;

        /**
         * Method used to get the content of the request.
         * @return the content in string representation
        **/
        const std::string& get_content() const
        {
            return content;
        }

        /**
         * Method to check whether the size of the content reached or exceeded content_size_limit.
         * @return boolean
        **/
        bool content_too_large() const
        {
            return content.size()>=content_size_limit;
        }
        /**
         * Method used to get the content of the query string..
         * @return the query string in string representation
        **/
        const std::string get_querystring() const;

        /**
         * Method used to get the version of the request.
         * @return the version in string representation
        **/
        const std::string& get_version() const
        {
            return version;
        }

        /**
         * Method used to get the requestor.
         * @return the requestor
        **/
        const std::string get_requestor() const;

        /**
         * Method used to get the requestor port used.
         * @return the requestor port
        **/
        unsigned short get_requestor_port() const;

        bool check_digest_auth(const std::string& realm,
                const std::string& password,
                int nonce_timeout, bool& reload_nonce
        ) const;

        friend std::ostream &operator<< (std::ostream &os, http_request &r);

    private:
        /**
         * Default constructor of the class. It is a specific responsibility of apis to initialize this type of objects.
        **/
        http_request():
            content(""),
            content_size_limit(static_cast<size_t>(-1)),
            underlying_connection(0x0),
            unescaper(0x0)
        {
        }

        http_request(MHD_Connection* underlying_connection, unescaper_ptr unescaper):
            content(""),
            content_size_limit(static_cast<size_t>(-1)),
            underlying_connection(underlying_connection),
            unescaper(unescaper)
        {
        }

        /**
         * Copy constructor.
         * @param b http_request b to copy attributes from.
        **/
        http_request(const http_request& b):
            path(b.path),
            method(b.method),
            args(b.args),
            content(b.content),
            content_size_limit(b.content_size_limit),
            version(b.version),
            underlying_connection(b.underlying_connection),
            unescaper(b.unescaper)
        {
        }

        http_request(http_request&& b) noexcept:
            path(std::move(b.path)),
            method(std::move(b.method)),
            args(std::move(b.args)),
            content(std::move(b.content)),
            content_size_limit(b.content_size_limit),
            version(std::move(b.version)),
            underlying_connection(std::move(b.underlying_connection))
        {
        }

        http_request& operator=(const http_request& b)
        {
            if (this == &b) return *this;

            this->path = b.path;
            this->method = b.method;
            this->args = b.args;
            this->content = b.content;
            this->content_size_limit = b.content_size_limit;
            this->version = b.version;
            this->underlying_connection = b.underlying_connection;

            return *this;
        }

        http_request& operator=(http_request&& b)
        {
            if (this == &b) return *this;

            this->path = std::move(b.path);
            this->method = std::move(b.method);
            this->args = std::move(b.args);
            this->content = std::move(b.content);
            this->content_size_limit = b.content_size_limit;
            this->version = std::move(b.version);
            this->underlying_connection = std::move(b.underlying_connection);

            return *this;
        }

        std::string path;
        std::string method;
        std::map<std::string, std::string, http::arg_comparator> args;
        std::string content;
        size_t content_size_limit;
        std::string version;

        struct MHD_Connection* underlying_connection;

        unescaper_ptr unescaper;

        static int build_request_header(void *cls, enum MHD_ValueKind kind,
                const char *key, const char *value
        );

        static int build_request_args(void *cls, enum MHD_ValueKind kind,
                const char *key, const char *value
        );

        static int build_request_querystring(void *cls, enum MHD_ValueKind kind,
                const char *key, const char *value
        );

        /**
         * Method used to set an argument value by key.
         * @param key The name identifying the argument
         * @param value The value assumed by the argument
        **/
        void set_arg(const std::string& key, const std::string& value)
        {
            args[key] = value.substr(0,content_size_limit);
        }

        /**
         * Method used to set an argument value by key.
         * @param key The name identifying the argument
         * @param value The value assumed by the argument
         * @param size The size in number of char of the value parameter.
        **/
        void set_arg(const char* key, const char* value, size_t size)
        {
            args[key] = std::string(value, std::min(size, content_size_limit));
        }

        /**
         * Method used to set the content of the request
         * @param content The content to set.
        **/
        void set_content(const std::string& content_src)
        {
            content = content_src.substr(0,content_size_limit);
        }

        /**
         * Method used to set the maximum size of the content
         * @param content_size_limit The limit on the maximum size of the content and arg's.
        **/
        void set_content_size_limit(size_t content_size_limit_src)
        {
            content_size_limit = content_size_limit_src;
        }

        /**
         * Method used to append content to the request preserving the previous inserted content
         * @param content The content to append.
         * @param size The size of the data to append.
        **/
        void grow_content(const char* content_ptr, size_t size)
        {
            content.append(content_ptr, size);
            if (content.size() > content_size_limit)
            {
                content.resize (content_size_limit);
            }
        }

        /**
         * Method used to set the path requested.
         * @param path The path searched by the request.
        **/
        void set_path(const std::string& path_src)
        {
            path = path_src;
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
        void set_version(const std::string& version_src)
        {
            version = version_src;
        }

        /**
         * Method used to set all arguments of the request.
         * @param args The args key-value map to set for the request.
        **/
        void set_args(const std::map<std::string, std::string>& args_src)
        {
            std::map<std::string, std::string>::const_iterator it;
            for(it = args_src.begin(); it != args_src.end(); ++it)
                args[it->first] = it->second.substr(0,content_size_limit);
        }

        const std::string get_connection_value(const std::string& key, enum MHD_ValueKind kind) const;
        const std::map<std::string, std::string, http::header_comparator> get_headerlike_values(enum MHD_ValueKind kind) const;

        friend class webserver;
};

std::ostream &operator<< (std::ostream &os, const http_request &r);

};
#endif
