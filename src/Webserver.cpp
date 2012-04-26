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
#include "HttpUtils.hpp"
#include "HttpResource.hpp"
#include "HttpResponse.hpp"
#include "HttpRequest.hpp"
#include "HttpEndpoint.hpp"
#include "string_utilities.hpp"
#include "Webserver.hpp"


using namespace std;

namespace httpserver {

using namespace http;

int policyCallback (void *, const struct sockaddr*, socklen_t);
void error_log(void*, const char*, va_list);
void* uri_log(void*, const char*);
void access_log(Webserver*, string);
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

static long get_file_size (const char *filename)
{
    FILE *fp;

    fp = fopen (filename, "rb");
    if (fp)
    {   
        long size;

        if ((0 != fseek (fp, 0, SEEK_END)) || (-1 == (size = ftell (fp))))
            size = 0;

        fclose (fp);

        return size;
    }   
    else
    return 0;
}

static char * load_file (const char *filename)
{
    FILE *fp;
    char *buffer;
    long size;

    size = get_file_size (filename);
    if (size == 0)
        return NULL;

    fp = fopen (filename, "rb");
    if (!fp)
        return NULL;

    buffer = (char*) malloc (size);
    if (!buffer)
    {   
        fclose (fp);
        return NULL;
    }   

    if (size != (int) fread (buffer, 1, size, fp))
    {   
        free (buffer);
        buffer = NULL;
    }   

    fclose (fp);
    return buffer;
}

//LOGGING DELEGATE
LoggingDelegate::LoggingDelegate() {}

LoggingDelegate::~LoggingDelegate() {}

void LoggingDelegate::log_access(const string& s) const {}

void LoggingDelegate::log_error(const string& s) const {}

//REQUEST VALIDATOR
RequestValidator::RequestValidator() {}

RequestValidator::~RequestValidator() {}

bool RequestValidator::validate(const string& address) const { return true; }

//UNESCAPER
Unescaper::Unescaper() {}

Unescaper::~Unescaper() {}

void Unescaper::unescape(char* s) const {}

//WEBSERVER CREATOR
CreateWebserver& CreateWebserver::httpsMemKey(const std::string& httpsMemKey)
{
    _httpsMemKey = load_file(httpsMemKey.c_str());
    return *this;
}

CreateWebserver& CreateWebserver::httpsMemCert(const std::string& httpsMemCert)
{
    _httpsMemCert = load_file(httpsMemCert.c_str());
    return *this;
}

CreateWebserver& CreateWebserver::httpsMemTrust(const std::string& httpsMemTrust)
{
    _httpsMemTrust = load_file(httpsMemTrust.c_str());
    return *this;
}

//WEBSERVER
Webserver::Webserver 
(
    int port, 
    const HttpUtils::StartMethod_T& startMethod,
    int maxThreads, 
    int maxConnections,
    int memoryLimit,
    int connectionTimeout,
    int perIPConnectionLimit,
    const LoggingDelegate* logDelegate,
    const RequestValidator* validator,
    const Unescaper* unescaper,
    const struct sockaddr* bindAddress,
    int bindSocket,
    int maxThreadStackSize,
    bool useSsl,
    bool useIpv6,
    bool debug,
    bool pedantic,
    const string& httpsMemKey,
    const string& httpsMemCert,
    const string& httpsMemTrust,
    const string& httpsPriorities,
    const HttpUtils::CredType_T& credType,
    const string digestAuthRandom,
    int nonceNcSize,
    const HttpUtils::Policy_T& defaultPolicy
) :
    port(port), 
    startMethod(startMethod),
    maxThreads(maxThreads), 
    maxConnections(maxConnections),
    memoryLimit(memoryLimit),
    connectionTimeout(connectionTimeout),
    perIPConnectionLimit(perIPConnectionLimit),
    logDelegate(logDelegate),
    validator(validator),
    unescaper(unescaper),
    bindAddress(bindAddress),
    bindSocket(bindSocket),
    maxThreadStackSize(maxThreadStackSize),
    useSsl(useSsl),
    useIpv6(useIpv6),
    debug(debug),
    pedantic(pedantic),
    httpsMemKey(httpsMemKey),
    httpsMemCert(httpsMemCert),
    httpsMemTrust(httpsMemTrust),
    httpsPriorities(httpsPriorities),
    credType(credType),
    digestAuthRandom(digestAuthRandom),
    nonceNcSize(nonceNcSize),
    running(false),
    defaultPolicy(defaultPolicy)
{
    ignore_sigpipe();
}

Webserver::Webserver(const CreateWebserver& params):
    port(params._port),
    startMethod(params._startMethod),
    maxThreads(params._maxThreads),
    maxConnections(params._maxConnections),
    memoryLimit(params._memoryLimit),
    connectionTimeout(params._connectionTimeout),
    perIPConnectionLimit(params._perIPConnectionLimit),
    logDelegate(params._logDelegate),
    validator(params._validator),
    unescaper(params._unescaper),
    bindAddress(params._bindAddress),
    bindSocket(params._bindSocket),
    maxThreadStackSize(params._maxThreadStackSize),
    useSsl(params._useSsl),
    useIpv6(params._useIpv6),
    debug(params._debug),
    pedantic(params._pedantic),
    httpsMemKey(params._httpsMemKey),
    httpsMemCert(params._httpsMemCert),
    httpsMemTrust(params._httpsMemTrust),
    httpsPriorities(params._httpsPriorities),
    credType(params._credType),
    digestAuthRandom(params._digestAuthRandom),
    nonceNcSize(params._nonceNcSize),
    running(false),
    defaultPolicy(params._defaultPolicy)
{
    ignore_sigpipe();
}

Webserver::~Webserver()
{
    this->stop();
}

void Webserver::sweetKill()
{
    this->running = false;
}

void Webserver::requestCompleted (void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) 
{
    ModdedRequest* mr = (struct ModdedRequest*) *con_cls;
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
    delete mr->completeUri;
    free(mr);
}

bool Webserver::start(bool blocking)
{
    struct {
        MHD_OptionItem operator ()(enum MHD_OPTION opt, intptr_t val, void *ptr = 0) {
            MHD_OptionItem x = {opt, val, ptr};
            return x;
        }
    } gen;
    vector<struct MHD_OptionItem> iov;

    iov.push_back(gen(MHD_OPTION_NOTIFY_COMPLETED, (intptr_t) &requestCompleted, NULL ));
    iov.push_back(gen(MHD_OPTION_URI_LOG_CALLBACK, (intptr_t) &uri_log, this));
    iov.push_back(gen(MHD_OPTION_EXTERNAL_LOGGER, (intptr_t) &error_log, this));
    iov.push_back(gen(MHD_OPTION_UNESCAPE_CALLBACK, (intptr_t) &unescaper_func, this));
    iov.push_back(gen(MHD_OPTION_CONNECTION_TIMEOUT, connectionTimeout));
    if(bindAddress != 0x0)
        iov.push_back(gen(MHD_OPTION_SOCK_ADDR, (intptr_t) bindAddress));
    if(bindSocket != 0)
        iov.push_back(gen(MHD_OPTION_LISTEN_SOCKET, bindSocket));
    if(maxThreads != 0)
        iov.push_back(gen(MHD_OPTION_THREAD_POOL_SIZE, maxThreads));
    if(maxConnections != 0)
        iov.push_back(gen(MHD_OPTION_CONNECTION_LIMIT, maxConnections));
    if(memoryLimit != 0)
        iov.push_back(gen(MHD_OPTION_CONNECTION_MEMORY_LIMIT, memoryLimit));
    if(perIPConnectionLimit != 0)
        iov.push_back(gen(MHD_OPTION_PER_IP_CONNECTION_LIMIT, perIPConnectionLimit));
    if(maxThreadStackSize != 0)
        iov.push_back(gen(MHD_OPTION_THREAD_STACK_SIZE, maxThreadStackSize));
    if(nonceNcSize != 0)
        iov.push_back(gen(MHD_OPTION_NONCE_NC_SIZE, nonceNcSize));
    if(useSsl)
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_KEY, 0, (void*)httpsMemKey.c_str()));
    if(useSsl)
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_CERT, 0, (void*)httpsMemCert.c_str()));
    if(httpsMemTrust != "" && useSsl)
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_TRUST, 0, (void*)httpsMemTrust.c_str()));
    if(httpsPriorities != "" && useSsl)
        iov.push_back(gen(MHD_OPTION_HTTPS_PRIORITIES, 0, (void*)httpsPriorities.c_str()));
    if(digestAuthRandom != "")
        iov.push_back(gen(MHD_OPTION_DIGEST_AUTH_RANDOM, digestAuthRandom.size(), (char*)digestAuthRandom.c_str()));
