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

#ifndef SRC_HTTPSERVER_HTTP_REQUEST_HPP_
#define SRC_HTTPSERVER_HTTP_REQUEST_HPP_

#include <microhttpd.h>

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif  // HAVE_GNUTLS

#include <stddef.h>
#include <algorithm>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "httpserver/http_utils.hpp"
#include "httpserver/file_info.hpp"

struct MHD_Connection;

namespace httpserver {

namespace details { struct modded_request; }

/**
 * Class representing an abstraction for an Http Request. It is used from classes using these apis to receive information through http protocol.
**/
class http_request {
 public:
     static const char EMPTY[];

     /**
      * Method used to get the username eventually passed through basic authentication.
      * @return string representation of the username.
     **/
     std::string_view get_user() const;

     /**
      * Method used to get the username extracted from a digest authentication
      * @return the username
     **/
     std::string_view get_digested_user() const;

     /**
      * Method used to get the password eventually passed through basic authentication.
      * @return string representation of the password.
     **/
     std::string_view get_pass() const;

     /**
      * Method used to get the path requested
      * @return string representing the path requested.
     **/
     std::string_view get_path() const {
         return path;
     }

     /**
      * Method used to get all pieces of the path requested; considering an url splitted by '/'.
      * @return a vector of strings containing all pieces
     **/
     const std::vector<std::string> get_path_pieces() const {
         return http::http_utils::tokenize_url(path);
     }

     /**
      * Method used to obtain a specified piece of the path; considering an url splitted by '/'.
      * @param index the index of the piece selected
      * @return the selected piece in form of string
     **/
     const std::string get_path_piece(int index) const {
         std::vector<std::string> post_path = get_path_pieces();
         if ((static_cast<int>((post_path.size()))) > index) {
             return post_path[index];
         }
         return EMPTY;
     }

     /**
      * Method used to get the METHOD used to make the request.
      * @return string representing the method.
     **/
     std::string_view get_method() const {
         return method;
     }

     /**
      * Method used to get all headers passed with the request.
      * @param result a map<string, string> > that will be filled with all headers
      * @result the size of the map
     **/
     const http::header_view_map get_headers() const;

     /**
      * Method used to get all footers passed with the request.
      * @param result a map<string, string> > that will be filled with all footers
      * @result the size of the map
     **/
     const http::header_view_map get_footers() const;

     /**
      * Method used to get all cookies passed with the request.
      * @param result a map<string, string> > that will be filled with all cookies
      * @result the size of the map
     **/
     const http::header_view_map get_cookies() const;

     /**
      * Method used to get all args passed with the request.
      * @param result a map<string, string> > that will be filled with all args
      * @result the size of the map
     **/
     const http::arg_view_map get_args() const;

     /**
      * Method to get or create a file info struct in the map if the provided filename is already in the map
      * return the exiting file info struct, otherwise create one in the map and return it.
      * @param upload_file_name the file name the user uploaded (this is the identifier for the map entry)
      * @result a http::file_info
     **/
     http::file_info& get_or_create_file_info(const std::string& key, const std::string& upload_file_name);

     /**
      * Method used to get all files passed with the request.
      * @result result a map<std::string, map<std::string, http::file_info> > that will be filled with all files
     **/
     const std::map<std::string, std::map<std::string, http::file_info>> get_files() const {
          return files;
     }

     /**
      * Method used to get a specific header passed with the request.
      * @param key the specific header to get the value from
      * @return the value of the header.
     **/
     std::string_view get_header(std::string_view key) const;

     std::string_view get_cookie(std::string_view key) const;

     /**
      * Method used to get a specific footer passed with the request.
      * @param key the specific footer to get the value from
      * @return the value of the footer.
     **/
     std::string_view get_footer(std::string_view key) const;

     /**
      * Method used to get a specific argument passed with the request.
      * @param ket the specific argument to get the value from
      * @return the value of the arg.
     **/
     std::string_view get_arg(std::string_view key) const;

     /**
      * Method used to get the content of the request.
      * @return the content in string representation
     **/
     std::string_view get_content() const {
         return content;
     }

     /**
      * Method to check whether the size of the content reached or exceeded content_size_limit.
      * @return boolean
     **/
     bool content_too_large() const {
         return content.size() >= content_size_limit;
     }
     /**
      * Method used to get the content of the query string..
      * @return the query string in string representation
     **/
     std::string_view get_querystring() const;

     /**
      * Method used to get the version of the request.
      * @return the version in string representation
     **/
     std::string_view get_version() const {
         return version;
     }

#ifdef HAVE_GNUTLS
     /**
      * Method used to check if there is a TLS session.
      * @return the TLS session
      **/
      bool has_tls_session() const;

     /**
      * Method used to get the TLS session.
      * @return the TLS session
      **/
      gnutls_session_t get_tls_session() const;
#endif  // HAVE_GNUTLS

     /**
      * Method used to get the requestor.
      * @return the requestor
     **/
     std::string_view get_requestor() const;

     /**
      * Method used to get the requestor port used.
      * @return the requestor port
     **/
     uint16_t get_requestor_port() const;

