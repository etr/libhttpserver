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
#include "Webserver.hpp"
#include "HttpUtils.hpp"
#include "iostream"
#include "string_utilities.hpp"
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

using namespace std;

int policyCallback (void *, const struct sockaddr*, socklen_t);
void error_log(void*, const char*, va_list);
void* uri_log(void*, const char*);
void access_log(Webserver*, string);
size_t unescaper_func(void*, struct MHD_Connection*, char*);
size_t internal_unescaper(void*, struct MHD_Connection*, char*);

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
        fprintf (stderr, "Failed to install SIGPIPE handler: %s\n", strerror (errno));
}

//ENDPOINT
HttpEndpoint::HttpEndpoint(bool family):
	url_complete("/"),
	url_modded("/"),
	family_url(family),
    reg_compiled(false)
{
}

HttpEndpoint::HttpEndpoint(const string& url, bool family, bool registration):
	url_complete(string_utilities::to_lower_copy(url)),
	url_modded("/"),
	family_url(family),
    reg_compiled(false)
{
	vector<string> parts = HttpUtils::tokenizeUrl(url);
	string buffered;
	bool first = true;
    if(registration)
    {
        for(int i = 0; i< parts.size(); i++)
        {
			if((parts[i] != "") && (parts[i][0] != '{')) 
			{
				if(first)
				{
					if(parts[i][0] == '^')
					{
						this->url_modded = parts[i];
					}
					else
					{
						this->url_modded += parts[i];
					}
					first = false;
				}
				else
				{
					this->url_modded += "/" + parts[i];
				}
			} 
			else 
			{
				if(( parts[i].size() >= 3) && (parts[i][0] == '{') && (parts[i][parts[i].size() - 1] == '}') ) 
				{
					int bar = parts[i].find_first_of('|');
					if(bar != string::npos)
					{
						this->url_pars.push_back(parts[i].substr(1, bar - 1));
						if(first)
						{
							this->url_modded += parts[i].substr(bar + 1, parts[i].size() - bar - 2);
							first = false;
						}
						else
						{
							this->url_modded += "/"+parts[i].substr(bar + 1, parts[i].size() - bar - 2);
						}
					}
					else
					{
						this->url_pars.push_back(parts[i].substr(1,parts[i].size() - 2));
						if(first)
						{
							this->url_modded += "([^\\/]+)";
							first = false;
						}
						else
						{
							this->url_modded += "/([^\\/]+)";
						}
					}
					this->chunk_positions.push_back(i);
				} 
				else 
				{
					// RITORNARE ECCEZIONE
				}
			}
            this->url_pieces.push_back(parts[i]);
        }
        regcomp(&(this->re_url_modded), url_modded.c_str(), REG_EXTENDED|REG_ICASE);
        reg_compiled = true;
    }
    else
    {
        for(int i = 0; i< parts.size(); i++)
        {
			if(first)
			{
				this->url_modded += parts[i];
				first = false;
			}
			else
			{
				this->url_modded += "/" + parts[i];
			}
            this->url_pieces.push_back(parts[i]);
        }
    }
//	this->re_url_modded = boost::xpressive::sregex::compile( url_modded, boost::xpressive::regex_constants::icase );
}

HttpEndpoint::HttpEndpoint(const HttpEndpoint& h)
{
    this->url_complete = h.url_complete;
    this->url_modded = h.url_modded;
    this->family_url = h.family_url;
    this->reg_compiled = h.reg_compiled;
    if(this->reg_compiled)
        regcomp(&(this->re_url_modded), url_modded.c_str(), REG_EXTENDED|REG_ICASE);
    this->url_pars = h.url_pars;
    this->url_pieces = h.url_pieces;
    this->chunk_positions = h.chunk_positions;
}

HttpEndpoint::~HttpEndpoint()
{
    
    if(reg_compiled)
    {
        regfree(&(this->re_url_modded));
    }
    
}

HttpEndpoint& HttpEndpoint::operator =(const HttpEndpoint& h)
{
    this->url_complete = h.url_complete;
    this->url_modded = h.url_modded;
    this->family_url = h.family_url;
    this->reg_compiled = h.reg_compiled;
    if(this->reg_compiled)
        regcomp(&(this->re_url_modded), url_modded.c_str(), REG_EXTENDED|REG_ICASE);
    this->url_pars = h.url_pars;
    this->url_pieces = h.url_pieces;
    this->chunk_positions = h.chunk_positions;
    return *this;
}