#ifdef HAVE_GNUTLS
    if(credType != HttpUtils::NONE)
        iov.push_back(gen(MHD_OPTION_HTTPS_CRED_TYPE, credType));
#endif

    iov.push_back(gen(MHD_OPTION_END, 0, NULL ));

    struct MHD_OptionItem ops[iov.size()];
    for(unsigned int i = 0; i < iov.size(); i++)
    {
        ops[i] = iov[i];
    }

    int startConf = startMethod;
    if(useSsl)
        startConf |= MHD_USE_SSL;
    if(useIpv6)
        startConf |= MHD_USE_IPv6;
    if(debug)
        startConf |= MHD_USE_DEBUG;
    if(pedantic)
        startConf |= MHD_USE_PEDANTIC_CHECKS;

    this->daemon = MHD_start_daemon
    (
            startConf, this->port, &policyCallback, this,
            &answerToConnection, this, MHD_OPTION_ARRAY, ops, MHD_OPTION_END
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
        while(blocking && running)
            sleep(1);
#ifdef WITH_PYTHON
        if(PyEval_ThreadsInitialized())
        {
            Py_END_ALLOW_THREADS;
        }
#endif
        value_onclose = this->stop();
    }
    return value_onclose;
}

bool Webserver::isRunning()
{
    return this->running;
}

