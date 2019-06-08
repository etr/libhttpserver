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

#include <stdint.h>
#include <inttypes.h>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdexcept>

#if defined(__MINGW32__) || defined(__CYGWIN32__)
#include <winsock2.h>
#define _WINDOWS
#else
#include <netinet/ip.h>
#endif

#include <signal.h>
#include <fcntl.h>
#include <algorithm>

#include <microhttpd.h>

#include "gettext.h"
#include "http_utils.hpp"
#include "http_resource.hpp"
#include "http_response.hpp"
#include "string_response.hpp"
#include "http_request.hpp"
#include "details/http_endpoint.hpp"
#include "string_utilities.hpp"
#include "create_webserver.hpp"
#include "webserver.hpp"
#include "details/modded_request.hpp"

#define _REENTRANT 1

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif

using namespace std;

namespace httpserver
{

using namespace http;

int policy_callback (void *, const struct sockaddr*, socklen_t);
void error_log(void*, const char*, va_list);
void* uri_log(void*, const char*);
void access_log(webserver*, string);
size_t unescaper_func(void*, struct MHD_Connection*, char*);

struct compare_value
{
    bool operator() (const std::pair<int, int>& left,
            const std::pair<int, int>& right
    ) const
    {
        return left.second < right.second;
    }
};

#ifndef __MINGW32__
static void catcher (int sig)
{
}
#endif

static void ignore_sigpipe ()
{
//Mingw doesn't implement SIGPIPE
#ifndef __MINGW32__
    struct sigaction oldsig;
    struct sigaction sig;

    sig.sa_handler = &catcher;
    sigemptyset (&sig.sa_mask);
#ifdef SA_INTERRUPT
    sig.sa_flags = SA_INTERRUPT;  /* SunOS */
#else //SA_INTERRUPT
    sig.sa_flags = SA_RESTART;
#endif //SA_INTERRUPTT
    if (0 != sigaction (SIGPIPE, &sig, &oldsig))
        fprintf (stderr,
                gettext("Failed to install SIGPIPE handler: %s\n"),
                strerror (errno)
        );
#endif
}

//WEBSERVER
webserver::webserver(const create_webserver& params):
    port(params._port),
    start_method(params._start_method),
    max_threads(params._max_threads),
    max_connections(params._max_connections),
    memory_limit(params._memory_limit),
    content_size_limit(params._content_size_limit),
    connection_timeout(params._connection_timeout),
    per_IP_connection_limit(params._per_IP_connection_limit),
    log_access(params._log_access),
    log_error(params._log_error),
    validator(params._validator),
    unescaper(params._unescaper),
    bind_address(params._bind_address),
    bind_socket(params._bind_socket),
    max_thread_stack_size(params._max_thread_stack_size),
    use_ssl(params._use_ssl),
    use_ipv6(params._use_ipv6),
    debug(params._debug),
    pedantic(params._pedantic),
    https_mem_key(params._https_mem_key),
    https_mem_cert(params._https_mem_cert),
    https_mem_trust(params._https_mem_trust),
    https_priorities(params._https_priorities),
    cred_type(params._cred_type),
    digest_auth_random(params._digest_auth_random),
    nonce_nc_size(params._nonce_nc_size),
    running(false),
    default_policy(params._default_policy),
    basic_auth_enabled(params._basic_auth_enabled),
    digest_auth_enabled(params._digest_auth_enabled),
    regex_checking(params._regex_checking),
    ban_system_enabled(params._ban_system_enabled),
    post_process_enabled(params._post_process_enabled),
    deferred_enabled(params._deferred_enabled),
    single_resource(params._single_resource),
    not_found_resource(params._not_found_resource),
    method_not_allowed_resource(params._method_not_allowed_resource),
    internal_error_resource(params._internal_error_resource),
    next_to_choose(0)
{
    ignore_sigpipe();
    pthread_mutex_init(&mutexwait, NULL);
    pthread_rwlock_init(&runguard, NULL);
    pthread_cond_init(&mutexcond, NULL);
}

webserver::~webserver()
{
    this->stop();
    pthread_mutex_destroy(&mutexwait);
    pthread_rwlock_destroy(&runguard);
    pthread_cond_destroy(&mutexcond);
}

void webserver::sweet_kill()
{
    this->stop();
}

void webserver::request_completed (
        void *cls,
        struct MHD_Connection *connection,
        void **con_cls,
        enum MHD_RequestTerminationCode toe
)
{
    details::modded_request* mr = static_cast<details::modded_request*>(*con_cls);
    if (mr == 0x0) return;

    delete mr;
    mr = 0x0;
}

bool webserver::register_resource(const std::string& resource, http_resource* hrm, bool family)
{
    if (single_resource && ((resource != "" && resource != "/") || !family))
    {
        throw std::invalid_argument("The resource should be '' or '/' and be marked as family when using a single_resource server");
    }

    details::http_endpoint idx(resource, family, true, regex_checking);

    pair<map<details::http_endpoint, http_resource*>::iterator, bool> result = registered_resources.insert(
        map<details::http_endpoint, http_resource*>::value_type(idx, hrm)
    );

    if(result.second)
    {
        registered_resources_str.insert(
            pair<string, http_resource*>(idx.get_url_complete(), result.first->second)
        );
    }

    return result.second;
}

bool webserver::start(bool blocking)
{

    struct {
        MHD_OptionItem operator ()(
                enum MHD_OPTION opt,
                intptr_t val,
                void *ptr = 0
        )
        {
            MHD_OptionItem x = {opt, val, ptr};
            return x;
        }
    } gen;
    vector<struct MHD_OptionItem> iov;

    iov.push_back(gen(MHD_OPTION_NOTIFY_COMPLETED,
                (intptr_t) &request_completed,
                NULL
    ));
    iov.push_back(gen(MHD_OPTION_URI_LOG_CALLBACK, (intptr_t) &uri_log, this));
    iov.push_back(gen(MHD_OPTION_EXTERNAL_LOGGER, (intptr_t) &error_log, this));
    iov.push_back(gen(MHD_OPTION_UNESCAPE_CALLBACK,
                (intptr_t) &unescaper_func,
                this)
    );
    iov.push_back(gen(MHD_OPTION_CONNECTION_TIMEOUT, connection_timeout));
    if(bind_socket != 0)
        iov.push_back(gen(MHD_OPTION_LISTEN_SOCKET, bind_socket));
    if(start_method == http_utils::THREAD_PER_CONNECTION && (max_threads != 0 || max_thread_stack_size != 0))
    {
        throw std::invalid_argument("Cannot specify maximum number of threads when using a thread per connection");
    }

    if(max_threads != 0)
        iov.push_back(gen(MHD_OPTION_THREAD_POOL_SIZE, max_threads));
    if(max_connections != 0)
        iov.push_back(gen(MHD_OPTION_CONNECTION_LIMIT, max_connections));
    if(memory_limit != 0)
        iov.push_back(gen(MHD_OPTION_CONNECTION_MEMORY_LIMIT, memory_limit));
    if(per_IP_connection_limit != 0)
        iov.push_back(gen(MHD_OPTION_PER_IP_CONNECTION_LIMIT,
                    per_IP_connection_limit)
        );
    if(max_thread_stack_size != 0)
        iov.push_back(gen(MHD_OPTION_THREAD_STACK_SIZE, max_thread_stack_size));
    if(nonce_nc_size != 0)
        iov.push_back(gen(MHD_OPTION_NONCE_NC_SIZE, nonce_nc_size));
    if(use_ssl)
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_KEY,
                    0,
                    (void*)https_mem_key.c_str())
        );
    if(use_ssl)
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_CERT,
                    0,
                    (void*)https_mem_cert.c_str())
        );
    if(https_mem_trust != "" && use_ssl)
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_TRUST,
                    0,
                    (void*)https_mem_trust.c_str())
        );
    if(https_priorities != "" && use_ssl)
        iov.push_back(gen(MHD_OPTION_HTTPS_PRIORITIES,
                    0,
                    (void*)https_priorities.c_str())
        );
    if(digest_auth_random != "")
        iov.push_back(gen(MHD_OPTION_DIGEST_AUTH_RANDOM,
                    digest_auth_random.size(),
                    (char*)digest_auth_random.c_str())
        );
