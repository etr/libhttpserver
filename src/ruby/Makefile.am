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

METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver_ruby.la
noinst_LIBRARIES = libsupport.a
libsupport_a_SOURCES = $(top_srcdir)/src/http_endpoint.cpp $(top_srcdir)/src/string_utilities.cpp
libsupport_a_CPPFLAGS = -I../ -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver
libhttpserver_ruby_la_LIBADD = -lmicrohttpd libsupport.a
libhttpserver_ruby_la_SOURCES = webserver_wrap.cpp $(top_srcdir)/src/webserver.cpp $(top_srcdir)/src/http_utils.cpp $(top_srcdir)/src/http_resource.cpp $(top_srcdir)/src/http_request.cpp $(top_srcdir)/src/http_response.cpp
libhttpserver_ruby_la_CPPFLAGS = -I../ -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver $(RUBYINCLUDES) $(RUBYDEFINES)
libhttpserver_ruby_la_includedir = 
libhttpserver_ruby_la_include_HEADERS =

language=ruby
swigoptions=-minherit -Wall
swigscripts=
@RUBY_AUTOGENERATION@
