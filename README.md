<!---
Copyright (C)  2011-2019  Sebastiano Merlino.
    Permission is granted to copy, distribute and/or modify this document
    under the terms of the GNU Free Documentation License, Version 1.3
    or any later version published by the Free Software Foundation;
    with no Invariant Sections, no Front-Cover Texts, and no Back-Cover Texts.
    A copy of the license is included in the section entitled "GNU
    Free Documentation License".
-->

# The libhttpserver reference manual
![GA: Build Status](https://github.com/etr/libhttpserver/actions/workflows/verify-build.yml/badge.svg)
[![Build status](https://ci.appveyor.com/api/projects/status/ktoy6ewkrf0q1hw6/branch/master?svg=true)](https://ci.appveyor.com/project/etr/libhttpserver/branch/master)
[![codecov](https://codecov.io/gh/etr/libhttpserver/branch/master/graph/badge.svg)](https://codecov.io/gh/etr/libhttpserver)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/1bd1e8c21f66400fb70e5a5ce357b525)](https://www.codacy.com/gh/etr/libhttpserver/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=etr/libhttpserver&amp;utm_campaign=Badge_Grade)
[![Gitter chat](https://badges.gitter.im/etr/libhttpserver.png)](https://gitter.im/libhttpserver/community)

[![ko-fi](https://www.ko-fi.com/img/donate_sm.png)](https://ko-fi.com/F1F5HY8B)

## Tl;dr
libhttpserver is a C++ library for building high performance RESTful web servers.
libhttpserver is built upon  [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) to provide a simple API for developers to create HTTP services in C++.

**Features:**
- HTTP 1.1 compatible request parser
- RESTful oriented interface
- Flexible handler API
- Cross-platform compatible
- Implementation is HTTP 1.1 compliant
- Multiple threading models
- Support for IPv6
- Support for SHOUTcast
- Support for incremental processing of POST data (optional)
- Support for basic and digest authentication (optional)
- Support for TLS (requires libgnutls, optional)

## Table of Contents
* [Introduction](#introduction)
* [Requirements](#requirements)
* [Building](#building)
* [Getting Started](#getting-started)
* [Structures and classes type definition](#structures-and-classes-type-definition)
* [Create and work with a webserver](#create-and-work-with-a-webserver)
* [The resource object](#the-resource-object)
* [Registering resources](#registering-resources)
* [Parsing requests](#parsing-requests)
* [Building responses to requests](#building-responses-to-requests)
* [IP Blacklisting and Whitelisting](#ip-blacklisting-and-whitelisting)
* [Authentication](#authentication)
* [HTTP Utils](#http-utils)
* [Other Examples](#other-examples)

#### Community
* [Code of Conduct (on a separate page)](https://github.com/etr/libhttpserver/blob/master/CODE_OF_CONDUCT.md)
* [Contributing (on a separate page)](https://github.com/etr/libhttpserver/blob/master/CONTRIBUTING.md) 

#### Appendices
* [Copying statement](#copying)
* [GNU-LGPL](#GNU-lesser-general-public-license): The GNU Lesser General Public License says how you can copy and share almost all of libhttpserver.
* [GNU-FDL](#GNU-free-documentation-license): The GNU Free Documentation License says how you can copy and share the documentation of libhttpserver.

## Introduction
libhttpserver is meant to constitute an easy system to build HTTP servers with REST fashion.
libhttpserver is based on [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) and, like this, it is a daemon library (parts of this documentation are, in fact, matching those of the wrapped library).
The mission of this library is to support all possible HTTP features directly and with a simple semantic allowing then the user to concentrate only on his application and not on HTTP request handling details.

The library is supposed to work transparently for the client Implementing the business logic and using the library itself to realize an interface.
If the user wants it must be able to change every behavior of the library itself through the registration of callbacks.

libhttpserver is able to decode certain body format a and automatically format them in object oriented fashion. This is true for query arguments and for *POST* and *PUT* requests bodies if *application/x-www-form-urlencoded* or *multipart/form-data* header are passed.

All functions are guaranteed to be completely reentrant and thread-safe (unless differently specified).
Additionally, clients can specify resource limits on the overall number of connections, number of connections per IP address and memory used per connection to avoid resource exhaustion.

[Back to TOC](#table-of-contents)

## Requirements
libhttpserver can be used without any dependencies aside from libmicrohttpd.

The minimum versions required are:
* g++ >= 5.5.0 or clang-3.6
* libmicrohttpd >= 0.9.64
* [Optionally]: for TLS (HTTPS) support, you'll need [libgnutls](http://www.gnutls.org/).
* [Optionally]: to compile the code-reference, you'll need [doxygen](http://www.doxygen.nl/).

Additionally, for MinGW on windows you will need:
* libwinpthread (For MinGW-w64, if you use thread model posix then you have this)

For versions before 0.18.0, on MinGW, you will need:
* libgnurx >= 2.5.1

Furthermore, the testcases use [libcurl](http://curl.haxx.se/libcurl/) but you don't need it to compile the library.

Please refer to the readme file for your particular distribution if there is one for important notes.

[Back to TOC](#table-of-contents)

## Building
libhttpserver uses the standard system where the usual build process involves running
> ./bootstrap  
> mkdir build  
> cd build  
> \.\./configure  
> make  
> make install # (optionally to install on the system)

[Back to TOC](#table-of-contents)

### Optional parameters to configure script
A complete list of parameters can be obtained running 'configure --help'.
Here are listed the libhttpserver specific options (the canonical configure options are also supported).

* _\-\-enable-same-directory-build:_ enable to compile in the same directory. This is heavily discouraged. (def=no)
* _\-\-enable-debug:_ enable debug data generation. (def=no)
* _\-\-disable-doxygen-doc:_ don't generate any doxygen documentation. Doxygen is automatically invoked if present on the system. Automatically disabled otherwise.
* _\-\-enable-fastopen:_ enable use of TCP_FASTOPEN (def=yes)
* _\-\-enable-static:_ enable use static linking (def=yes)

[Back to TOC](#table-of-contents)

## Getting Started
The most basic example of creating a server and handling a requests for the path `/hello`:
```cpp
    #include <httpserver.hpp>

    using namespace httpserver;

    class hello_world_resource : public http_resource {
    public:
        std::shared_ptr<http_response> render(const http_request&) {
            return std::shared_ptr<http_response>(new string_response("Hello, World!"));
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080);

        hello_world_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);
        
        return 0;
    }
```
To test the above example, you could run the following command from a terminal:
    
    curl -XGET -v http://localhost:8080/hello

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/minimal_hello_world.cpp).

[Back to TOC](#table-of-contents)

## Structures and classes type definition
* _webserver:_ Represents the daemon listening on a socket for HTTP traffic.
	* _create_webserver:_ Builder class to support the creation of a webserver.
* _http_resource:_ Represents the resource associated with a specific http endpoint.
* _http_request:_ Represents the request received by the resource that process it.
* _http_response:_ Represents the response sent by the server once the resource finished its work.
	* _string_response:_ A simple string response.
	* _file_response:_ A response getting content from a file.
	* _basic_auth_fail_response:_ A failure in basic authentication.
	* _digest_auth_fail_response:_ A failure in digest authentication.
	* _deferred_response:_ A response getting content from a callback.

[Back to TOC](#table-of-contents)

## Create and work with a webserver
As you can see from the example above, creating a webserver with standard configuration is quite simple:
```cpp
    webserver ws = create_webserver(8080);
```
The `create_webserver` class is a supporting _builder_ class that eases the building of a webserver through chained syntax.

### Basic Startup Options

In this section we will explore other basic options that you can use when configuring your server. More advanced options (custom callbacks, https support, etc...) will be discussed separately.

* _.port(**int** port):_ The port at which the server will listen. This can also be passed to the consturctor of `create_webserver`. E.g. `create_webserver(8080)`.
* _.max_connections(**int** max_conns):_ Maximum number of concurrent connections to accept. The default is `FD_SETSIZE - 4` (the maximum number of file descriptors supported by `select` minus four for `stdin`, `stdout`, `stderr` and the server socket). In other words, the default is as large as possible. Note that if you set a low connection limit, you can easily get into trouble with browsers doing request pipelining.
For example, if your connection limit is “1”, a browser may open a first connection to access your “index.html” file, keep it open but use a second connection to retrieve CSS files, images and the like. In fact, modern browsers are typically by default configured for up to 15 parallel connections to a single server. If this happens, the library will refuse to even accept the second connection until the first connection is closed — which does not happen until timeout. As a result, the browser will fail to render the page and seem to hang. If you expect your server to operate close to the connection limit, you should first consider using a lower timeout value and also possibly add a “Connection: close” header to your response to ensure that request pipelining is not used and connections are closed immediately after the request has completed.
* _.content_size_limit(**size_t** size_limit):_ Sets the maximum size of the content that a client can send over in a single block. The default is `-1 = unlimited`.
* _.connection_timeout(**int** timeout):_ Determines after how many seconds of inactivity a connection should be timed out automatically. The default timeout is `180 seconds`.
* _.memory_limit(**int** memory_limit):_ Maximum memory size per connection (followed by a `size_t`). The default is 32 kB (32*1024 bytes). Values above 128k are unlikely to result in much benefit, as half of the memory will be typically used for IO, and TCP buffers are unlikely to support window sizes above 64k on most systems.
* _.per_IP_connection_limit(**int** connection_limit):_ Limit on the number of (concurrent) connections made to the server from the same IP address. Can be used to prevent one IP from taking over all of the allowed connections. If the same IP tries to establish more than the specified number of connections, they will be immediately rejected. The default is `0`, which means no limit on the number of connections from the same IP address.
* _.bind_socket(**int** socket_fd):_ Listen socket to use. Pass a listen socket for the daemon to use (systemd-style). If this option is used, the daemon will not open its own listen socket(s). The argument passed must be of type "int" and refer to an existing socket that has been bound to a port and is listening.
* _.max_thread_stack_size(**int** stack_size):_ Maximum stack size for threads created by the library. Not specifying this option or using a value of zero means using the system default (which is likely to differ based on your platform). Default is `0 (system default)`.
* _.use_ipv6() and .no_ipv6():_ Enable or disable the IPv6 protocol support (by default, libhttpserver will just support IPv4). If you specify this and the local platform does not support it, starting up the server will throw an exception. `off` by default.
* _.use_dual_stack() and .no_dual_stack():_ Enable or disable the support for both IPv6 and IPv4 protocols at the same time (by default, libhttpserver will just support IPv4). If you specify this and the local platform does not support it, starting up the server will throw an exception. Note that this will mean that IPv4 addresses are returned in the IPv6-mapped format (the ’structsockaddrin6’ format will be used for IPv4 and IPv6). `off` by default.
* _.pedantic() and .no_pedantic():_ Enables pedantic checks about the protocol (as opposed to as tolerant as possible). Specifically, at the moment, this flag causes the library to reject HTTP 1.1 connections without a `Host` header. This is required by the standard, but of course in violation of the “be as liberal as possible in what you accept” norm. It is recommended to turn this **off** if you are testing clients against the library, and **on** in production. `off` by default.
* _.debug() and .no_debug():_ Enables debug messages from the library. `off` by default.
* _.regex_checking() and .no_regex_checking():_ Enables pattern matching for endpoints. Read more [here](#registering-resources). `on` by default.
* _.post_process() and .no_post_process():_ Enables/Disables the library to automatically parse the body of the http request as arguments if in querystring format. Read more [here](#parsing-requests). `on` by default.
* _.put_processed_data_to_content() and .no_put_processed_data_to_content():_ Enables/Disables the library to copy parsed body data to the content or to only store it in the arguments map. `on` by default.
* _.file_upload_target(**file_upload_target_T** file_upload_target):_ Controls, how the library stores uploaded files. Default value is `FILE_UPLOAD_MEMORY_ONLY`.
	* `FILE_UPLOAD_MEMORY_ONLY`: The content of the file is only stored in memory. Depending on `put_processed_data_to_content` only as part of the arguments map or additionally in the content.
	* `FILE_UPLOAD_DISK_ONLY`: The content of the file is stored only in the file system. The path is created from `file_upload_dir` and either a random name (if `generate_random_filename_on_upload` is true) or the actually uploaded file name.
	* `FILE_UPLOAD_MEMORY_AND_DISK`: The content of the file is stored in memory and on the file system.
* _.file_upload_dir(**const std::string&** file_upload_dir):_ Specifies the directory to store all uploaded files. Default value is `/tmp`.
* _.generate_random_filename_on_upload() and .no_generate_random_filename_on_upload():_ Enables/Disables the library to generate a unique and unused filename to store the uploaded file to. Otherwise the actually uploaded file name is used. `off` by default.
* _.deferred()_ and _.no_deferred():_ Enables/Disables the ability for the server to suspend and resume connections. Simply put, it enables/disables the ability to use `deferred_response`. Read more [here](#building-responses-to-requests). `on` by default.
* _.single_resource() and .no_single_resource:_ Sets or unsets the server in single resource mode. This limits all endpoints to be served from a single resource. The resultant is that the webserver will process the request matching to the endpoint skipping any complex semantic. Because of this, the option is incompatible with `regex_checking` and requires the resource to be registered against an empty endpoint or the root endpoint (`"/"`). The resource will also have to be registered as family. (For more information on resource registration, read more [here](#registering-resources)). `off` by default.

### Threading Models
* _.start_method(**const http::http_utils::start_method_T&** start_method):_ libhttpserver can operate with two different threading models that can be selected through this method. Default value is `INTERNAL_SELECT`.
	* `http::http_utils::INTERNAL_SELECT`: In this mode, libhttpserver uses only a single thread to handle listening on the port and processing of requests. This mode is preferable if spawning a thread for each connection would be costly. If the HTTP server is able to quickly produce responses without much computational overhead for each connection, this mode can be a great choice. Note that libhttpserver will still start a single thread for itself -- this way, the main program can continue with its operations after calling the start method. Naturally, if the HTTP server needs to interact with shared state in the main application, synchronization will be required. If such synchronization in code providing a response results in blocking, all HTTP server operations on all connections will stall. This mode is a bad choice if response data cannot always be provided instantly. The reason is that the code generating responses should not block (since that would block all other connections) and on the other hand, if response data is not available immediately, libhttpserver will start to busy wait on it. If you need to scale along the number of concurrent connection and scale on multiple thread you can specify a value for `max_threads` (see below) thus enabling a thread pool - this is different from `THREAD_PER_CONNECTION` below where a new thread is spawned for each connection. 
	* `http::http_utils::THREAD_PER_CONNECTION`: In this mode, libhttpserver starts one thread to listen on the port for new connections and then spawns a new thread to handle each connection. This mode is great if the HTTP server has hardly any state that is shared between connections (no synchronization issues!) and may need to perform blocking operations (such as extensive IO or running of code) to handle an individual connection.
* _.max_threads(**int** max_threads):_ A thread pool can be combined with the `INTERNAL_SELECT` mode to benefit implementations that require scalability. As said before, by default this mode only uses a single thread. When combined with the thread pool option, it is possible to handle multiple connections with multiple threads. Any value greater than one for this option will activate the use of the thread pool. In contrast to the `THREAD_PER_CONNECTION` mode (where each thread handles one and only one connection), threads in the pool can handle a large number of concurrent connections. Using `INTERNAL_SELECT` in combination with a thread pool is typically the most scalable (but also hardest to debug) mode of operation for libhttpserver. Default value is `1`. This option is incompatible with `THREAD_PER_CONNECTION`.

### Custom defaulted error messages
libhttpserver allows to override internal error retrieving functions to provide custom messages to the HTTP client. There are only 3 cases in which implementing logic (an http_resource) cannot be invoked: (1) a not found resource, where the library is not being able to match the URL requested by the client to any implementing http_resource object; (2) a not allowed method, when the HTTP client is requesting a method explicitly marked as not allowed (more info [here](#allowing-and-disallowing-methods-on-a-resource)) by the implementation; (3) an exception being thrown.
In all these 3 cases libhttpserver would provide a standard HTTP response to the client with the correct error code; respectively a `404`, a `405` and a `500`. The library allows its user to specify custom callbacks that will be called to replace the default behavior.
* _.not_found_resource(**const  shared_ptr<http_response>(&ast;render_ptr)(const http_request&)** resource):_ Specifies a function to handle a request when no matching registered endpoint exist for the URL requested by the client.
* _.method_not_allowed_resource(**const  shared_ptr<http_response>(&ast;render_ptr)(const http_request&)** resource):_ Specifies a function to handle a request that is asking for a method marked as not allowed on the matching http_resource.
* _.internal_error_resource(**const  shared_ptr<http_response>(&ast;render_ptr)(const http_request&)** resource):_ Specifies a function to handle a request that is causing an uncaught exception during its execution. **REMEMBER:** is this callback is causing an exception itself, the standard default response from libhttpserver will be reported to the HTTP client.

#### Example of custom errors:
```cpp
      #include <httpserver.hpp>

      using namespace httpserver;

      std::shared_ptr<http_response> not_found_custom(const http_request& req) {
          return std::shared_ptr<string_response>(new string_response("Not found custom", 404, "text/plain"));
      }

      std::shared_ptr<http_response> not_allowed_custom(const http_request& req) {
          return std::shared_ptr<string_response>(new string_response("Not allowed custom", 405, "text/plain"));
      }

      class hello_world_resource : public http_resource {
      public:
          std::shared_ptr<http_response> render(const http_request&) {
              return std::shared_ptr<http_response>(new string_response("Hello, World!"));
          }
      };

      int main(int argc, char** argv) {
          webserver ws = create_webserver(8080)
              .not_found_resource(not_found_custom)
              .method_not_allowed_resource(not_allowed_custom);

          hello_world_resource hwr;
          hwr.disallow_all();
          hwr.set_allowing("GET", true);
          ws.register_resource("/hello", &hwr);
          ws.start(true);

          return 0;
      }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v http://localhost:8080/hello

If you try to run either of the two following commands, you'll see your custom errors:
* `curl -XGET -v http://localhost:8080/morning`: will return your custom `not found` error.
* `curl -XPOST -v http://localhost:8080/hello`: will return your custom `not allowed` error.

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/custom_error.cpp).

### Custom logging callbacks
* _.log_access(**void(&ast;log_access_ptr)(const std::string&)** functor):_ Specifies a function used to log accesses (requests) to the server.
* _.log_error(**void(&ast;log_error_ptr)(const std::string&)** functor):_ Specifies a function used to log errors generating from the server.

#### Example of custom logging callback
```cpp
    #include <httpserver.hpp>
    #include <iostream>

    using namespace httpserver;

    void custom_access_log(const std::string& url) {
        std::cout << "ACCESSING: " << url << std::endl;
    }

    class hello_world_resource : public http_resource {
    public:
        std::shared_ptr<http_response> render(const http_request&) {
            return std::shared_ptr<http_response>(new string_response("Hello, World!"));
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080)
            .log_access(custom_access_log);

        hello_world_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);

        return 0;
    }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v http://localhost:8080/hello

You'll notice how, on the terminal runing your server, the logs will now be printed in output for each request received.

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/custom_access_log.cpp).

### TLS/HTTPS
* _.use_ssl() and .no_ssl():_ Determines whether to run in HTTPS-mode or not. If you set this as on and libhttpserver was compiled without SSL support, the library will throw an exception at start of the server. `off` by default.
* _.cred_type(**const http::http_utils::cred_type_T&** cred_type):_ Daemon credentials type. Either certificate or anonymous. Acceptable values are:
	* `NONE`: No credentials.
	* `CERTIFICATE`: Certificate credential.
	* `ANON`: Anonymous credential.
	* `SRP`: SRP credential.
	* `PSK`: PSK credential.
	* `IA`: IA credential.
* _.https_mem_key(**const std::string&** filename):_ String representing the path to a file containing the private key to be used by the HTTPS daemon. This must be used in conjunction with `https_mem_cert`.
* _.https_mem_cert(**const std::string&** filename):_ String representing the path to a file containing the certificate to be used by the HTTPS daemon. This must be used in conjunction with `https_mem_key`.
* _.https_mem_trust(**const std::string&** filename):_ String representing the path to a file containing the CA certificate to be used by the HTTPS daemon to authenticate and trust clients certificates. The presence of this option activates the request of certificate to the client. The request to the client is marked optional, and it is the responsibility of the server to check the presence of the certificate if needed. Note that most browsers will only present a client certificate only if they have one matching the specified CA, not sending any certificate otherwise.
* _.https_priorities(**const std::string&** priority_string):_ SSL/TLS protocol version and ciphers. Must be followed by a string specifying the SSL/TLS protocol versions and ciphers that are acceptable for the application. The string is passed unchanged to gnutls_priority_init. If this option is not specified, `"NORMAL"` is used.

#### Minimal example using HTTPS
```cpp
    #include <httpserver.hpp>

    using namespace httpserver;

    class hello_world_resource : public http_resource {
    public:
        std::shared_ptr<http_response> render(const http_request&) {
            return std::shared_ptr<http_response>(new string_response("Hello, World!"));
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080)
            .use_ssl()
            .https_mem_key("key.pem")
            .https_mem_cert("cert.pem");

        hello_world_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);

        return 0;
    }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v -k 'https://localhost:8080/hello'

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/minimal_https.cpp).

### IP Blacklisting/Whitelisting
libhttpserver supports IP blacklisting and whitelisting as an internal feature. This section explains the startup options related with IP blacklisting/whitelisting. See the [specific section](#ip-blacklisting-and-whitelisting) to read more about the topic.
* _.ban_system() and .no_ban_system:_ Can be used to enable/disable the ban system. `on` by default.
* _.default_policy(**const http::http_utils::policy_T&** default_policy):_ Specifies what should be the default behavior when receiving a request. Possible values are `ACCEPT` and `REJECT`. Default is `ACCEPT`.

### Authentication Parameters
* _.basic_auth() and .no_basic_auth:_ Can be used to enable/disable parsing of the basic authorization header sent by the client. `on` by default.
* _.digest_auth() and .no_digest_auth:_ Can be used to enable/disable parsing of the digested authentication data sent by the client. `on` by default.
* _.nonce_nc_size(**int** nonce_size):_ Size of an array of nonce and nonce counter map. This option represents the size (number of elements) of a map of a nonce and a nonce-counter. If this option is not specified, a default value of 4 will be used (which might be too small for servers handling many requests).
You should calculate the value of NC_SIZE based on the number of connections per second multiplied by your expected session duration plus a factor of about two for hash table collisions. For example, if you expect 100 digest-authenticated connections per second and the average user to stay on your site for 5 minutes, then you likely need a value of about 60000. On the other hand, if you can only expect only 10 digest-authenticated connections per second, tolerate browsers getting a fresh nonce for each request and expect a HTTP request latency of 250 ms, then a value of about 5 should be fine.
* _.digest_auth_random(**const std::string&** nonce_seed):_ Digest Authentication nonce’s seed. For security, you SHOULD provide a fresh random nonce when actually using Digest Authentication with libhttpserver in production.

### Examples of chaining syntax to create a webserver
```cpp
    webserver ws = create_webserver(8080)
        .no_ssl()
        .no_ipv6()
        .no_debug()
        .no_pedantic()
        .no_basic_auth()
        .no_digest_auth()
        .no_comet()
        .no_regex_checking()
        .no_ban_system()
        .no_post_process();
```
##
```cpp
    webserver ws = create_webserver(8080)
        .use_ssl()
        .https_mem_key("key.pem")
        .https_mem_cert("cert.pem");
```
### Starting and stopping a webserver
Once a webserver is created, you can manage its execution through the following methods on the `webserver` class:
* _**void** webserver::start(**bool** blocking):_ Allows to start a server. If the `blocking` flag is passed as `true`, it will block the execution of the current thread until a call to stop on the same webserver object is performed. 
* _**void** webserver::stop():_ Allows to stop a server. It immediately stops it.
* _**bool** webserver::is_running():_ Checks if a server is running
* _**void** webserver::sweet_kill():_ Allows to stop a server. It doesn't guarantee an immediate halt to allow for thread termination and connection closure.

[Back to TOC](#table-of-contents)

## The Resource Object
The `http_resource` class represents a logical collection of HTTP methods that will be associated to a URL when registered on the webserver. The class is **designed for extension** and it is where most of your code should ideally live. When the webserver matches a request against a resource (see: [resource registration](#registering-resources)), the method correspondent to the one in the request (GET, POST, etc..) (see below) is called on the resource.

Given this, the `http_resource` class contains the following extensible methods (also called `handlers` or `render methods`):
* _**std::shared_ptr<http_response>** http_resource::render_GET(**const http_request&** req):_ Invoked on an HTTP GET request.
* _**std::shared_ptr<http_response>** http_resource::render_POST(**const http_request&** req):_ Invoked on an HTTP POST request.
* _**std::shared_ptr<http_response>** http_resource::render_PUT(**const http_request&** req):_ Invoked on an HTTP PUT request.
* _**std::shared_ptr<http_response>** http_resource::render_HEAD(**const http_request&** req):_ Invoked on an HTTP HEAD request.
* _**std::shared_ptr<http_response>** http_resource::render_DELETE(**const http_request&** req):_ Invoked on an HTTP DELETE request.
* _**std::shared_ptr<http_response>** http_resource::render_TRACE(**const http_request&** req):_ Invoked on an HTTP TRACE request.
* _**std::shared_ptr<http_response>** http_resource::render_OPTIONS(**const http_request&** req):_ Invoked on an HTTP OPTIONS request.
* _**std::shared_ptr<http_response>** http_resource::render_CONNECT(**const http_request&** req):_ Invoked on an HTTP CONNECT request.
* _**std::shared_ptr<http_response>** http_resource::render(**const http_request&** req):_ Invoked as a backup method if the matching method is not implemented. It can be used whenever you want all the invocations on a URL to activate the same behavior regardless of the HTTP method requested. The default implementation of the `render` method returns an empty response with a `404`.

#### Example of implementation of render methods
```cpp
    #include <httpserver.hpp>

    using namespace httpserver;

    class hello_world_resource : public http_resource {
    public:
        std::shared_ptr<http_response> render_GET(const http_request&) {
            return std::shared_ptr<http_response>(new string_response("GET: Hello, World!"));
        }

        std::shared_ptr<http_response> render(const http_request&) {
            return std::shared_ptr<http_response>(new string_response("OTHER: Hello, World!"));
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080);

        hello_world_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);

        return 0;
    }
```
To test the above example, you can run the following commands from a terminal:
 * `curl -XGET -v http://localhost:8080/hello`: will return `GET: Hello, World!`.
 * `curl -XPOST -v http://localhost:8080/hello`: will return `OTHER: Hello, World!`. You can try requesting other methods beside `POST` to verify how the same message will be returned.

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/handlers.cpp).

### Allowing and disallowing methods on a resource
By default, all methods an a resource are allowed, meaning that an HTTP request with that method will be invoked. It is possible to mark methods as `not allowed` on a resource. When a method not allowed is requested on a resource, the default `method_not_allowed` method is invoked - the default can be overriden as explain in the section [Custom defaulted error messages](custom-defaulted-error-messages).
The base `http_resource` class has a set of methods that can be used to allow and disallow HTTP methods.
* _**void**  http_resource::set_allowing(**const std::string&** method, **bool** allowed):_ Used to allow or disallow a method. The `method` parameter is a string representing an HTTP method (GET, POST, PUT, etc...).
* _**void**  http_resource::allow_all():_ Marks all HTTP methods as allowed.
* _**void**  http_resource::disallow_all():_ Marks all HTTP methods as not allowed.

#### Example of methods allowed/disallowed
```cpp
      #include <httpserver.hpp>

      using namespace httpserver;

      class hello_world_resource : public http_resource {
      public:
          std::shared_ptr<http_response> render(const http_request&) {
              return std::shared_ptr<http_response>(new string_response("Hello, World!"));
          }
      };

      int main(int argc, char** argv) {
          webserver ws = create_webserver(8080);

          hello_world_resource hwr;
          hwr.disallow_all();
          hwr.set_allowing("GET", true);
          ws.register_resource("/hello", &hwr);
          ws.start(true);

          return 0;
      }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v http://localhost:8080/hello

If you try to run the following command, you'll see a `method_not_allowed` error:
* `curl -XPOST -v http://localhost:8080/hello`.

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/allowing_disallowing_methods.cpp).

[Back to TOC](#table-of-contents)

## Registering resources
Once you have created your resource and extended its methods, you'll have to register the resource on the webserver. Registering a resource will associate it with an endpoint and allows the webserver to route it.
The `webserver` class offers a method to register a resource:
* _**bool** register_resource(**const std::string&** endpoint, **http_resource&ast;** resource, **bool** family = `false`):_ Registers the `resource` to an `endpoint`. The endpoint is a string representing the path on your webserver from where you want your resource to be served from (e.g. `"/path/to/resource"`). The optional `family` parameter allows to register a resource as a "family" resource that will match any path nested into the one specified. For example, if family is set to `true` and endpoint is set to `"/path"`, the webserver will route to the resource not only the requests against  `"/path"` but also everything in its nested path `"/path/on/the/previous/one"`.

### Specifying endpoints
There are essentially four ways to specify an endpoint string:
* **A simple path (e.g. `"/path/to/resource"`).** In this case, the webserver will try to match exactly the value of the endpoint.
* **A regular exception.** In this case, the webserver will try to match the URL of the request with the regex passed. For example, if passing `"/path/as/decimal/[0-9]+`, requests on URLs like `"/path/as/decimal/5"` or `"/path/as/decimal/42"` will be matched; instead, URLs like `"/path/as/decimal/three"` will not.
* **A parametrized path. (e.g. `"/path/to/resource/with/{arg1}/{arg2}/in/url"`)**. In this case, the webserver will match the argument with any value passed. In addition to this, the arguments will be passed to the resource as part of the arguments (readable from the `http_request::get_arg` method - see [here](#parsing-requests)). For example, if passing `"/path/to/resource/with/{arg1}/{arg2}/in/url"` will match any request on URL with any value in place of `{arg1}` and `{arg2}`. 
* **A parametrized path with custom parameters.** This is the same of a normal parametrized path, but allows to specify a regular expression for the argument (e.g. `"/path/to/resource/with/{arg1|[0-9]+}/{arg2|[a-z]+}/in/url"`. In this case, the webserver will match the arguments with any value passed that satisfies the regex. In addition to this, as above, the arguments will be passed to the resource as part of the arguments (readable from the `http_request::get_arg` method - see [here](#parsing-requests)). For example, if passing `"/path/to/resource/with/{arg1|[0-9]+}/{arg2|[a-z]+}/in/url"` will match requests on URLs like `"/path/to/resource/with/10/AA/in/url"` but not like `""/path/to/resource/with/BB/10/in/url""`
* Any of the above marked as `family`. Will match any request on URLs having path that is prefixed by the path passed. For example, if family is set to `true` and endpoint is set to `"/path"`, the webserver will route to the resource not only the requests against  `"/path"` but also everything in its nested path `"/path/on/the/previous/one"`.
```cpp
      #include <httpserver.hpp>

      using namespace httpserver;

      class hello_world_resource : public http_resource {
      public:
          std::shared_ptr<http_response> render(const http_request&) {
              return std::shared_ptr<http_response>(new string_response("Hello, World!"));
          }
      };

      class handling_multiple_resource : public http_resource {
      public:
          std::shared_ptr<http_response> render(const http_request& req) {
              return std::shared_ptr<http_response>(new string_response("Your URL: " + req.get_path()));
          }
      };

      class url_args_resource : public http_resource {
      public:
          std::shared_ptr<http_response> render(const http_request& req) {
              std::string arg1(req.get_arg("arg1"));
              std::string arg2(req.get_arg("arg2"));
              return std::shared_ptr<http_response>(new string_response("ARGS: " + arg1 + " and " + arg2));
          }
      };

      int main(int argc, char** argv) {
          webserver ws = create_webserver(8080);

          hello_world_resource hwr;
          ws.register_resource("/hello", &hwr);

          handling_multiple_resource hmr;
          ws.register_resource("/family", &hmr, true);
          ws.register_resource("/with_regex_[0-9]+", &hmr);

          url_args_resource uar;
          ws.register_resource("/url/with/{arg1}/and/{arg2}", &uar);
          ws.register_resource("/url/with/parametric/args/{arg1|[0-9]+}/and/{arg2|[A-Z]+}", &uar);

          ws.start(true);

          return 0;
      }
```
To test the above example, you can run the following commands from a terminal:
    
* `curl -XGET -v http://localhost:8080/hello`: will return the `Hello, World!` message.
* `curl -XGET -v http://localhost:8080/family`: will return the `Your URL: /family` message.
* `curl -XGET -v http://localhost:8080/family/with/suffix`: will return the `Your URL: /family/with/suffix` message.
* `curl -XGET -v http://localhost:8080/with_regex_10`: will return the `Your URL: /with_regex_10` message.
* `curl -XGET -v http://localhost:8080/url/with/AA/and/BB`: will return the `ARGS: AA and BB` message. You can change `AA` and `BB` with any value and observe how the URL is still matched and parameters are read.
* `curl -XGET -v http://localhost:8080/url/with/parametric/args/10/and/AA`: will return the `ARGS: 10 and AA` message. You can change `10` and `AA` with any value matching the regexes and observe how the URL is still matched and parameters are read.

Conversely, you can observe how these URL will not be matched (al the following will give you a `not found` message):
* `curl -XGET -v http://localhost:8080/with_regex_A`
* `curl -XGET -v http://localhost:8080/url/with/parametric/args/AA/and/BB`

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/url_registration.cpp).

[Back to TOC](#table-of-contents)

## Parsing requests
As seen in the documentation of [http_resource](#the-resource-object), every extensible method takes in input a `http_request` object. The webserver takes the responsibility to extract the data from the HTTP request on the network and does all the heavy lifting to build the instance of `http_request`.

The `http_request` class has a set of methods you will have access to when implementing your handlers:
* _**const std::string&** get_path() **const**:_ Returns the path as requested from the HTTP client.
* _**const std::vector\<std::string\>&** get_path_pieces() **const**:_ Returns the components of the path requested by the HTTP client (each piece of the path split by `'/'`.
* _**const std::string&** get_path_piece(int index) **const**:_ Returns one piece of the path requested by the HTTP client. The piece is selected through the `index` parameter (0-indexed). 
* _**const std::string&** get_method() **const**:_ Returns the method requested by the HTTP client.
* _**std::string_view** get_header(**std::string_view** key) **const**:_ Returns the header with name equal to `key` if present in the HTTP request. Returns an `empty string` otherwise.
* _**std::string_view** get_cookie(**std::string_view** key) **const**:_ Returns the cookie with name equal to `key` if present in the HTTP request. Returns an `empty string` otherwise.
* _**std::string_view** get_footer(**std::string_view** key) **const**:_ Returns the footer with name equal to `key` if present in the HTTP request (only for http 1.1 chunked encodings). Returns an `empty string` otherwise.
* _**std::string_view** get_arg(**std::string_view** key) **const**:_ Returns the argument with name equal to `key` if present in the HTTP request. Arguments can be (1) querystring parameters, (2) path argument (in case of parametric endpoint, (3) parameters parsed from the HTTP request body if the body is in `application/x-www-form-urlencoded` or `multipart/form-data` formats and the postprocessor is enabled in the webserver (enabled by default).
* _**const std::map<std::string_view, std::string_view, http::header_comparator>** get_headers() **const**:_ Returns a map containing all the headers present in the HTTP request.
* _**const std::map<std::string_view, std::string_view, http::header_comparator>** get_cookies() **const**:_ Returns a map containing all the cookies present in the HTTP request.
* _**const std::map<std::string_view, std::string_view, http::header_comparator>** get_footers() **const**:_ Returns a map containing all the footers present in the HTTP request (only for http 1.1 chunked encodings).
* _**const std::map<std::string, std::string, http::arg_comparator>** get_args() **const**:_ Returns all the arguments present in the HTTP request. Arguments can be (1) querystring parameters, (2) path argument (in case of parametric endpoint, (3) parameters parsed from the HTTP request body if the body is in `application/x-www-form-urlencoded` or `multipart/form-data` formats and the postprocessor is enabled in the webserver (enabled by default).
* _**const std::map<std::string, std::map<std::string, http::file_info>>** get_files() **const**:_ Returns information about all the uploaded files (if the files are stored to disk). This information includes the key (as identifier of the outer map), the original file name (as identifier of the inner map) and a class `file_info`, which includes the size of the file and the path to the file in the file system.
* _**const std::string&** get_content() **const**:_ Returns the body of the HTTP request.
* _**bool**  content_too_large() **const**:_ Returns `true` if the body length of the HTTP request sent by the client is longer than the max allowed on the server.
* _**const std::string** get_querystring() **const**:_ Returns the `querystring` of the HTTP request.
* _**const std::string&** get_version() **const**:_ Returns the HTTP version of the client request.
* _**const std::string** get_requestor() **const**:_ Returns the IP from which the client is sending the request.
* _**unsigned  short**  get_requestor_port() **const**:_ Returns the port from which the client is sending the request.
* _**const std::string** get_user() **const**:_ Returns the `user` as self-identified through basic authentication. The content of the user header will be parsed only if basic authentication is enabled on the server (enabled by default).
* _**const std::string** get_pass() **const**:_ Returns the `password` as self-identified through basic authentication. The content of the password header will be parsed only if basic authentication is enabled on the server (enabled by default).
* _**const std::string** get_digested_user() **const**:_ Returns the `digested user` as self-identified through digest authentication. The content of the user header will be parsed only if digest authentication is enabled on the server (enabled by default).
* _**bool** check_digest_auth(**const std::string&** realm, **const std::string&** password, **int** nonce_timeout, **bool*** reload_nonce) **const**:_ Allows to check the validity of the authentication token sent through digest authentication (if the provided values in the WWW-Authenticate header are valid and sound according to RFC2716). Takes in input the `realm` of validity of the authentication, the `password` as known to the server to compare against, the `nonce_timeout` to indicate how long the nonce is valid and `reload_nonce` a boolean that will be set by the method to indicate a nonce being reloaded. The method returns `true` if the authentication is valid, `false` otherwise.
* _**gnutls_session_t** get_tls_session() **const**:_ Tests if there is am underlying TLS state of the current request.
* _**gnutls_session_t** get_tls_session() **const**:_ Returns the underlying TLS state of the current request for inspection. (It is an error to call this if the state does not exist.)

Details on the `http::file_info` structure.

* _**size_t** get_file_size() **const**:_ Returns the size of the file uploaded through the HTTP request.
* _**const std::string** get_file_system_file_name() **const**:_ Returns the name of the file uploaded through the HTTP request as stored on the filesystem.
* _**const std::string** get_content_type() **const**:_ Returns the content type of the file uploaded through the HTTP request.
* _**const std::string** get_transfer_encoding() **const**:_ Returns the transfer encoding of the file uploaded through the HTTP request.

#### Example of handler reading arguments from a request
```cpp
    #include <httpserver.hpp>

    using namespace httpserver;

    class hello_world_resource : public http_resource {
    public:
        std::shared_ptr<http_response> render(const http_request& req) {
            return std::shared_ptr<http_response>(new string_response("Hello: " + std::string(req.get_arg("name"))));
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080);

        hello_world_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);

        return 0;
    }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v "http://localhost:8080/hello?name=John"

You will receive the message `Hello: John` in reply. Given that the body post processing is enabled, you can also run `curl -d "name=John" -X POST http://localhost:8080/hello` to obtain the same result.

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/hello_with_get_arg.cpp).

[Back to TOC](#table-of-contents)

## Building responses to requests
As seen in the documentation of [http_resource](#the-resource-object), every extensible method returns in output a `http_response` object. The webserver takes the responsibility to convert the `http_response` object you create into a response on the network.

There are 5 types of response that you can create - we will describe them here through their constructors:
* _string_response(**const std::string&** content, **int** response_code = `200`, **const std::string&** content_type = `"text/plain"`):_ The most basic type of response. It uses the `content` string passed in construction as body of the HTTP response. The other two optional parameters are the `response_code` and the `content_type`. You can find constant definition for the various response codes within the [http_utils](https://github.com/etr/libhttpserver/blob/master/src/httpserver/http_utils.hpp) library file.
* _file_response(**const std::string&** filename, **int** response_code = `200`, **const std::string&** content_type = `"text/plain"`):_ Uses the `filename` passed in construction as pointer to a file on disk. The body of the HTTP response will be set using the content of the file. The file must be a regular file and exist on disk. Otherwise libhttpserver will return an error 500 (Internal Server Error). The other two optional parameters are the `response_code` and the `content_type`. You can find constant definition for the various response codes within the [http_utils](https://github.com/etr/libhttpserver/blob/master/src/httpserver/http_utils.hpp) library file.
* _basic_auth_fail_response(**const std::string&** content, **const std::string&** realm = `""`, **int** response_code = `200`, **const std::string&** content_type = `"text/plain"`):_ A response in return to a failure during basic authentication. It allows to specify a `content` string as a message to send back to the client. The `realm` parameter should contain your realm of authentication (if any). The other two optional parameters are the `response_code` and the `content_type`. You can find constant definition for the various response codes within the [http_utils](https://github.com/etr/libhttpserver/blob/master/src/httpserver/http_utils.hpp) library file.
* _digest_auth_fail_response(**const std::string&** content, **const std::string&** realm = `""`, **const std::string&** opaque = `""`, **bool** reload_nonce = `false`, **int** response_code = `200`, **const std::string&** content_type = `"text/plain"`):_ A response in return to a failure during digest authentication. It allows to specify a `content` string as a message to send back to the client. The `realm` parameter should contain your realm of authentication (if any). The `opaque` represents a value that gets passed to the client and expected to be passed again to the server as-is. This value can be a hexadecimal or base64 string. The `reload_nonce` parameter tells the server to reload the nonce (you should use the value returned by the `check_digest_auth` method on the `http_request`. The other two optional parameters are the `response_code` and the `content_type`. You can find constant definition for the various response codes within the [http_utils](https://github.com/etr/libhttpserver/blob/master/src/httpserver/http_utils.hpp) library file.
* _deferred_response(**ssize_t(&ast;cycle_callback_ptr)(shared_ptr&lt;T&gt;, char&ast;, size_t)** cycle_callback, **const std::string&** content = `""`, **int** response_code = `200`, **const std::string&** content_type = `"text/plain"`):_ A response that obtains additional content from a callback executed in a deferred way. It leaves the client in pending state (returning a `100 CONTINUE` message) and suspends the connection. Besides the callback, optionally, you can provide a `content` parameter that sets the initial message sent immediately to the client. The other two optional parameters are the `response_code` and the `content_type`. You can find constant definition for the various response codes within the [http_utils](https://github.com/etr/libhttpserver/blob/master/src/httpserver/http_utils.hpp) library file. To use `deferred_response` you need to have the `deferred` option active on your webserver (enabled by default).
	* The `cycle_callback_ptr` has this shape:
		_**ssize_t** cycle_callback(**shared_ptr&lt;T&gt; closure_data, char&ast;** buf, **size_t** max_size)_.
		You are supposed to implement a function in this shape and provide it to the `deferred_repsonse` method. The webserver will provide a `char*` to the function. It is responsibility of the function to allocate it and fill its content. The method is supposed to respect the `max_size` parameter passed in input. The function must return  a `ssize_t` value representing the actual size you filled the `buf` with. Any value different from `-1` will keep the resume the connection, deliver the content and suspend it again (with a `100 CONTINUE`). If the method returns `-1`, the webserver will complete the communication with the client and close the connection. You can also pass a `shared_ptr` pointing to a data object of your choice (this will be templetized with a class of your choice). The server will guarantee that this object is passed at each invocation of the method allowing the client code to use it as a memory buffer during computation.

### Setting additional properties of the response
The `http_response` class offers an additional set of methods to "decorate" your responses. This set of methods is:
* _**void**  with_header(**const std::string&** key, **const std::string&** value):_ Sets an HTTP header with name set to `key` and value set to `value`.
* _**void**  with_footer(**const std::string&** key, **const std::string&** value):_ Sets an HTTP footer with name set to `key` and value set to `value`.
* _**void**  with_cookie(**const std::string&** key, **const std::string&** value):_ Sets an HTTP cookie with name set to `key` and value set to `value` (only for http 1.1 chunked encodings). 
* _**void**  shoutCAST():_ Mark the response as a `shoutCAST` one.

### Example of response setting headers
```cpp
    #include <httpserver.hpp>

    using namespace httpserver;

    class hello_world_resource : public http_resource {
    public:
        std::shared_ptr<http_response> render(const http_request&) {
            std::shared_ptr<http_response> response = std::shared_ptr<http_response>(new string_response("Hello, World!"));
            response->with_header("MyHeader", "MyValue");
            return response;
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080);

        hello_world_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);

        return 0;
    }
```
To test the above example, you could run the following command from a terminal:
    
    curl -XGET -v "http://localhost:8080/hello"

You will receive the message custom header in reply.

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/setting_headers.cpp).

[Back to TOC](#table-of-contents)

## IP Blacklisting and Whitelisting
libhttpserver provides natively a system to blacklist and whitelist IP addresses. To enable/disable the system, it is possible to use the `ban_system` and `no_ban_system` methods on the `create_webserver` class. In the same way, you can specify what you want to be your "default behavior" (allow by default or disallow by default) by using the `default_policy` method (see [here](#create-and-work-with-a-webserver)).

The system supports both IPV4 and IPV6 and manages them transparently. The only requirement is for ipv6 to be enabled on your server - you'll have to enable this by using the `use_ipv6` method on `create_webserver`.

You can explicitly ban or allow an IP address using the following methods on the `webserver` class:
* _**void** ban_ip(**const std::string&** ip):_ Adds one IP (or a range of IPs) to the list of the banned ones. Takes in input a `string` that contains the IP (or range of IPs) to ban. To use when the `default_policy` is `ACCEPT`.
* _**void** allow_ip(**const std::string&** ip):_ Adds one IP (or a range of IPs) to the list of the allowed ones. Takes in input a `string` that contains the IP (or range of IPs) to allow.  To use when the `default_policy` is `REJECT`.
* _**void** unban_ip(**const std::string&** ip):_ Removes one IP (or a range of IPs) from the list of the banned ones. Takes in input a `string` that contains the IP (or range of IPs) to remove from the list.  To use when the `default_policy` is `ACCEPT`.
* _**void** disallow_ip(**const std::string&** ip):_ Removes one IP (or a range of IPs) from the list of the allowed ones. Takes in input a `string` that contains the IP (or range of IPs) to remove from the list.  To use when the `default_policy` is `REJECT`.

### IP String Format
The IP string format can represent both IPV4 and IPV6. Addresses will be normalized by the webserver to operate in the same sapce. Any valid IPV4 or IPV6 textual representation works.
It is also possible to specify ranges of IPs. To do so, omit the octect you want to express as a range and specify a `'*'` in its place.
Examples of valid IPs include:
* `"192.168.5.5"`: standard IPV4
* `"192.168.*.*"`: range of IPV4 addresses. In the example, everything between `192.168.0.0` and `192.168.255.255`.
* `"2001:db8:8714:3a90::12"`: standard IPV6 - clustered empty ranges are fully supported.
* `"2001:db8:8714:3a90:*:*"`: range of IPV6 addresses.
* `"::ffff:192.0.2.128"`: IPV4 IPs nested into IPV6.
* `"::192.0.2.128"`: IPV4 IPs nested into IPV6 (without `'ffff'` prefix)
* `"::ffff:192.0.*.*"`: ranges of IPV4 IPs nested into IPV6.

#### Example of IP Whitelisting/Blacklisting
```cpp
    #include <httpserver.hpp>

    using namespace httpserver;

    class hello_world_resource : public http_resource {
    public:
        std::shared_ptr<http_response> render(const http_request&) {
            return std::shared_ptr<http_response>(new string_response("Hello, World!"));
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080)
            .default_policy(http::http_utils::REJECT);

        ws.allow_ip("127.0.0.1");

        hello_world_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);

        return 0;
    }
```
To test the above example, you could run the following command from a terminal:
    
    curl -XGET -v "http://localhost:8080/hello"

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/minimal_ip_ban.cpp).

[Back to TOC](#table-of-contents)

## Authentication
libhttpserver support three types of client authentication.

Basic authentication uses a simple authentication method based on BASE64 algorithm. Username and password are exchanged in clear between the client and the server, so this method must only be used for non-sensitive content or when the session is protected with https. When using basic authentication libhttpserver will have access to the clear password, possibly allowing to create a chained authentication toward an external authentication server. You can enable/disable support for Basic authentication through the `basic_auth` and `no_basic_auth` methods of the `create_webserver` class.

Digest authentication uses a one-way authentication method based on MD5 hash algorithm. Only the hash will transit over the network, hence protecting the user password. The nonce will prevent replay attacks. This method is appropriate for general use, especially when https is not used to encrypt the session. You can enable/disable support for Digest authentication through the `digest_auth` and `no_digest_auth` methods of the `create_webserver` class.

Client certificate authentication uses a X.509 certificate from the client. This is the strongest authentication mechanism but it requires the use of HTTPS. Client certificate authentication can be used simultaneously with Basic or Digest Authentication in order to provide a two levels authentication (like for instance separate machine and user authentication). You can enable/disable support for Certificate authentication through the `use_ssl` and `no_ssl` methods of the `create_webserver` class.

### Using Basic Authentication
```cpp
    #include <httpserver.hpp>

    using namespace httpserver;

    class user_pass_resource : public httpserver::http_resource {
    public:
        std::shared_ptr<http_response> render_GET(const http_request& req) {
            if (req.get_user() != "myuser" || req.get_pass() != "mypass") {
                return std::shared_ptr<basic_auth_fail_response>(new basic_auth_fail_response("FAIL", "test@example.com"));
            }
            return std::shared_ptr<string_response>(new string_response(req.get_user() + " " + req.get_pass(), 200, "text/plain"));
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080);

        user_pass_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);

        return 0;
    }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v -u myuser:mypass "http://localhost:8080/hello"

You will receive back the user and password you passed in input. Try to pass the wrong credentials to see the failure.

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/basic_authentication.cpp).

### Using Digest Authentication
```cpp
    #include <httpserver.hpp>

    #define MY_OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

    using namespace httpserver;

    class digest_resource : public httpserver::http_resource {
    public:
        std::shared_ptr<http_response> render_GET(const http_request& req) {
            if (req.get_digested_user() == "") {
                return std::shared_ptr<digest_auth_fail_response>(new digest_auth_fail_response("FAIL", "test@example.com", MY_OPAQUE, true));
            }
            else {
                bool reload_nonce = false;
                if(!req.check_digest_auth("test@example.com", "mypass", 300, reload_nonce)) {
                    return std::shared_ptr<digest_auth_fail_response>(new digest_auth_fail_response("FAIL", "test@example.com", MY_OPAQUE, reload_nonce));
                }
            }
            return std::shared_ptr<string_response>(new string_response("SUCCESS", 200, "text/plain"));
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080);

        digest_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);

        return 0;
    }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v --digest --user myuser:mypass localhost:8080/hello

You will receive a `SUCCESS` in response (observe the response message from the server in detail and you'll see the full interaction). Try to pass the wrong credentials or send a request without `digest` active to see the failure.

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/digest_authentication.cpp).

[Back to TOC](#table-of-contents)

## HTTP Utils
libhttpserver provides a set of constants to help you develop your HTTP server. It would be redundant to list them here; so, please, consult the list directly [here](https://github.com/etr/libhttpserver/blob/master/src/httpserver/http_utils.hpp).

[Back to TOC](#table-of-contents)

## Other Examples

#### Example of returning a response from a file
```cpp
    #include <httpserver.hpp>

    using namespace httpserver;

    class file_response_resource : public http_resource {
    public:
        std::shared_ptr<http_response> render_GET(const http_request& req) {
            return std::shared_ptr<file_response>(new file_response("test_content", 200, "text/plain"));
        }
    };

    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080);

        file_response_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);

        return 0;
    }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v localhost:8080/hello

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/minimal_file_response.cpp).

#### Example of a deferred response through callback
```cpp
    #include <httpserver.hpp>
    
    using namespace httpserver;
    
    static int counter = 0;
    
    ssize_t test_callback (std::shared_ptr<void> closure_data, char* buf, size_t max) {
        if (counter == 2) {
            return -1;
        }
        else {
            memset(buf, 0, max);
            strcat(buf, " test ");
            counter++;
            return std::string(buf).size();
        }
    }
    
    class deferred_resource : public http_resource {
        public:
            std::shared_ptr<http_response> render_GET(const http_request& req) {
                return std::shared_ptr<deferred_response<void> >(new deferred_response<void>(test_callback, nullptr, "cycle callback response"));
            }
    };
    
    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080);
    
        deferred_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);
    
        return 0;
    }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v localhost:8080/hello

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/minimal_deferred.cpp).

#### Example of a deferred response through callback (passing additional data along)
```cpp
    #include <atomic>
    #include <httpserver.hpp>
    
    using namespace httpserver;
    
    std::atomic<int> counter;
    
    ssize_t test_callback (std::shared_ptr<std::atomic<int> > closure_data, char* buf, size_t max) {
        int reqid;
        if (closure_data == nullptr) {
            reqid = -1;
        } else {
            reqid = *closure_data;
        }
    
        // only first 5 connections can be established
        if (reqid >= 5) {
            return -1;
        } else {
            // respond corresponding request IDs to the clients
            std::string str = "";
            str += std::to_string(reqid) + " ";
            memset(buf, 0, max);
            std::copy(str.begin(), str.end(), buf);
    
            // keep sending reqid
            sleep(1);
    
            return (ssize_t)max;
        }
    }
    
    class deferred_resource : public http_resource {
        public:
            std::shared_ptr<http_response> render_GET(const http_request& req) {
                std::shared_ptr<std::atomic<int> > closure_data(new std::atomic<int>(counter++));
                return std::shared_ptr<deferred_response<std::atomic<int> > >(new deferred_response<std::atomic<int> >(test_callback, closure_data, "cycle callback response"));
            }
    };
    
    int main(int argc, char** argv) {
        webserver ws = create_webserver(8080);
    
        deferred_resource hwr;
        ws.register_resource("/hello", &hwr);
        ws.start(true);
    
        return 0;
    }
```
To test the above example, you can run the following command from a terminal:
    
    curl -XGET -v localhost:8080/hello

You can also check this example on [github](https://github.com/etr/libhttpserver/blob/master/examples/deferred_with_accumulator.cpp).

[Back to TOC](#table-of-contents)

## Copying
This manual is for libhttpserver, C++ library for creating an embedded Rest HTTP server (and more).

> Permission is granted to copy, distribute and/or modify this document
> under the terms of the GNU Free Documentation License, Version 1.3
> or any later version published by the Free Software Foundation;
> with no Invariant Sections, no Front-Cover Texts, and no Back-Cover
> Texts.  A copy of the license is included in the section entitled GNU
> Free Documentation License.

[Back to TOC](#table-of-contents)

## GNU Lesser General Public License
Version 2.1, February 1999

Copyright &copy; 1991, 1999 Free Software Foundation, Inc.
51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
Everyone is permitted to copy and distribute verbatim copies
of this license document, but changing it is not allowed.

_This is the first released version of the Lesser GPL.  It also counts
as the successor of the GNU Library Public License, version 2, hence
the version number 2.1._

### Preamble

The licenses for most software are designed to take away your
freedom to share and change it.  By contrast, the GNU General Public
Licenses are intended to guarantee your freedom to share and change
free software--to make sure the software is free for all its users.

This license, the Lesser General Public License, applies to some
specially designated software packages--typically libraries--of the
Free Software Foundation and other authors who decide to use it.  You
can use it too, but we suggest you first think carefully about whether
this license or the ordinary General Public License is the better
strategy to use in any particular case, based on the explanations below.

When we speak of free software, we are referring to freedom of use,
not price.  Our General Public Licenses are designed to make sure that
you have the freedom to distribute copies of free software (and charge
for this service if you wish); that you receive source code or can get
it if you want it; that you can change the software and use pieces of
it in new free programs; and that you are informed that you can do
these things.

To protect your rights, we need to make restrictions that forbid
distributors to deny you these rights or to ask you to surrender these
rights.  These restrictions translate to certain responsibilities for
you if you distribute copies of the library or if you modify it.

For example, if you distribute copies of the library, whether gratis
or for a fee, you must give the recipients all the rights that we gave
you.  You must make sure that they, too, receive or can get the source
code.  If you link other code with the library, you must provide
complete object files to the recipients, so that they can relink them
with the library after making changes to the library and recompiling
it.  And you must show them these terms so they know their rights.

We protect your rights with a two-step method: (1) we copyright the
library, and (2) we offer you this license, which gives you legal
permission to copy, distribute and/or modify the library.

To protect each distributor, we want to make it very clear that
there is no warranty for the free library.  Also, if the library is
modified by someone else and passed on, the recipients should know
that what they have is not the original version, so that the original
author's reputation will not be affected by problems that might be
introduced by others.

Finally, software patents pose a constant threat to the existence of
any free program.  We wish to make sure that a company cannot
effectively restrict the users of a free program by obtaining a
restrictive license from a patent holder.  Therefore, we insist that
any patent license obtained for a version of the library must be
consistent with the full freedom of use specified in this license.

Most GNU software, including some libraries, is covered by the
ordinary GNU General Public License.  This license, the GNU Lesser
General Public License, applies to certain designated libraries, and
is quite different from the ordinary General Public License.  We use
this license for certain libraries in order to permit linking those
libraries into non-free programs.

When a program is linked with a library, whether statically or using
a shared library, the combination of the two is legally speaking a
combined work, a derivative of the original library.  The ordinary
General Public License therefore permits such linking only if the
entire combination fits its criteria of freedom.  The Lesser General
Public License permits more lax criteria for linking other code with
the library.

We call this license the &ldquo;Lesser&rdquo; General Public License because it
does Less to protect the user's freedom than the ordinary General
Public License.  It also provides other free software developers Less
of an advantage over competing non-free programs.  These disadvantages
are the reason we use the ordinary General Public License for many
libraries.  However, the Lesser license provides advantages in certain
special circumstances.

For example, on rare occasions, there may be a special need to
encourage the widest possible use of a certain library, so that it becomes
a de-facto standard.  To achieve this, non-free programs must be
allowed to use the library.  A more frequent case is that a free
library does the same job as widely used non-free libraries.  In this
case, there is little to gain by limiting the free library to free
software only, so we use the Lesser General Public License.

In other cases, permission to use a particular library in non-free
programs enables a greater number of people to use a large body of
free software.  For example, permission to use the GNU C Library in
non-free programs enables many more people to use the whole GNU
operating system, as well as its variant, the GNU/Linux operating
system.

Although the Lesser General Public License is Less protective of the
users' freedom, it does ensure that the user of a program that is
linked with the Library has the freedom and the wherewithal to run
that program using a modified version of the Library.

The precise terms and conditions for copying, distribution and
modification follow.  Pay close attention to the difference between a
&ldquo;work based on the library&rdquo; and a &ldquo;work that uses the library&rdquo;.  The
former contains code derived from the library, whereas the latter must
be combined with the library in order to run.

### TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

**0.** This License Agreement applies to any software library or other
program which contains a notice placed by the copyright holder or
other authorized party saying it may be distributed under the terms of
this Lesser General Public License (also called &ldquo;this License&rdquo;).
Each licensee is addressed as &ldquo;you&rdquo;.

A &ldquo;library&rdquo; means a collection of software functions and/or data
prepared so as to be conveniently linked with application programs
(which use some of those functions and data) to form executables.

The &ldquo;Library&rdquo;, below, refers to any such software library or work
which has been distributed under these terms.  A &ldquo;work based on the
Library&rdquo; means either the Library or any derivative work under
copyright law: that is to say, a work containing the Library or a
portion of it, either verbatim or with modifications and/or translated
straightforwardly into another language.  (Hereinafter, translation is
included without limitation in the term &ldquo;modification&rdquo;.)

&ldquo;Source code&rdquo; for a work means the preferred form of the work for
making modifications to it.  For a library, complete source code means
all the source code for all modules it contains, plus any associated
interface definition files, plus the scripts used to control compilation
and installation of the library.

Activities other than copying, distribution and modification are not
covered by this License; they are outside its scope.  The act of
running a program using the Library is not restricted, and output from
such a program is covered only if its contents constitute a work based
on the Library (independent of the use of the Library in a tool for
writing it).  Whether that is true depends on what the Library does
and what the program that uses the Library does.

**1.** You may copy and distribute verbatim copies of the Library's
complete source code as you receive it, in any medium, provided that
you conspicuously and appropriately publish on each copy an
appropriate copyright notice and disclaimer of warranty; keep intact
all the notices that refer to this License and to the absence of any
warranty; and distribute a copy of this License along with the
Library.

You may charge a fee for the physical act of transferring a copy,
and you may at your option offer warranty protection in exchange for a
fee.

**2.** You may modify your copy or copies of the Library or any portion
of it, thus forming a work based on the Library, and copy and
distribute such modifications or work under the terms of Section 1
above, provided that you also meet all of these conditions:

* **a)** The modified work must itself be a software library.
* **b)** You must cause the files modified to carry prominent notices
stating that you changed the files and the date of any change.
* **c)** You must cause the whole of the work to be licensed at no
charge to all third parties under the terms of this License.
* **d)** If a facility in the modified Library refers to a function or a
table of data to be supplied by an application program that uses
the facility, other than as an argument passed when the facility
is invoked, then you must make a good faith effort to ensure that,
in the event an application does not supply such function or
table, the facility still operates, and performs whatever part of
its purpose remains meaningful.  
(For example, a function in a library to compute square roots has
a purpose that is entirely well-defined independent of the
application.  Therefore, Subsection 2d requires that any
application-supplied function or table used by this function must
be optional: if the application does not supply it, the square
root function must still compute square roots.)

These requirements apply to the modified work as a whole.  If
identifiable sections of that work are not derived from the Library,
and can be reasonably considered independent and separate works in
themselves, then this License, and its terms, do not apply to those
sections when you distribute them as separate works.  But when you
distribute the same sections as part of a whole which is a work based
on the Library, the distribution of the whole must be on the terms of
this License, whose permissions for other licensees extend to the
entire whole, and thus to each and every part regardless of who wrote
it.

Thus, it is not the intent of this section to claim rights or contest
your rights to work written entirely by you; rather, the intent is to
exercise the right to control the distribution of derivative or
collective works based on the Library.

In addition, mere aggregation of another work not based on the Library
with the Library (or with a work based on the Library) on a volume of
a storage or distribution medium does not bring the other work under
the scope of this License.

**3.** You may opt to apply the terms of the ordinary GNU General Public
License instead of this License to a given copy of the Library.  To do
this, you must alter all the notices that refer to this License, so
that they refer to the ordinary GNU General Public License, version 2,
instead of to this License.  (If a newer version than version 2 of the
ordinary GNU General Public License has appeared, then you can specify
that version instead if you wish.)  Do not make any other change in
these notices.

Once this change is made in a given copy, it is irreversible for
that copy, so the ordinary GNU General Public License applies to all
subsequent copies and derivative works made from that copy.

This option is useful when you wish to copy part of the code of
the Library into a program that is not a library.

**4.** You may copy and distribute the Library (or a portion or
derivative of it, under Section 2) in object code or executable form
under the terms of Sections 1 and 2 above provided that you accompany
it with the complete corresponding machine-readable source code, which
must be distributed under the terms of Sections 1 and 2 above on a
medium customarily used for software interchange.

If distribution of object code is made by offering access to copy
from a designated place, then offering equivalent access to copy the
source code from the same place satisfies the requirement to
distribute the source code, even though third parties are not
compelled to copy the source along with the object code.

**5.** A program that contains no derivative of any portion of the
Library, but is designed to work with the Library by being compiled or
linked with it, is called a &ldquo;work that uses the Library&rdquo;.  Such a
work, in isolation, is not a derivative work of the Library, and
therefore falls outside the scope of this License.

However, linking a &ldquo;work that uses the Library&rdquo; with the Library
creates an executable that is a derivative of the Library (because it
contains portions of the Library), rather than a &ldquo;work that uses the
library&rdquo;.  The executable is therefore covered by this License.
Section 6 states terms for distribution of such executables.

When a &ldquo;work that uses the Library&rdquo; uses material from a header file
that is part of the Library, the object code for the work may be a
derivative work of the Library even though the source code is not.
Whether this is true is especially significant if the work can be
linked without the Library, or if the work is itself a library.  The
threshold for this to be true is not precisely defined by law.

If such an object file uses only numerical parameters, data
structure layouts and accessors, and small macros and small inline
functions (ten lines or less in length), then the use of the object
file is unrestricted, regardless of whether it is legally a derivative
work.  (Executables containing this object code plus portions of the
Library will still fall under Section 6.)

Otherwise, if the work is a derivative of the Library, you may
distribute the object code for the work under the terms of Section 6.
Any executables containing that work also fall under Section 6,
whether or not they are linked directly with the Library itself.

**6.** As an exception to the Sections above, you may also combine or
link a &ldquo;work that uses the Library&rdquo; with the Library to produce a
work containing portions of the Library, and distribute that work
under terms of your choice, provided that the terms permit
modification of the work for the customer's own use and reverse
engineering for debugging such modifications.

You must give prominent notice with each copy of the work that the
Library is used in it and that the Library and its use are covered by
this License.  You must supply a copy of this License.  If the work
during execution displays copyright notices, you must include the
copyright notice for the Library among them, as well as a reference
directing the user to the copy of this License.  Also, you must do one
of these things:

* **a)** Accompany the work with the complete corresponding
machine-readable source code for the Library including whatever
changes were used in the work (which must be distributed under
Sections 1 and 2 above); and, if the work is an executable linked
with the Library, with the complete machine-readable &ldquo;work that
uses the Library&rdquo;, as object code and/or source code, so that the
user can modify the Library and then relink to produce a modified
executable containing the modified Library.  (It is understood
that the user who changes the contents of definitions files in the
Library will not necessarily be able to recompile the application
to use the modified definitions.)
* **b)** Use a suitable shared library mechanism for linking with the
Library.  A suitable mechanism is one that (1) uses at run time a
copy of the library already present on the user's computer system,
rather than copying library functions into the executable, and (2)
will operate properly with a modified version of the library, if
the user installs one, as long as the modified version is
interface-compatible with the version that the work was made with.
* **c)** Accompany the work with a written offer, valid for at
least three years, to give the same user the materials
specified in Subsection 6a, above, for a charge no more
than the cost of performing this distribution.
* **d)** If distribution of the work is made by offering access to copy
from a designated place, offer equivalent access to copy the above
specified materials from the same place.
* **e)** Verify that the user has already received a copy of these
materials or that you have already sent this user a copy.

For an executable, the required form of the &ldquo;work that uses the
Library&rdquo; must include any data and utility programs needed for
reproducing the executable from it.  However, as a special exception,
the materials to be distributed need not include anything that is
normally distributed (in either source or binary form) with the major
components (compiler, kernel, and so on) of the operating system on
which the executable runs, unless that component itself accompanies
the executable.

It may happen that this requirement contradicts the license
restrictions of other proprietary libraries that do not normally
accompany the operating system.  Such a contradiction means you cannot
use both them and the Library together in an executable that you
distribute.

**7.** You may place library facilities that are a work based on the
Library side-by-side in a single library together with other library
facilities not covered by this License, and distribute such a combined
library, provided that the separate distribution of the work based on
the Library and of the other library facilities is otherwise
permitted, and provided that you do these two things:

* **a)** Accompany the combined library with a copy of the same work
based on the Library, uncombined with any other library
facilities.  This must be distributed under the terms of the
Sections above.
* **b)** Give prominent notice with the combined library of the fact
that part of it is a work based on the Library, and explaining
where to find the accompanying uncombined form of the same work.

**8.** You may not copy, modify, sublicense, link with, or distribute
the Library except as expressly provided under this License.  Any
attempt otherwise to copy, modify, sublicense, link with, or
distribute the Library is void, and will automatically terminate your
rights under this License.  However, parties who have received copies,
or rights, from you under this License will not have their licenses
terminated so long as such parties remain in full compliance.

**9.** You are not required to accept this License, since you have not
signed it.  However, nothing else grants you permission to modify or
distribute the Library or its derivative works.  These actions are
prohibited by law if you do not accept this License.  Therefore, by
modifying or distributing the Library (or any work based on the
Library), you indicate your acceptance of this License to do so, and
all its terms and conditions for copying, distributing or modifying
the Library or works based on it.

**10.** Each time you redistribute the Library (or any work based on the
Library), the recipient automatically receives a license from the
original licensor to copy, distribute, link with or modify the Library
subject to these terms and conditions.  You may not impose any further
restrictions on the recipients' exercise of the rights granted herein.
You are not responsible for enforcing compliance by third parties with
this License.

**11.** If, as a consequence of a court judgment or allegation of patent
infringement or for any other reason (not limited to patent issues),
conditions are imposed on you (whether by court order, agreement or
otherwise) that contradict the conditions of this License, they do not
excuse you from the conditions of this License.  If you cannot
distribute so as to satisfy simultaneously your obligations under this
License and any other pertinent obligations, then as a consequence you
may not distribute the Library at all.  For example, if a patent
license would not permit royalty-free redistribution of the Library by
all those who receive copies directly or indirectly through you, then
the only way you could satisfy both it and this License would be to
refrain entirely from distribution of the Library.

If any portion of this section is held invalid or unenforceable under any
particular circumstance, the balance of the section is intended to apply,
and the section as a whole is intended to apply in other circumstances.

It is not the purpose of this section to induce you to infringe any
patents or other property right claims or to contest validity of any
such claims; this section has the sole purpose of protecting the
integrity of the free software distribution system which is
implemented by public license practices.  Many people have made
generous contributions to the wide range of software distributed
through that system in reliance on consistent application of that
system; it is up to the author/donor to decide if he or she is willing
to distribute software through any other system and a licensee cannot
impose that choice.

This section is intended to make thoroughly clear what is believed to
be a consequence of the rest of this License.

**12.** If the distribution and/or use of the Library is restricted in
certain countries either by patents or by copyrighted interfaces, the
original copyright holder who places the Library under this License may add
an explicit geographical distribution limitation excluding those countries,
so that distribution is permitted only in or among countries not thus
excluded.  In such case, this License incorporates the limitation as if
written in the body of this License.

**13.** The Free Software Foundation may publish revised and/or new
versions of the Lesser General Public License from time to time.
Such new versions will be similar in spirit to the present version,
but may differ in detail to address new problems or concerns.

Each version is given a distinguishing version number.  If the Library
specifies a version number of this License which applies to it and
&ldquo;any later version&rdquo;, you have the option of following the terms and
conditions either of that version or of any later version published by
the Free Software Foundation.  If the Library does not specify a
license version number, you may choose any version ever published by
the Free Software Foundation.

**14.** If you wish to incorporate parts of the Library into other free
programs whose distribution conditions are incompatible with these,
write to the author to ask for permission.  For software which is
copyrighted by the Free Software Foundation, write to the Free
Software Foundation; we sometimes make exceptions for this.  Our
decision will be guided by the two goals of preserving the free status
of all derivatives of our free software and of promoting the sharing
and reuse of software generally.

### NO WARRANTY

**15.** BECAUSE THE LIBRARY IS LICENSED FREE OF CHARGE, THERE IS NO
WARRANTY FOR THE LIBRARY, TO THE EXTENT PERMITTED BY APPLICABLE LAW.
EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR
OTHER PARTIES PROVIDE THE LIBRARY &ldquo;AS IS&rdquo; WITHOUT WARRANTY OF ANY
KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE
LIBRARY IS WITH YOU.  SHOULD THE LIBRARY PROVE DEFECTIVE, YOU ASSUME
THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.

**16.** IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN
WRITING WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MAY MODIFY
AND/OR REDISTRIBUTE THE LIBRARY AS PERMITTED ABOVE, BE LIABLE TO YOU
FOR DAMAGES, INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR
CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OR INABILITY TO USE THE
LIBRARY (INCLUDING BUT NOT LIMITED TO LOSS OF DATA OR DATA BEING
RENDERED INACCURATE OR LOSSES SUSTAINED BY YOU OR THIRD PARTIES OR A
FAILURE OF THE LIBRARY TO OPERATE WITH ANY OTHER SOFTWARE), EVEN IF
SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGES.

END OF TERMS AND CONDITIONS

### How to Apply These Terms to Your New Libraries

If you develop a new library, and you want it to be of the greatest
possible use to the public, we recommend making it free software that
everyone can redistribute and change.  You can do so by permitting
redistribution under these terms (or, alternatively, under the terms of the
ordinary General Public License).

To apply these terms, attach the following notices to the library.  It is
safest to attach them to the start of each source file to most effectively
convey the exclusion of warranty; and each file should have at least the
&ldquo;copyright&rdquo; line and a pointer to where the full notice is found.

    <one line to give the library's name and a brief idea of what it does.>
    Copyright (C) <year>  <name of author>

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

Also add information on how to contact you by electronic and paper mail.

You should also get your employer (if you work as a programmer) or your
school, if any, to sign a &ldquo;copyright disclaimer&rdquo; for the library, if
necessary.  Here is a sample; alter the names:

  Yoyodyne, Inc., hereby disclaims all copyright interest in the
  library `Frob' (a library for tweaking knobs) written by James Random Hacker.

  <signature of Ty Coon>, 1 April 1990
  Ty Coon, President of Vice

That's all there is to it!

[Back to TOC](#table-of-contents)

## GNU Free Documentation License

Version 1.3, 3 November 2008

Copyright &copy; 2000, 2001, 2002, 2007, 2008 Free Software Foundation, Inc. &lt;<http://fsf.org/>&gt;

Everyone is permitted to copy and distribute verbatim copies
of this license document, but changing it is not allowed.

### 0. PREAMBLE

The purpose of this License is to make a manual, textbook, or other
functional and useful document &ldquo;free&rdquo; in the sense of freedom: to
assure everyone the effective freedom to copy and redistribute it,
with or without modifying it, either commercially or noncommercially.
Secondarily, this License preserves for the author and publisher a way
to get credit for their work, while not being considered responsible
for modifications made by others.

This License is a kind of &ldquo;copyleft&rdquo;, which means that derivative
works of the document must themselves be free in the same sense.  It
complements the GNU General Public License, which is a copyleft
license designed for free software.

We have designed this License in order to use it for manuals for free
software, because free software needs free documentation: a free
program should come with manuals providing the same freedoms that the
software does.  But this License is not limited to software manuals;
it can be used for any textual work, regardless of subject matter or
whether it is published as a printed book.  We recommend this License
principally for works whose purpose is instruction or reference.


### 1. APPLICABILITY AND DEFINITIONS

This License applies to any manual or other work, in any medium, that
contains a notice placed by the copyright holder saying it can be
distributed under the terms of this License.  Such a notice grants a
world-wide, royalty-free license, unlimited in duration, to use that
work under the conditions stated herein.  The &ldquo;Document&rdquo;, below,
refers to any such manual or work.  Any member of the public is a
licensee, and is addressed as &ldquo;you&rdquo;.  You accept the license if you
copy, modify or distribute the work in a way requiring permission
under copyright law.

A &ldquo;Modified Version&rdquo; of the Document means any work containing the
Document or a portion of it, either copied verbatim, or with
modifications and/or translated into another language.

A &ldquo;Secondary Section&rdquo; is a named appendix or a front-matter section of
the Document that deals exclusively with the relationship of the
publishers or authors of the Document to the Document's overall
subject (or to related matters) and contains nothing that could fall
directly within that overall subject.  (Thus, if the Document is in
part a textbook of mathematics, a Secondary Section may not explain
any mathematics.)  The relationship could be a matter of historical
connection with the subject or with related matters, or of legal,
commercial, philosophical, ethical or political position regarding
them.

The &ldquo;Invariant Sections&rdquo; are certain Secondary Sections whose titles
are designated, as being those of Invariant Sections, in the notice
that says that the Document is released under this License.  If a
section does not fit the above definition of Secondary then it is not
allowed to be designated as Invariant.  The Document may contain zero
Invariant Sections.  If the Document does not identify any Invariant
Sections then there are none.

The &ldquo;Cover Texts&rdquo; are certain short passages of text that are listed,
as Front-Cover Texts or Back-Cover Texts, in the notice that says that
the Document is released under this License.  A Front-Cover Text may
be at most 5 words, and a Back-Cover Text may be at most 25 words.

A &ldquo;Transparent&rdquo; copy of the Document means a machine-readable copy,
represented in a format whose specification is available to the
general public, that is suitable for revising the document
straightforwardly with generic text editors or (for images composed of
pixels) generic paint programs or (for drawings) some widely available
drawing editor, and that is suitable for input to text formatters or
for automatic translation to a variety of formats suitable for input
to text formatters.  A copy made in an otherwise Transparent file
format whose markup, or absence of markup, has been arranged to thwart
or discourage subsequent modification by readers is not Transparent.
An image format is not Transparent if used for any substantial amount
of text.  A copy that is not &ldquo;Transparent&rdquo; is called &ldquo;Opaque&rdquo;.

Examples of suitable formats for Transparent copies include plain
ASCII without markup, Texinfo input format, LaTeX input format, SGML
or XML using a publicly available DTD, and standard-conforming simple
HTML, PostScript or PDF designed for human modification.  Examples of
transparent image formats include PNG, XCF and JPG.  Opaque formats
include proprietary formats that can be read and edited only by
proprietary word processors, SGML or XML for which the DTD and/or
processing tools are not generally available, and the
machine-generated HTML, PostScript or PDF produced by some word
processors for output purposes only.

The &ldquo;Title Page&rdquo; means, for a printed book, the title page itself,
plus such following pages as are needed to hold, legibly, the material
this License requires to appear in the title page.  For works in
formats which do not have any title page as such, &ldquo;Title Page&rdquo; means
the text near the most prominent appearance of the work's title,
preceding the beginning of the body of the text.

The &ldquo;publisher&rdquo; means any person or entity that distributes copies of
the Document to the public.

A section &ldquo;Entitled XYZ&rdquo; means a named subunit of the Document whose
title either is precisely XYZ or contains XYZ in parentheses following
text that translates XYZ in another language.  (Here XYZ stands for a
specific section name mentioned below, such as &ldquo;Acknowledgements&rdquo;,
&ldquo;Dedications&rdquo;, &ldquo;Endorsements&rdquo;, or &ldquo;History&rdquo;.)  To &ldquo;Preserve the Title&rdquo;
of such a section when you modify the Document means that it remains a
section &ldquo;Entitled XYZ&rdquo; according to this definition.

The Document may include Warranty Disclaimers next to the notice which
states that this License applies to the Document.  These Warranty
Disclaimers are considered to be included by reference in this
License, but only as regards disclaiming warranties: any other
implication that these Warranty Disclaimers may have is void and has
no effect on the meaning of this License.

### 2. VERBATIM COPYING

You may copy and distribute the Document in any medium, either
commercially or noncommercially, provided that this License, the
copyright notices, and the license notice saying this License applies
to the Document are reproduced in all copies, and that you add no
other conditions whatsoever to those of this License.  You may not use
technical measures to obstruct or control the reading or further
copying of the copies you make or distribute.  However, you may accept
compensation in exchange for copies.  If you distribute a large enough
number of copies you must also follow the conditions in section 3.

You may also lend copies, under the same conditions stated above, and
you may publicly display copies.


### 3. COPYING IN QUANTITY

If you publish printed copies (or copies in media that commonly have
printed covers) of the Document, numbering more than 100, and the
Document's license notice requires Cover Texts, you must enclose the
copies in covers that carry, clearly and legibly, all these Cover
Texts: Front-Cover Texts on the front cover, and Back-Cover Texts on
the back cover.  Both covers must also clearly and legibly identify
you as the publisher of these copies.  The front cover must present
the full title with all words of the title equally prominent and
visible.  You may add other material on the covers in addition.
Copying with changes limited to the covers, as long as they preserve
the title of the Document and satisfy these conditions, can be treated
as verbatim copying in other respects.

If the required texts for either cover are too voluminous to fit
legibly, you should put the first ones listed (as many as fit
reasonably) on the actual cover, and continue the rest onto adjacent
pages.

If you publish or distribute Opaque copies of the Document numbering
more than 100, you must either include a machine-readable Transparent
copy along with each Opaque copy, or state in or with each Opaque copy
a computer-network location from which the general network-using
public has access to download using public-standard network protocols
a complete Transparent copy of the Document, free of added material.
If you use the latter option, you must take reasonably prudent steps,
when you begin distribution of Opaque copies in quantity, to ensure
that this Transparent copy will remain thus accessible at the stated
location until at least one year after the last time you distribute an
Opaque copy (directly or through your agents or retailers) of that
edition to the public.

It is requested, but not required, that you contact the authors of the
Document well before redistributing any large number of copies, to
give them a chance to provide you with an updated version of the
Document.


### 4. MODIFICATIONS

You may copy and distribute a Modified Version of the Document under
the conditions of sections 2 and 3 above, provided that you release
the Modified Version under precisely this License, with the Modified
Version filling the role of the Document, thus licensing distribution
and modification of the Modified Version to whoever possesses a copy
of it.  In addition, you must do these things in the Modified Version:

* **A.** Use in the Title Page (and on the covers, if any) a title distinct
from that of the Document, and from those of previous versions
(which should, if there were any, be listed in the History section
of the Document).  You may use the same title as a previous version
if the original publisher of that version gives permission.
* **B.** List on the Title Page, as authors, one or more persons or entities
responsible for authorship of the modifications in the Modified
Version, together with at least five of the principal authors of the
Document (all of its principal authors, if it has fewer than five),
unless they release you from this requirement.
* **C.** State on the Title page the name of the publisher of the
Modified Version, as the publisher.
* **D.** Preserve all the copyright notices of the Document.
* **E.** Add an appropriate copyright notice for your modifications
adjacent to the other copyright notices.
* **F.** Include, immediately after the copyright notices, a license notice
giving the public permission to use the Modified Version under the
terms of this License, in the form shown in the Addendum below.
* **G.** Preserve in that license notice the full lists of Invariant Sections
and required Cover Texts given in the Document's license notice.
* **H.** Include an unaltered copy of this License.
* **I.** Preserve the section Entitled &ldquo;History&rdquo;, Preserve its Title, and add
to it an item stating at least the title, year, new authors, and
publisher of the Modified Version as given on the Title Page.  If
there is no section Entitled &ldquo;History&rdquo; in the Document, create one
stating the title, year, authors, and publisher of the Document as
given on its Title Page, then add an item describing the Modified
Version as stated in the previous sentence.
* **J.** Preserve the network location, if any, given in the Document for
public access to a Transparent copy of the Document, and likewise
the network locations given in the Document for previous versions
it was based on.  These may be placed in the &ldquo;History&rdquo; section.
You may omit a network location for a work that was published at
least four years before the Document itself, or if the original
publisher of the version it refers to gives permission.
* **K.** For any section Entitled &ldquo;Acknowledgements&rdquo; or &ldquo;Dedications&rdquo;,
Preserve the Title of the section, and preserve in the section all
the substance and tone of each of the contributor acknowledgements
and/or dedications given therein.
* **L.** Preserve all the Invariant Sections of the Document,
unaltered in their text and in their titles.  Section numbers
or the equivalent are not considered part of the section titles.
* **M.** Delete any section Entitled &ldquo;Endorsements&rdquo;.  Such a section
may not be included in the Modified Version.
* **N.** Do not retitle any existing section to be Entitled &ldquo;Endorsements&rdquo;
or to conflict in title with any Invariant Section.
* **O.** Preserve any Warranty Disclaimers.

If the Modified Version includes new front-matter sections or
appendices that qualify as Secondary Sections and contain no material
copied from the Document, you may at your option designate some or all
of these sections as invariant.  To do this, add their titles to the
list of Invariant Sections in the Modified Version's license notice.
These titles must be distinct from any other section titles.

You may add a section Entitled &ldquo;Endorsements&rdquo;, provided it contains
nothing but endorsements of your Modified Version by various
parties--for example, statements of peer review or that the text has
been approved by an organization as the authoritative definition of a
standard.

You may add a passage of up to five words as a Front-Cover Text, and a
passage of up to 25 words as a Back-Cover Text, to the end of the list
of Cover Texts in the Modified Version.  Only one passage of
Front-Cover Text and one of Back-Cover Text may be added by (or
through arrangements made by) any one entity.  If the Document already
includes a cover text for the same cover, previously added by you or
by arrangement made by the same entity you are acting on behalf of,
you may not add another; but you may replace the old one, on explicit
permission from the previous publisher that added the old one.

The author(s) and publisher(s) of the Document do not by this License
give permission to use their names for publicity for or to assert or
imply endorsement of any Modified Version.


### 5. COMBINING DOCUMENTS

You may combine the Document with other documents released under this
License, under the terms defined in section 4 above for modified
versions, provided that you include in the combination all of the
Invariant Sections of all of the original documents, unmodified, and
list them all as Invariant Sections of your combined work in its
license notice, and that you preserve all their Warranty Disclaimers.

The combined work need only contain one copy of this License, and
multiple identical Invariant Sections may be replaced with a single
copy.  If there are multiple Invariant Sections with the same name but
different contents, make the title of each such section unique by
adding at the end of it, in parentheses, the name of the original
author or publisher of that section if known, or else a unique number.
Make the same adjustment to the section titles in the list of
Invariant Sections in the license notice of the combined work.

In the combination, you must combine any sections Entitled &ldquo;History&rdquo;
in the various original documents, forming one section Entitled
&ldquo;History&rdquo;; likewise combine any sections Entitled &ldquo;Acknowledgements&rdquo;,
and any sections Entitled &ldquo;Dedications&rdquo;.  You must delete all sections
Entitled &ldquo;Endorsements&rdquo;.


### 6. COLLECTIONS OF DOCUMENTS

You may make a collection consisting of the Document and other
documents released under this License, and replace the individual
copies of this License in the various documents with a single copy
that is included in the collection, provided that you follow the rules
of this License for verbatim copying of each of the documents in all
other respects.

You may extract a single document from such a collection, and
distribute it individually under this License, provided you insert a
copy of this License into the extracted document, and follow this
License in all other respects regarding verbatim copying of that
document.


### 7. AGGREGATION WITH INDEPENDENT WORKS

A compilation of the Document or its derivatives with other separate
and independent documents or works, in or on a volume of a storage or
distribution medium, is called an &ldquo;aggregate&rdquo; if the copyright
resulting from the compilation is not used to limit the legal rights
of the compilation's users beyond what the individual works permit.
When the Document is included in an aggregate, this License does not
apply to the other works in the aggregate which are not themselves
derivative works of the Document.

If the Cover Text requirement of section 3 is applicable to these
copies of the Document, then if the Document is less than one half of
the entire aggregate, the Document's Cover Texts may be placed on
covers that bracket the Document within the aggregate, or the
electronic equivalent of covers if the Document is in electronic form.
Otherwise they must appear on printed covers that bracket the whole
aggregate.


### 8. TRANSLATION

Translation is considered a kind of modification, so you may
distribute translations of the Document under the terms of section 4.
Replacing Invariant Sections with translations requires special
permission from their copyright holders, but you may include
translations of some or all Invariant Sections in addition to the
original versions of these Invariant Sections.  You may include a
translation of this License, and all the license notices in the
Document, and any Warranty Disclaimers, provided that you also include
the original English version of this License and the original versions
of those notices and disclaimers.  In case of a disagreement between
the translation and the original version of this License or a notice
or disclaimer, the original version will prevail.

If a section in the Document is Entitled &ldquo;Acknowledgements&rdquo;,
&ldquo;Dedications&rdquo;, or &ldquo;History&rdquo;, the requirement (section 4) to Preserve
its Title (section 1) will typically require changing the actual
title.


### 9. TERMINATION

You may not copy, modify, sublicense, or distribute the Document
except as expressly provided under this License.  Any attempt
otherwise to copy, modify, sublicense, or distribute it is void, and
will automatically terminate your rights under this License.

However, if you cease all violation of this License, then your license
from a particular copyright holder is reinstated (a) provisionally,
unless and until the copyright holder explicitly and finally
terminates your license, and (b) permanently, if the copyright holder
fails to notify you of the violation by some reasonable means prior to
60 days after the cessation.

Moreover, your license from a particular copyright holder is
reinstated permanently if the copyright holder notifies you of the
violation by some reasonable means, this is the first time you have
received notice of violation of this License (for any work) from that
copyright holder, and you cure the violation prior to 30 days after
your receipt of the notice.

Termination of your rights under this section does not terminate the
licenses of parties who have received copies or rights from you under
this License.  If your rights have been terminated and not permanently
reinstated, receipt of a copy of some or all of the same material does
not give you any rights to use it.


### 10. FUTURE REVISIONS OF THIS LICENSE

The Free Software Foundation may publish new, revised versions of the
GNU Free Documentation License from time to time.  Such new versions
will be similar in spirit to the present version, but may differ in
detail to address new problems or concerns.  See
&lt;<http://www.gnu.org/copyleft/>&gt;.

Each version of the License is given a distinguishing version number.
If the Document specifies that a particular numbered version of this
License &ldquo;or any later version&rdquo; applies to it, you have the option of
following the terms and conditions either of that specified version or
of any later version that has been published (not as a draft) by the
Free Software Foundation.  If the Document does not specify a version
number of this License, you may choose any version ever published (not
as a draft) by the Free Software Foundation.  If the Document
specifies that a proxy can decide which future versions of this
License can be used, that proxy's public statement of acceptance of a
version permanently authorizes you to choose that version for the
Document.

### 11. RELICENSING

&ldquo;Massive Multiauthor Collaboration Site&rdquo; (or &ldquo;MMC Site&rdquo;) means any
World Wide Web server that publishes copyrightable works and also
provides prominent facilities for anybody to edit those works.  A
public wiki that anybody can edit is an example of such a server.  A
&ldquo;Massive Multiauthor Collaboration&rdquo; (or &ldquo;MMC&rdquo;) contained in the site
means any set of copyrightable works thus published on the MMC site.

&ldquo;CC-BY-SA&rdquo; means the Creative Commons Attribution-Share Alike 3.0 
license published by Creative Commons Corporation, a not-for-profit 
corporation with a principal place of business in San Francisco, 
California, as well as future copyleft versions of that license 
published by that same organization.

&ldquo;Incorporate&rdquo; means to publish or republish a Document, in whole or in 
part, as part of another Document.

An MMC is &ldquo;eligible for relicensing&rdquo; if it is licensed under this 
License, and if all works that were first published under this License 
somewhere other than this MMC, and subsequently incorporated in whole or 
in part into the MMC, (1) had no cover texts or invariant sections, and 
(2) were thus incorporated prior to November 1, 2008.

The operator of an MMC Site may republish an MMC contained in the site
under CC-BY-SA on the same site at any time before August 1, 2009,
provided the MMC is eligible for relicensing.


## ADDENDUM: How to use this License for your documents

To use this License in a document you have written, include a copy of
the License in the document and put the following copyright and
license notices just after the title page:

    Copyright (c)  YEAR  YOUR NAME.
    Permission is granted to copy, distribute and/or modify this document
    under the terms of the GNU Free Documentation License, Version 1.3
    or any later version published by the Free Software Foundation;
    with no Invariant Sections, no Front-Cover Texts, and no Back-Cover Texts.
    A copy of the license is included in the section entitled &ldquo;GNU
    Free Documentation License&rdquo;.

If you have Invariant Sections, Front-Cover Texts and Back-Cover Texts,
replace the `with...Texts.` line with this:

    with the Invariant Sections being LIST THEIR TITLES, with the
    Front-Cover Texts being LIST, and with the Back-Cover Texts being LIST.

If you have Invariant Sections without Cover Texts, or some other
combination of the three, merge those two alternatives to suit the
situation.

If your document contains nontrivial examples of program code, we
recommend releasing these examples in parallel under your choice of
free software license, such as the GNU General Public License,
to permit their use in free software.

[Back to TOC](#table-of-contents)

## Thanks

This library has been originally developed under the zencoders flags and this community has always supported me all along this work so I am happy to put the logo on this readme.

              When you see this tree, know that you've came across ZenCoders
    
                                   with open('ZenCoders.                            
                             `num` in numbers   synchronized                        
                         datetime d      glob.     sys.argv[2] .                    
                      def myclass   `..` @@oscla   org.   .  class {                
                   displ  hooks(   public static void   ma    functor:              
                 $myclass->method(  impport sys, os.pipe `   @param name`           
               fcl   if(system(cmd) myc. /de   `  $card( array("a"   srand          
             format  lists:  ++:   conc   ++ "my  an   WHERE  for(   == myi         
           `sys:  myvalue(myvalue) sys.t   Console.W  try{    rais     using        
          connec  SELECT * FROM table mycnf acco desc and or selector::clas  at     
         openldap string  sys.   print "zenc der " { 'a':  `ls -l` >  appe &firs    
        import Tkinter    paste( $obh  &a or it myval  bro roll:  :: [] require a   
       case `` super. +y  <svg x="100">  expr    say " %rooms 1  --account fb- yy   
      proc    meth Animate => send(D, open)    putd    EndIf 10  whi   myc`   cont  
     and    main (--) import loop $$ or  end onload  UNION WITH tab   timer 150 *2  
     end. begin True GtkLabel *label    doto partition te   let auto  i<- (i + d ); 
    .mushup ``/.  ^/zenc/    myclass->her flv   op             <> element >> 71  or 
    QFileDi   :   and  ..    with myc  toA  channel::bo    myc isEmpty a  not  bodt;
    class T  public pol    str    mycalc d   pt &&a     *i fc  add               ^ac
    ::ZenCoders::core::namespac  boost::function st  f = std:   ;;     int    assert
    cout << endl   public genera   #include "b ost   ::ac myna const cast<char*> mys
    ac  size_t   return ran  int (*getNextValue)(void) ff   double sa_family_t famil
    pu        a   do puts("      ac   int main(int argc, char*   "%5d    struct nam
    cs               float       for     typedef    enum  puts            getchar() 
    if(                        else      #define     fp    FILE* f         char* s 
     i++                                 strcat(           %s                  int 
     31]                                 total+=                               do  
      }do                                while(1)                             sle  
      getc                              strcpy( a                            for   
       prin                            scanf(%d, &                          get    
         int                       void myfunc(int pa                     retu      
           BEQ                   BNEQZ R1 10 ANDI R1 R2                  SYS        
            XOR                SYSCALL 5 SLTIU MFLO 15 SW               JAL         
              BNE            BLTZAL R1 1 LUI 001 NOOP MULTU           SLLV          
                MOV R1     ADD R1 R2  JUMP  10 1001 BEQ R1 R2 1      ANDI            
                   1101  1010001100  111 001 01  1010 101100 1001  100              
                     110110 100   0  01 101 01100 100 100 1000100011                
                        11101001001  00   11  100   11  10100010                    
                            000101001001 10  1001   101000101                       
                                 010010010010110101001010

For further information:
visit our website https://zencoders.github.io

**Author:** Sebastiano Merlino

[Back to TOC](#table-of-contents)
