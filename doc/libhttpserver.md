The libhttpserver (0.7.1) reference manual
==========================================

Copying
=======
This manual is for libhttpserver, C++ library for creating an
embedded Rest HTTP server (and more).

> Permission is granted to copy, distribute and/or modify this document
> under the terms of the GNU Free Documentation License, Version 1.3
> or any later version published by the Free Software Foundation;
> with no Invariant Sections, no Front-Cover Texts, and no Back-Cover
> Texts.  A copy of the license is included in the section entitled GNU
> Free Documentation License.

Contents
========
* Introduction.
* Compilation.
* Constants.
* Structures and classes type definition.
* Callback functions definition.
* Create and work with server.
* Registering resources.
* Building responses to requests.
* Whitelists and Blacklists.
* Simple comet semantics.
* Utilizing Authentication.
* Obtaining and modifying status information.

Appendices
----------
* GNU-LGPL: The GNU Lesser General Public License says how you can copy and share almost all of libhttpserver.
* GNU-FDL: The GNU Free Documentation License says how you can copy and share the documentation of libhttpserver.

Introduction
============
libhttpserver is meant to constitute an easy system to build HTTP
servers with REST fashion.
libhttpserver is based on libmicrohttpd and, like this, it is a
daemon library.
The mission of this library is to support all possible HTTP features
directly and with a simple semantic allowing then the user to concentrate
only on his application and not on HTTP request handling details.

The library is supposed to work transparently for the client Implementing
the business logic and using the library itself to realize an interface.
If the user wants it must be able to change every behavior of the library
itself through the registration of callbacks.

Like the api is based on (libmicrohttpd), libhttpserver is able to decode
certain body format a and automatically format them in object oriented
fashion. This is true for query arguments and for *POST* and *PUT*
requests bodies if *application/x-www-form-urlencoded* or
*multipart/form-data* header are passed.

The header reproduce all the constants defined by libhttpserver.
These maps various constant used by the HTTP protocol that are exported
as a convenience for users of the library. Is is possible for the user
to define their own extensions of the HTTP standard and use those with
libhttpserver.

All functions are guaranteed to be completely reentrant and
thread-safe (unless differently specified).
Additionally, clients can specify resource limits on the overall
number of connections, number of connections per IP address and memory
used per connection to avoid resource exhaustion.

Compilation
===========
libhttpserver uses the standard system where the usual build process
involves running
> ./bootstrap  
> mkdir build  
> cd build  
> ./configure  
> make  
> make install  