bool HttpEndpoint::operator <(const HttpEndpoint& b) const 
{
	return string_utilities::to_lower_copy(this->url_modded) < string_utilities::to_lower_copy(b.url_modded);
}

bool HttpEndpoint::match(const HttpEndpoint& url) const 
{
	if(this->family_url && (url.url_pieces.size() >= this->url_pieces.size()))
	{
		string nn = "/";
		bool first = true;
		for(int i = 0; i < this->url_pieces.size(); i++)
		{
			if(first)
			{
				nn += url.url_pieces[i];
				first = false;
			}
			else
			{
				nn += "/" + url.url_pieces[i];
			}
		}
        return regexec(&(this->re_url_modded), nn.c_str(), 0, NULL, 0) == 0;
//		return boost::xpressive::regex_match(nn, this->re_url_modded);
	}
	else
	{
        return regexec(&(this->re_url_modded), url.url_modded.c_str(), 0, NULL, 0) == 0;
//		return boost::xpressive::regex_match(url.url_modded, this->re_url_modded);
	}
}

const std::vector<std::string> HttpEndpoint::get_url_pars() const 
{
	return this->url_pars;
}

const std::vector<std::string> HttpEndpoint::get_url_pieces() const 
{
	return this->url_pieces;
}

const std::vector<int> HttpEndpoint::get_chunk_positions() const 
{
	return this->chunk_positions;
}

//RESOURCE
HttpResource::HttpResource() 
{
	this->allowedMethods[MHD_HTTP_METHOD_GET] = true;
	this->allowedMethods[MHD_HTTP_METHOD_POST] = true;
	this->allowedMethods[MHD_HTTP_METHOD_PUT] = true;
	this->allowedMethods[MHD_HTTP_METHOD_HEAD] = true;
	this->allowedMethods[MHD_HTTP_METHOD_DELETE] = true;
	this->allowedMethods[MHD_HTTP_METHOD_TRACE] = true;
	this->allowedMethods[MHD_HTTP_METHOD_CONNECT] = true;
	this->allowedMethods[MHD_HTTP_METHOD_OPTIONS] = true;
}

HttpResource::~HttpResource() 
{
}

HttpResponse HttpResource::render(const HttpRequest& r) 
{
	if(this->isAllowed(r.getMethod()))
	{
		return this->render_404();
	} 
	else 
	{
		return this->render_405();
	}
}

HttpResponse HttpResource::render_404() 
{
	return HttpResponse(NOT_FOUND_ERROR, 404);
}

HttpResponse HttpResource::render_405() 
{
	return HttpResponse(METHOD_ERROR, 405);
}

HttpResponse HttpResource::render_500() 
{
	return HttpResponse(GENERIC_ERROR, 500);
}