bool Webserver::stop()
{
    if(this->running)
    {
        MHD_stop_daemon (this->daemon);
        this->running = false;
    }
    return true;
}

void Webserver::registerResource(const string& resource, HttpResource* http_resource, bool family)
{
    this->registeredResources[HttpEndpoint(resource, family, true)] = http_resource;
}

void Webserver::unregisterResource(const string& resource)
{
    this->registeredResources.erase(HttpEndpoint(resource));
}

void Webserver::banIp(const string& ip)
{
    ip_representation t_ip(ip);
    set<ip_representation>::iterator it = bans.find(t_ip);
    if(t_ip.weight() > (*it).weight())
    {
        this->bans.erase(it);
        this->bans.insert(t_ip);
    }
    else
    {
        this->bans.insert(t_ip);
    }
}

void Webserver::allowIp(const string& ip)
{
    ip_representation t_ip(ip);
    set<ip_representation>::iterator it = allowances.find(t_ip);
    if(t_ip.weight() > (*it).weight())
    {
        this->allowances.erase(it);
        this->allowances.insert(t_ip);
    }
    else
    {
        this->allowances.insert(t_ip);
    }
}

void Webserver::unbanIp(const string& ip)
{
    this->bans.erase(ip);
}

void Webserver::disallowIp(const string& ip)
{
    this->allowances.erase(ip);
}

int Webserver::buildRequestHeader (void *cls, enum MHD_ValueKind kind, const char *key, const char *value)
{
    HttpRequest* dhr = (HttpRequest*)(cls);
    dhr->setHeader(key, value);
    return MHD_YES;
}

int Webserver::buildRequestCookie (void *cls, enum MHD_ValueKind kind, const char *key, const char *value)
{
    HttpRequest* dhr = (HttpRequest*)(cls);
    dhr->setCookie(key, value);
    return MHD_YES;
}

int Webserver::buildRequestFooter (void *cls, enum MHD_ValueKind kind, const char *key, const char *value)
{
    HttpRequest* dhr = (HttpRequest*)(cls);
    dhr->setFooter(key, value);
    return MHD_YES;
}

int Webserver::buildRequestArgs (void *cls, enum MHD_ValueKind kind, const char *key, const char *value)
{
    ModdedRequest* mr = (ModdedRequest*)(cls);
    int size = internal_unescaper((void*)mr->ws, (char*) value);
    mr->dhr->setArg(key, string(value, size));
    return MHD_YES;
}

