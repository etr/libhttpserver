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

#ifndef SRC_HTTPSERVER_WEBSERVER_HPP_
#define SRC_HTTPSERVER_WEBSERVER_HPP_

#define NOT_FOUND_ERROR "Not Found"
#define METHOD_ERROR "Method not Allowed"
#define NOT_METHOD_ERROR "Method not Acceptable"
#define GENERIC_ERROR "Internal Error"

#include <microhttpd.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#if !defined(__MINGW32__)
#include <sys/socket.h>
#endif

#include <map>
#include <memory>
#include <set>
#include <string>

#include "httpserver/http_utils.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/details/http_endpoint.hpp"

namespace httpserver { class http_resource; }
namespace httpserver { class http_response; }
namespace httpserver { namespace details { struct modded_request; } }

struct MHD_Connection;

namespace httpserver {

/**
 * Class representing the webserver. Main class of the apis.
**/
class webserver {
 public:
     // Keeping this non explicit on purpose to easy construction through builder class.
     webserver(const create_webserver& params);  // NOLINT(runtime/explicit)
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
     bool register_resource(const std::string& resource, http_resource* res, bool family = false);

     void unregister_resource(const std::string& resource);
     void ban_ip(const std::string& ip);
     void allow_ip(const std::string& ip);
     void unban_ip(const std::string& ip);
     void disallow_ip(const std::string& ip);

     log_access_ptr get_access_logger() const {
         return log_access;
     }

     log_error_ptr get_error_logger() const {
         return log_error;
     }

     validator_ptr get_request_validator() const {
         return validator;
     }

     unescaper_ptr get_unescaper() const {
         return unescaper;
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
     const size_t content_size_limit;
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
     const bool use_dual_stack;
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
     const bool put_processed_data_to_content;
     const file_upload_target_T file_upload_target;
     const std::string file_upload_dir;
     const bool generate_random_filename_on_upload;
     const bool deferred_enabled;
     bool single_resource;
     bool tcp_nodelay;
     pthread_mutex_t mutexwait;
     pthread_cond_t mutexcond;
     render_ptr not_found_resource;
     render_ptr method_not_allowed_resource;
     render_ptr internal_error_resource;
     std::map<details::http_endpoint, http_resource*> registered_resources;
     std::map<std::string, http_resource*> registered_resources_str;

     std::set<http::ip_representation> bans;
     std::set<http::ip_representation> allowances;

     struct MHD_Daemon* daemon;

     std::shared_ptr<http_response> method_not_allowed_page(details::modded_request* mr) const;
     std::shared_ptr<http_response> internal_error_page(details::modded_request* mr, bool force_our = false) const;
     std::shared_ptr<http_response> not_found_page(details::modded_request* mr) const;

     static void request_completed(void *cls,
             struct MHD_Connection *connection, void **con_cls,
             enum MHD_RequestTerminationCode toe);

     static MHD_Result answer_to_connection(void* cls, MHD_Connection* connection, const char* url,
             const char* method, const char* version, const char* upload_data,
             size_t* upload_data_size, void** con_cls);

     static MHD_Result post_iterator(void *cls, enum MHD_ValueKind kind, const char *key,
             const char *filename, const char *content_type, const char *transfer_encoding,
             const char *data, uint64_t off, size_t size);

     static void upgrade_handler(void *cls, struct MHD_Connection* connection, void **con_cls, int upgrade_socket);

     MHD_Result requests_answer_first_step(MHD_Connection* connection, struct details::modded_request* mr);

     MHD_Result requests_answer_second_step(MHD_Connection* connection,
             const char* method, const char* version, const char* upload_data,
             size_t* upload_data_size, struct details::modded_request* mr);

     MHD_Result finalize_answer(MHD_Connection* connection, struct details::modded_request* mr, const char* method);

     MHD_Result complete_request(MHD_Connection* connection, struct details::modded_request* mr, const char* version, const char* method);

     friend MHD_Result policy_callback(void *cls, const struct sockaddr* addr, socklen_t addrlen);
     friend void error_log(void* cls, const char* fmt, va_list ap);
     friend void access_log(webserver* cls, std::string uri);
     friend void* uri_log(void* cls, const char* uri);
     friend size_t unescaper_func(void * cls, struct MHD_Connection *c, char *s);
     friend class http_response;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_WEBSERVER_HPP_
