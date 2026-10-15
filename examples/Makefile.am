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

noinst_PROGRAMS += v3_routes v3_protocol_options v3_websocket_echo v3_http3
v3_http3_SOURCES = v3_http3.cpp v3_host_poll.hpp v3_tls_unavailable.hpp v3_example_ws.hpp
v3_http3_LDADD = $(top_builddir)/src/libhttpserver.la
v3_http3_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
v3_routes_SOURCES = v3_routes.cpp v3_route_handlers.hpp v3_example_host.hpp v3_example_reply.hpp
v3_protocol_options_SOURCES = v3_protocol_options.cpp
v3_websocket_echo_SOURCES = v3_websocket_echo.cpp v3_example_host.hpp v3_example_ws.hpp
v3_routes_LDADD = $(top_builddir)/src/libhttpserver.la
v3_protocol_options_LDADD = $(v3_routes_LDADD)
v3_websocket_echo_LDADD = $(v3_routes_LDADD)
v3_routes_LDFLAGS = $(V3_TLS_LDFLAGS) $(AM_LDFLAGS)
v3_protocol_options_LDFLAGS = $(v3_routes_LDFLAGS)
v3_websocket_echo_LDFLAGS = $(v3_routes_LDFLAGS)
v3_external_event_loop_LDFLAGS = $(v3_routes_LDFLAGS)

noinst_PROGRAMS += v3_request_data
v3_request_data_SOURCES = v3_request_data.cpp v3_example_host.hpp v3_example_reply.hpp
v3_request_data_LDFLAGS = $(v3_routes_LDFLAGS)

noinst_PROGRAMS += v3_body_streaming
v3_body_streaming_SOURCES = v3_body_streaming.cpp v3_example_host.hpp v3_example_reply.hpp
v3_body_streaming_LDFLAGS = $(v3_routes_LDFLAGS)

noinst_PROGRAMS += v3_auth
v3_auth_SOURCES = v3_auth.cpp v3_example_host.hpp v3_example_reply.hpp
v3_auth_LDFLAGS = $(v3_routes_LDFLAGS)

noinst_PROGRAMS += v3_forms
v3_forms_SOURCES = v3_forms.cpp v3_example_host.hpp v3_example_reply.hpp
v3_forms_LDFLAGS = $(v3_routes_LDFLAGS)

noinst_PROGRAMS += v3_response_sources
v3_response_sources_SOURCES = v3_response_sources.cpp v3_example_host.hpp v3_example_reply.hpp
v3_response_sources_LDFLAGS = $(v3_routes_LDFLAGS)

noinst_PROGRAMS += v3_operations v3_lifecycle
v3_operations_SOURCES = v3_operations.cpp v3_example_host.hpp v3_example_reply.hpp
v3_lifecycle_SOURCES = v3_lifecycle.cpp v3_example_host.hpp v3_example_reply.hpp
v3_operations_LDFLAGS = $(v3_routes_LDFLAGS)
v3_lifecycle_LDFLAGS = $(v3_routes_LDFLAGS)

noinst_PROGRAMS += v3_tls_profiles v3_http2 v3_tls_rotation
v3_tls_profiles_SOURCES = v3_tls_profiles.cpp v3_example_reply.hpp v3_tls_unavailable.hpp
v3_http2_SOURCES = v3_http2.cpp v3_tls_unavailable.hpp v3_example_ws.hpp
v3_tls_rotation_SOURCES = $(top_srcdir)/test/examples/v3_tls_rotation.cpp v3_host_poll.hpp v3_tls_unavailable.hpp
# Distinct object prefix avoids colliding with the retained test target.
v3_tls_rotation_SHORTNAME = rotation_demo
v3_tls_rotation_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/examples
v3_tls_profiles_LDFLAGS = $(v3_routes_LDFLAGS)
v3_http2_LDFLAGS = $(v3_routes_LDFLAGS)
v3_tls_rotation_LDFLAGS = $(v3_routes_LDFLAGS)

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
    external_event_loop.cpp \
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
    websocket_echo.cpp
