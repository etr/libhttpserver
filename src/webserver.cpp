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
#include <stdint.h>
#include <inttypes.h>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>

#ifdef WITH_PYTHON
#include <Python.h>
#endif

#include <microhttpd.h>

#include "gettext.h"
#include "http_utils.hpp"
#include "http_resource.hpp"
#include "http_response.hpp"
#include "http_request.hpp"
#include "http_endpoint.hpp"
#include "string_utilities.hpp"
#include "webserver.hpp"
#include "modded_request.hpp"


using namespace std;

namespace httpserver {

using namespace http;

int policy_callback (void *, const struct sockaddr*, socklen_t);
void error_log(void*, const char*, va_list);
void* uri_log(void*, const char*);
void access_log(webserver*, string);
size_t unescaper_func(void*, struct MHD_Connection*, char*);
size_t internal_unescaper(void*, char*);

static void catcher (int sig)
{
}

static void ignore_sigpipe ()
{
    struct sigaction oldsig;
    struct sigaction sig;

    sig.sa_handler = &catcher;
    sigemptyset (&sig.sa_mask);
#ifdef SA_INTERRUPT
    sig.sa_flags = SA_INTERRUPT;  /* SunOS */
#else
    sig.sa_flags = SA_RESTART;
#endif
    if (0 != sigaction (SIGPIPE, &sig, &oldsig))
        fprintf (stderr, gettext("Failed to install SIGPIPE handler: %s\n"), strerror (errno));
}

//LOGGING DELEGATE
logging_delegate::logging_delegate() {}

logging_delegate::~logging_delegate() {}

void logging_delegate::log_access(const string& s) const {}

void logging_delegate::log_error(const string& s) const {}

//REQUEST VALIDATOR
request_validator::request_validator() {}

request_validator::~request_validator() {}

bool request_validator::validate(const string& address) const { return true; }

//UNESCAPER
unescaper::unescaper() {}

unescaper::~unescaper() {}

void unescaper::unescape(char* s) const {}

//WEBSERVER CREATOR
create_webserver& create_webserver::https_mem_key(const std::string& https_mem_key)
{
    char* _https_mem_key_pt = http::load_file(https_mem_key.c_str());
    _https_mem_key = _https_mem_key_pt;
    free(_https_mem_key_pt);
    return *this;
}

create_webserver& create_webserver::https_mem_cert(const std::string& https_mem_cert)
{
    char* _https_mem_cert_pt = http::load_file(https_mem_cert.c_str());
    _https_mem_cert = _https_mem_cert_pt;
    free(_https_mem_cert_pt);
    return *this;
}

create_webserver& create_webserver::https_mem_trust(const std::string& https_mem_trust)
{
    char* _https_mem_trust_pt = http::load_file(https_mem_trust.c_str());
    _https_mem_trust = _https_mem_trust_pt;
    free(_https_mem_trust_pt);
    return *this;
}

//WEBSERVER
webserver::webserver 
(
    int port, 
    const http_utils::start_method_T& start_method,
    int max_threads, 
    int max_connections,
    int memory_limit,
    int connection_timeout,
    int per_IP_connection_limit,
    const logging_delegate* log_delegate,
    const request_validator* validator,
    const unescaper* unescaper_pointer,
    const struct sockaddr* bind_address,
    int bind_socket,
    int max_thread_stack_size,
    bool use_ssl,
    bool use_ipv6,
    bool debug,
    bool pedantic,
    const string& https_mem_key,
    const string& https_mem_cert,
    const string& https_mem_trust,
    const string& https_priorities,
    const http_utils::cred_type_T& cred_type,
    const string digest_auth_random,
    int nonce_nc_size,
    const http_utils::policy_T& default_policy,
    bool basic_auth_enabled,
    bool digest_auth_enabled,
    bool regex_checking,
    bool ban_system_enabled,
    bool post_process_enabled,
    http_resource* single_resource,
    http_resource* not_found_resource,
    http_resource* method_not_allowed_resource,
    http_resource* method_not_acceptable_resource,
    http_resource* internal_error_resource

) :
    port(port), 
    start_method(start_method),
    max_threads(max_threads), 
    max_connections(max_connections),
    memory_limit(memory_limit),
    connection_timeout(connection_timeout),
    per_IP_connection_limit(per_IP_connection_limit),
    log_delegate(log_delegate),
    validator(validator),
    unescaper_pointer(unescaper_pointer),
    bind_address(bind_address),
    bind_socket(bind_socket),
    max_thread_stack_size(max_thread_stack_size),
    use_ssl(use_ssl),
    use_ipv6(use_ipv6),
    debug(debug),
    pedantic(pedantic),
    https_mem_key(https_mem_key),
    https_mem_cert(https_mem_cert),
    https_mem_trust(https_mem_trust),
    https_priorities(https_priorities),
    cred_type(cred_type),
    digest_auth_random(digest_auth_random),
    nonce_nc_size(nonce_nc_size),
    running(false),
    default_policy(default_policy),
    basic_auth_enabled(basic_auth_enabled),
    digest_auth_enabled(digest_auth_enabled),
    regex_checking(regex_checking),
    ban_system_enabled(ban_system_enabled),
    post_process_enabled(post_process_enabled),
    not_found_resource(not_found_resource),
    method_not_allowed_resource(method_not_allowed_resource),
    method_not_acceptable_resource(method_not_acceptable_resource),
    internal_error_resource(internal_error_resource)
{
    init(single_resource);
}

webserver::webserver(const create_webserver& params):
    port(params._port),
    start_method(params._start_method),
    max_threads(params._max_threads),
    max_connections(params._max_connections),
    memory_limit(params._memory_limit),
    connection_timeout(params._connection_timeout),
    per_IP_connection_limit(params._per_IP_connection_limit),
    log_delegate(params._log_delegate),
    validator(params._validator),
    unescaper_pointer(params._unescaper_pointer),
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
    not_found_resource(params._not_found_resource),
    method_not_allowed_resource(params._method_not_allowed_resource),
    method_not_acceptable_resource(params._method_not_acceptable_resource),
    internal_error_resource(params._internal_error_resource)
{
    init(params._single_resource);
}

void webserver::init(http_resource* single_resource)
{
    if(single_resource != 0x0)
    {
        this->single_resource = true;
        register_resource("", single_resource);
    }
    else
        this->single_resource = false;
    ignore_sigpipe();
    pthread_mutex_init(&mutexwait, NULL);
    pthread_cond_init(&mutexcond, NULL);
}

webserver::~webserver()
{
    this->stop();
    pthread_mutex_destroy(&mutexwait);
    pthread_cond_destroy(&mutexcond);
}

void webserver::sweet_kill()
{
    this->stop();
}

void webserver::request_completed (void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) 
{
    modded_request* mr = (struct modded_request*) *con_cls;
    if (NULL == mr) 
    {
        return;
    }
    if (NULL != mr->pp) 
    {
        MHD_destroy_post_processor (mr->pp);
    }
    if(mr->second)
        delete mr->dhr; //TODO: verify. It could be an error
    if(mr->dhrs)
        delete mr->dhrs;
    delete mr->complete_uri;
    free(mr);
}

bool webserver::start(bool blocking)
{
    struct {
        MHD_OptionItem operator ()(enum MHD_OPTION opt, intptr_t val, void *ptr = 0) {
            MHD_OptionItem x = {opt, val, ptr};
            return x;
        }
    } gen;
    vector<struct MHD_OptionItem> iov;

    iov.push_back(gen(MHD_OPTION_NOTIFY_COMPLETED, (intptr_t) &request_completed, NULL ));
    iov.push_back(gen(MHD_OPTION_URI_LOG_CALLBACK, (intptr_t) &uri_log, this));
    iov.push_back(gen(MHD_OPTION_EXTERNAL_LOGGER, (intptr_t) &error_log, this));
    iov.push_back(gen(MHD_OPTION_UNESCAPE_CALLBACK, (intptr_t) &unescaper_func, this));
    iov.push_back(gen(MHD_OPTION_CONNECTION_TIMEOUT, connection_timeout));
    if(bind_address != 0x0)
        iov.push_back(gen(MHD_OPTION_SOCK_ADDR, (intptr_t) bind_address));
    if(bind_socket != 0)
        iov.push_back(gen(MHD_OPTION_LISTEN_SOCKET, bind_socket));
    if(max_threads != 0)
        iov.push_back(gen(MHD_OPTION_THREAD_POOL_SIZE, max_threads));
    if(max_connections != 0)
        iov.push_back(gen(MHD_OPTION_CONNECTION_LIMIT, max_connections));
    if(memory_limit != 0)
        iov.push_back(gen(MHD_OPTION_CONNECTION_MEMORY_LIMIT, memory_limit));
    if(per_IP_connection_limit != 0)
        iov.push_back(gen(MHD_OPTION_PER_IP_CONNECTION_LIMIT, per_IP_connection_limit));
    if(max_thread_stack_size != 0)
        iov.push_back(gen(MHD_OPTION_THREAD_STACK_SIZE, max_thread_stack_size));
    if(nonce_nc_size != 0)
        iov.push_back(gen(MHD_OPTION_NONCE_NC_SIZE, nonce_nc_size));
    if(use_ssl)
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_KEY, 0, (void*)https_mem_key.c_str()));
    if(use_ssl)
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_CERT, 0, (void*)https_mem_cert.c_str()));
    if(https_mem_trust != "" && use_ssl)
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_TRUST, 0, (void*)https_mem_trust.c_str()));
    if(https_priorities != "" && use_ssl)
        iov.push_back(gen(MHD_OPTION_HTTPS_PRIORITIES, 0, (void*)https_priorities.c_str()));
    if(digest_auth_random != "")
        iov.push_back(gen(MHD_OPTION_DIGEST_AUTH_RANDOM, digest_auth_random.size(), (char*)digest_auth_random.c_str()));