#ifdef HAVE_GNUTLS
    if(cred_type != http_utils::NONE)
        iov.push_back(gen(MHD_OPTION_HTTPS_CRED_TYPE, cred_type));
#endif //HAVE_GNUTLS

    iov.push_back(gen(MHD_OPTION_END, 0, NULL ));

    int start_conf = start_method;
    if(use_ssl)
        start_conf |= MHD_USE_SSL;
    if(use_ipv6)
        start_conf |= MHD_USE_IPv6;
    if(debug)
        start_conf |= MHD_USE_DEBUG;
    if(pedantic)
        start_conf |= MHD_USE_PEDANTIC_CHECKS;
    if(deferred_enabled)
        start_conf |= MHD_USE_SUSPEND_RESUME;

#ifdef USE_FASTOPEN
    start_conf |= MHD_USE_TCP_FASTOPEN;
#endif

    this->daemon = NULL;
    if(bind_address == 0x0) {
        this->daemon = MHD_start_daemon
        (
                start_conf, this->port, &policy_callback, this,
                &answer_to_connection, this, MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_END
        );
    } else {
        this->daemon = MHD_start_daemon
        (
                start_conf, 1, &policy_callback, this,
                &answer_to_connection, this, MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_SOCK_ADDR, bind_address, MHD_OPTION_END
        );
    }

    if(this->daemon == NULL)
    {
        throw std::invalid_argument("Unable to connect daemon to port: " + std::to_string(this->port));
    }

    bool value_onclose = false;

    this->running = true;

    if(blocking)
    {
        pthread_mutex_lock(&mutexwait);
        while(blocking && running)
            pthread_cond_wait(&mutexcond, &mutexwait);
        pthread_mutex_unlock(&mutexwait);
        value_onclose = true;
    }
    return value_onclose;
}

