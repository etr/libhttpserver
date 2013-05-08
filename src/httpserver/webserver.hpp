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

#ifndef _FRAMEWORK_WEBSERVER_HPP_
#define _FRAMEWORK_WEBSERVER_HPP_

#define NOT_FOUND_ERROR "Not Found"
#define METHOD_ERROR "Method not Allowed"
#define NOT_METHOD_ERROR "Method not Acceptable"
#define GENERIC_ERROR "Internal Error"
#define DEFAULT_WS_PORT 9898
#define DEFAULT_WS_TIMEOUT 180

#define CREATE_METHOD_DETECTOR(X) \
    template<typename T, typename RESULT, typename ARG1, typename ARG2> \
    class has_##X \
    { \
        template <typename U, RESULT (U::*)(ARG1, ARG2)> struct Check; \
        template <typename U> static char func(Check<U, &U::X> *); \
        template <typename U> static int func(...); \
        public: \
            enum { value = sizeof(func<T>(0)) == sizeof(char) }; \
    };

#define HAS_METHOD(X, T, RESULT, ARG1, ARG2) \
    has_##X<T, RESULT, ARG1, ARG2>::value

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
#include <deque>

#include <pthread.h>
#include "httpserver/binders.hpp"

namespace httpserver {

template<typename CHILD>
class http_resource;
class http_response;
class cache_response;
class http_request;
class long_polling_receive_response;
class long_polling_send_response;
struct cache_entry;
class webserver;
template<typename CHILD>
class event_supplier;

typedef void(*render_ptr)(const http_request&, http_response**);

namespace details
{
    class http_endpoint;
    struct modded_request;
    struct daemon_item;
    typedef bool(*is_allowed_ptr)(const std::string&);

    CREATE_METHOD_DETECTOR(render);
    CREATE_METHOD_DETECTOR(render_GET);
    CREATE_METHOD_DETECTOR(render_POST);
    CREATE_METHOD_DETECTOR(render_PUT);
    CREATE_METHOD_DETECTOR(render_HEAD);
    CREATE_METHOD_DETECTOR(render_DELETE);
    CREATE_METHOD_DETECTOR(render_TRACE);
    CREATE_METHOD_DETECTOR(render_OPTIONS);
    CREATE_METHOD_DETECTOR(render_CONNECT);
    CREATE_METHOD_DETECTOR(render_not_acceptable);

    void empty_render(const http_request& r, http_response** res);
    void empty_not_acceptable_render(
            const http_request& r, http_response** res
    );
    bool empty_is_allowed(const std::string& method);

    class http_resource_mirror
    {
        public:
            http_resource_mirror()
            {
            }

            ~http_resource_mirror()
            {
            }
        private:
            typedef binders::functor_two<const http_request&,
                    http_response**, void> functor;

            typedef binders::functor_one<const std::string&,
                    bool> functor_allowed;

            const functor render;
            const functor render_GET;
            const functor render_POST;
            const functor render_PUT;
            const functor render_HEAD;
            const functor render_DELETE;
            const functor render_TRACE;
            const functor render_OPTIONS;
            const functor render_CONNECT;
            const functor_allowed is_allowed;

            functor method_not_acceptable_resource;

            http_resource_mirror& operator= (const http_resource_mirror& o)
            {
                return *this;
            }

