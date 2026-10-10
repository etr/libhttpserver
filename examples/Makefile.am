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
noinst_PROGRAMS =

noinst_PROGRAMS += v3_external_event_loop
v3_external_event_loop_SOURCES = v3_external_event_loop.cpp v3_host_poll.hpp
v3_external_event_loop_LDADD = $(top_builddir)/src/libhttpserver.la

noinst_PROGRAMS += v3_routes v3_protocol_options v3_websocket_echo
v3_routes_SOURCES = v3_routes.cpp v3_route_handlers.hpp v3_example_host.hpp
v3_protocol_options_SOURCES = v3_protocol_options.cpp
v3_websocket_echo_SOURCES = v3_websocket_echo.cpp v3_example_host.hpp
v3_routes_LDADD = $(top_builddir)/src/libhttpserver.la
v3_protocol_options_LDADD = $(v3_routes_LDADD)
v3_websocket_echo_LDADD = $(v3_routes_LDADD)
v3_routes_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
v3_protocol_options_LDFLAGS = $(v3_routes_LDFLAGS)
v3_websocket_echo_LDFLAGS = $(v3_routes_LDFLAGS)
v3_external_event_loop_LDFLAGS = $(v3_routes_LDFLAGS)
