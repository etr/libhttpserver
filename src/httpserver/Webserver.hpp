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
#ifndef _FRAMEWORK_WEBSERVER_HPP_
#define _FRAMEWORK_WEBSERVER_HPP_

#define NOT_FOUND_ERROR "Not Found"
#define METHOD_ERROR "Method not Allowed"
#define NOT_METHOD_ERROR "Method not Acceptable"
#define GENERIC_ERROR "Internal Error"
#define DEFAULT_WS_PORT 9898
#define DEFAULT_WS_TIMEOUT 180

#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <microhttpd.h>
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
//#include <boost/xpressive/xpressive.hpp>

namespace httpserver {

class HttpResource;
class HttpResponse;
class HttpRequest;
class HttpEndpoint;

using namespace http;

namespace http {
struct ip_representation;
};

/**
 * Delegate class used to wrap callbacks dedicated to logging.
**/
class LoggingDelegate
{
	public:
		/**
		 * Delegate constructor.
		**/
		LoggingDelegate();
		/**
		 * Destructor of the class
		**/
		virtual ~LoggingDelegate();
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
class RequestValidator
{
	public:
		/**
		 * Delegate constructor
		**/
		RequestValidator();
		/**
		 * Destructor of the class
		**/
		virtual ~RequestValidator();
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
class Unescaper
{
	public:
		/**
		 * Delegate constructor
		**/
		Unescaper();
		/**
		 * Destructor of the class
		**/
		virtual ~Unescaper();
		/**
		 * Method used to unescape the uri.
		 * @param s pointer to the uri string representation to unescape.
		**/
		virtual void unescape(char* s) const;
};

class CreateWebserver;

/**
 * Class representing the webserver. Main class of the apis.
**/
class Webserver 
{
	public:
		/**
		 * Constructor of the class.
		 * @param port Integer representing the port the webserver is listening on.
         * @param startMethod
		 * @param maxThreads max number of serving threads (0 -> infty)
		 * @param maxConnections max number of allowed connections (0 -> infty).
		 * @param memoryLimit max memory allocated to serve requests (0 -> infty).
		 * @param perIPConnectionLimit max number of connections allowed for a single IP (0 -> infty).
		 * @param logDelegate LoggingDelegate object used to log
		 * @param validator RequestValidator object used to validate requestors
		 * @param unescaper Unescaper object used to unescape urls.
		 * @param maxThreadStackSize max dimesion of request stack
		 * @param httpsMemKey used with https. Private key used for requests.
		 * @param httpsMemCert used with https. Certificate used for requests.
		 * @param httpsMemTrust used with https. CA Certificate used to trust request certificates.
		 * @param httpsPriorities used with https. Determinates the SSL/TLS version to be used.
		 * @param credType used with https. Daemon credential type. Can be NONE, certificate or anonymous.
		 * @param digestAuthRandom used with https. Digest authentication nonce's seed.
		 * @param nonceNcSize used with https. Size of an array of nonce and nonce counter map.
		**/
		Webserver
		(
			int port = DEFAULT_WS_PORT, 
            const HttpUtils::StartMethod_T& startMethod = HttpUtils::INTERNAL_SELECT,
			int maxThreads = 0, 
			int maxConnections = 0,
			int memoryLimit = 0,
			int connectionTimeout = DEFAULT_WS_TIMEOUT,
			int perIPConnectionLimit = 0,
			const LoggingDelegate* logDelegate = 0x0,
			const RequestValidator* validator = 0x0,
			const Unescaper* unescaper = 0x0,
            const struct sockaddr* bindAddress = 0x0,
            int bindSocket = 0,
			int maxThreadStackSize = 0,
            bool useSsl = false,
            bool useIpv6 = false,
            bool debug = false,
            bool pedantic = false,
			const std::string& httpsMemKey = "",
			const std::string& httpsMemCert = "",
			const std::string& httpsMemTrust = "",
			const std::string& httpsPriorities = "",
			const HttpUtils::CredType_T& credType= HttpUtils::NONE,
			const std::string digestAuthRandom = "", //IT'S CORRECT TO PASS THIS PARAMETER BY VALUE
			int nonceNcSize = 0
		);
        Webserver(const CreateWebserver& params);
		/**
		 * Destructor of the class
		**/
		~Webserver();
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
		bool isRunning();
		/**
		 * Method used to registrate a resource to the webserver.
		 * @param resource The url pointing to the resource. This url could be also parametrized in the form /path/to/url/{par1}/and/{par2}
		 *                 or a regular expression.
		 * @param http_resource HttpResource pointer to register.
		 * @param family boolean indicating whether the resource is registered for the endpoint and its child or not.
		**/
		void registerResource(const std::string& resource, HttpResource* http_resource, bool family = false);
        void unregisterResource(const std::string& resource);
        void banIp(const std::string& ip);
        void unbanIp(const std::string& ip);
		/**
		 * Method used to kill the webserver waiting for it to terminate
		**/
		void sweetKill();
	private:
		int port;
        HttpUtils::StartMethod_T startMethod;
		int maxThreads;
		int maxConnections;
		int memoryLimit;
		int connectionTimeout;
		int perIPConnectionLimit;
		const LoggingDelegate* logDelegate;
		const RequestValidator* validator;
		const Unescaper* unescaper;
        const struct sockaddr* bindAddress;
        int bindSocket;
		int maxThreadStackSize;
        bool useSsl;
        bool useIpv6;
        bool debug;
        bool pedantic;
		std::string httpsMemKey;
		std::string httpsMemCert;
		std::string httpsMemTrust;
		std::string httpsPriorities;
		HttpUtils::CredType_T credType;
		std::string digestAuthRandom;
		int nonceNcSize;
		bool running;