     bool check_digest_auth(const std::string& realm, const std::string& password, int nonce_timeout, bool* reload_nonce) const;

     friend std::ostream &operator<< (std::ostream &os, http_request &r);

 private:
     /**
      * Default constructor of the class. It is a specific responsibility of apis to initialize this type of objects.
     **/
     http_request() : cache(std::make_unique<http_request_data_cache>()) {}

     http_request(MHD_Connection* underlying_connection, unescaper_ptr unescaper):
         underlying_connection(underlying_connection),
         unescaper(unescaper), cache(std::make_unique<http_request_data_cache>()) {}

     /**
      * Copy constructor. Deleted to make class move-only. The class is move-only for several reasons:
      *  - Internal cache structure is expensive to copy
      *  - Various string members are expensive to copy
      *  - The destructor removes transient files from disk, which must only happen once.
      *  - unique_ptr members are not copyable.
     **/
     http_request(const http_request& b) = delete;
     /**
      * Move constructor.
      * @param b http_request b to move attributes from.
     **/
     http_request(http_request&& b) noexcept = default;

     /**
      * Copy-assign. Deleted to make class move-only. The class is move-only for several reasons:
      *  - Internal cache structure is expensive to copy
      *  - Various string members are expensive to copy
      *  - The destructor removes transient files from disk, which must only happen once.
      *  - unique_ptr members are not copyable.
     **/
     http_request& operator=(const http_request& b) = delete;
     http_request& operator=(http_request&& b) = default;

     ~http_request();

     std::string path;
     std::string method;
     std::map<std::string, std::map<std::string, http::file_info>> files;
     std::string content = "";
     size_t content_size_limit = static_cast<size_t>(-1);
     std::string version;

     struct MHD_Connection* underlying_connection = nullptr;

     unescaper_ptr unescaper = nullptr;

     static MHD_Result build_request_header(void *cls, enum MHD_ValueKind kind, const char *key, const char *value);

     static MHD_Result build_request_args(void *cls, enum MHD_ValueKind kind, const char *key, const char *value);

     static MHD_Result build_request_querystring(void *cls, enum MHD_ValueKind kind, const char *key, const char *value);

     void fetch_user_pass() const;

     /**
      * Method used to set an argument value by key.
      * @param key The name identifying the argument
      * @param value The value assumed by the argument
     **/
     void set_arg(const std::string& key, const std::string& value) {
         cache->unescaped_args[key] = value.substr(0, content_size_limit);
     }

     /**
      * Method used to set an argument value by key.
      * @param key The name identifying the argument
      * @param value The value assumed by the argument
      * @param size The size in number of char of the value parameter.
     **/
     void set_arg(const char* key, const char* value, size_t size) {
         cache->unescaped_args[key] = std::string(value, std::min(size, content_size_limit));
     }

     /**
      * Method used to set the content of the request
      * @param content The content to set.
     **/
     void set_content(const std::string& content) {
         this->content = content.substr(0, content_size_limit);
     }

     /**
      * Method used to set the maximum size of the content
      * @param content_size_limit The limit on the maximum size of the content and arg's.
     **/
     void set_content_size_limit(size_t content_size_limit) {
         this->content_size_limit = content_size_limit;
     }

     /**
      * Method used to append content to the request preserving the previous inserted content
      * @param content The content to append.
      * @param size The size of the data to append.
     **/
     void grow_content(const char* content, size_t size) {
         this->content.append(content, size);
         if (this->content.size() > content_size_limit) {
             this->content.resize(content_size_limit);
         }
     }

     /**
      * Method used to set the path requested.
      * @param path The path searched by the request.
     **/
     void set_path(const std::string& path) {
         this->path = path;
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
     void set_version(const std::string& version) {
         this->version = version;
     }

     /**
      * Method used to set all arguments of the request.
      * @param args The args key-value map to set for the request.
     **/
     void set_args(const std::map<std::string, std::string>& args) {
         std::map<std::string, std::string>::const_iterator it;
         for (it = args.begin(); it != args.end(); ++it) {
             this->cache->unescaped_args[it->first] = it->second.substr(0, content_size_limit);
         }
     }

     std::string_view get_connection_value(std::string_view key, enum MHD_ValueKind kind) const;
     const http::header_view_map get_headerlike_values(enum MHD_ValueKind kind) const;

     // Cache certain data items on demand so we can consistently return views
     // over the data. Some things we transform before returning to the user for
     // simplicity (e.g. query_str, requestor), others out of necessity (arg unescaping).
     // Others (username, password, digested_user) MHD returns as char* that we need
     // to make a copy of and free anyway.
     struct http_request_data_cache {
        std::string username;
        std::string password;
        std::string querystring;
        std::string requestor_ip;
        std::string digested_user;
        http::arg_map unescaped_args;
     };
     std::unique_ptr<http_request_data_cache> cache;

     friend class webserver;
     friend struct details::modded_request;
};

std::ostream &operator<< (std::ostream &os, const http_request &r);

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_REQUEST_HPP_