            template<typename T>
            http_resource_mirror(http_resource<T>* res):
                render(
                    HAS_METHOD(render, T, void, 
                        const http_request&, http_response**
                    ) ? functor(res, &T::render) : functor(&empty_render)
                ),
                render_GET(
                    HAS_METHOD(render_GET, T, void, 
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_GET) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_POST(
                    HAS_METHOD(render_POST, T, void, 
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_POST) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_PUT(
                    HAS_METHOD(render_PUT, T, void, 
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_PUT) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_HEAD(
                    HAS_METHOD(render_HEAD, T, void, 
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_HEAD) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_DELETE(
                    HAS_METHOD(render_DELETE, T, void, 
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_DELETE) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_TRACE(
                    HAS_METHOD(render_TRACE, T, void, 
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_TRACE) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_OPTIONS(
                    HAS_METHOD(render_OPTIONS, T, void, 
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_OPTIONS) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_CONNECT(
                    HAS_METHOD(render_CONNECT, T, void, 
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_CONNECT) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                is_allowed(res, &T::is_allowed)
            {
            }

            friend class ::httpserver::webserver;
    };

    class event_tuple
    {
        private:
            typedef void(*supply_events_ptr)(
                            fd_set*, 
                            fd_set*, 
                            fd_set*, 
                            int*
                    );

            typedef struct timeval(*get_timeout_ptr)();
            typedef void(*dispatch_events_ptr)();
            supply_events_ptr supply_events;
            get_timeout_ptr get_timeout;
            dispatch_events_ptr dispatch_events;

            event_tuple();
            
            friend class ::httpserver::webserver;
        public: 
            template<typename T>
            event_tuple(event_supplier<T>* es):
                supply_events(std::bind1st(std::mem_fun(&T::supply_events),es)),
                get_timeout(std::bind1st(std::mem_fun(&T::get_timeout), es)),
                dispatch_events(std::bind1st(
                            std::mem_fun(&T::dispatch_events), es)
                )
            {
            }
    };
}

using namespace http;

namespace http {
struct ip_representation;
};

template <typename CHILD>
class event_supplier
{
    public:
        event_supplier()
        {
        }

        ~event_supplier()
        {
        }

        void supply_events(
                fd_set* read_fdset, 
                fd_set* write_fdset, 
                fd_set* exc_fdset, 
                int* max
        ) const
        {
            static_cast<CHILD*>(this)->supply_events(
                    read_fdset, write_fdset, exc_fdset, max
            );
        }

        struct timeval get_timeout() const
        {
            return static_cast<CHILD*>(this)->get_timeout();
        }

        void dispatch_events() const
        {
            static_cast<CHILD*>(this)->dispatch_events();
        }
};

class create_webserver;
typedef bool(*validator_ptr)(const std::string&);
typedef void(*unescaper_ptr)(char*);
typedef void(*log_access_ptr)(const std::string&);
typedef void(*log_error_ptr)(const std::string&);

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
            log_access_ptr log_access = 0x0,
            log_error_ptr log_error = 0x0,
            validator_ptr validator = 0x0,
            unescaper_ptr unescaper = 0x0,
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
            render_ptr single_resource = 0x0,
            render_ptr not_found_resource = 0x0,
            render_ptr method_not_allowed_resource = 0x0,
            render_ptr method_not_acceptable_resource = 0x0,
            render_ptr internal_error_resource = 0x0
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
        template <typename T>
        void register_resource(const std::string& resource,
                http_resource<T>* res, bool family = false
        )
        {
            details::http_resource_mirror hrm(res);
            register_resource(resource, hrm, family);
        }

        void unregister_resource(const std::string& resource);
        void ban_ip(const std::string& ip);
        void allow_ip(const std::string& ip);
        void unban_ip(const std::string& ip);
        void disallow_ip(const std::string& ip);

        void send_message_to_topic(const std::string& topic,
                const std::string& message
        );
        void send_message_to_consumer(const httpserver_ska& connection_id,
                const std::string& message, bool to_lock = true
        );
        void register_to_topics(const std::vector<std::string>& topics, 
                const httpserver_ska& connection_id, int keepalive_secs = -1, 
                std::string keepalive_msg = ""
        );
        size_t read_message(const httpserver_ska& connection_id,
            std::string& message
        );
        size_t get_topic_consumers(const std::string& topic,
                std::set<httpserver_ska>& consumers
        );
        bool pop_signaled(const httpserver_ska& consumer);

        http_response* get_from_cache(const std::string& key, bool* valid,
                bool lock = false, bool write = false
        );
        http_response* get_from_cache(const std::string& key, bool* valid,
                cache_entry** ce, bool lock = false, bool write = false
        );
        void lock_cache_element(cache_entry* ce, bool write = false);
        void unlock_cache_element(cache_entry* ce);
        cache_entry* put_in_cache(const std::string& key, http_response* value,
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
        
        void set_access_logger(log_access_ptr log_access)
        {
            this->log_access = log_access;
        }

        void set_error_logger(log_error_ptr log_error)
        {
            this->log_error = log_error;
        }

        const validator_ptr get_request_validator() const
        {
            return this->validator;
        }

        void set_request_validator(validator_ptr validator)
        {
            this->validator = validator;
        }

        const unescaper_ptr get_unescaper() const
        {
            return this->unescaper;
        }

        void set_unescaper(unescaper_ptr unescaper)
        {
            this->unescaper = unescaper;
        }

        template<typename T>
        void register_event_supplier(const std::string& id,
                event_supplier<T>* ev_supplier
        )
        {
            pthread_rwlock_wrlock(&runguard);
            std::map<std::string, details::event_tuple>::iterator it =
                event_suppliers.find(id);
            if(it != event_suppliers.end())
                delete it->second;
            event_suppliers[id] = details::event_tuple(&ev_supplier);
            pthread_rwlock_unlock(&runguard);
        }


        void remove_event_supplier(const std::string& id);

        /**
         * Method used to kill the webserver waiting for it to terminate
        **/
        void sweet_kill();
    private:
        const int port;
        http_utils::start_method_T start_method;
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
        int bind_socket;
        const int max_thread_stack_size;
        const bool use_ssl;
        const bool use_ipv6;
        const bool debug;
        const bool pedantic;
        const std::string https_mem_key;
        const std::string https_mem_cert;
        const std::string https_mem_trust;
        const std::string https_priorities;
        const http_utils::cred_type_T cred_type;
        const std::string digest_auth_random;
        const int nonce_nc_size;
        bool running;
        const http_utils::policy_T default_policy;
        const bool basic_auth_enabled;
        const bool digest_auth_enabled;
        const bool regex_checking;
        const bool ban_system_enabled;
        const bool post_process_enabled;
        bool single_resource;
        pthread_mutex_t mutexwait;
        pthread_rwlock_t runguard;
        pthread_mutex_t cleanmux;
        pthread_cond_t mutexcond;
        pthread_cond_t cleancond;
        render_ptr not_found_resource;
        render_ptr method_not_allowed_resource;
        render_ptr method_not_acceptable_resource;
        render_ptr internal_error_resource;
        std::map<details::http_endpoint, details::http_resource_mirror> registered_resources;
        std::map<std::string, details::http_resource_mirror*> registered_resources_str;

        std::map<std::string, cache_entry*> response_cache;
        int next_to_choose;
        pthread_rwlock_t cache_guard;
#ifdef USE_CPP_ZEROX
        std::unordered_set<ip_representation> bans;
        std::unordered_set<ip_representation> allowances;
#else
        std::set<ip_representation> bans;
        std::set<ip_representation> allowances;
#endif

        std::map<httpserver_ska, std::deque<std::string> > q_messages;
        std::map<std::string, std::set<httpserver_ska> > q_waitings;
        std::map<httpserver_ska, std::pair<pthread_mutex_t, pthread_cond_t> > q_blocks;
        std::set<httpserver_ska> q_signal;
        std::map<httpserver_ska, long> q_keepalives;
        std::map<httpserver_ska, std::pair<int, std::string> > q_keepalives_mem;
        pthread_rwlock_t comet_guard;

        std::vector<details::daemon_item*> daemons;
        std::vector<pthread_t> threads;

        std::map<std::string, details::event_tuple> event_suppliers;

        webserver& operator=(const webserver& b);

        void init(render_ptr single_resource);
        static void* select(void* self);
        static void* cleaner(void* self);

        void register_resource(const std::string& resource,
                details::http_resource_mirror hrm, bool family = false
        );

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

        static void unlock_cache_entry(cache_entry*);
        static void lock_cache_entry(cache_entry*);
        static void get_response(cache_entry*, http_response** res);

        int bodyless_requests_answer(MHD_Connection* connection,
            const char* url, const char* method,
            const char* version, struct details::modded_request* mr
        );

        int bodyfull_requests_answer_first_step(MHD_Connection* connection,
                struct details::modded_request* mr
        );

        int bodyfull_requests_answer_second_step(MHD_Connection* connection,
            const char* url, const char* method,
            const char* version, const char* upload_data,
            size_t* upload_data_size, struct details::modded_request* mr
        );

        void end_request_construction(MHD_Connection* connection, 
                struct details::modded_request* mr, const char* version, 
                const char* st_url, const char* method, 
                char* user, char* pass, char* digested_user
        );

        int finalize_answer(MHD_Connection* connection, 
                struct details::modded_request* mr, const char* st_url, 
                const char* method
        );

        int complete_request(MHD_Connection* connection, 
                struct details::modded_request* mr, const char* version, 
                const char* st_url, const char* method 
        );

        bool use_internal_select()
        {
            return this->start_method == http_utils::INTERNAL_SELECT;
        }

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
            _log_access(0x0),
            _log_error(0x0),
            _validator(0x0),
            _unescaper(0x0),
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
            _single_resource(0x0),
            _not_found_resource(0x0),
            _method_not_allowed_resource(0x0),
            _method_not_acceptable_resource(0x0),
            _internal_error_resource(0x0)
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
            _log_access(0x0),
            _log_error(0x0),
            _validator(0x0),
            _unescaper(0x0),
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
            _single_resource(0x0),
            _not_found_resource(0x0),
            _method_not_allowed_resource(0x0),
            _method_not_acceptable_resource(0x0),
            _internal_error_resource(0x0)
        {
        }

        create_webserver& port(int port) { _port = port; return *this; }
        create_webserver& start_method(
                const http_utils::start_method_T& start_method
        )
        {
            _start_method = start_method; return *this;
        }
        create_webserver& max_threads(int max_threads)
        {
            _max_threads = max_threads; return *this;
        }
        create_webserver& max_connections(int max_connections)
        {
            _max_connections = max_connections; return *this;
        }
        create_webserver& memory_limit(int memory_limit)
        {
            _memory_limit = memory_limit; return *this;
        }
        create_webserver& connection_timeout(int connection_timeout)
        {
            _connection_timeout = connection_timeout; return *this;
        }
        create_webserver& per_IP_connection_limit(int per_IP_connection_limit)
        {
            _per_IP_connection_limit = per_IP_connection_limit; return *this;
        }
        create_webserver& log_access(log_access_ptr log_access)
        {
            _log_access = log_access; return *this;
        }
        create_webserver& log_error(log_error_ptr log_error)
        {
            _log_error = log_error; return *this;
        }
        create_webserver& validator(validator_ptr validator)
        {
            _validator = validator; return *this;
        }
        create_webserver& unescaper(unescaper_ptr unescaper)
        {
            _unescaper = unescaper; return *this;
        }
        create_webserver& bind_address(const struct sockaddr* bind_address)
        {
            _bind_address = bind_address; return *this;
        }
        create_webserver& bind_socket(int bind_socket)
        {
            _bind_socket = bind_socket; return *this;
        }
        create_webserver& max_thread_stack_size(int max_thread_stack_size)
        {
            _max_thread_stack_size = max_thread_stack_size; return *this;
        }
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
        create_webserver& raw_https_mem_key(const std::string& https_mem_key)
        {
            _https_mem_key = https_mem_key; return *this;
        }
        create_webserver& raw_https_mem_cert(const std::string& https_mem_cert)
        {
            _https_mem_cert = https_mem_cert; return *this;
        }
        create_webserver& raw_https_mem_trust(
                const std::string& https_mem_trust
        )
        {
            _https_mem_trust = https_mem_trust; return *this;
        }
        create_webserver& https_priorities(const std::string& https_priorities)
        {
            _https_priorities = https_priorities; return *this;
        }
        create_webserver& cred_type(const http_utils::cred_type_T& cred_type)
        {
            _cred_type = cred_type; return *this;
        }
        create_webserver& digest_auth_random(
                const std::string& digest_auth_random
        )
        {
            _digest_auth_random = digest_auth_random; return *this;
        }
        create_webserver& nonce_nc_size(int nonce_nc_size)
        {
            _nonce_nc_size = nonce_nc_size; return *this;
        }
        create_webserver& default_policy(
                const http_utils::policy_T& default_policy
        )
        {
            _default_policy = default_policy; return *this;
        }
        create_webserver& basic_auth()
        {
            _basic_auth_enabled = true; return *this;
        }
        create_webserver& no_basic_auth()
        {
            _basic_auth_enabled = false; return *this;
        }
        create_webserver& digest_auth()
        {
            _digest_auth_enabled = true; return *this;
        }
        create_webserver& no_digest_auth()
        {
            _digest_auth_enabled = false; return *this;
        }
        create_webserver& regex_checking()
        {
            _regex_checking = true; return *this;
        }
        create_webserver& no_regex_checking()
        {
            _regex_checking = false; return *this;
        }
        create_webserver& ban_system()
        {
            _ban_system_enabled = true; return *this;
        }
        create_webserver& no_ban_system()
        {
            _ban_system_enabled = false; return *this;
        }
        create_webserver& post_process()
        {
            _post_process_enabled = true; return *this;
        }
        create_webserver& no_post_process()
        {
            _post_process_enabled = false; return *this;
        }

        create_webserver& single_resource(render_ptr single_resource)
        {
            _single_resource = single_resource; return *this;
        }
        create_webserver& not_found_resource(render_ptr not_found_resource)
        {
            _not_found_resource = not_found_resource; return *this;
        }
        create_webserver& method_not_allowed_resource(
                render_ptr method_not_allowed_resource
        )
        {
            _method_not_allowed_resource = method_not_allowed_resource;
            return *this;
        }
        create_webserver& method_not_acceptable_resource(
                render_ptr method_not_acceptable_resource
        )
        {
            _method_not_acceptable_resource = method_not_acceptable_resource;
            return *this;
        }
        create_webserver& internal_error_resource(
                render_ptr internal_error_resource
        )
        {
            _internal_error_resource = internal_error_resource; return *this; 
        }

    private:
        int _port;
        http_utils::start_method_T _start_method;
        int _max_threads;
        int _max_connections;
        int _memory_limit;
        int _connection_timeout;
        int _per_IP_connection_limit;
        log_access_ptr _log_access;
        log_error_ptr _log_error;
        validator_ptr _validator;
        unescaper_ptr _unescaper;
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
        render_ptr _single_resource;
        render_ptr _not_found_resource;
        render_ptr _method_not_allowed_resource;
        render_ptr _method_not_acceptable_resource;
        render_ptr _internal_error_resource;

        friend class webserver;
};

};
#endif //_FRAMEWORK_WEBSERVER_HPP__