int policyCallback (void *cls, const struct sockaddr* addr, socklen_t addrlen)
{
#ifdef DEBUG
    cout << "IP: " << get_ip_str(addr, addrlen) << " - " << "IP-LEN: " << addrlen << endl;
    cout << "DEFAULT POLICY: " << (((Webserver*)cls)->defaultPolicy == HttpUtils::ACCEPT) << endl;
    cout << "BANNED: " << (((Webserver*)cls)->bans.count(addr)) << endl;
    cout << "ALLOWED: " << (((Webserver*)cls)->allowances.count(addr)) << endl;
#endif
    if(((((Webserver*)cls)->defaultPolicy == HttpUtils::ACCEPT) && 
       (((Webserver*)cls)->bans.count(addr)) && 
       (!((Webserver*)cls)->allowances.count(addr))
    ) ||
    ((((Webserver*)cls)->defaultPolicy == HttpUtils::REJECT) &&
       ((!((Webserver*)cls)->allowances.count(addr)) ||
       (((Webserver*)cls)->bans.count(addr)))
    ))
        return MHD_NO;
    return MHD_YES;
}

void* uri_log(void* cls, const char* uri)
{
    struct ModdedRequest* mr = (struct ModdedRequest*) calloc(1,sizeof(struct ModdedRequest));
    mr->completeUri = new string(uri);
    mr->second = false;
    return ((void*)mr);
}

void error_log(void* cls, const char* fmt, va_list ap)
{
    Webserver* dws = (Webserver*) cls;
    if(dws->logDelegate != 0x0)
    {
        dws->logDelegate->log_error(fmt);
    }
    else
    {
        cout << fmt << endl;
    }
}

