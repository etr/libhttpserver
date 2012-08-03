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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef _FRAMEWORK_WEBSERVER_HPP_
#define _FRAMEWORK_WEBSERVER_HPP_

#define NOT_FOUND_ERROR "Not Found"
#define METHOD_ERROR "Method not Allowed"
#define NOT_METHOD_ERROR "Method not Acceptable"
#define GENERIC_ERROR "Internal Error"
#define DEFAULT_WS_PORT 9898
#define DEFAULT_WS_TIMEOUT 180

#include <cstring>
#include <map>
#include <vector>
#ifdef USE_CPP_ZEROX
#include <unordered_set>
#else
#include <set>
#endif
#include <string>
#include <utility>
#include <memory>

namespace httpserver {

class http_resource;
class http_response;
class http_request;
class http_endpoint;

using namespace http;

namespace http {
struct ip_representation;
};

/**
 * Delegate class used to wrap callbacks dedicated to logging.
**/
class logging_delegate
{
    public:
        /**
         * Delegate constructor.
        **/
        logging_delegate();
        /**
         * Destructor of the class
        **/
        virtual ~logging_delegate();
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
class request_validator
{
    public:
        /**
         * Delegate constructor
        **/
        request_validator();
        /**
         * Destructor of the class
        **/
        virtual ~request_validator();
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
class unescaper
{
    public:
        /**
         * Delegate constructor
        **/
        unescaper();
        /**
         * Destructor of the class
        **/
        virtual ~unescaper();
        /**
         * Method used to unescape the uri.
         * @param s pointer to the uri string representation to unescape.
        **/
        virtual void unescape(char* s) const;
};

class create_webserver;

/**
 * Class representing the webserver. Main class of the apis.
**/
class webserver 
{
    public:
        /**
         * Constructor of the class.
         * @param port Integer representing the port the webserver is listening on.
         * @param start_method
         * @param max_threads max number of serving threads (0 -> infty)
         * @param max_connections max number of allowed connections (0 -> infty).
         * @param memory_limit max memory allocated to serve requests (0 -> infty).
         * @param per_IP_connection_limit max number of connections allowed for a single IP (0 -> infty).
         * @param log_delegate logging_delegate object used to log
         * @param validator request_validator object used to validate requestors
         * @param unescaper unescaper object used to unescape urls.
         * @param max_thread_stack_size max dimesion of request stack
         * @param https_mem_key used with https. Private key used for requests.
         * @param https_mem_cert used with https. Certificate used for requests.
         * @param https_mem_trust used with https. CA Certificate used to trust request certificates.
         * @param https_priorities used with https. Determinates the SSL/TLS version to be used.
         * @param cred_type used with https. Daemon credential type. Can be NONE, certificate or anonymous.
         * @param digest_auth_random used with https. Digest authentication nonce's seed.
         * @param nonce_nc_size used with https. Size of an array of nonce and nonce counter map.
        **/
        explicit webserver
        (
            int port = DEFAULT_WS_PORT, 
            const http_utils::start_method_T& start_method = http_utils::INTERNAL_SELECT,
            int max_threads = 0, 
            int max_connections = 0,
            int memory_limit = 0,
            int connection_timeout = DEFAULT_WS_TIMEOUT,
            int per_IP_connection_limit = 0,
            const logging_delegate* log_delegate = 0x0,
            const request_validator* validator = 0x0,
            const unescaper* unescaper_pointer = 0x0,
            const struct sockaddr* bind_address = 0x0,
            int bind_socket = 0,
            int max_thread_stack_size = 0,
            bool use_ssl = false,
            bool use_ipv6 = false,
            bool debug = false,
            bool pedantic = false,
            const std::string& https_mem_key = "",
            const std::string& https_mem_cert = "",
            const std::string& https_mem_trust = "",
            const std::string& https_priorities = "",
            const http_utils::cred_type_T& cred_type= http_utils::NONE,
            const std::string digest_auth_random = "", //IT'S CORRECT TO PASS THIS PARAMETER BY VALUE
            int nonce_nc_size = 0,
            const http_utils::policy_T& default_policy = http_utils::ACCEPT,
            bool basic_auth_enabled = true,
            bool digest_auth_enabled = true,
            bool regex_checking = true,
            bool ban_system_enabled = true,
            bool post_process_enabled = true,
            http_resource* single_resource = 0x0
        );
        webserver(const create_webserver& params);
        /**
         * Destructor of the class
        **/
        ~webserver();
        /**
         * Method used to start the webserver.
         * This method can be blocking or not.
         * @param blocking param indicating if the method is blocking or not
         * @return a boolean indicating if the webserver is running or not.
        **/
        bool start(bool blocking = false);
        /**
         * Method used to stop the webserver.
         * @return true if the webserver is stopped.
        **/
        bool stop();
        /**
         * Method used to evaluate if the server is running or not.
         * @return true if the webserver is running
        **/
        bool is_running();
        /**
         * Method used to registrate a resource to the webserver.
         * @param resource The url pointing to the resource. This url could be also parametrized in the form /path/to/url/{par1}/and/{par2}
         *                 or a regular expression.
         * @param http_resource http_resource pointer to register.
         * @param family boolean indicating whether the resource is registered for the endpoint and its child or not.
        **/
        void register_resource(const std::string& resource, http_resource* http_resource, bool family = false);
        void unregister_resource(const std::string& resource);
        void ban_ip(const std::string& ip);
        void allow_ip(const std::string& ip);
        void unban_ip(const std::string& ip);
        void disallow_ip(const std::string& ip);
        /**
         * Method used to kill the webserver waiting for it to terminate
        **/
        void sweet_kill();
    private:
        int port;
        http_utils::start_method_T start_method;
        int max_threads;
        int max_connections;
        int memory_limit;
        int connection_timeout;
        int per_IP_connection_limit;
        const logging_delegate* log_delegate;
        const request_validator* validator;
        const unescaper* unescaper_pointer;
        const struct sockaddr* bind_address;
        int bind_socket;
        int max_thread_stack_size;
        bool use_ssl;
        bool use_ipv6;
        bool debug;
        bool pedantic;
        std::string https_mem_key;
        std::string https_mem_cert;
        std::string https_mem_trust;
        std::string https_priorities;
        http_utils::cred_type_T cred_type;
        std::string digest_auth_random;
        int nonce_nc_size;
        bool running;
        http_utils::policy_T default_policy;
        bool basic_auth_enabled;
        bool digest_auth_enabled;
        bool regex_checking;
        bool ban_system_enabled;
        bool post_process_enabled;
        bool single_resource;