bool webserver::is_running()
{
    return this->running;
}

bool webserver::stop()
{
    if(!this->running) return false;

    pthread_mutex_lock(&mutexwait);
    this->running = false;
    pthread_cond_signal(&mutexcond);
    pthread_mutex_unlock(&mutexwait);

    MHD_stop_daemon(this->daemon);

    shutdown(bind_socket, 2);

    return true;
}

void webserver::unregister_resource(const string& resource)
{
    details::http_endpoint he(resource);
    this->registered_resources.erase(he);
    this->registered_resources.erase(he.get_url_complete());
    this->registered_resources_str.erase(he.get_url_complete());
}

void webserver::ban_ip(const string& ip)
{
    ip_representation t_ip(ip);
    set<ip_representation>::iterator it = this->bans.find(t_ip);
    if(it != this->bans.end() && (t_ip.weight() < (*it).weight()))
    {
        this->bans.erase(it);
        this->bans.insert(t_ip);
    }
    else
        this->bans.insert(t_ip);
}

void webserver::allow_ip(const string& ip)
{
    ip_representation t_ip(ip);
    set<ip_representation>::iterator it = this->allowances.find(t_ip);
    if(it != this->allowances.end() && (t_ip.weight() < (*it).weight()))
    {
        this->allowances.erase(it);
        this->allowances.insert(t_ip);
    }
    else
        this->allowances.insert(t_ip);
}

void webserver::unban_ip(const string& ip)
{
    this->bans.erase(ip);
}

void webserver::disallow_ip(const string& ip)
{
    this->allowances.erase(ip);
}

