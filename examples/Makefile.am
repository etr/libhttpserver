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

noinst_PROGRAMS += external_event_loop
external_event_loop_SOURCES = external_event_loop.cpp host_poll.hpp
external_event_loop_LDADD = $(top_builddir)/src/libhttpserver.la

noinst_PROGRAMS += routes protocol_options websocket_echo http3
http3_SOURCES = http3.cpp host_poll.hpp tls_unavailable.hpp example_ws.hpp
http3_LDADD = $(top_builddir)/src/libhttpserver.la
http3_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
routes_SOURCES = routes.cpp route_handlers.hpp example_host.hpp example_reply.hpp
protocol_options_SOURCES = protocol_options.cpp
websocket_echo_SOURCES = websocket_echo.cpp example_host.hpp example_ws.hpp
routes_LDADD = $(top_builddir)/src/libhttpserver.la
protocol_options_LDADD = $(routes_LDADD)
websocket_echo_LDADD = $(routes_LDADD)
routes_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
protocol_options_LDFLAGS = $(routes_LDFLAGS)
websocket_echo_LDFLAGS = $(routes_LDFLAGS)
external_event_loop_LDFLAGS = $(routes_LDFLAGS)

noinst_PROGRAMS += request_data
request_data_SOURCES = request_data.cpp example_host.hpp example_reply.hpp
request_data_LDFLAGS = $(routes_LDFLAGS)

noinst_PROGRAMS += body_streaming
body_streaming_SOURCES = body_streaming.cpp example_host.hpp example_reply.hpp
body_streaming_LDFLAGS = $(routes_LDFLAGS)

noinst_PROGRAMS += auth
auth_SOURCES = auth.cpp example_host.hpp example_reply.hpp
auth_LDFLAGS = $(routes_LDFLAGS)

noinst_PROGRAMS += forms
forms_SOURCES = forms.cpp example_host.hpp example_reply.hpp
forms_LDFLAGS = $(routes_LDFLAGS)

noinst_PROGRAMS += response_sources
response_sources_SOURCES = response_sources.cpp example_host.hpp example_reply.hpp
response_sources_LDFLAGS = $(routes_LDFLAGS)

noinst_PROGRAMS += operations lifecycle
operations_SOURCES = operations.cpp example_host.hpp example_reply.hpp
lifecycle_SOURCES = lifecycle.cpp example_host.hpp example_reply.hpp
operations_LDFLAGS = $(routes_LDFLAGS)
lifecycle_LDFLAGS = $(routes_LDFLAGS)

noinst_PROGRAMS += tls_profiles http2 tls_rotation
tls_profiles_SOURCES = tls_profiles.cpp example_reply.hpp tls_unavailable.hpp
http2_SOURCES = http2.cpp tls_unavailable.hpp example_ws.hpp
tls_rotation_SOURCES = $(top_srcdir)/test/examples/tls_rotation.cpp host_poll.hpp tls_unavailable.hpp
# Distinct object prefix avoids colliding with the retained test target.
tls_rotation_SHORTNAME = rotation_demo
tls_rotation_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/examples
tls_profiles_LDFLAGS = $(routes_LDFLAGS)
http2_LDFLAGS = $(routes_LDFLAGS)
tls_rotation_LDFLAGS = $(routes_LDFLAGS)

# Historical v2 sources are documentation artifacts, never native build targets.
EXTRA_DIST = allowing_disallowing_methods.cpp \
    args_processing.cpp \
    basic_authentication.cpp \
    benchmark_nodelay.cpp \
    benchmark_select.cpp \
    benchmark_threads.cpp \
    binary_buffer_response.cpp \
    centralized_authentication.cpp \
    clf_access_log.cpp \
    client_cert_auth.cpp \
    custom_access_log.cpp \
    custom_error.cpp \
    daemon_info.cpp \
    deferred_with_accumulator.cpp \
    denied_ip_log.cpp \
    digest_authentication.cpp \
    early_413.cpp \
    empty_response_example.cpp \
    legacy/external_event_loop.cpp \
    file_upload.cpp \
    file_upload_with_callback.cpp \
    handlers.cpp \
    hello_with_get_arg.cpp \
    hello_world.cpp \
    iovec_response_example.cpp \
    minimal_deferred.cpp \
    minimal_file_response.cpp \
    minimal_https.cpp \
    minimal_https_psk.cpp \
    minimal_ip_access_control.cpp \
    per_route_auth.cpp \
    pipe_response_example.cpp \
    service.cpp \
    setting_headers.cpp \
    shared_state.cpp \
    turbo_mode.cpp \
    url_registration.cpp \
    legacy/websocket_echo.cpp