void access_log(Webserver* dws, string uri)
{
    if(dws->logDelegate != 0x0)
    {
        dws->logDelegate->log_access(uri);
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
    Webserver* dws = (Webserver*) cls;
    if(dws->unescaper != 0x0)
    {
        dws->unescaper->unescape(s);
        return strlen(s);
    }
    else
    {
        return http_unescape(s);
    }
}

int Webserver::post_iterator (void *cls, enum MHD_ValueKind kind,
    const char *key,
    const char *filename,
    const char *content_type,
    const char *transfer_encoding,
    const char *data, uint64_t off, size_t size
    )
{
    struct ModdedRequest* mr = (struct ModdedRequest*) cls;
    mr->dhr->setArg(key, data, size);
    return MHD_YES;
}

int Webserver::not_found_page (const void *cls,
    struct MHD_Connection *connection)
{
    int ret;
    struct MHD_Response *response;

    /* unsupported HTTP method */
    response = MHD_create_response_from_buffer (strlen (NOT_FOUND_ERROR),
        (void *) NOT_FOUND_ERROR,
        MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response (connection, 
        MHD_HTTP_NOT_FOUND, 
        response);
    MHD_add_response_header (response,
        MHD_HTTP_HEADER_CONTENT_ENCODING,
        "text/plain");
    MHD_destroy_response (response);
    return ret;
}

int Webserver::method_not_acceptable_page (const void *cls,
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

int Webserver::answerToConnection(void* cls, MHD_Connection* connection,
    const char* url, const char* method,
    const char* version, const char* upload_data,
    size_t* upload_data_size, void** con_cls
    )
{
    struct MHD_Response *response;
    struct ModdedRequest *mr;
    HttpRequest supportReq;
    Webserver* dws = (Webserver*)(cls);
    internal_unescaper(cls, (char*) url);
    string st_url = HttpUtils::standardizeUrl(url);

    mr = (struct ModdedRequest*) *con_cls;
    access_log(dws, *(mr->completeUri) + " METHOD: " + method);
    mr->ws = dws;
    if (0 == strcmp (method, MHD_HTTP_METHOD_POST) || 0 == strcmp(method, MHD_HTTP_METHOD_PUT)) 
    {
        if (mr->second == false) 
        {
            mr->second = true;
            mr->dhr = new HttpRequest();
            const char *encoding = MHD_lookup_connection_value (connection, MHD_HEADER_KIND, HttpUtils::http_header_content_type.c_str());
            //mr->dhr->setHeader(HttpUtils::http_header_content_type, string(encoding));
            if ( 0x0 != encoding && 0 == strcmp(method, MHD_HTTP_METHOD_POST) && ((0 == strncasecmp (MHD_HTTP_POST_ENCODING_FORM_URLENCODED, encoding, strlen (MHD_HTTP_POST_ENCODING_FORM_URLENCODED))))) 
            {
                mr->pp = MHD_create_post_processor (connection, 1024, &post_iterator, mr);
            } 
            else 
            {
                mr->pp = NULL;
            }
            return MHD_YES;
        }
    }
    else 
    {
        supportReq = HttpRequest();
        mr->dhr = &supportReq;
    }

    if (    0 == strcmp(method, MHD_HTTP_METHOD_DELETE) || 
        0 == strcmp(method, MHD_HTTP_METHOD_GET) ||
        0 == strcmp(method, MHD_HTTP_METHOD_HEAD) ||
        0 == strcmp(method, MHD_HTTP_METHOD_CONNECT) ||
        0 == strcmp(method, MHD_HTTP_METHOD_HEAD) ||
        0 == strcmp(method, MHD_HTTP_METHOD_TRACE)
    ) 
    {
        MHD_get_connection_values (connection, MHD_GET_ARGUMENT_KIND, &buildRequestArgs, (void*) mr);
    } 
    else if (0 == strcmp (method, MHD_HTTP_METHOD_POST) || 0 == strcmp(method, MHD_HTTP_METHOD_PUT)) 
    {
        string encoding = mr->dhr->getHeader(HttpUtils::http_header_content_type);
        if ( 0 == strcmp(method, MHD_HTTP_METHOD_POST) && ((0 == strncasecmp (MHD_HTTP_POST_ENCODING_FORM_URLENCODED, encoding.c_str(), strlen (MHD_HTTP_POST_ENCODING_FORM_URLENCODED))))) 
        {
            MHD_post_process(mr->pp, upload_data, *upload_data_size);
        }
        if ( 0 != *upload_data_size)
        {
#ifdef DEBUG
            cout << "Writing content: " << upload_data << endl;
#endif
            mr->dhr->growContent(upload_data, *upload_data_size);
            *upload_data_size = 0;
            return MHD_YES;
        } 
    } 
    else 
    {
        return method_not_acceptable_page(cls, connection);
    }

    MHD_get_connection_values (connection, MHD_HEADER_KIND, &buildRequestHeader, (void*) mr->dhr);
    MHD_get_connection_values (connection, MHD_FOOTER_KIND, &buildRequestFooter, (void*) mr->dhr);
    MHD_get_connection_values (connection, MHD_COOKIE_KIND, &buildRequestCookie, (void*) mr->dhr);

    mr->dhr->setPath(st_url);
    mr->dhr->setMethod(method);

    if (0 == strcmp (method, MHD_HTTP_METHOD_POST) || 0 == strcmp(method, MHD_HTTP_METHOD_PUT)) 
    {
        supportReq = *(mr->dhr);
    } 

    char* pass = NULL;
    char* user = MHD_basic_auth_get_username_password(connection, &pass);
    char* digestedUser = MHD_digest_auth_get_username(connection);
    supportReq.setVersion(version);
    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
    supportReq.setRequestor(get_ip_str(conninfo->client_addr));
    supportReq.setRequestorPort(get_port(conninfo->client_addr));
    if(pass != NULL)
    {
        supportReq.setPass(pass);
        supportReq.setUser(user);
    }
    if(digestedUser != NULL)
    {
        supportReq.setDigestedUser(digestedUser);
    }
    HttpEndpoint endpoint = HttpEndpoint(st_url);
    HttpResponse dhrs;
    void* page;
    size_t size = 0;
    bool to_free = false;
    const HttpEndpoint* matchingEndpoint = 0x0;
    if(!(dws->registeredResources.count(endpoint) > 0)) 
    {
        map<HttpEndpoint, HttpResource* >::iterator it;
        int len = -1;
        int tot_len = -1;
        bool found = false;
        for(it=dws->registeredResources.begin(); it!=dws->registeredResources.end(); it++) 
        {
            int endpoint_pieces_len = ((int)((*it).first.get_url_pieces().size()));
            int endpoint_tot_len = ((int)((*it).first.get_url_complete().size()));
            if(tot_len == -1 || len == -1 || endpoint_pieces_len > len || (endpoint_pieces_len == len && endpoint_tot_len > tot_len))
            {
                if((*it).first.match(endpoint))
                {
                    found = true;
                    len = endpoint_pieces_len;
                    tot_len = endpoint_tot_len;
                    matchingEndpoint = &((*it).first);
                }
            }
        }
        if(!found) 
        {
            if (user != 0x0)
                free (user);
            if (pass != 0x0)
                free (pass);
            if (digestedUser != 0x0)
                free (digestedUser);
            return not_found_page(cls, connection);
        } 
        else 
        {
            vector<string> url_pars = matchingEndpoint->get_url_pars();
            vector<string> url_pieces = endpoint.get_url_pieces();
            vector<int> chunkes = matchingEndpoint->get_chunk_positions();
            for(unsigned int i = 0; i < url_pars.size(); i++) 
            {
                supportReq.setArg(url_pars[i], url_pieces[chunkes[i]]);
            }
        }
    }
    else
    {
        matchingEndpoint = &endpoint;
    }
    supportReq.set_underlying_connection(connection);
#ifdef DEBUG
        cout << "Using: " << matchingEndpoint->get_url_complete() << endl;
#endif
#ifdef WITH_PYTHON
    PyGILState_STATE gstate;
    if(PyEval_ThreadsInitialized())
    {
        gstate = PyGILState_Ensure();
    }
#endif
    dhrs = dws->registeredResources[*matchingEndpoint]->routeRequest(supportReq);
#ifdef WITH_PYTHON
    if(PyEval_ThreadsInitialized())
    {
        PyGILState_Release(gstate);
    }
#endif
    if(dhrs.content != "")
    {
        vector<char> v_page(dhrs.content.begin(), dhrs.content.end());
        size = v_page.size();
        page = (void*) malloc(size*sizeof(char));
        memcpy( page, &v_page[0], sizeof( char ) * size );
        to_free = true;
    }
    else
    {
        page = (void*)"";
    }
    if(dhrs.responseType == HttpResponse::FILE_CONTENT)
    {
        struct stat st;
        fstat(dhrs.fp, &st);
        size_t filesize = st.st_size;
        response = MHD_create_response_from_fd_at_offset(filesize, dhrs.fp, 0);
    }
    else
        response = MHD_create_response_from_buffer(size, page, MHD_RESPMEM_MUST_COPY);
    vector<pair<string,string> > response_headers = dhrs.getHeaders();
    vector<pair<string,string> > response_footers = dhrs.getFooters();
    vector<pair<string,string> >::iterator it;
    for (it=response_headers.begin() ; it != response_headers.end(); it++)
        MHD_add_response_header(response, (*it).first.c_str(), (*it).second.c_str());
    for (it=response_footers.begin() ; it != response_footers.end(); it++)
        MHD_add_response_footer(response, (*it).first.c_str(), (*it).second.c_str());
    int to_ret;
    if(dhrs.responseType == HttpResponse::DIGEST_AUTH_FAIL)
        to_ret = MHD_queue_auth_fail_response(connection, dhrs.getRealm().c_str(), dhrs.getOpaque().c_str(), response, dhrs.needNonceReload() ? MHD_YES : MHD_NO);
    else if(dhrs.responseType == HttpResponse::BASIC_AUTH_FAIL)
        to_ret = MHD_queue_basic_auth_fail_response(connection, dhrs.getRealm().c_str(), response);
    else
        to_ret = MHD_queue_response(connection, dhrs.getResponseCode(), response);

    if (user != 0x0)
        free (user);
    if (pass != 0x0)
        free (pass);
    if (digestedUser != 0x0)
        free (digestedUser);
    MHD_destroy_response (response);
    if(to_free)
        free(page);
    return to_ret;
}

};