int policy_callback (void *cls, const struct sockaddr* addr, socklen_t addrlen)
{
    if(!(static_cast<webserver*>(cls))->ban_system_enabled) return MHD_YES;

    if((((static_cast<webserver*>(cls))->default_policy == http_utils::ACCEPT) &&
       ((static_cast<webserver*>(cls))->bans.count(addr)) &&
       (!(static_cast<webserver*>(cls))->allowances.count(addr))
    ) ||
    (((static_cast<webserver*>(cls))->default_policy == http_utils::REJECT)
       && ((!(static_cast<webserver*>(cls))->allowances.count(addr)) ||
       ((static_cast<webserver*>(cls))->bans.count(addr)))
    ))
    {
        return MHD_NO;
    }

    return MHD_YES;
}

void* uri_log(void* cls, const char* uri)
{
    struct details::modded_request* mr = new details::modded_request();
    mr->complete_uri = new string(uri);
    mr->second = false;
    return ((void*)mr);
}

void error_log(void* cls, const char* fmt, va_list ap)
{
    webserver* dws = static_cast<webserver*>(cls);
    if(dws->log_error != 0x0) dws->log_error(fmt);
}

void access_log(webserver* dws, string uri)
{
    if(dws->log_access != 0x0) dws->log_access(uri);
}

size_t unescaper_func(void * cls, struct MHD_Connection *c, char *s)
{
    // THIS IS USED TO AVOID AN UNESCAPING OF URL BEFORE THE ANSWER.
    // IT IS DUE TO A BOGUS ON libmicrohttpd (V0.99) THAT PRODUCING A
    // STRING CONTAINING '\0' AFTER AN UNESCAPING, IS UNABLE TO PARSE
    // ARGS WITH get_connection_values FUNC OR lookup FUNC.
    return std::string(s).size();
}

int webserver::post_iterator (void *cls, enum MHD_ValueKind kind,
    const char *key,
    const char *filename,
    const char *content_type,
    const char *transfer_encoding,
    const char *data, uint64_t off, size_t size
    )
{
    struct details::modded_request* mr = (struct details::modded_request*) cls;
    mr->dhr->set_arg(key, mr->dhr->get_arg(key) + std::string(data, size));
    return MHD_YES;
}

void webserver::upgrade_handler (void *cls, struct MHD_Connection* connection,
    void **con_cls, int upgrade_socket)
{
}

const std::shared_ptr<http_response> webserver::not_found_page(details::modded_request* mr) const
{
    if(not_found_resource != 0x0)
    {
        return not_found_resource(*mr->dhr);
    }
    else
    {
        return std::shared_ptr<http_response>(new string_response(NOT_FOUND_ERROR, http_utils::http_not_found));
    }
}

const std::shared_ptr<http_response> webserver::method_not_allowed_page(details::modded_request* mr) const
{
    if(method_not_allowed_resource != 0x0)
    {
        return method_not_allowed_resource(*mr->dhr);
    }
    else
    {
        return std::shared_ptr<http_response>(new string_response(METHOD_ERROR, http_utils::http_method_not_allowed));
    }
}

const std::shared_ptr<http_response> webserver::internal_error_page(details::modded_request* mr, bool force_our) const
{
    if(internal_error_resource != 0x0 && !force_our)
    {
        return internal_error_resource(*mr->dhr);
    }
    else
    {
        return std::shared_ptr<http_response>(new string_response(GENERIC_ERROR, http_utils::http_internal_server_error, "text/plain"));
    }
}

int webserver::bodyless_requests_answer(
    MHD_Connection* connection, const char* method,
    const char* version, struct details::modded_request* mr
    )
{
    http_request req(connection, unescaper);
    mr->dhr = &(req);
    return complete_request(connection, mr, version, method);
}

