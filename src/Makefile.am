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

AM_CPPFLAGS = -I../ -I$(srcdir)/httpserver/ -DHTTPSERVER_COMPILATION
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver.la
# TASK-034 / PRD-FLG-REQ-001: websocket_handler.cpp is built and
# websocket_handler.hpp is installed unconditionally so the public
# surface is identical across HAVE_WEBSOCKET-on and HAVE_WEBSOCKET-off
# builds. The WS-off branch in websocket_handler.cpp provides stub
# definitions (every member throws feature_unavailable except is_valid()
# which returns false).
libhttpserver_la_SOURCES = string_utilities.cpp webserver.cpp webserver_add_hook.cpp http_utils.cpp file_info.cpp http_request.cpp http_request_auth.cpp http_response.cpp http_response_factories.cpp http_resource.cpp create_webserver.cpp create_test_request.cpp websocket_handler.cpp hook_handle.cpp peer_address.cpp resource_hook_table.cpp cookie.cpp detail/http_endpoint.cpp detail/body.cpp detail/ip_representation.cpp detail/http_request_impl.cpp detail/http_request_impl_args.cpp detail/http_request_impl_tls.cpp detail/webserver_lifecycle.cpp detail/webserver_register.cpp detail/webserver_routes.cpp detail/webserver_routes_upsert.cpp detail/webserver_callbacks.cpp detail/webserver_callbacks_lifecycle.cpp detail/webserver_websocket.cpp detail/webserver_dispatch.cpp detail/webserver_request.cpp detail/webserver_response_queue.cpp detail/webserver_body_pipeline.cpp detail/webserver_error_pages.cpp detail/webserver_aliases.cpp detail/webserver_hook_firing.cpp detail/hook_phase_dispatch.cpp
# noinst_HEADERS: shipped in the tarball but NEVER installed under $prefix/include.
# Detail headers (httpserver/detail/*.hpp) live here so they cannot leak to
# downstream consumers — the public surface comes in through <httpserver.hpp>.
noinst_HEADERS = httpserver/string_utilities.hpp httpserver/detail/modded_request.hpp httpserver/detail/http_endpoint.hpp httpserver/detail/body.hpp httpserver/detail/webserver_impl.hpp httpserver/detail/webserver_impl_dispatch.hpp httpserver/detail/connection_state.hpp httpserver/detail/secure_zero.hpp httpserver/detail/http_request_impl.hpp httpserver/detail/resource_hook_table.hpp httpserver/detail/route_entry.hpp httpserver/detail/lambda_resource.hpp httpserver/detail/segment_trie.hpp httpserver/detail/route_cache.hpp httpserver/detail/route_tier.hpp httpserver/detail/unescape_helpers.hpp gettext.h
nobase_include_HEADERS = httpserver.hpp httpserver/body_kind.hpp httpserver/cookie.hpp httpserver/constants.hpp httpserver/create_webserver.hpp httpserver/create_webserver_setters.hpp httpserver/create_test_request.hpp httpserver/webserver.hpp httpserver/webserver_routes.hpp httpserver/webserver_runtime.hpp httpserver/webserver_websocket.hpp httpserver/webserver_hooks.hpp httpserver/websocket_handler.hpp httpserver/http_utils.hpp httpserver/http_utils_helpers.hpp httpserver/ip_representation.hpp httpserver/file_info.hpp httpserver/http_request.hpp httpserver/http_request_auth.hpp httpserver/http_request_getters.hpp httpserver/http_response.hpp httpserver/http_response_factories.hpp httpserver/http_resource.hpp httpserver/feature_unavailable.hpp httpserver/iovec_entry.hpp httpserver/http_arg_value.hpp httpserver/http_method.hpp httpserver/hook_phase.hpp httpserver/hook_action.hpp httpserver/hook_handle.hpp httpserver/hook_context.hpp

AM_CXXFLAGS += -fPIC -Wall

if COND_GCOV
AM_CFLAGS += -O0 --coverage --no-inline
AM_CXXFLAGS += -O0 --coverage --no-inline
AM_LDFLAGS += -O0 --coverage -lgcov --no-inline
endif

if !COND_CROSS_COMPILE
libhttpserver_la_LIBADD = -lmicrohttpd
if HAVE_WEBSOCKET
libhttpserver_la_LIBADD += -lmicrohttpd_ws
endif
endif

libhttpserver_la_CFLAGS = $(AM_CFLAGS)
libhttpserver_la_CXXFLAGS = $(AM_CXXFLAGS)
# -version-number is scoped to the shared library link step only (not global
# LDFLAGS, which would affect executables and test runners unnecessarily).
libhttpserver_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined -version-number @LHT_LDF_VERSION@

install-data-hook:
	@# Install an `<httpserverpp>` alias next to `<httpserver.hpp>`. On POSIX
	@# this is a symlink. On MSYS2/mingw `ln -s` typically falls back to a
	@# file copy (it produces a regular file, not a link); when even that
	@# fails (e.g. tightened sandboxes, or `ln` missing the `-s` flag in
	@# pristine mingw shells) we fall back to an explicit `cp`. The
	@# downstream `check-install-layout` rule accepts either form.
	mkdir -p $(DESTDIR)$(includedir)
	cd $(DESTDIR)$(includedir) && \
	    rm -f httpserverpp && \
	    { $(LN_S) httpserver.hpp httpserverpp || cp httpserver.hpp httpserverpp; }

uninstall-hook:
	(cd $(DESTDIR)$(includedir) && rm -f httpserverpp)