#ifdef HAVE_GNUTLS
    if(cred_type != http_utils::NONE)
        iov.push_back(gen(MHD_OPTION_HTTPS_CRED_TYPE, cred_type));
#endif

    iov.push_back(gen(MHD_OPTION_END, 0, NULL ));

    struct MHD_OptionItem ops[iov.size()];
    for(unsigned int i = 0; i < iov.size(); i++)
    {
        ops[i] = iov[i];
    }

    int start_conf = start_method;
    if(use_ssl)
        start_conf |= MHD_USE_SSL;
    if(use_ipv6)
        start_conf |= MHD_USE_IPv6;
    if(debug)
        start_conf |= MHD_USE_DEBUG;
    if(pedantic)
        start_conf |= MHD_USE_PEDANTIC_CHECKS;

    this->daemon = MHD_start_daemon
    (
            start_conf, this->port, &policy_callback, this,
            &answer_to_connection, this, MHD_OPTION_ARRAY, ops, MHD_OPTION_END
    );

    if(NULL == daemon)
    {
        cout << gettext("Unable to connect daemon to port: ") << this->port << endl;
        return false;
    }
    this->running = true;
    bool value_onclose = false;
    if(blocking)
    {
#ifdef WITH_PYTHON
        if(PyEval_ThreadsInitialized())
        {
            Py_BEGIN_ALLOW_THREADS;
        }
#endif
        pthread_mutex_lock(&mutexwait);
        while(blocking && running)
            pthread_cond_wait(&mutexcond, &mutexwait);
        pthread_mutex_unlock(&mutexwait);
#ifdef WITH_PYTHON
        if(PyEval_ThreadsInitialized())
        {
            Py_END_ALLOW_THREADS;
        }
#endif
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
    pthread_mutex_lock(&mutexwait);
    if(this->running)
    {
        MHD_stop_daemon (this->daemon);
        this->running = false;
    }
    pthread_cond_signal(&mutexcond);
    pthread_mutex_unlock(&mutexwait);
    return true;
}