        std::map<http_endpoint, http_resource* > registered_resources;
#ifdef USE_CPP_ZEROX
        std::unordered_set<ip_representation> bans;
        std::unordered_set<ip_representation> allowances;
#else
        std::set<ip_representation> bans;
        std::set<ip_representation> allowances;
#endif
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
        static void request_completed(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe);
        static int build_request_header (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
        static int build_request_footer (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
        static int build_request_cookie (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
        static int build_request_args (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
        static int answer_to_connection
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
        static void upgrade_handler 
        (
            void *cls, 
            struct MHD_Connection* connection,
            void **con_cls, int upgrade_socket
        );

        friend int policy_callback (void *cls, const struct sockaddr* addr, socklen_t addrlen);
        friend void error_log(void* cls, const char* fmt, va_list ap);
        friend void access_log(webserver* cls, std::string uri);
        friend void* uri_log(void* cls, const char* uri);
        friend size_t unescaper_func(void * cls, struct MHD_Connection *c, char *s);
        friend size_t internal_unescaper(void * cls, char *s);
};

class create_webserver 
{
    public:
        create_webserver():
            _port(DEFAULT_WS_PORT),
            _start_method(http_utils::INTERNAL_SELECT),
            _max_threads(0),
            _max_connections(0),
            _memory_limit(0),
            _connection_timeout(DEFAULT_WS_TIMEOUT),
            _per_IP_connection_limit(0),
            _log_delegate(0x0),
            _validator(0x0),
            _unescaper_pointer(0x0),
            _bind_address(0x0),
            _bind_socket(0),
            _max_thread_stack_size(0),
            _use_ssl(false),
            _use_ipv6(false),
            _debug(false),
            _pedantic(false),
            _https_mem_key(""),
            _https_mem_cert(""),
            _https_mem_trust(""),
            _https_priorities(""),
            _cred_type(http_utils::NONE),
            _digest_auth_random(""),
            _nonce_nc_size(0),
            _default_policy(http_utils::ACCEPT),
            _basic_auth_enabled(true),
            _digest_auth_enabled(true),
            _regex_checking(true),
            _ban_system_enabled(true),
            _post_process_enabled(true),
            _single_resource(0x0)
        {
        }

        explicit create_webserver(int port):
            _port(port),
            _start_method(http_utils::INTERNAL_SELECT),
            _max_threads(0),
            _max_connections(0),
            _memory_limit(0),
            _connection_timeout(DEFAULT_WS_TIMEOUT),
            _per_IP_connection_limit(0),
            _log_delegate(0x0),
            _validator(0x0),
            _unescaper_pointer(0x0),
            _bind_address(0x0),
            _bind_socket(0),
            _max_thread_stack_size(0),
            _use_ssl(false),
            _use_ipv6(false),
            _debug(false),
            _pedantic(false),
            _https_mem_key(""),
            _https_mem_cert(""),
            _https_mem_trust(""),
            _https_priorities(""),
            _cred_type(http_utils::NONE),
            _digest_auth_random(""),
            _nonce_nc_size(0),
            _default_policy(http_utils::ACCEPT),
            _basic_auth_enabled(true),
            _digest_auth_enabled(true),
            _regex_checking(true),
            _ban_system_enabled(true),
            _post_process_enabled(true),
            _single_resource(0x0)
        {
        }

        create_webserver& port(int port) { _port = port; return *this; }
        create_webserver& start_method(const http_utils::start_method_T& start_method) { _start_method = start_method; return *this; }
        create_webserver& max_threads(int max_threads) { _max_threads = max_threads; return *this; }
        create_webserver& max_connections(int max_connections) { _max_connections = max_connections; return *this; }
        create_webserver& memory_limit(int memory_limit) { _memory_limit = memory_limit; return *this; }
        create_webserver& connection_timeout(int connection_timeout) { _connection_timeout = connection_timeout; return *this; }
        create_webserver& per_IP_connection_limit(int per_IP_connection_limit) { _per_IP_connection_limit = per_IP_connection_limit; return *this; }
        create_webserver& log_delegate(const logging_delegate* log_delegate) { _log_delegate = log_delegate; return *this; }
        create_webserver& validator(const request_validator* validator) { _validator = validator; return *this; }
        create_webserver& unescaper_pointer(const unescaper* unescaper_pointer) { _unescaper_pointer = unescaper_pointer; return *this; }
        create_webserver& bind_address(const struct sockaddr* bind_address) { _bind_address = bind_address; return *this; }
        create_webserver& bind_socket(int bind_socket) { _bind_socket = bind_socket; return *this; }
        create_webserver& max_thread_stack_size(int max_thread_stack_size) { _max_thread_stack_size = max_thread_stack_size; return *this; }
        create_webserver& use_ssl() { _use_ssl = true; return *this; }
        create_webserver& no_ssl() { _use_ssl = false; return *this; }
        create_webserver& use_ipv6() { _use_ipv6 = true; return *this; }
        create_webserver& no_ipv6() { _use_ipv6 = false; return *this; }
        create_webserver& debug() { _debug = true; return *this; }
        create_webserver& no_debug() { _debug = false; return *this; }
        create_webserver& pedantic() { _pedantic = true; return *this; }
        create_webserver& no_pedantic() { _pedantic = false; return *this; }
        create_webserver& https_mem_key(const std::string& https_mem_key);
        create_webserver& https_mem_cert(const std::string& https_mem_cert);
        create_webserver& https_mem_trust(const std::string& https_mem_trust);
        create_webserver& raw_https_mem_key(const std::string& https_mem_key) { _https_mem_key = https_mem_key; return *this; }
        create_webserver& raw_https_mem_cert(const std::string& https_mem_cert) { _https_mem_cert = https_mem_cert; return *this; }
        create_webserver& raw_https_mem_trust(const std::string& https_mem_trust) { _https_mem_trust = https_mem_trust; return *this; }
        create_webserver& https_priorities(const std::string& https_priorities) { _https_priorities = https_priorities; return *this; }
        create_webserver& cred_type(const http_utils::cred_type_T& cred_type) { _cred_type = cred_type; return *this; }
        create_webserver& digest_auth_random(const std::string& digest_auth_random) { _digest_auth_random = digest_auth_random; return *this; }
        create_webserver& nonce_nc_size(int nonce_nc_size) { _nonce_nc_size = nonce_nc_size; return *this; }
        create_webserver& default_policy(const http_utils::policy_T& default_policy) { _default_policy = default_policy; return *this; }
        create_webserver& basic_auth() { _basic_auth_enabled = true; return *this; }
        create_webserver& no_basic_auth() { _basic_auth_enabled = false; return *this; }
        create_webserver& digest_auth() { _digest_auth_enabled = true; return *this; }
        create_webserver& no_digest_auth() { _digest_auth_enabled = false; return *this; }
        create_webserver& regex_checking() { _regex_checking = true; return *this; }
        create_webserver& no_regex_checking() { _regex_checking = false; return *this; }
        create_webserver& ban_system() { _ban_system_enabled = true; return *this; }
        create_webserver& no_ban_system() { _ban_system_enabled = false; return *this; }
        create_webserver& post_process() { _post_process_enabled = true; return *this; }
        create_webserver& no_post_process() { _post_process_enabled = false; return *this; }
        create_webserver& single_resource(http_resource* single_resource) { _single_resource = single_resource; return *this; }
    private:
        int _port;
        http_utils::start_method_T _start_method;
        int _max_threads;
        int _max_connections;
        int _memory_limit;
        int _connection_timeout;
        int _per_IP_connection_limit;
        const logging_delegate* _log_delegate;
        const request_validator* _validator;
        const unescaper* _unescaper_pointer;
        const struct sockaddr* _bind_address;
        int _bind_socket;
        int _max_thread_stack_size;
        bool _use_ssl;
        bool _use_ipv6;
        bool _debug;
        bool _pedantic;
        std::string _https_mem_key;
        std::string _https_mem_cert;
        std::string _https_mem_trust;
        std::string _https_priorities;
        http_utils::cred_type_T _cred_type;
        std::string _digest_auth_random;
        int _nonce_nc_size;
        http_utils::policy_T _default_policy;
        bool _basic_auth_enabled;
        bool _digest_auth_enabled;
        bool _regex_checking;
        bool _ban_system_enabled;
        bool _post_process_enabled;
        http_resource* _single_resource;

        friend class webserver;
};

struct modded_request
{
    struct MHD_PostProcessor *pp;
    std::string* complete_uri;
    http_request *dhr;
    webserver* ws;
    bool second;
};

};
#endif //_FRAMEWORK_WEBSERVER_HPP__
