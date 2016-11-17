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
#include "http_request.hpp"
#include "http_response_builder.hpp"
#include "details/http_endpoint.hpp"
#include "string_utilities.hpp"
#include "create_webserver.hpp"
#include "details/comet_manager.hpp"
#include "webserver.hpp"
#include "details/modded_request.hpp"
#include "details/cache_entry.hpp"

#define _REENTRANT 1

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif

#define NEW_OR_MOVE(TYPE, VALUE) new TYPE(VALUE)

using namespace std;

namespace httpserver
{

namespace details
{

struct daemon_item
{
    webserver* ws;
    struct MHD_Daemon* daemon;
    daemon_item(webserver* ws, struct MHD_Daemon* daemon):
        ws(ws),
        daemon(daemon)
    {
    }
    ~daemon_item()
    {
        MHD_stop_daemon (this->daemon);
    }
};

}

using namespace http;

int policy_callback (void *, const struct sockaddr*, socklen_t);
void error_log(void*, const char*, va_list);
void* uri_log(void*, const char*);
void access_log(webserver*, string);
size_t unescaper_func(void*, struct MHD_Connection*, char*);
size_t internal_unescaper(void*, char*);

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
    comet_enabled(params._comet_enabled),
    single_resource(params._single_resource),
    not_found_resource(params._not_found_resource),
    method_not_allowed_resource(params._method_not_allowed_resource),
    method_not_acceptable_resource(params._method_not_acceptable_resource),
    internal_error_resource(params._internal_error_resource),
    next_to_choose(0),
    internal_comet_manager(new details::comet_manager())
{
    if(single_resource != 0x0)
        this->single_resource = true;
    else
        this->single_resource = false;
    ignore_sigpipe();
    pthread_mutex_init(&mutexwait, NULL);
    pthread_rwlock_init(&runguard, NULL);
    pthread_cond_init(&mutexcond, NULL);
    pthread_rwlock_init(&cache_guard, NULL);
}

webserver::~webserver()
{
    this->stop();
    pthread_mutex_destroy(&mutexwait);
    pthread_rwlock_destroy(&runguard);
    pthread_rwlock_destroy(&cache_guard);
    pthread_cond_destroy(&mutexcond);
    delete internal_comet_manager;
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

    if (mr->ws != 0x0) mr->ws->internal_comet_manager->complete_request(mr->dhrs->connection_id);

    delete mr;
    mr = 0x0;
}

bool webserver::register_resource(const std::string& resource, http_resource* hrm, bool family)
{
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

MHD_socket create_socket (int domain, int type, int protocol)
{
    int sock_cloexec = SOCK_CLOEXEC;
    int ctype = SOCK_STREAM | sock_cloexec;

    /* use SOCK_STREAM rather than ai_socktype: some getaddrinfo
    * implementations do not set ai_socktype, e.g. RHL6.2. */
    MHD_socket fd = socket(domain, ctype, protocol);

#ifdef _WINDOWS
    if (fd == INVALID_SOCKET)
#else
    if ((fd == -1) &&
        (errno == EINVAL || errno == EPROTONOSUPPORT) && (sock_cloexec != 0)
    )
#endif
    {
        fd = socket(domain, type, protocol);
    }
    return fd;
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
    if(bind_address != 0x0)
        iov.push_back(gen(MHD_OPTION_SOCK_ADDR, (intptr_t) bind_address));
    if(bind_socket != 0)
        iov.push_back(gen(MHD_OPTION_LISTEN_SOCKET, bind_socket));
    if(start_method == http_utils::THREAD_PER_CONNECTION && max_threads != 0)
    {
        cout << "Cannot specify maximum number of threads when using a thread per connection" << endl;
        throw ::httpserver::webserver_exception();
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
    if(comet_enabled)
        start_conf |= MHD_USE_SUSPEND_RESUME;

#ifdef USE_FASTOPEN
    start_conf |= MHD_USE_TCP_FASTOPEN;
#endif

    struct MHD_Daemon* daemon = MHD_start_daemon
    (
            start_conf, this->port, &policy_callback, this,
            &answer_to_connection, this, MHD_OPTION_ARRAY,
            &iov[0], MHD_OPTION_END
    );
    if(NULL == daemon)
    {
        cout << gettext("Unable to connect daemon to port: ") <<
            this->port << endl;
        throw ::httpserver::webserver_exception();
    }
    details::daemon_item* di = new details::daemon_item(this, daemon);
    daemons.push_back(di);

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
    for(unsigned int i = 0; i < threads.size(); ++i)
    {
        void* t_res;
        pthread_join(threads[i], &t_res);
        free(t_res);
    }
    threads.clear();
    typedef vector<details::daemon_item*>::const_iterator daemon_item_it;

    for(daemon_item_it it = daemons.begin(); it != daemons.end(); ++it)
        delete *it;
    daemons.clear();

    shutdown(bind_socket, 2);

    return true;
}

void webserver::unregister_resource(const string& resource)
{
    details::http_endpoint he(resource);
    this->registered_resources.erase(he);
    this->registered_resources.erase(he.url_complete);
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

int webserver::build_request_header (
        void *cls,
        enum MHD_ValueKind kind,
        const char *key,
        const char *value
)
{
    http_request* dhr = static_cast<http_request*>(cls);
    dhr->set_header(key, value);
    return MHD_YES;
}

int webserver::build_request_cookie (
        void *cls,
        enum MHD_ValueKind kind,
        const char *key,
        const char *value
)
{
    http_request* dhr = static_cast<http_request*>(cls);
    dhr->set_cookie(key, value);
    return MHD_YES;
}

int webserver::build_request_footer (
        void *cls,
        enum MHD_ValueKind kind,
        const char *key,
        const char *value
)
{
    http_request* dhr = static_cast<http_request*>(cls);
    dhr->set_footer(key, value);
    return MHD_YES;
}

int webserver::build_request_args (
        void *cls,
        enum MHD_ValueKind kind,
        const char *key,
        const char *arg_value
)
{
    details::modded_request* mr = static_cast<details::modded_request*>(cls);
    char* value = (char*) ((arg_value == NULL) ? "" : arg_value);
    {
        char buf[strlen(key) + strlen(value) + 3];
        if(mr->dhr->querystring == "")
        {
            snprintf(buf, sizeof buf, "?%s=%s", key, value);
            mr->dhr->querystring = buf;
        }
        else
        {
            snprintf(buf, sizeof buf, "&%s=%s", key, value);
            mr->dhr->querystring += string(buf);
        }
    }
    size_t size = internal_unescaper((void*) mr->ws, value);
    mr->dhr->set_arg(key, string(value, size));
    return MHD_YES;
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
    return strlen(s);
}

size_t internal_unescaper(void* cls, char* s)
{
    if(s[0] == 0) return 0;

    webserver* dws = static_cast<webserver*>(cls);
    if(dws->unescaper != 0x0)
    {
        dws->unescaper(s);
        return strlen(s);
    }

    return http_unescape(s);
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

const http_response webserver::not_found_page(details::modded_request* mr) const
{
    if(not_found_resource != 0x0)
    {
        return not_found_resource(*mr->dhr);
    }
    else
    {
        return http_response_builder(NOT_FOUND_ERROR, http_utils::http_not_found).string_response();
    }
}

const http_response webserver::method_not_allowed_page(details::modded_request* mr) const
{
    if(method_not_acceptable_resource != 0x0)
    {
        return method_not_allowed_resource(*mr->dhr);
    }
    else
    {
        return http_response_builder(METHOD_ERROR, http_utils::http_method_not_allowed).string_response();
    }
}

const http_response webserver::internal_error_page(details::modded_request* mr, bool force_our) const
{
    if(internal_error_resource != 0x0 && !force_our)
        return internal_error_resource(*mr->dhr);
    else
        return http_response_builder(GENERIC_ERROR, http_utils::http_internal_server_error).string_response();
}

int webserver::bodyless_requests_answer(
    MHD_Connection* connection, const char* method,
    const char* version, struct details::modded_request* mr
    )
{
    http_request req;
    mr->dhr = &(req);
    return complete_request(connection, mr, version, method);
}

int webserver::bodyfull_requests_answer_first_step(
        MHD_Connection* connection,
        struct details::modded_request* mr
)
{
    mr->second = true;
    mr->dhr = new http_request();
    mr->dhr->set_content_size_limit(content_size_limit);
    const char *encoding = MHD_lookup_connection_value (
            connection,
            MHD_HEADER_KIND,
            http_utils::http_header_content_type.c_str()
    );
    if(encoding != 0x0)
        mr->dhr->set_header(http_utils::http_header_content_type, encoding);
    if ( post_process_enabled &&
        (
            0x0 != encoding &&
            ((0 == strncasecmp (
                                MHD_HTTP_POST_ENCODING_FORM_URLENCODED,
                                encoding,
                                strlen (MHD_HTTP_POST_ENCODING_FORM_URLENCODED)
                                )
              )
             || (0 == strncasecmp (
                                   MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA,
                                   encoding,
                                   strlen (MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA)
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

void webserver::end_request_construction(
        MHD_Connection* connection,
        struct details::modded_request* mr,
        const char* version,
        const char* method,
        char* &user,
        char* &pass,
        char* &digested_user
)
{
    mr->ws = this;
    MHD_get_connection_values (
            connection,
            MHD_GET_ARGUMENT_KIND,
            &build_request_args,
            (void*) mr
    );
    MHD_get_connection_values (
            connection,
            MHD_HEADER_KIND,
            &build_request_header,
            (void*) mr->dhr
    );
    MHD_get_connection_values (
            connection,
            MHD_FOOTER_KIND,
            &build_request_footer,
            (void*) mr->dhr
    );
    MHD_get_connection_values (
            connection,
            MHD_COOKIE_KIND,
            &build_request_cookie,
            (void*) mr->dhr
    );

    mr->dhr->set_path(mr->standardized_url->c_str());
    mr->dhr->set_method(method);

    if(basic_auth_enabled)
    {
        user = MHD_basic_auth_get_username_password(connection, &pass);
    }
    if(digest_auth_enabled)
        digested_user = MHD_digest_auth_get_username(connection);
    mr->dhr->set_version(version);
    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(
            connection,
            MHD_CONNECTION_INFO_CLIENT_ADDRESS
    );
    std::string ip_str;
    get_ip_str(conninfo->client_addr, ip_str);
    mr->dhr->set_requestor(ip_str);
    mr->dhr->set_requestor_port(get_port(conninfo->client_addr));
    if(pass != 0x0)
    {
        mr->dhr->set_pass(pass);
        mr->dhr->set_user(user);
    }
    if(digested_user != 0x0)
    {
        mr->dhr->set_digested_user(digested_user);
    }
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
                    size_t endpoint_pieces_len = (*it).first.get_url_pieces_num();
                    size_t endpoint_tot_len = (*it).first.get_url_complete_size();
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
                    vector<string> url_pars;

                    size_t pars_size = found_endpoint->first.get_url_pars(url_pars);

                    vector<string> url_pieces;
                    endpoint.get_url_pieces(url_pieces);
                    vector<int> chunkes;
                    found_endpoint->first.get_chunk_positions(chunkes);
                    for(unsigned int i = 0; i < pars_size; i++)
                    {
                        mr->dhr->set_arg(url_pars[i], url_pieces[chunkes[i]]);
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

    mr->dhr->set_underlying_connection(connection);

    if(found)
    {
        try
        {
            if(hrm->is_allowed(method))
            {
                mr->dhrs = NEW_OR_MOVE(http_response, ((hrm)->*(mr->callback))(*mr->dhr)); //copy in memory (move in case)
                if (mr->dhrs->get_response_code() == -1) mr->dhrs = NEW_OR_MOVE(http_response, internal_error_page(mr));
            }
            else
            {
                mr->dhrs = NEW_OR_MOVE(http_response, method_not_allowed_page(mr));
            }
        }
        catch(const std::exception& e)
        {
            mr->dhrs = NEW_OR_MOVE(http_response, internal_error_page(mr));
        }
        catch(...)
        {
            mr->dhrs = NEW_OR_MOVE(http_response, internal_error_page(mr));
        }
    }
    else
    {
        mr->dhrs = NEW_OR_MOVE(http_response, not_found_page(mr));
    }

    mr->dhrs->underlying_connection = connection;

    try
    {
        try
        {
            mr->dhrs->get_raw_response(&raw_response, this);
        }
        catch(const file_access_exception& fae)
        {
            mr->dhrs = NEW_OR_MOVE(http_response, not_found_page(mr));
            mr->dhrs->get_raw_response(&raw_response, this);
        }
        catch(const std::exception& e)
        {
            mr->dhrs = NEW_OR_MOVE(http_response, internal_error_page(mr));
            mr->dhrs->get_raw_response(&raw_response, this);
        }
        catch(...)
        {
            mr->dhrs = NEW_OR_MOVE(http_response, internal_error_page(mr));
            mr->dhrs->get_raw_response(&raw_response, this);
        }
    }
    catch(...)
    {
        mr->dhrs = NEW_OR_MOVE(http_response, internal_error_page(mr, true));
        mr->dhrs->get_raw_response(&raw_response, this);
    }
    mr->dhrs->decorate_response(raw_response);
    to_ret = mr->dhrs->enqueue_response(connection, raw_response);
    MHD_destroy_response (raw_response);
    return to_ret;
}

int webserver::complete_request(
        MHD_Connection* connection,
        struct details::modded_request* mr,
        const char* version,
        const char* method
)
{
    char* pass = 0x0;
    char* user = 0x0;
    char* digested_user = 0x0;

    end_request_construction(
            connection,
            mr,
            version,
            method,
            pass,
            user,
            digested_user
    );

    int to_ret = finalize_answer(connection, mr, method);

    if (user != 0x0)
        free (user);
    if (pass != 0x0)
        free (pass);
    if (digested_user != 0x0)
        free (digested_user);

    return to_ret;
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

    mr->standardized_url = new string();
    internal_unescaper((void*) static_cast<webserver*>(cls), (char*) url);
    http_utils::standardize_url(url, *mr->standardized_url);

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

void webserver::send_message_to_topic(
        const std::string& topic,
        const std::string& message
)
{
    internal_comet_manager->send_message_to_topic(topic, message);
}

void webserver::register_to_topics(
        const std::vector<std::string>& topics,
        MHD_Connection* connection_id
)
{
    internal_comet_manager->register_to_topics(topics, connection_id);
}

size_t webserver::read_message(MHD_Connection* connection_id,
    std::string& message
)
{
    return internal_comet_manager->read_message(connection_id, message);
}

http_response* webserver::get_from_cache(
        const std::string& key,
        bool* valid,
        bool lock,
        bool write
)
{
    details::cache_entry* ce = 0x0;
    return get_from_cache(key, valid, &ce, lock, write);
}

http_response* webserver::get_from_cache(
        const std::string& key,
        bool* valid,
        details::cache_entry** ce,
        bool lock,
        bool write
)
{
    pthread_rwlock_rdlock(&cache_guard);
    *valid = true;
    map<string, details::cache_entry*>::iterator it(response_cache.find(key));
    if(it != response_cache.end())
    {
        if(lock)
            (*it).second->lock(write);
        if((*it).second->validity != -1)
        {
            timeval now;
            gettimeofday(&now, NULL);
            if( now.tv_sec - (*it).second->ts > (*it).second->validity)
                *valid = false;
        }
        *ce = (*it).second;
        pthread_rwlock_unlock(&cache_guard);
        return (*it).second->response.ptr();
    }
    else
    {
        pthread_rwlock_unlock(&cache_guard);
        *valid = false;
        return 0x0;
    }
}

bool webserver::is_valid(const std::string& key)
{
    pthread_rwlock_rdlock(&cache_guard);
    map<string, details::cache_entry*>::iterator it(response_cache.find(key));
    if(it != response_cache.end())
    {
        if((*it).second->validity != -1)
        {
            timeval now;
            gettimeofday(&now, NULL);
            if( now.tv_sec - (*it).second->ts > (*it).second->validity)
            {
                pthread_rwlock_unlock(&cache_guard);
                return false;
            }
            else
            {
                pthread_rwlock_unlock(&cache_guard);
                return true;
            }
        }
        else
        {
            pthread_rwlock_unlock(&cache_guard);
            return true;
        }
    }
    pthread_rwlock_unlock(&cache_guard);
    return false;
}

void webserver::lock_cache_element(details::cache_entry* ce, bool write)
{
    if(ce)
        ce->lock(write);
}

void webserver::unlock_cache_element(details::cache_entry* ce)
{
    if(ce)
        ce->unlock();
}

details::cache_entry* webserver::put_in_cache(
        const std::string& key,
        http_response* value,
        bool* new_elem,
        bool lock,
        bool write,
        int validity
)
{
    pthread_rwlock_wrlock(&cache_guard);
    map<string, details::cache_entry*>::iterator it(response_cache.find(key));
    details::cache_entry* to_ret;
    bool already_in = false;
    if(it != response_cache.end())
    {
        (*it).second->lock(true);
        already_in = true;
    }
    if(validity == -1)
    {
        if(already_in)
        {
            (*it).second->response = value;
            to_ret = (*it).second;
            *new_elem = false;
        }
        else
        {
            pair<map<string, details::cache_entry*>::iterator, bool> res =
                response_cache.insert(pair<string, details::cache_entry*>(
                            key, new details::cache_entry(value))
                );

            to_ret = (*res.first).second;
            *new_elem = res.second;
        }
    }
    else
    {
        timeval now;
        gettimeofday(&now, NULL);
        if(already_in)
        {
            (*it).second->response = value;
            (*it).second->ts = now.tv_sec;
            (*it).second->validity = validity;
            to_ret = (*it).second;
            *new_elem = false;
        }
        else
        {
            pair<map<string, details::cache_entry*>::iterator, bool> res =
                response_cache.insert(pair<string, details::cache_entry*>(
                            key, new details::cache_entry(value, now.tv_sec, validity))
                );
            to_ret = (*res.first).second;
            *new_elem = res.second;
        }
    }
    if(already_in)
        (*it).second->unlock();
    if(lock)
        to_ret->lock(write);
    pthread_rwlock_unlock(&cache_guard);
    return to_ret;
}

void webserver::remove_from_cache(const std::string& key)
{
    pthread_rwlock_wrlock(&cache_guard);
    map<string, details::cache_entry*>::iterator it(response_cache.find(key));
    if(it != response_cache.end())
    {
        details::cache_entry* ce = (*it).second;
        response_cache.erase(it);
        delete ce;
    }
    pthread_rwlock_unlock(&cache_guard);
}

void webserver::clean_cache()
{
    pthread_rwlock_wrlock(&cache_guard);
    response_cache.clear(); //manage this because obviously causes leaks
    pthread_rwlock_unlock(&cache_guard);
}

void webserver::unlock_cache_entry(details::cache_entry* ce)
{
    ce->unlock();
}

void webserver::lock_cache_entry(details::cache_entry* ce)
{
    ce->lock();
}

void webserver::get_response(details::cache_entry* ce, http_response** res)
{
    *res = ce->response.ptr();
}

};
