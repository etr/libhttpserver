#
#     This file is part of libhttpserver
#     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino
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
noinst_PROGRAMS = hello_world service minimal_hello_world custom_error allowing_disallowing_methods handlers hello_with_get_arg setting_headers custom_access_log basic_authentication digest_authentication minimal_https minimal_file_response minimal_deferred url_registration minimal_ip_ban benchmark_select benchmark_threads benchmark_nodelay deferred_with_accumulator file_upload

hello_world_SOURCES = hello_world.cpp
service_SOURCES = service.cpp
minimal_hello_world_SOURCES = minimal_hello_world.cpp
custom_error_SOURCES = custom_error.cpp
allowing_disallowing_methods_SOURCES = allowing_disallowing_methods.cpp
handlers_SOURCES = handlers.cpp
hello_with_get_arg_SOURCES = hello_with_get_arg.cpp
setting_headers_SOURCES = setting_headers.cpp
custom_access_log_SOURCES = custom_access_log.cpp
basic_authentication_SOURCES = basic_authentication.cpp
digest_authentication_SOURCES = digest_authentication.cpp
minimal_https_SOURCES = minimal_https.cpp
minimal_file_response_SOURCES = minimal_file_response.cpp
minimal_deferred_SOURCES = minimal_deferred.cpp
deferred_with_accumulator_SOURCES = deferred_with_accumulator.cpp
url_registration_SOURCES = url_registration.cpp
minimal_ip_ban_SOURCES = minimal_ip_ban.cpp
benchmark_select_SOURCES = benchmark_select.cpp
benchmark_threads_SOURCES = benchmark_threads.cpp
benchmark_nodelay_SOURCES = benchmark_nodelay.cpp
file_upload_SOURCES = file_upload.cpp