int webserver::bodyfull_requests_answer_first_step(
        MHD_Connection* connection,
        struct details::modded_request* mr
)
{
    mr->second = true;
    mr->dhr = new http_request(connection, unescaper);
    mr->dhr->set_content_size_limit(content_size_limit);
    const char *encoding = MHD_lookup_connection_value (
            connection,
            MHD_HEADER_KIND,
            http_utils::http_header_content_type.c_str()
    );

    if ( post_process_enabled &&
        (
            0x0 != encoding &&
            ((0 == strncasecmp (
                                http_utils::http_post_encoding_form_urlencoded.c_str(),
                                encoding,
                                http_utils::http_post_encoding_form_urlencoded.size()
                                )
              )
             || (0 == strncasecmp (
                                   http_utils::http_post_encoding_multipart_formdata.c_str(),
                                   encoding,
                                   http_utils::http_post_encoding_multipart_formdata.size()
                                   )))
        )
    )
    {
        const size_t post_memory_limit (32*1024);  // Same as #MHD_POOL_SIZE_DEFAULT
        mr->pp = MHD_create_post_processor (
                connection,
                post_memory_limit,
                &post_iterator,
                mr
        );
    }
    else
    {
        mr->pp = NULL;
    }
    return MHD_YES;
}

int webserver::bodyfull_requests_answer_second_step(
    MHD_Connection* connection, const char* method,
    const char* version, const char* upload_data,
    size_t* upload_data_size, struct details::modded_request* mr
)
{
    if (0 == *upload_data_size) return complete_request(connection, mr, version, method);

#ifdef DEBUG
    cout << "Writing content: " << upload_data << endl;
#endif //DEBUG
    mr->dhr->grow_content(upload_data, *upload_data_size);

    if (mr->pp != NULL) MHD_post_process(mr->pp, upload_data, *upload_data_size);
    *upload_data_size = 0;
    return MHD_YES;
}

int webserver::finalize_answer(
        MHD_Connection* connection,
        struct details::modded_request* mr,
        const char* method
)
{
    int to_ret = MHD_NO;

    map<string, http_resource*>::iterator fe;

    http_resource* hrm;

    bool found = false;
    struct MHD_Response* raw_response;
    if(!single_resource)
    {
        const char* st_url = mr->standardized_url->c_str();
        fe = registered_resources_str.find(st_url);
        if(fe == registered_resources_str.end())
        {
            if(regex_checking)
            {

                map<details::http_endpoint, http_resource*>::iterator found_endpoint;

                details::http_endpoint endpoint(st_url, false, false, regex_checking);

                map<details::http_endpoint, http_resource*>::iterator it;

                size_t len = 0;
                size_t tot_len = 0;
                for(it=registered_resources.begin(); it!=registered_resources.end(); ++it)
                {
                    size_t endpoint_pieces_len = (*it).first.get_url_pieces().size();
                    size_t endpoint_tot_len = (*it).first.get_url_complete().size();
                    if(!found || endpoint_pieces_len > len || (endpoint_pieces_len == len && endpoint_tot_len > tot_len))
                    {
                        if((*it).first.match(endpoint))
                        {
                            found = true;
                            len = endpoint_pieces_len;
                            tot_len = endpoint_tot_len;
                            found_endpoint = it;
                        }
                    }
                }
                if(found)
                {
                    vector<string> url_pars = found_endpoint->first.get_url_pars();

                    vector<string> url_pieces = endpoint.get_url_pieces();
                    vector<int> chunks = found_endpoint->first.get_chunk_positions();
                    for(unsigned int i = 0; i < url_pars.size(); i++)
                    {
                        mr->dhr->set_arg(url_pars[i], url_pieces[chunks[i]]);
                    }

                    hrm = found_endpoint->second;
                }
            }
        }
        else
        {
            hrm = fe->second;
            found = true;
        }
    }
    else
    {
        hrm = registered_resources.begin()->second;
        found = true;
    }

    if(found)
    {
        try
        {
            if(hrm->is_allowed(method))
            {
                mr->dhrs = ((hrm)->*(mr->callback))(*mr->dhr); //copy in memory (move in case)
                if (mr->dhrs->get_response_code() == -1)
                {
                    mr->dhrs = internal_error_page(mr);
                }
            }
            else
            {
                mr->dhrs = method_not_allowed_page(mr);
            }
        }
        catch(const std::exception& e)
        {
            mr->dhrs = internal_error_page(mr);
        }
        catch(...)
        {
            mr->dhrs = internal_error_page(mr);
        }
    }
    else
    {
        mr->dhrs = not_found_page(mr);
    }

    try
    {
        try
        {
            raw_response = mr->dhrs->get_raw_response();
        }
        catch(const std::invalid_argument& iae)
        {
            mr->dhrs = not_found_page(mr);
            raw_response = mr->dhrs->get_raw_response();
        }
        catch(const std::exception& e)
        {
            mr->dhrs = internal_error_page(mr);
            raw_response = mr->dhrs->get_raw_response();
        }
        catch(...)
        {
            mr->dhrs = internal_error_page(mr);
            raw_response = mr->dhrs->get_raw_response();
        }
    }
    catch(...) // catches errors in internal error page
    {
        mr->dhrs = internal_error_page(mr, true);
        raw_response = mr->dhrs->get_raw_response();
    }
    mr->dhrs->decorate_response(raw_response);
    to_ret = mr->dhrs->enqueue_response(connection, raw_response);
    MHD_destroy_response(raw_response);
    return to_ret;
}

