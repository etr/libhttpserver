/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino

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

#ifndef _FRAMEWORK_WEBSERVER_HPP_
#define _FRAMEWORK_WEBSERVER_HPP_

#define NOT_FOUND_ERROR "Not Found"
#define METHOD_ERROR "Method not Allowed"
#define NOT_METHOD_ERROR "Method not Acceptable"
#define GENERIC_ERROR "Internal Error"

#include <cstring>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <utility>
#include <stdlib.h>
#include <memory>
#include <deque>

#include <pthread.h>
#include <stdexcept>

#include "httpserver/create_webserver.hpp"

namespace httpserver
{

class http_resource;
class http_response;
class create_webserver;

namespace http
{

struct ip_representation;
struct httpserver_ska;
} // namespace http

namespace details
{
    class http_endpoint;
    struct daemon_item;
    struct modded_request;
    struct cache_entry;
    class comet_manager;
}

class webserver_exception : public std::runtime_error
{
public:
	webserver_exception()
	: std::runtime_error("httpserver runtime error")
	{
	}
};

/**
 * Class representing the webserver. Main class of the apis.
**/
class webserver
{
    public:
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
         * Method used to register a resource with the webserver.
         * @param resource The url pointing to the resource. This url could be also parametrized in the form /path/to/url/{par1}/and/{par2}
         *                 or a regular expression.
         * @param http_resource http_resource pointer to register.
         * @param family boolean indicating whether the resource is registered for the endpoint and its child or not.
         * @return true if the resource was registered
        **/
        bool register_resource(const std::string& resource,
                http_resource* res, bool family = false
        );

        void unregister_resource(const std::string& resource);
        void ban_ip(const std::string& ip);
        void allow_ip(const std::string& ip);
        void unban_ip(const std::string& ip);
        void disallow_ip(const std::string& ip);

        void send_message_to_topic(const std::string& topic,
                const std::string& message
        );
        void register_to_topics(const std::vector<std::string>& topics,
                MHD_Connection* connection_id
        );
        size_t read_message(MHD_Connection* connection_id,
            std::string& message
        );

        http_response* get_from_cache(const std::string& key, bool* valid,
                bool lock = false, bool write = false
        );
        http_response* get_from_cache(const std::string& key, bool* valid,
                details::cache_entry** ce, bool lock = false, bool write = false
        );
        void lock_cache_element(details::cache_entry* ce, bool write = false);
        void unlock_cache_element(details::cache_entry* ce);
        details::cache_entry* put_in_cache(const std::string& key, http_response* value,
                bool* new_elem, bool lock = false,
                bool write = false, int validity = -1
        );
        void remove_from_cache(const std::string& key);
        bool is_valid(const std::string& key);
        void clean_cache();

        const log_access_ptr get_access_logger() const
        {
            return this->log_access;
        }

        const log_error_ptr get_error_logger() const
        {
            return this->log_error;
        }

        const validator_ptr get_request_validator() const
        {
            return this->validator;
        }

        const unescaper_ptr get_unescaper() const
        {
            return this->unescaper;
        }

        /**
         * Method used to kill the webserver waiting for it to terminate
        **/
        void sweet_kill();

    protected:
        webserver& operator=(const webserver& other);