void webserver::register_resource(const string& resource, http_resource* http_resource, bool family)
{
    this->registered_resources[details::http_endpoint(resource, family, true, regex_checking)] = http_resource;
    if(method_not_acceptable_resource)
        http_resource->method_not_acceptable_resource = method_not_acceptable_resource;
}

void webserver::unregister_resource(const string& resource)
{
    this->registered_resources.erase(details::http_endpoint(resource));
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

int webserver::build_request_header (void *cls, enum MHD_ValueKind kind, const char *key, const char *value)
{
    http_request* dhr = static_cast<http_request*>(cls);
    dhr->set_header(key, value);
    return MHD_YES;
}

int webserver::build_request_cookie (void *cls, enum MHD_ValueKind kind, const char *key, const char *value)
{
    http_request* dhr = static_cast<http_request*>(cls);
    dhr->set_cookie(key, value);
    return MHD_YES;
}

int webserver::build_request_footer (void *cls, enum MHD_ValueKind kind, const char *key, const char *value)
{
    http_request* dhr = static_cast<http_request*>(cls);
    dhr->set_footer(key, value);
    return MHD_YES;
}

int webserver::build_request_args (void *cls, enum MHD_ValueKind kind, const char *key, const char *value)
{
    modded_request* mr = static_cast<modded_request*>(cls);
    int size = internal_unescaper((void*) mr->ws, (char*) value);
    mr->dhr->set_arg(key, string(value, size));
    return MHD_YES;
}

int policy_callback (void *cls, const struct sockaddr* addr, socklen_t addrlen)
{
    if((static_cast<webserver*>(cls))->ban_system_enabled)
    {
        if((((static_cast<webserver*>(cls))->default_policy == http_utils::ACCEPT) && 
           ((static_cast<webserver*>(cls))->bans.count(addr)) && 
           (!(static_cast<webserver*>(cls))->allowances.count(addr))
        ) ||
        (((static_cast<webserver*>(cls))->default_policy == http_utils::REJECT) &&
           ((!(static_cast<webserver*>(cls))->allowances.count(addr)) ||
           ((static_cast<webserver*>(cls))->bans.count(addr)))
        ))
            return MHD_NO;
    }
    return MHD_YES;
}

void* uri_log(void* cls, const char* uri)
{
    struct modded_request* mr = (struct modded_request*) calloc(1,sizeof(struct modded_request));
    mr->complete_uri = new string(uri);
    mr->second = false;
    return ((void*)mr);
}

void error_log(void* cls, const char* fmt, va_list ap)
{
    webserver* dws = static_cast<webserver*>(cls);
    if(dws->log_delegate != 0x0)
    {
        dws->log_delegate->log_error(fmt);
    }
    else
    {
        cout << fmt << endl;
    }
}

void access_log(webserver* dws, string uri)
{
    if(dws->log_delegate != 0x0)
    {
        dws->log_delegate->log_access(uri);
    }
    else
    {
        cout << uri << endl;
    }
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
    webserver* dws = static_cast<webserver*>(cls);
    if(dws->unescaper_pointer != 0x0)
    {
        dws->unescaper_pointer->unescape(s);
        return strlen(s);
    }
    else
    {
        return http_unescape(s);
    }
}

int webserver::post_iterator (void *cls, enum MHD_ValueKind kind,
    const char *key,
    const char *filename,
    const char *content_type,
    const char *transfer_encoding,
    const char *data, uint64_t off, size_t size
    )
{
    struct modded_request* mr = (struct modded_request*) cls;
    mr->dhr->set_arg(key, data, size);
    return MHD_YES;
}

void webserver::upgrade_handler (void *cls, struct MHD_Connection* connection,
    void **con_cls, int upgrade_socket)
{
}

void webserver::not_found_page(http_response** dhrs, modded_request* mr)
{
    if(not_found_resource != 0x0)
        ((not_found_resource)->*(mr->callback))(*mr->dhr, dhrs);
    else
        *dhrs = new http_string_response(NOT_FOUND_ERROR, http_utils::http_not_found);
}

int webserver::method_not_acceptable_page (const void *cls,
    struct MHD_Connection *connection)
{
    int ret;
    struct MHD_Response *response;

    /* unsupported HTTP method */
    response = MHD_create_response_from_buffer (strlen (NOT_METHOD_ERROR),
        (void *) NOT_METHOD_ERROR,
        MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response (connection, 
        MHD_HTTP_METHOD_NOT_ACCEPTABLE, 
        response);
    MHD_add_response_header (response,
        MHD_HTTP_HEADER_CONTENT_ENCODING,
        "text/plain");
    MHD_destroy_response (response);
    return ret;
}

void webserver::method_not_allowed_page(http_response** dhrs, modded_request* mr)
{
    if(method_not_allowed_resource != 0x0)
        ((method_not_allowed_resource)->*(mr->callback))(*mr->dhr, dhrs);
    else
        *dhrs = new http_string_response(METHOD_ERROR, http_utils::http_method_not_allowed);
}

void webserver::internal_error_page(http_response** dhrs, modded_request* mr)
{
    if(internal_error_resource != 0x0)
        ((internal_error_resource)->*(mr->callback))(*mr->dhr, dhrs);
    else
        *dhrs = new http_string_response(GENERIC_ERROR, http_utils::http_internal_server_error);
}

int webserver::bodyless_requests_answer(MHD_Connection* connection,
    const char* url, const char* method,
    const char* version, struct modded_request* mr
    )
{
    string st_url;
    internal_unescaper((void*) this, (char*) url);
    http_utils::standardize_url(url, st_url);
    http_request req;
    mr->dhr = &(req);

    return complete_request(connection, mr, version, st_url.c_str(), method);
}

int webserver::bodyfull_requests_answer_first_step(MHD_Connection* connection, struct modded_request* mr)
{
    mr->second = true;
    mr->dhr = new http_request();
    const char *encoding = MHD_lookup_connection_value (connection, MHD_HEADER_KIND, http_utils::http_header_content_type.c_str());
    if(encoding != 0x0)
        mr->dhr->set_header(http_utils::http_header_content_type, encoding);
    if ( post_process_enabled &&
        (   
            0x0 != encoding && 
            ((0 == strncasecmp (MHD_HTTP_POST_ENCODING_FORM_URLENCODED, encoding, strlen (MHD_HTTP_POST_ENCODING_FORM_URLENCODED))))
        )
    ) 
    {
        mr->pp = MHD_create_post_processor (connection, 1024, &post_iterator, mr);
    } 
    else 
    {
        mr->pp = NULL;
    }
    return MHD_YES;
}

int webserver::bodyfull_requests_answer_second_step(MHD_Connection* connection,
    const char* url, const char* method,
    const char* version, const char* upload_data,
    size_t* upload_data_size, struct modded_request* mr
)
{
    mr->st_url = new string();
    internal_unescaper((void*) this, (char*) url);
    http_utils::standardize_url(url, *mr->st_url);
    if ( 0 != *upload_data_size)
    {
#ifdef DEBUG
        cout << "Writing content: " << upload_data << endl;
#endif
        mr->dhr->grow_content(upload_data, *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }
    if (mr->pp != NULL)
    {
        MHD_post_process(mr->pp, upload_data, *upload_data_size);
    }

    return complete_request(connection, mr, version, mr->st_url->c_str(), method);
}

void webserver::end_request_construction(MHD_Connection* connection, struct modded_request* mr, const char* version, const char* st_url, const char* method, char* user, char* pass, char* digested_user)
{
    mr->ws = this;
    MHD_get_connection_values (connection, MHD_GET_ARGUMENT_KIND, &build_request_args, (void*) mr);
    MHD_get_connection_values (connection, MHD_HEADER_KIND, &build_request_header, (void*) mr->dhr);
    MHD_get_connection_values (connection, MHD_FOOTER_KIND, &build_request_footer, (void*) mr->dhr);
    MHD_get_connection_values (connection, MHD_COOKIE_KIND, &build_request_cookie, (void*) mr->dhr);

    mr->dhr->set_path(st_url);
    mr->dhr->set_method(method);

    if(basic_auth_enabled)
    {
        user = MHD_basic_auth_get_username_password(connection, &pass);
    }
    if(digest_auth_enabled)
        digested_user = MHD_digest_auth_get_username(connection);
    mr->dhr->set_version(version);
    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
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

int webserver::finalize_answer(MHD_Connection* connection, struct modded_request* mr, const char* st_url, const char* method)
{
    int to_ret = MHD_NO;
    struct MHD_Response *response = 0x0;
    http_response* dhrs = 0x0;
    map<details::http_endpoint, http_resource* >::iterator found_endpoint;
    bool found = false;
    if(!single_resource)
    {
        details::http_endpoint endpoint(st_url, false, false, regex_checking);
        found_endpoint = registered_resources.find(endpoint);
        if(found_endpoint == registered_resources.end())
        {
            if(regex_checking)
            {
                map<details::http_endpoint, http_resource* >::iterator it;
                int len = -1;
                int tot_len = -1;
                for(it=registered_resources.begin(); it!=registered_resources.end(); ++it) 
                {
                    int endpoint_pieces_len = (*it).first.get_url_pieces_num();
                    int endpoint_tot_len = (*it).first.get_url_complete_size();
                    if(tot_len == -1 || len == -1 || endpoint_pieces_len > len || (endpoint_pieces_len == len && endpoint_tot_len > tot_len))
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
                    unsigned int pars_size = found_endpoint->first.get_url_pars(url_pars);
                    vector<string> url_pieces;
                    endpoint.get_url_pieces(url_pieces);
                    vector<int> chunkes;
                    found_endpoint->first.get_chunk_positions(chunkes);
                    for(unsigned int i = 0; i < pars_size; i++) 
                    {
                        mr->dhr->set_arg(url_pars[i], url_pieces[chunkes[i]]);
                    }
                }
            }
        }
        else
            found = true;
    }
    else
    {
        found_endpoint = registered_resources.begin();
        found = true;
    }
    mr->dhr->set_underlying_connection(connection);
#ifdef DEBUG
    if(found)
        cout << "Using: " << found_endpoint->first.get_url_complete() << endl;
    else
        cout << "Endpoint not found!" << endl;
#endif
#ifdef WITH_PYTHON
    PyGILState_STATE gstate;
    if(PyEval_ThreadsInitialized())
    {
        gstate = PyGILState_Ensure();
    }
#endif
    if(found)
    {
        try
        {
            if(found_endpoint->second->is_allowed(method))
                ((found_endpoint->second)->*(mr->callback))(*mr->dhr, &dhrs);
            else
            {
                method_not_allowed_page(&dhrs, mr);
            }
        }
        catch(const std::exception& e)
        {
            internal_error_page(&dhrs, mr);
        }
        catch(...)
        {
            internal_error_page(&dhrs, mr);
        }
    }
    else
    {
        not_found_page(&dhrs, mr);
    }
#ifdef WITH_PYTHON
    if(PyEval_ThreadsInitialized())
    {
        PyGILState_Release(gstate);
    }
#endif
    mr->dhrs = dhrs;
    dhrs->get_raw_response(&response, &found, this);
    vector<pair<string,string> > response_headers;
    dhrs->get_headers(response_headers);
    vector<pair<string,string> > response_footers;
    dhrs->get_footers(response_footers);
    vector<pair<string,string> >::iterator it;
    for (it=response_headers.begin() ; it != response_headers.end(); ++it)
        MHD_add_response_header(response, (*it).first.c_str(), (*it).second.c_str());
    for (it=response_footers.begin() ; it != response_footers.end(); ++it)
        MHD_add_response_footer(response, (*it).first.c_str(), (*it).second.c_str());
    if(dhrs->response_type == http_response::DIGEST_AUTH_FAIL)
        to_ret = MHD_queue_auth_fail_response(connection, dhrs->get_realm().c_str(), dhrs->get_opaque().c_str(), response, dhrs->need_nonce_reload() ? MHD_YES : MHD_NO);
    else if(dhrs->response_type == http_response::BASIC_AUTH_FAIL)
        to_ret = MHD_queue_basic_auth_fail_response(connection, dhrs->get_realm().c_str(), response);
    else
        to_ret = MHD_queue_response(connection, dhrs->get_response_code(), response);
    MHD_destroy_response (response);
    return to_ret;
}

int webserver::complete_request(MHD_Connection* connection, struct modded_request* mr, const char* version, const char* st_url, const char* method)
{
    char* pass = 0x0;
    char* user = 0x0;
    char* digested_user = 0x0;

    end_request_construction(connection, mr, version, st_url, method, pass, user, digested_user);

    int to_ret = finalize_answer(connection, mr, st_url, method);

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
    struct modded_request* mr = static_cast<struct modded_request*>(*con_cls);
    if(mr->second == false)
    {
        bool body = false;
        access_log(static_cast<webserver*>(cls), *(mr->complete_uri) + " METHOD: " + method);
        if( 0 == strcmp(method, http_utils::http_method_get.c_str()))
        {
            mr->callback = &http_resource::render_GET;
        }
        else if (0 == strcmp(method, http_utils::http_method_post.c_str()))
        {
            mr->callback = &http_resource::render_POST;
            body = true;
        }
        else if (0 == strcmp(method, http_utils::http_method_put.c_str()))
        {
            mr->callback = &http_resource::render_PUT;
            body = true;
        }
        else if (0 == strcmp(method, http_utils::http_method_delete.c_str()))
        {
            mr->callback = &http_resource::render_DELETE;
        }
        else if (0 == strcmp(method, http_utils::http_method_head.c_str()))
        {
            mr->callback = &http_resource::render_HEAD;
        }
        else if (0 == strcmp(method, http_utils::http_method_connect.c_str()))
        {
            mr->callback = &http_resource::render_CONNECT;
        }
        else if (0 == strcmp(method, http_utils::http_method_trace.c_str()))
        {
            mr->callback = &http_resource::render_TRACE;
        }
        else
        {
            if(static_cast<webserver*>(cls)->method_not_acceptable_resource)
                mr->callback = &http_resource::render_not_acceptable;
            else
                return static_cast<webserver*>(cls)->method_not_acceptable_page(cls, connection);
        }

        if(body)
            return static_cast<webserver*>(cls)->bodyfull_requests_answer_first_step(connection, mr);
        else
            return static_cast<webserver*>(cls)->bodyless_requests_answer(connection, url, method, version, mr);
    }
    else
    {
        return static_cast<webserver*>(cls)->bodyfull_requests_answer_second_step(connection, url, method, version, upload_data, upload_data_size, mr);
    }
}

};
