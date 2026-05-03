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

if HAVE_GNUTLS
LDADD += -lgnutls
endif

LDADD += -lcurl

AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver/ -DHTTPSERVER_COMPILATION
METASOURCES = AUTO
check_PROGRAMS = basic file_upload http_utils threaded nodelay string_utilities http_endpoint ban_system ws_start_stop authentication deferred http_resource http_response create_webserver new_response_types daemon_info uri_log feature_unavailable header_hygiene_iovec header_hygiene iovec_entry iovec_response http_method constants body http_response_sbo

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
http_response_SOURCES = unit/http_response_test.cpp
create_webserver_SOURCES = unit/create_webserver_test.cpp
new_response_types_SOURCES = integ/new_response_types.cpp
daemon_info_SOURCES = integ/daemon_info.cpp
uri_log_SOURCES = unit/uri_log_test.cpp
# uri_log_test directly references libmicrohttpd via ~modded_request(), so
# it needs an explicit -lmicrohttpd in its link line on top of the default
# LDADD (modern ld enforces --no-copy-dt-needed-entries).
uri_log_LDADD = $(LDADD) -lmicrohttpd
feature_unavailable_SOURCES = unit/feature_unavailable_test.cpp
header_hygiene_iovec_SOURCES = unit/header_hygiene_iovec_test.cpp
# header_hygiene: TASK-007 sentinel TU. Mimics a true consumer:
# - per-target CPPFLAGS overrides AM_CPPFLAGS so HTTPSERVER_COMPILATION
#   and the build-tree -I src/httpserver/ entries are NOT in scope (a
#   real consumer wouldn't have either). Only -I$(top_srcdir)/src is
#   passed so <httpserver.hpp> resolves.
# - LDADD is overridden to empty: this is a pure-compile assertion, the
#   `int main(){}` body has no library dependencies.
# Currently in XFAIL_TESTS (see below); flips to PASS when M5 lands and
# the umbrella is free of backend-header leakage.
header_hygiene_SOURCES = unit/header_hygiene_test.cpp
header_hygiene_CPPFLAGS = -I$(top_srcdir)/src $(CPPFLAGS)
header_hygiene_LDADD =
iovec_entry_SOURCES = unit/iovec_entry_test.cpp
iovec_response_SOURCES = unit/iovec_response_test.cpp
http_method_SOURCES = unit/http_method_test.cpp
constants_SOURCES = unit/constants_test.cpp
# body: TASK-008 unit test for the internal detail::body hierarchy. It
# constructs/destroys MHD_Response objects directly via the libmicrohttpd
# inspection APIs (no daemon), so it needs an explicit -lmicrohttpd link
# the same way uri_log does.
body_SOURCES = unit/body_test.cpp
body_LDADD = $(LDADD) -lmicrohttpd

# http_response_sbo: TASK-009 unit test for the SBO value-type layout of
# http_response. Pokes the private body_/body_storage_/body_inline_/kind_
# members through a friend declaration, exercises the four-case move
# cross-product, and asserts the type-trait acceptance criteria. Needs
# -lmicrohttpd because it transitively touches detail::body subclasses
# whose materialize() returns MHD_Response*.
http_response_sbo_SOURCES = unit/http_response_sbo_test.cpp
http_response_sbo_LDADD = $(LDADD) -lmicrohttpd

noinst_HEADERS = littletest.hpp
AM_CXXFLAGS += -Wall -fPIC -Wno-overloaded-virtual

if COND_GCOV
AM_CFLAGS += -O0 --coverage --no-inline
AM_CXXFLAGS += -O0 --coverage --no-inline
AM_LDFLAGS += -O0 --coverage -lgcov --no-inline
endif

TESTS = $(check_PROGRAMS)

# header_hygiene is expected to fail until M5 (TASK-014/015/019/020) lands and
# <httpserver.hpp> stops transitively pulling in <microhttpd.h>, <pthread.h>,
# <gnutls/gnutls.h>, <sys/socket.h>, and <sys/uio.h>. Automake's XFAIL_TESTS
# mechanism marks the failure as "expected" so the suite stays green, and --
# importantly -- when the umbrella becomes clean and the test starts passing,
# Automake reports XPASS and treats it as a hard error. That XPASS is the
# explicit signal for TASK-020 to remove this line. Do NOT silently delete the
# XFAIL until the umbrella is clean.
XFAIL_TESTS = header_hygiene

@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES = libhttpserver.supp
EXTRA_DIST = libhttpserver.supp