		std::map<HttpEndpoint, HttpResource* > registeredResources;
#ifdef USE_CPP_ZEROX
        std::unordered_set<ip_representation> bans;
#else
        std::set<ip_representation> bans;
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
		static void requestCompleted(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe);
		static int buildRequestHeader (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
		static int buildRequestFooter (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
		static int buildRequestCookie (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
		static int buildRequestArgs (void *cls, enum MHD_ValueKind kind, const char *key, const char *value);
		static int answerToConnection
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

		friend int policyCallback (void *cls, const struct sockaddr* addr, socklen_t addrlen);
		friend void error_log(void* cls, const char* fmt, va_list ap);
		friend void access_log(Webserver* cls, std::string uri);
		friend void* uri_log(void* cls, const char* uri);
		friend size_t unescaper_func(void * cls, struct MHD_Connection *c, char *s);
		friend size_t internal_unescaper(void * cls, char *s);
};

class CreateWebserver 
{
    public:
        CreateWebserver():
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

        CreateWebserver(int port):
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
            _bindAddress(0x0),
            _bindSocket(0),
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

        CreateWebserver& port(int port) { _port = port; return *this; }
        CreateWebserver& startMethod(const HttpUtils::StartMethod_T& startMethod) { _startMethod = startMethod; return *this; }
        CreateWebserver& maxThreads(int maxThreads) { _maxThreads = maxThreads; return *this; }
        CreateWebserver& maxConnections(int maxConnections) { _maxConnections = maxConnections; return *this; }
        CreateWebserver& memoryLimit(int memoryLimit) { _memoryLimit = memoryLimit; return *this; }
        CreateWebserver& connectionTimeout(int connectionTimeout) { _connectionTimeout = connectionTimeout; return *this; }
        CreateWebserver& perIPConnectionLimit(int perIPConnectionLimit) { _perIPConnectionLimit = perIPConnectionLimit; return *this; }
        CreateWebserver& logDelegate(const LoggingDelegate* logDelegate) { _logDelegate = logDelegate; return *this; }
        CreateWebserver& validator(const RequestValidator* validator) { _validator = validator; return *this; }
        CreateWebserver& unescaper(const Unescaper* unescaper) { _unescaper = unescaper; return *this; }
        CreateWebserver& bindAddress(const struct sockaddr* bindAddress) { _bindAddress = bindAddress; return *this; }
        CreateWebserver& bindSocket(int bindSocket) { _bindSocket = bindSocket; return *this; }
        CreateWebserver& maxThreadStackSize(int maxThreadStackSize) { _maxThreadStackSize = maxThreadStackSize; return *this; }
        CreateWebserver& useSsl() { _useSsl = true; return *this; }
        CreateWebserver& noSsl() { _useSsl = false; return *this; }
        CreateWebserver& useIpv6() { _useIpv6 = true; return *this; }
        CreateWebserver& noIpv6() { _useIpv6 = false; return *this; }
        CreateWebserver& debug() { _debug = true; return *this; }
        CreateWebserver& noDebug() { _debug = false; return *this; }
        CreateWebserver& pedantic() { _pedantic = true; return *this; }
        CreateWebserver& noPedantic() { _pedantic = false; return *this; }
        CreateWebserver& httpsMemKey(const std::string& httpsMemKey);
        CreateWebserver& httpsMemCert(const std::string& httpsMemCert);
        CreateWebserver& httpsMemTrust(const std::string& httpsMemTrust);
        CreateWebserver& rawHttpsMemKey(const std::string& httpsMemKey) { _httpsMemKey = httpsMemKey; return *this; }
        CreateWebserver& rawHttpsMemCert(const std::string& httpsMemCert) { _httpsMemCert = httpsMemCert; return *this; }
        CreateWebserver& rawHttpsMemTrust(const std::string& httpsMemTrust) { _httpsMemTrust = httpsMemTrust; return *this; }
        CreateWebserver& httpsPriorities(const std::string& httpsPriorities) { _httpsPriorities = httpsPriorities; return *this; }
        CreateWebserver& credType(const HttpUtils::CredType_T& credType) { _credType = credType; return *this; }
        CreateWebserver& digestAuthRandom(const std::string& digestAuthRandom) { _digestAuthRandom = digestAuthRandom; return *this; }
        CreateWebserver& nonceNcSize(int nonceNcSize) { _nonceNcSize = nonceNcSize; return *this; }
    private:
        int _port;
        HttpUtils::StartMethod_T _startMethod;
        int _maxThreads;
        int _maxConnections;
        int _memoryLimit;
        int _connectionTimeout;
        int _perIPConnectionLimit;
        const LoggingDelegate* _logDelegate;
        const RequestValidator* _validator;
        const Unescaper* _unescaper;
        const struct sockaddr* _bindAddress;
        int _bindSocket;
        int _maxThreadStackSize;
        bool _useSsl;
        bool _useIpv6;
        bool _debug;
        bool _pedantic;
        std::string _httpsMemKey;
        std::string _httpsMemCert;
        std::string _httpsMemTrust;
        std::string _httpsPriorities;
        HttpUtils::CredType_T _credType;
        std::string _digestAuthRandom;
        int _nonceNcSize;

        friend class Webserver;
};

struct ModdedRequest
{
	struct MHD_PostProcessor *pp;
	std::string* completeUri;
	HttpRequest *dhr;
	Webserver* ws;
	bool second;
};

};
#endif //_FRAMEWORK_WEBSERVER_HPP__
