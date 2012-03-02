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

INCLUDES = -I../
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver.la
libhttpserver_la_SOURCES = string_utilities.cpp Webserver.cpp HttpUtils.cpp 
noinst_HEADERS = string_utilities.hpp
include_HEADERS = Webserver.hpp HttpUtils.hpp
AM_CXXFLAGS = -fPIC -Wall
libhttpserver_la_LIBADD = -lmicrohttpd
libhttpserver_la_LDFLAGS =
