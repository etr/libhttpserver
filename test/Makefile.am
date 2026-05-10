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
check_PROGRAMS = basic file_upload http_utils threaded nodelay string_utilities http_endpoint ban_system ws_start_stop authentication deferred http_resource http_response create_webserver new_response_types daemon_info uri_log feature_unavailable header_hygiene_iovec header_hygiene iovec_entry http_method constants body http_response_sbo http_response_factories webserver_pimpl http_request_pimpl create_test_request http_request_arena http_request_const_getters http_request_tls_accessors webserver_register_smartptr webserver_register_path_prefix webserver_on_methods

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

# http_response_factories: TASK-010 unit test for the static factory
# functions on http_response (string/file/iovec/pipe/empty/deferred/
# unauthorized). Each factory placement-news the corresponding
# detail::body subclass into the SBO buffer, so this TU includes the
# private detail/body.hpp via the build-tree -DHTTPSERVER_COMPILATION
# path. Needs -lmicrohttpd for the same transitive reasons as
# http_response_sbo.
http_response_factories_SOURCES = unit/http_response_factories_test.cpp
http_response_factories_LDADD = $(LDADD) -lmicrohttpd

# webserver_pimpl: TASK-014 sentinel. Compile-time assertions that the
# PIMPL split has happened: <microhttpd.h>/<pthread.h> no longer leak
# through the public webserver.hpp, and webserver is non-copyable and
# non-movable. Pure compile test -- empty LDADD.
webserver_pimpl_SOURCES = unit/webserver_pimpl_test.cpp
webserver_pimpl_LDADD =

# http_request_pimpl: TASK-015 sentinel. Compile-time assertions that the
# http_request PIMPL split has happened: <microhttpd.h>/<gnutls/gnutls.h>
# no longer leak through the public http_request.hpp, http_request is
# move-only, and sizeof(http_request) shrank to a small bag plus one
# impl pointer. Pure compile test -- empty LDADD.
http_request_pimpl_SOURCES = unit/http_request_pimpl_test.cpp
http_request_pimpl_LDADD =

# create_test_request: TASK-015 functional tests for the create_test_request
# builder and the http_request public API on test-constructed requests. Wires
# up headers/footers/cookies/args/querystring/requestor/auth via impl_ local
# maps rather than a live MHD_Connection*.
create_test_request_SOURCES = unit/create_test_request_test.cpp
create_test_request_LDADD = $(LDADD) -lmicrohttpd

# http_request_arena: TASK-016 unit test for the per-connection arena. Asserts
# that connection_state owns a std::pmr::monotonic_buffer_resource arena_,
# that arena_.release() rewinds the bump pointer, and that an
# http_request_impl constructed from the arena does not touch the upstream
# memory resource on the warm path. Exercises detail/http_request_impl.hpp
# directly via the build-tree HTTPSERVER_COMPILATION include path. Needs
# -lmicrohttpd because it transitively touches MHD types through the impl.
http_request_arena_SOURCES = unit/http_request_arena_test.cpp
http_request_arena_LDADD = $(LDADD) -lmicrohttpd

# http_request_const_getters: TASK-018 sentinel. Compile-time assertions
# that the per-key getters (get_header / get_cookie / get_footer /
# get_arg / get_arg_flat) and the always-present getters (get_path /
# get_method / get_version / get_content / get_querystring) are callable
# on `const http_request&` and (for the always-present five) noexcept.
# Locks the public-API contract via static_assert so accidental drift is
# caught at compile time. Pure compile test -- empty LDADD.
http_request_const_getters_SOURCES = unit/http_request_const_getters_test.cpp
http_request_const_getters_LDADD =

# http_request_tls_accessors: TASK-019 sentinel. Compile-time assertions
# that the high-level GnuTLS accessors on http_request exist
# unconditionally (no #ifdef HAVE_GNUTLS gate around the public
# declarations), that the raw `gnutls_session_t get_tls_session()` member
# has been removed from the public surface, that return types match the
# TASK-019 spec (string_view / int64_t / bool), and that the five
# documented-noexcept accessors carry the noexcept specifier. Pure
# compile test -- empty LDADD.
http_request_tls_accessors_SOURCES = unit/http_request_tls_accessors_test.cpp
http_request_tls_accessors_LDADD =

# webserver_register_smartptr: TASK-023. Compile-time signature
# assertions that the new unique_ptr/shared_ptr register_resource
# overloads exist and the raw-pointer overload is gone, plus runtime
# tests for ownership semantics (unique_ptr ownership transfer,
# shared_ptr caller-retains-ref, throw-on-null, throw-on-duplicate).
# Default LDADD (libhttpserver + curl) is sufficient; no microhttpd
# direct link needed.
webserver_register_smartptr_SOURCES = unit/webserver_register_smartptr_test.cpp

# webserver_register_path_prefix: TASK-024. Compile-time signature
# assertions that register_path / register_prefix exist with both
# unique_ptr and shared_ptr overloads, that unregister_path /
# unregister_prefix exist, and that the bool-family register_resource
# overload has been removed (negative SFINAE pin for acceptance criterion
# #1). Runtime tests: register_prefix matches a longer URL; register_path
# does not; parameterized exact paths bind their args; unregister_path /
# unregister_prefix / the umbrella unregister_resource alias all 404 the
# route after removal; the [[deprecated]] register_resource forwarder
# still serves and behaves like register_path.
webserver_register_path_prefix_SOURCES = unit/webserver_register_path_prefix_test.cpp

# webserver_on_methods: TASK-025. Compile-time signature contract for
# the seven on_* lambda registration overloads (on_get / on_post / on_put
# / on_delete / on_patch / on_options / on_head) plus static_asserts on
# the §4.7 detail::route_entry shape (method_set + variant<lambda_handler,
# shared_ptr<http_resource>> + bool is_prefix). Runtime tests over real
# curl round-trips: PRD §3.4 hello-world; per-method 405 + Allow header;
# composition (on_get + on_post on the same path serve both); all seven
# methods dispatch correctly; duplicate (method, path) registration
# throws std::invalid_argument; merge-then-conflict still throws;
# parameterized path binds {id}; class-resource registration on a
# lambda-owned path throws. Default LDADD (libhttpserver + curl) is
# sufficient.
webserver_on_methods_SOURCES = unit/webserver_on_methods_test.cpp

noinst_HEADERS = littletest.hpp
AM_CXXFLAGS += -Wall -fPIC -Wno-overloaded-virtual

if COND_GCOV
AM_CFLAGS += -O0 --coverage --no-inline
AM_CXXFLAGS += -O0 --coverage --no-inline
AM_LDFLAGS += -O0 --coverage -lgcov --no-inline
endif

TESTS = $(check_PROGRAMS)

# TASK-020 has landed: <httpserver.hpp> no longer transitively pulls in any
# forbidden backend header, so header_hygiene is now an unconditional PASS.
# (Earlier milestones tracked the failure via XFAIL_TESTS; that line was
# removed when the umbrella went clean.)

@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES = libhttpserver.supp
EXTRA_DIST = libhttpserver.supp
