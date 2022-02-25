#
#     This file is part of libhttpserver
#     Copyright (C) 2011-2019 Sebastiano Merlino
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

LDADD = $(top_builddir)/src/libhttpserver.la
AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver/
METASOURCES = AUTO
check_PROGRAMS = basic file_upload http_utils threaded nodelay string_utilities http_endpoint ban_system ws_start_stop authentication deferred http_resource

MOSTLYCLEANFILES = *.gcda *.gcno *.gcov

basic_SOURCES = integ/basic.cpp
file_upload_SOURCES = integ/file_upload.cpp
threaded_SOURCES = integ/threaded.cpp
ban_system_SOURCES = integ/ban_system.cpp
ws_start_stop_SOURCES = integ/ws_start_stop.cpp
authentication_SOURCES = integ/authentication.cpp
deferred_SOURCES = integ/deferred.cpp
http_utils_SOURCES = unit/http_utils_test.cpp
string_utilities_SOURCES = unit/string_utilities_test.cpp
http_endpoint_SOURCES = unit/http_endpoint_test.cpp
nodelay_SOURCES = integ/nodelay.cpp
http_resource_SOURCES = unit/http_resource_test.cpp

noinst_HEADERS = littletest.hpp
AM_CXXFLAGS += -lcurl -Wall -fPIC

if COND_GCOV
AM_CFLAGS += -O0 --coverage --no-inline
AM_CXXFLAGS += -O0 --coverage --no-inline
AM_LDFLAGS += -O0 --coverage -lgcov --no-inline
endif

TESTS = $(check_PROGRAMS)

@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES = libhttpserver.supp
EXTRA_DIST = libhttpserver.supp