HttpResponse HttpResource::render_GET(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_POST(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_PUT(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_DELETE(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_HEAD(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_TRACE(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_OPTIONS(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::render_CONNECT(const HttpRequest& r) 
{
	return this->render(r);
}

HttpResponse HttpResource::routeRequest(const HttpRequest& r) 
{
	string method = string_utilities::to_upper_copy(r.getMethod());

	HttpResponse res;

	if(method == MHD_HTTP_METHOD_GET) 
	{
		res = this->render_GET(r);
	} 
	else if (method == MHD_HTTP_METHOD_POST) 
	{
		res = this->render_POST(r);
	} 
	else if (method == MHD_HTTP_METHOD_PUT) 
	{
		res = this->render_PUT(r);
	} 
	else if (method == MHD_HTTP_METHOD_DELETE) 
	{
		res = this->render_DELETE(r);
	} 
	else if (method == MHD_HTTP_METHOD_HEAD) 
	{
		res = this->render_HEAD(r);
	} 
	else if (method == MHD_HTTP_METHOD_TRACE) 
	{
		res = this->render_TRACE(r);
	} 
	else if (method == MHD_HTTP_METHOD_OPTIONS) 
	{
		res = this->render_OPTIONS(r);
	} 
	else if (method == MHD_HTTP_METHOD_CONNECT) 
	{
		res = this->render_CONNECT(r);
	} 
	else 
	{
		res = this->render(r);
	}
	return res;
}

void HttpResource::setAllowing(const string& method, bool allowing) 
{
	if(this->allowedMethods.count(method)) 
	{
		this->allowedMethods[method] = allowing;
	}
}

void HttpResource::allowAll() 
{
    map<string,bool>::iterator it;
    for ( it=this->allowedMethods.begin() ; it != this->allowedMethods.end(); it++ )
        this->allowedMethods[(*it).first] = true;
}

void HttpResource::disallowAll() 
{
	map<string,bool>::iterator it;
	for ( it=this->allowedMethods.begin() ; it != this->allowedMethods.end(); it++ )
		this->allowedMethods[(*it).first] = false;
}

bool HttpResource::isAllowed(const string& method) 
{
	if(this->allowedMethods.count(method))
	{
		return this->allowedMethods[method];
	}
	else
	{
		return false;
	}
}

//REQUEST
HttpRequest::HttpRequest():
	content("")
{
	this->content = "";
}

HttpRequest::HttpRequest(const HttpRequest& o)
{
	this->user = o.user;
	this->pass = o.pass;
	this->path = o.path;
	this->method = o.method;
	this->post_path = o.post_path;
	this->headers = o.headers;
	this->args = o.args;
	this->content = o.content;
}

const string HttpRequest::getUser() const 
{
	return this->user;
}

const string HttpRequest::getPass() const 
{
	return this->pass;
}

void HttpRequest::setUser(const std::string& user) 
{
	this->user = user;
}

void HttpRequest::setPass(const std::string& pass) 
{
	this->pass = pass;
}

const string HttpRequest::getPath() const 
{
	return this->path;
}

const vector<string> HttpRequest::getPathPieces() const 
{
	return this->post_path;
}

int HttpRequest::getPathPiecesSize() const
{
	return this->post_path.size();
}

const string HttpRequest::getPathPiece(int idx) const
{
	return this->post_path[idx];
}

const string HttpRequest::getMethod() const 
{
	return this->method;
}

void HttpRequest::setPath(const string& path)
{
	//this->path = boost::to_lower_copy(path);
	this->path = path;
	vector<string> complete_path = HttpUtils::tokenizeUrl(this->path);
	for(int i = 0; i < complete_path.size(); i++) 
	{
		this->post_path.push_back(complete_path[i]);
	}
}

void HttpRequest::setMethod(const string& method) 
{
	this->method = string_utilities::to_upper_copy(method);
}

void HttpRequest::setHeader(const string& key, const string& value) 
{
	this->headers[key] = value;
}

void HttpRequest::setFooter(const string& key, const string& value) 
{
	this->footers[key] = value;
}

void HttpRequest::setCookie(const string& key, const string& value) 
{
	this->cookies[key] = value;
}

void HttpRequest::setVersion(const string& version)
{
	this->version = version;
}

const std::string HttpRequest::getVersion() const
{
	return version;
}

void HttpRequest::setHeaders(const map<string, string>& headers) 
{
	map<string, string>::const_iterator it;
	for(it = headers.begin(); it != headers.end(); it++)
		this->headers[it->first] = it->second;
}

void HttpRequest::setFooters(const map<string, string>& footers) 
{
	map<string, string>::const_iterator it;
	for(it = footers.begin(); it != footers.end(); it++)
		this->footers[it->first] = it->second;
}

void HttpRequest::setCookies(const map<string, string>& cookies) 
{
	map<string, string>::const_iterator it;
	for(it = cookies.begin(); it != cookies.end(); it++)
		this->cookies[it->first] = it->second;
}

void HttpRequest::setArgs(const map<string, string>& args) 
{
	map<string, string>::const_iterator it;
	for(it = args.begin(); it != args.end(); it++)
		this->args[it->first] = it->second;
}

void HttpRequest::setArg(const string& key, const string& value) 
{
	this->args[key] = value;
}

void HttpRequest::setArg(const char* key, const char* value, size_t size)
{
	this->args[key] = string(value, size);
}

const string HttpRequest::getArg(const string& key) const 
{
	//string new_key = string_utilities::to_lower_copy(key);
	map<string, string>::const_iterator it = this->args.find(key);
	if(it != this->args.end())
		return it->second;
	else
		return "";
}

const string HttpRequest::getRequestor() const
{
	return this->requestor;
}

void HttpRequest::setRequestor(const std::string& requestor)
{
	this->requestor = requestor;
}

short HttpRequest::getRequestorPort() const
{
	return this->requestorPort;
}

void HttpRequest::setRequestorPort(short requestorPort)
{
	this->requestorPort = requestorPort;
}

const string HttpRequest::getHeader(const string& key) const 
{
	//string new_key = boost::to_lower_copy(key);
	map<string, string>::const_iterator it = this->headers.find(key);
	if(it != this->headers.end())
		return it->second;
	else
		return "";
}

const string HttpRequest::getFooter(const string& key) const 
{
	//string new_key = boost::to_lower_copy(key);
	map<string, string>::const_iterator it = this->footers.find(key);
	if(it != this->footers.end())
		return it->second;
	else
		return "";
}

const std::vector<std::pair<std::string, std::string> > HttpRequest::getHeaders() const
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, HeaderComparator>::const_iterator it;
	for(it = headers.begin(); it != headers.end(); it++)
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
	return toRet;
}

const std::vector<std::pair<std::string, std::string> > HttpRequest::getCookies() const
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, HeaderComparator>::const_iterator it;
	for(it = cookies.begin(); it != cookies.end(); it++)
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
	return toRet;
}

const std::vector<std::pair<std::string, std::string> > HttpRequest::getFooters() const
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, HeaderComparator>::const_iterator it;
	for(it = footers.begin(); it != footers.end(); it++)
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
	return toRet;
}

const std::vector<std::pair<std::string, std::string> > HttpRequest::getArgs() const
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, ArgComparator>::const_iterator it;
	for(it = args.begin(); it != args.end(); it++)
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
	return toRet;
}

const string HttpRequest::getContent() const
{
	return this->content;
}

void HttpRequest::setContent(const string& content) 
{
	this->content = content;
}

void HttpRequest::growContent(const char* content, size_t size)
{
	this->content += string(content, size);
}

void HttpRequest::removeHeader(const string& key) 
{
	this->headers.erase(key);
}

//RESPONSE

HttpResponse::HttpResponse():
	content("{}"),
	responseCode(200),
	fp(-1)
{
	this->setHeader(HttpUtils::http_header_content_type, "application/json");
}

HttpResponse::HttpResponse
(
	const string& content, 
	int responseCode,
	const std::string& contentType,
	const HttpResponse::ResponseType_T& responseType
):
	responseType(responseType)
{
	HttpResponseInit(content, responseCode, contentType, responseType);
}

void HttpResponse::HttpResponseInit
(
	const string& content,
	int responseCode,
	const std::string& contentType,
	const HttpResponse::ResponseType_T& responseType
)
{
	if(responseType == HttpResponse::FILE_CONTENT)
	{
		FILE* f;
		if(!(f = fopen(content.c_str(), "r")))
		{
			this->content = NOT_FOUND_ERROR;
			this->responseCode = 404;
			this->setHeader(HttpUtils::http_header_content_type, contentType);
			this->fp = -1;
		}
		else
		{
			this->responseCode = responseCode;
			this->filename = content;
			this->fp = fileno(f);
		}
	}
	else
	{
		this->content = content;
		this->responseCode = responseCode;
		this->setHeader(HttpUtils::http_header_content_type, contentType);
		this->fp = -1;
	}
}

HttpResponse::HttpResponse(const HttpResponse& o)
{
	this->content = o.content;
	this->responseCode = o.responseCode;
	this->headers = o.headers;
	this->footers = o.footers;
}

void HttpResponse::setContentType(const string& value)
{
	this->headers[HttpUtils::http_header_content_type] = value;
}

void HttpResponse::setHeader(const string& key, const string& value)
{
	this->headers[key] = value;
}

void HttpResponse::setFooter(const string& key, const string& value)
{
	this->footers[key] = value;
}

const string HttpResponse::getHeader(const string& key) 
{
	return this->headers[key];
}

const string HttpResponse::getFooter(const string& key) 
{
	return this->footers[key];
}

void HttpResponse::setContent(const string& content) 
{
	this->content = content;
}

const string HttpResponse::getContent() 
{
	return this->content;
}

void HttpResponse::removeHeader(const string& key) 
{
	this->headers.erase(key);
}

const std::vector<std::pair<std::string, std::string> > HttpResponse::getHeaders() 
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, HeaderComparator>::const_iterator it;
	for(it = headers.begin(); it != headers.end(); it++)
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
	return toRet;
}

const std::vector<std::pair<std::string, std::string> > HttpResponse::getFooters() 
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, ArgComparator>::const_iterator it;
	for(it = footers.begin(); it != footers.end(); it++)
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
	return toRet;
}

void HttpResponse::setHeaders(const map<string, string>& headers) 
{
	map<string, string>::const_iterator it;
	for(it = headers.begin(); it != headers.end(); it ++)
		this->headers[it->first] = it->second;
}

void HttpResponse::setFooters(const map<string, string>& footers) 
{
	map<string, string>::const_iterator it;
	for(it = footers.begin(); it != footers.end(); it ++)
		this->footers[it->first] = it->second;
}

int HttpResponse::getResponseCode() 
{
	return this->responseCode;
}

void HttpResponse::setResponseCode(int responseCode) 
{
	this->responseCode = responseCode;
}

//LOGGING DELEGATE
LoggingDelegate::LoggingDelegate() {}

LoggingDelegate::~LoggingDelegate() {}

void LoggingDelegate::log_access(const string& s) const {}

void LoggingDelegate::log_error(const string& s) const {}

//REQUEST VALIDATOR
RequestValidator::RequestValidator() {}

RequestValidator::~RequestValidator() {}

bool RequestValidator::validate(const string& address) const {}

//UNESCAPER
Unescaper::Unescaper() {}

Unescaper::~Unescaper() {}

void Unescaper::unescape(char* s) const {}

inline CreateWebserver::CreateWebserver():
    _port(DEFAULT_WS_PORT),
    _startMethod(HttpUtils::INTERNAL_SELECT),
    _maxThreads(0),
    _maxConnections(0),
    _memoryLimit(0),
    _connectionTimeout(DEFAULT_WS_TIMEOUT),
    _perIPConnectionLimit(0),
    _logDelegate(0x0),
    _validator(0x0),
    _unescaper(0x0),
    _maxThreadStackSize(0),
    _useSsl(false),
    _useIpv6(false),
    _debug(false),
    _pedantic(false),
    _httpsMemKey(""),
    _httpsMemCert(""),
    _httpsMemTrust(""),
    _httpsPriorities(""),
    _credType(HttpUtils::NONE),
    _digestAuthRandom(""),
    _nonceNcSize(0)
{
}

inline CreateWebserver::CreateWebserver(const int port):
    _port(port),
    _startMethod(HttpUtils::INTERNAL_SELECT),
    _maxThreads(0),
    _maxConnections(0),
    _memoryLimit(0),
    _connectionTimeout(DEFAULT_WS_TIMEOUT),
    _perIPConnectionLimit(0),
    _logDelegate(0x0),
    _validator(0x0),
    _unescaper(0x0),
    _maxThreadStackSize(0),
    _useSsl(false),
    _useIpv6(false),
    _debug(false),
    _pedantic(false),
    _httpsMemKey(""),
    _httpsMemCert(""),
    _httpsMemTrust(""),
    _httpsPriorities(""),
    _credType(HttpUtils::NONE),
    _digestAuthRandom(""),
    _nonceNcSize(0)
{
}

inline CreateWebserver& CreateWebserver::port(const int port) { _port = port; return *this; }
inline CreateWebserver& CreateWebserver::startMethod(const HttpUtils::StartMethod_T& startMethod) { _startMethod = startMethod; return *this; }
inline CreateWebserver& CreateWebserver::maxThreads(const int maxThreads) { _maxThreads = maxThreads; return *this; }
inline CreateWebserver& CreateWebserver::maxConnections(const int maxConnections) { _maxConnections = maxConnections; return *this; }
inline CreateWebserver& CreateWebserver::memoryLimit(const int memoryLimit) { _memoryLimit = memoryLimit; return *this; }
inline CreateWebserver& CreateWebserver::connectionTimeout(const int connectionTimeout) { _connectionTimeout = connectionTimeout; return *this; }
inline CreateWebserver& CreateWebserver::perIPConnectionLimit(const int perIPConnectionLimit) { _perIPConnectionLimit = perIPConnectionLimit; return *this; }
inline CreateWebserver& CreateWebserver::logDelegate(const LoggingDelegate* logDelegate) { _logDelegate = logDelegate; return *this; }
inline CreateWebserver& CreateWebserver::validator(const RequestValidator* validator) { _validator = validator; return *this; }
inline CreateWebserver& CreateWebserver::unescaper(const Unescaper* unescaper) { _unescaper = unescaper; return *this; }
inline CreateWebserver& CreateWebserver::maxThreadStackSize(const int maxThreadStackSize) { _maxThreadStackSize = maxThreadStackSize; return *this; }
inline CreateWebserver& CreateWebserver::useSsl() { _useSsl = true; return *this; }
inline CreateWebserver& CreateWebserver::noSsl() { _useSsl = false; return *this; }
inline CreateWebserver& CreateWebserver::useIpv6() { _useIpv6 = true; return *this; }
inline CreateWebserver& CreateWebserver::noIpv6() { _useIpv6 = false; return *this; }
inline CreateWebserver& CreateWebserver::debug() { _debug = true; return *this; }
inline CreateWebserver& CreateWebserver::noDebug() { _debug = false; return *this; }
inline CreateWebserver& CreateWebserver::pedantic() { _pedantic = true; return *this; }
inline CreateWebserver& CreateWebserver::noPedantic() { _pedantic = false; return *this; }
inline CreateWebserver& CreateWebserver::httpsMemKey(const string& httpsMemKey) { _httpsMemKey = httpsMemKey; return *this; }
inline CreateWebserver& CreateWebserver::httpsMemCert(const string& httpsMemCert) { _httpsMemCert = httpsMemCert; return *this; }
inline CreateWebserver& CreateWebserver::httpsMemTrust(const string& httpsMemTrust) { _httpsMemTrust = httpsMemTrust; return *this; }
inline CreateWebserver& CreateWebserver::httpsPriorities(const string& httpsPriorities) { _httpsPriorities = httpsPriorities; return *this; }
inline CreateWebserver& CreateWebserver::credType(const HttpUtils::CredType_T& credType) { _credType = credType; return *this; }
inline CreateWebserver& CreateWebserver::digestAuthRandom(const string& digestAuthRandom) { _digestAuthRandom = digestAuthRandom; return *this; }
inline CreateWebserver& CreateWebserver::nonceNcSize(const int nonceNcSize) { _nonceNcSize = nonceNcSize; return *this; }

//WEBSERVER
Webserver::Webserver 
(
	const int port, 
    const HttpUtils::StartMethod_T startMethod,
	const int maxThreads, 
	const int maxConnections,
	const int memoryLimit,
	const int connectionTimeout,
	const int perIPConnectionLimit,
	const LoggingDelegate* logDelegate,
	const RequestValidator* validator,
	const Unescaper* unescaper,
	const int maxThreadStackSize,
    const bool useSsl,
    const bool useIpv6,
    const bool debug,
    const bool pedantic,
	const string& httpsMemKey,
	const string& httpsMemCert,
	const string& httpsMemTrust,
	const string& httpsPriorities,
	const HttpUtils::CredType_T credType,
	const string digestAuthRandom,
	const int nonceNcSize
) :
	port(port), 
	maxThreads(maxThreads), 
	maxConnections(maxConnections),
	memoryLimit(memoryLimit),
	connectionTimeout(connectionTimeout),
	perIPConnectionLimit(perIPConnectionLimit),
	logDelegate(logDelegate),
	validator(validator),
	unescaper(unescaper),
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
	running(false)
{
    ignore_sigpipe();
}

Webserver::Webserver(const CreateWebserver& params):
    port(params._port),
    maxThreads(params._maxThreads),
    maxConnections(params._maxConnections),
    memoryLimit(params._memoryLimit),
    connectionTimeout(params._connectionTimeout),
    perIPConnectionLimit(params._perIPConnectionLimit),
    logDelegate(params._logDelegate),
    validator(params._validator),
    unescaper(params._unescaper),
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
    running(false)
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
	delete mr->dhr;
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
	if(httpsMemKey != "")
		iov.push_back(gen(MHD_OPTION_HTTPS_MEM_KEY, (intptr_t)httpsMemKey.c_str()));
	if(httpsMemCert != "")
		iov.push_back(gen(MHD_OPTION_HTTPS_MEM_CERT, (intptr_t)httpsMemCert.c_str()));
	if(httpsMemTrust != "")
		iov.push_back(gen(MHD_OPTION_HTTPS_MEM_TRUST, (intptr_t)httpsMemTrust.c_str()));
	if(httpsPriorities != "")
		iov.push_back(gen(MHD_OPTION_HTTPS_PRIORITIES, (intptr_t)httpsPriorities.c_str()));
	if(digestAuthRandom != "")
		iov.push_back(gen(MHD_OPTION_DIGEST_AUTH_RANDOM, digestAuthRandom.size(), (char*)digestAuthRandom.c_str()));
	if(credType != HttpUtils::NONE)
		iov.push_back(gen(MHD_OPTION_HTTPS_CRED_TYPE, credType));

	iov.push_back(gen(MHD_OPTION_END, 0, NULL ));

	struct MHD_OptionItem ops[iov.size()];
	for(int i = 0; i < iov.size(); i++)
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
		cout << "Unable to connect daemon to port: " << this->port << endl;
		return false;
	}
	this->running = true;
	if(blocking)
	{
		while(blocking && running)
			sleep(1);
		this->stop();
	}
	return true;
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
}

