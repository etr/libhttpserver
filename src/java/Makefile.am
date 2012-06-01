#
#     This file is part of libhttpserver
#     Copyright (C) 2011 Sebastiano Merlino
#
#     This library is free software; you can redistribute it and/or
#     modify it under the terms of the GNU Lesser General Public
#     License as published by the Free Software Foundation; either
#     version 2.1 of the License, or (at your option) any later version.
#
#     This library is distributed in the hope that it will be useful,
#     but WITHOUT ANY WARRANTY; without even the implied warranty of
#     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#     Lesser General Public License for more details.
#
#     You should have received a copy of the GNU Lesser General Public
#     License along with this library; if not, write to the Free Software
#     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

INCLUDES = -I../ -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver $(JAVAINCLUDES)
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver_java.la
libhttpserver_java_la_LDFLAGS = $(JAVALDFLAGS)
libhttpserver_java_la_LIBADD = -lmicrohttpd $(JAVALIBS)
libhttpserver_java_la_SOURCES = webserver_wrap.cpp $(top_srcdir)/src/webserver.cpp $(top_srcdir)/src/http_utils.cpp $(top_srcdir)/src/http_endpoint.cpp $(top_srcdir)/src/http_resource.cpp $(top_srcdir)/src/http_request.cpp $(top_srcdir)/src/http_response.cpp $(top_srcdir)/src/string_utilities.cpp
libjavasqi_la_includedir = $(includedir)/amgx/swig/java
libjavasqi_la_include_HEADERS = webserver.java http_request.java http_response.java http_resource.java http_endpoint.java http_utils.java

javadir=$(datadir)/java

language=java
swigoptions=-Wall
swigscripts=webserver.java http_request.java http_response.java http_resource.java http_endpoint.java http_utils.java
@JAVA_AUTOGENERATION@