int webserver::complete_request(
        MHD_Connection* connection,
        struct details::modded_request* mr,
        const char* version,
        const char* method
)
{
    mr->ws = this;

    mr->dhr->set_path(mr->standardized_url->c_str());
    mr->dhr->set_method(method);
    mr->dhr->set_version(version);

    return finalize_answer(connection, mr, method);
}

int webserver::answer_to_connection(void* cls, MHD_Connection* connection,
    const char* url, const char* method,
    const char* version, const char* upload_data,
    size_t* upload_data_size, void** con_cls
    )
{
    struct details::modded_request* mr =
        static_cast<struct details::modded_request*>(*con_cls);

    if(mr->second != false)
    {
        return static_cast<webserver*>(cls)->
            bodyfull_requests_answer_second_step(
                    connection,
                    method,
                    version,
                    upload_data,
                    upload_data_size,
                    mr
            );
    }

    std::string t_url = url;

    base_unescaper(t_url, static_cast<webserver*>(cls)->unescaper);
    mr->standardized_url = new string(http_utils::standardize_url(t_url));

    bool body = false;

    access_log(
            static_cast<webserver*>(cls),
            *(mr->complete_uri) + " METHOD: " + method
    );

    if( 0 == strcasecmp(method, http_utils::http_method_get.c_str()))
    {
        mr->callback = &http_resource::render_GET;
    }
    else if (0 == strcmp(method, http_utils::http_method_post.c_str()))
    {
        mr->callback = &http_resource::render_POST;
        body = true;
    }
    else if (0 == strcasecmp(method, http_utils::http_method_put.c_str()))
    {
        mr->callback = &http_resource::render_PUT;
        body = true;
    }
    else if (0 == strcasecmp(method,http_utils::http_method_delete.c_str()))
    {
        mr->callback = &http_resource::render_DELETE;
    }
    else if (0 == strcasecmp(method, http_utils::http_method_head.c_str()))
    {
        mr->callback = &http_resource::render_HEAD;
    }
    else if (0 ==strcasecmp(method,http_utils::http_method_connect.c_str()))
    {
        mr->callback = &http_resource::render_CONNECT;
    }
    else if (0 == strcasecmp(method, http_utils::http_method_trace.c_str()))
    {
        mr->callback = &http_resource::render_TRACE;
    }
    else if (0 ==strcasecmp(method,http_utils::http_method_options.c_str()))
    {
        mr->callback = &http_resource::render_OPTIONS;
    }

    return body ? static_cast<webserver*>(cls)->bodyfull_requests_answer_first_step(connection, mr) : static_cast<webserver*>(cls)->bodyless_requests_answer(connection, method, version, mr);
}

};
