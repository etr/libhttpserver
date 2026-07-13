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
noinst_PROGRAMS = hello_world shared_state service custom_error allowing_disallowing_methods handlers hello_with_get_arg args_processing setting_headers custom_access_log clf_access_log minimal_https minimal_file_response minimal_deferred url_registration minimal_ip_access_control denied_ip_log early_413 per_route_auth benchmark_select benchmark_threads benchmark_nodelay deferred_with_accumulator file_upload file_upload_with_callback empty_response_example iovec_response_example pipe_response_example daemon_info external_event_loop turbo_mode binary_buffer_response

hello_world_SOURCES = hello_world.cpp
shared_state_SOURCES = shared_state.cpp
service_SOURCES = service.cpp
custom_error_SOURCES = custom_error.cpp
allowing_disallowing_methods_SOURCES = allowing_disallowing_methods.cpp
handlers_SOURCES = handlers.cpp
hello_with_get_arg_SOURCES = hello_with_get_arg.cpp
args_processing_SOURCES = args_processing.cpp
setting_headers_SOURCES = setting_headers.cpp
custom_access_log_SOURCES = custom_access_log.cpp
# TASK-050: CLF-format access logger written as a response_sent hook.
# Demonstrates the resolution of issues #281 and #69 -- with the
# structured response_sent_ctx (status / bytes_queued / elapsed), users
# can write a real CLF / time-taken log line in user code, without a
# library change.
clf_access_log_SOURCES = clf_access_log.cpp
minimal_https_SOURCES = minimal_https.cpp
minimal_file_response_SOURCES = minimal_file_response.cpp
minimal_deferred_SOURCES = minimal_deferred.cpp
deferred_with_accumulator_SOURCES = deferred_with_accumulator.cpp
url_registration_SOURCES = url_registration.cpp
minimal_ip_access_control_SOURCES = minimal_ip_access_control.cpp
denied_ip_log_SOURCES = denied_ip_log.cpp
early_413_SOURCES = early_413.cpp
# TASK-052: per-route auth via http_resource::add_hook(before_handler).
# Demonstrates that a hook registered on one resource fires only when
# that resource is dispatched -- a private route can require credentials
# without touching sibling routes (DR-012).
per_route_auth_SOURCES = per_route_auth.cpp
benchmark_select_SOURCES = benchmark_select.cpp
benchmark_threads_SOURCES = benchmark_threads.cpp
benchmark_nodelay_SOURCES = benchmark_nodelay.cpp
file_upload_SOURCES = file_upload.cpp
file_upload_with_callback_SOURCES = file_upload_with_callback.cpp
empty_response_example_SOURCES = empty_response_example.cpp
iovec_response_example_SOURCES = iovec_response_example.cpp
pipe_response_example_SOURCES = pipe_response_example.cpp
daemon_info_SOURCES = daemon_info.cpp
external_event_loop_SOURCES = external_event_loop.cpp
turbo_mode_SOURCES = turbo_mode.cpp
binary_buffer_response_SOURCES = binary_buffer_response.cpp

if HAVE_BAUTH
noinst_PROGRAMS += basic_authentication centralized_authentication
basic_authentication_SOURCES = basic_authentication.cpp
centralized_authentication_SOURCES = centralized_authentication.cpp
endif

if HAVE_GNUTLS
LDADD += -lgnutls
# client_cert_auth demonstrates mutual-TLS / client-certificate access control.
# Its handler consumes only the public http_request cert accessors (which are
# always on the surface), but the example is meaningful only when TLS support
# is compiled in, so it is built on the GnuTLS lane where an HTTPS server can
# actually be started.
noinst_PROGRAMS += minimal_https_psk client_cert_auth
minimal_https_psk_SOURCES = minimal_https_psk.cpp
client_cert_auth_SOURCES = client_cert_auth.cpp
endif

if HAVE_DAUTH
noinst_PROGRAMS += digest_authentication
digest_authentication_SOURCES = digest_authentication.cpp
endif

if HAVE_WEBSOCKET
noinst_PROGRAMS += websocket_echo
websocket_echo_SOURCES = websocket_echo.cpp
websocket_echo_LDADD = $(LDADD) -lmicrohttpd_ws
endif