void Webserver::registerResource(const string& resource, HttpResource* http_resource, bool family)
{
	this->registeredResources[HttpEndpoint(resource, family, true)] = http_resource;
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
	HttpRequest* dhr = (HttpRequest*)(cls);
	int size = http_unescape((char*) value);
	dhr->setArg(key, string(value, size));
	return MHD_YES;
}

int policyCallback (void *cls, const struct sockaddr* addr, socklen_t addrlen)
{
	// TODO: Develop a system that allow to study a configurable policy callback
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

size_t internal_unescaper(void* cls, struct MHD_Connection *c, char* s)
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
		"application/json");
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
		"application/json");
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
	int ret;
	HttpRequest supportReq;
	Webserver* dws = (Webserver*)(cls);
	http_unescape((char*) url);
	string st_url = HttpUtils::standardizeUrl(url);

	mr = (struct ModdedRequest*) *con_cls;
	access_log(dws, *(mr->completeUri) + " METHOD: " + method);
	if (0 == strcmp (method, MHD_HTTP_METHOD_POST) || 0 == strcmp(method, MHD_HTTP_METHOD_PUT)) 
	{
		if (mr->second == false) 
		{
			mr->second = true;
			mr->dhr = new HttpRequest();
			mr->dhr->setPath(st_url);
			mr->dhr->setMethod(method);
			const char *encoding = MHD_lookup_connection_value (connection, MHD_HEADER_KIND, HttpUtils::http_header_content_type.c_str());
			if ( 0x0 != encoding && 0 == strcmp(method, MHD_HTTP_METHOD_POST) && ((0 == strncasecmp (MHD_HTTP_POST_ENCODING_FORM_URLENCODED, encoding, strlen (MHD_HTTP_POST_ENCODING_FORM_URLENCODED))))) 
			{
				mr->dhr->setHeader(HttpUtils::http_header_content_type, string(encoding));
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
		supportReq.setMethod(string(method));
		supportReq.setPath(string(st_url));
		const char *encoding = MHD_lookup_connection_value (connection, MHD_HEADER_KIND, HttpUtils::http_header_content_type.c_str());
		if(encoding != 0x0)
			supportReq.setHeader(HttpUtils::http_header_content_type, string(encoding));
	}

	if(0 == strcmp (method, MHD_HTTP_METHOD_POST) || 0 == strcmp(method, MHD_HTTP_METHOD_PUT))
	{
		MHD_get_connection_values (connection, MHD_HEADER_KIND, &buildRequestHeader, (void*) mr->dhr);
		MHD_get_connection_values (connection, MHD_FOOTER_KIND, &buildRequestFooter, (void*) mr->dhr);
		MHD_get_connection_values (connection, MHD_COOKIE_KIND, &buildRequestCookie, (void*) mr->dhr);
	}
	else
	{
		MHD_get_connection_values (connection, MHD_HEADER_KIND, &buildRequestHeader, (void*) &supportReq);
		MHD_get_connection_values (connection, MHD_FOOTER_KIND, &buildRequestFooter, (void*) &supportReq);
		MHD_get_connection_values (connection, MHD_COOKIE_KIND, &buildRequestCookie, (void*) &supportReq);
	}
	if (    0 == strcmp(method, MHD_HTTP_METHOD_DELETE) || 
		0 == strcmp(method, MHD_HTTP_METHOD_GET) ||
		0 == strcmp(method, MHD_HTTP_METHOD_HEAD) ||
		0 == strcmp(method, MHD_HTTP_METHOD_CONNECT) ||
		0 == strcmp(method, MHD_HTTP_METHOD_HEAD) ||
		0 == strcmp(method, MHD_HTTP_METHOD_TRACE)
	) 
	{
		MHD_get_connection_values (connection, MHD_GET_ARGUMENT_KIND, &buildRequestArgs, (void*) &supportReq);
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
			mr->dhr->growContent(upload_data, *upload_data_size);
			*upload_data_size = 0;
			return MHD_YES;
		} 
	} 
	else 
	{
		return method_not_acceptable_page(cls, connection);
	}

	if (0 == strcmp (method, MHD_HTTP_METHOD_POST) || 0 == strcmp(method, MHD_HTTP_METHOD_PUT)) 
	{
		supportReq = *(mr->dhr);
	} 

	char* pass = NULL;
	char* user = MHD_basic_auth_get_username_password (connection, &pass);
	supportReq.setVersion(version);
	const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
	supportReq.setRequestor(get_ip_str(conninfo->client_addr));
	supportReq.setRequestorPort(get_port(conninfo->client_addr));
	if(pass != NULL)
	{
		supportReq.setPass(string(pass));
		supportReq.setUser(string(user));
	}
	int toRet;
	HttpEndpoint endpoint = HttpEndpoint(st_url);
	HttpResponse dhrs;
	void* page;
	size_t size = 0;
	bool to_free = false;
	if((dws->registeredResources.count(endpoint) > 0)) 
	{
#ifdef WITH_PYTHON
		PyGILState_STATE gstate;
		if(PyEval_ThreadsInitialized())
		{
			gstate = PyGILState_Ensure();
		}
#endif
		dhrs = dws->registeredResources[endpoint]->routeRequest(supportReq);
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
			page = (void*) "";
		}
	} 
	else 
	{
		map<HttpEndpoint, HttpResource* >::iterator it;
		int len = -1;
		bool found = false;
		HttpEndpoint matchingEndpoint;
		for(it=dws->registeredResources.begin(); it!=dws->registeredResources.end(); it++) 
		{
			if(len == -1 || (*it).first.get_url_pieces().size() > len)
			{
				if((*it).first.match(endpoint))
				{
					found = true;
					len = (*it).first.get_url_pieces().size();
					matchingEndpoint = (*it).first;
				}
			}
		}
		if(!found) 
		{
			toRet = not_found_page(cls, connection);
			if (user != 0x0)
				free (user);
			if (pass != 0x0)
				free (pass);
			return MHD_YES;
		} 
		else 
		{
			vector<string> url_pars = matchingEndpoint.get_url_pars();
			vector<string> url_pieces = endpoint.get_url_pieces();
			vector<int> chunkes = matchingEndpoint.get_chunk_positions();
			for(int i = 0; i < url_pars.size(); i++) 
			{
				supportReq.setArg(url_pars[i], url_pieces[chunkes[i]]);
			}
#ifdef WITH_PYTHON
			PyGILState_STATE gstate;
			if(PyEval_ThreadsInitialized())
			{
				gstate = PyGILState_Ensure();
			}
#endif
			dhrs = dws->registeredResources[matchingEndpoint]->routeRequest(supportReq);
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
		}
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
	ret = MHD_queue_response(connection, dhrs.getResponseCode(), response);
	toRet = ret;

	if (user != 0x0)
		free (user);
	if (pass != 0x0)
		free (pass);
	MHD_destroy_response (response);
	if(to_free)
		free(page);
	return MHD_YES;
}

//int main(){
    //Webserver dw = Webserver(9999); 
    //dw.start(true);
//}

