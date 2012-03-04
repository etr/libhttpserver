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

INCLUDES = -I../ -I$(srcdir)/httpserver/
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver.la
libhttpserver_la_SOURCES = string_utilities.cpp Webserver.cpp HttpUtils.cpp HttpEndpoint.cpp HttpRequest.cpp HttpResponse.cpp HttpResource.cpp
noinst_HEADERS = httpserver/string_utilities.hpp gettext.h
nobase_include_HEADERS = httpserver.h httpserver/Webserver.hpp httpserver/HttpUtils.hpp httpserver/HttpEndpoint.hpp httpserver/HttpRequest.hpp httpserver/HttpResponse.hpp httpserver/HttpResource.hpp
AM_CXXFLAGS = -fPIC -Wall
libhttpserver_la_LIBADD = -lmicrohttpd
libhttpserver_la_LDFLAGS =