    private:
        const uint16_t port;
        http::http_utils::start_method_T start_method;
        const int max_threads;
        const int max_connections;
        const int memory_limit;
        const int connection_timeout;
        const int per_IP_connection_limit;
        log_access_ptr log_access;
        log_error_ptr log_error;
        validator_ptr validator;
        unescaper_ptr unescaper;
        const struct sockaddr* bind_address;
        /* Changed type to MHD_socket because this type will always reflect the
        platform's actual socket type (e.g. SOCKET on windows, int on unixes)*/
        MHD_socket bind_socket;
        const int max_thread_stack_size;
        const bool use_ssl;
        const bool use_ipv6;
        const bool debug;
        const bool pedantic;
        const std::string https_mem_key;
        const std::string https_mem_cert;
        const std::string https_mem_trust;
        const std::string https_priorities;
        const http::http_utils::cred_type_T cred_type;
        const std::string digest_auth_random;
        const int nonce_nc_size;
        bool running;
        const http::http_utils::policy_T default_policy;
        const bool basic_auth_enabled;
        const bool digest_auth_enabled;
        const bool regex_checking;
        const bool ban_system_enabled;
        const bool post_process_enabled;
        const bool comet_enabled;
        bool single_resource;
        pthread_mutex_t mutexwait;
        pthread_rwlock_t runguard;
        pthread_cond_t mutexcond;
        render_ptr not_found_resource;
        render_ptr method_not_allowed_resource;
        render_ptr method_not_acceptable_resource;
        render_ptr internal_error_resource;
        std::map<details::http_endpoint, http_resource*> registered_resources;
        std::map<std::string, http_resource*> registered_resources_str;

        std::map<std::string, details::cache_entry*> response_cache;
        int next_to_choose;
        pthread_rwlock_t cache_guard;
        std::set<http::ip_representation> bans;
        std::set<http::ip_representation> allowances;

        std::vector<details::daemon_item*> daemons;
        std::vector<pthread_t> threads;

        details::comet_manager* internal_comet_manager;

        static void* select(void* self);
        static void* cleaner(void* self);

        void method_not_allowed_page(http_response** dhrs,
                details::modded_request* mr
        );
        void internal_error_page(http_response** dhrs,
                details::modded_request* mr, bool force_our = false
        );
        void not_found_page(http_response** dhrs, details::modded_request* mr);

        static int method_not_acceptable_page
        (
            const void *cls,
            struct MHD_Connection *connection
        );
        static void request_completed(void *cls,
                struct MHD_Connection *connection, void **con_cls,
                enum MHD_RequestTerminationCode toe
        );
        static int build_request_header (void *cls, enum MHD_ValueKind kind,
                const char *key, const char *value
        );
        static int build_request_footer (void *cls, enum MHD_ValueKind kind,
                const char *key, const char *value
        );
        static int build_request_cookie (void *cls, enum MHD_ValueKind kind,
                const char *key, const char *value
        );
        static int build_request_args (void *cls, enum MHD_ValueKind kind,
                const char *key, const char *value
        );
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

        static void unlock_cache_entry(details::cache_entry*);
        static void lock_cache_entry(details::cache_entry*);
        static void get_response(details::cache_entry*, http_response** res);

        int bodyless_requests_answer(MHD_Connection* connection,
            const char* method, const char* version,
            struct details::modded_request* mr
        );

        int bodyfull_requests_answer_first_step(MHD_Connection* connection,
                struct details::modded_request* mr
        );

        int bodyfull_requests_answer_second_step(MHD_Connection* connection,
            const char* method, const char* version, const char* upload_data,
            size_t* upload_data_size, struct details::modded_request* mr
        );

        void end_request_construction(MHD_Connection* connection,
                struct details::modded_request* mr, const char* version,
                const char* method, char* user, char* pass, char* digested_user
        );

        int finalize_answer(MHD_Connection* connection,
                struct details::modded_request* mr, const char* method
        );

        int complete_request(MHD_Connection* connection,
                struct details::modded_request* mr,
                const char* version, const char* method
        );

        friend int policy_callback (void *cls,
                const struct sockaddr* addr, socklen_t addrlen
        );
        friend void error_log(void* cls, const char* fmt, va_list ap);
        friend void access_log(webserver* cls, std::string uri);
        friend void* uri_log(void* cls, const char* uri);
        friend size_t unescaper_func(void * cls,
                struct MHD_Connection *c, char *s
        );
        friend size_t internal_unescaper(void * cls, char *s);
        friend class http_response;
};

} // namespace httpserver
#endif //_FRAMEWORK_WEBSERVER_HPP__
