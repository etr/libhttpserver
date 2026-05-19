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
check_PROGRAMS = basic file_upload http_utils threaded nodelay string_utilities http_endpoint ban_system ws_start_stop authentication deferred http_resource http_response create_webserver create_webserver_explicit new_response_types daemon_info uri_log feature_unavailable header_hygiene_iovec header_hygiene iovec_entry http_method constants body http_response_sbo http_response_factories webserver_pimpl http_request_pimpl create_test_request http_request_arena http_request_const_getters http_request_tls_accessors webserver_register_smartptr webserver_register_path_prefix webserver_on_methods webserver_route route_table lookup_pipeline route_table_concurrency routing_regression threadsafety_stress webserver_features webserver_ws_unavailable webserver_register_ws_smartptr webserver_dauth_unavailable consumer_fixture

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
# create_webserver_explicit: TASK-030 compile-time sentinel. Pins:
#   (1) PRD-NAM-REQ-004: webserver(const create_webserver&) is explicit
#       (negative is_convertible_v + positive is_constructible_v);
#   (2) PRD-NAM-REQ-003: the three error-page setters carry the _handler
#       suffix with std::function<http_response(const http_request&)>
#       (positive SFINAE detectors) and the legacy _resource-suffixed
#       setters are gone (negative SFINAE detectors).
# Pure compile test -- empty LDADD.
create_webserver_explicit_SOURCES = unit/create_webserver_explicit_test.cpp
create_webserver_explicit_LDADD =
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

# webserver_route: TASK-026. Compile-time signature contract for the two
# generic route() overloads (route(http_method, ...) and
# route(method_set, ...)) plus runtime curl tests covering: GET/POST
# dispatch, 405 + Allow on unregistered methods, the headline acceptance
# test (load (method, path) pairs from a vector at runtime and dispatch),
# method_set GET+HEAD serves both, method_set partial overlap with an
# existing handler throws atomically (POST stays unregistered), empty
# method_set throws, http_method::count_ sentinel throws, duplicate /
# cross-overload conflicts with on_* throw, and parameterized paths bind
# through the regex tier. Default LDADD (libhttpserver + curl) is
# sufficient.
webserver_route_SOURCES = unit/webserver_route_test.cpp

# route_table: TASK-027. Unit tests for the bespoke segment-trie
# (detail::radix_tree<route_entry>) and the LRU route cache
# (detail::route_cache). Exercises insert/find/remove on exact,
# parameterized and prefix routes; cache key equality (method-distinct),
# LRU eviction order, hit-promotion, and clear(). Default LDADD
# (libhttpserver + curl) is sufficient.
route_table_SOURCES = unit/route_table_test.cpp

# lookup_pipeline: TASK-027 Cycle F. Drives the public webserver
# registration surface (register_path / register_prefix) and probes the
# impl-private lookup_v2() to pin the tier-order pipeline:
# cache -> exact -> radix -> regex. Uses the webserver_test_access
# friend gated on HTTPSERVER_COMPILATION (already supplied by AM_CPPFLAGS
# in this Makefile). Needs -lmicrohttpd because webserver's PIMPL impl
# carries microhttpd-typed members.
lookup_pipeline_SOURCES = unit/lookup_pipeline_test.cpp
lookup_pipeline_LDADD = $(LDADD) -lmicrohttpd

# route_table_concurrency: TASK-027 Cycle I. Multi-thread stress test.
# 4 writers + 16 readers for ~500ms, registering / unregistering /
# looking up against the v2 3-tier table. Gate for the lock-order
# discipline (table BEFORE cache). TSan is a documented manual rebuild
# gate (see file header) — not added to CI matrix in this task.
# Needs -lmicrohttpd because webserver's PIMPL impl carries
# microhttpd-typed members.
route_table_concurrency_SOURCES = integ/route_table_concurrency.cpp
route_table_concurrency_LDADD = $(LDADD) -lmicrohttpd

# threadsafety_stress: TASK-032. DR-008 multi-thread contract gate.
# Spawns 16 curl clients hitting an on_get handler that re-enters the
# public webserver surface (register_path / unregister_path / block_ip /
# unblock_ip) for HTTPSERVER_STRESS_SECONDS (default 60). The TSan-clean
# rerun is the headline acceptance; the CI matrix `build-type: tsan`
# entry in verify-build.yml picks this binary up via `make check`
# without further wiring. A second sub-test reproduces the documented
# stop()-from-handler deadlock; it is skipped by default and runs only
# when HTTPSERVER_RUN_STOP_FROM_HANDLER=1.
# Default LDADD (libhttpserver + curl) is sufficient — this test
# touches only public webserver APIs and does not need a direct
# microhttpd link.
threadsafety_stress_SOURCES = integ/threadsafety_stress.cpp

# routing_regression: TASK-028. v1 routing-test corpus regression gate.
# Drives the public webserver registration surface (register_path /
# register_prefix / on_get / on_post / unregister_path / unregister_prefix)
# and probes webserver_impl::lookup_v2 to assert that every routing
# pattern in the v1 corpus resolves to the same handler / tier / captures
# the v2 3-tier table is expected to produce after TASK-036 cuts dispatch
# over. Documented as the v2.0 routing regression gate in
# test/REGRESSION.md. Needs -lmicrohttpd because webserver's PIMPL impl
# carries microhttpd-typed members.
routing_regression_SOURCES = unit/routing_regression_test.cpp
routing_regression_LDADD = $(LDADD) -lmicrohttpd

# webserver_features: TASK-034. Pins the public contract of
# webserver::features() (PRD-FLG-REQ-003): nested `struct features` with
# four bool fields in documented order; the static call is noexcept; the
# fields match the HAVE_* state the library was built with. The test TU
# is compiled with the same AM_CXXFLAGS as the library so its #ifdef
# HAVE_* probes are in lockstep with libhttpserver's. Default LDADD is
# sufficient.
webserver_features_SOURCES = unit/webserver_features_test.cpp

# webserver_ws_unavailable: TASK-034 / TASK-035. On HAVE_WEBSOCKET-off
# builds, pins that all three public WebSocket entry points
# (register_ws_resource(unique_ptr), register_ws_resource(shared_ptr),
# unregister_ws_resource) throw feature_unavailable whose what() names
# both "websocket" and "HAVE_WEBSOCKET". On HAVE_WEBSOCKET-on builds the
# suite is empty (the integ-level ws_start_stop test owns the success
# path; the smartptr ownership semantics live in
# webserver_register_ws_smartptr).
webserver_ws_unavailable_SOURCES = unit/webserver_ws_unavailable_test.cpp

# webserver_register_ws_smartptr: TASK-035. Compile-time signature
# assertions that the new unique_ptr/shared_ptr register_ws_resource
# overloads exist, the raw-pointer overload is gone, and
# unregister_ws_resource exists. On HAVE_WEBSOCKET-on builds the
# runtime suite mirrors webserver_register_smartptr (unique_ptr
# ownership transfer, shared_ptr caller-retains-ref, throw-on-null,
# throw-on-duplicate, unregister drops the slot). On HAVE_WEBSOCKET-off
# builds only the compile-time contract is exercised; the
# feature_unavailable throw-site contract lives in
# webserver_ws_unavailable. Default LDADD (libhttpserver + curl) is
# sufficient.
webserver_register_ws_smartptr_SOURCES = unit/webserver_register_ws_smartptr_test.cpp

# webserver_dauth_unavailable: security-reviewer-iter1-1 / TASK-034.
# On HAVE_DAUTH-off builds, pins that webserver(create_webserver(port).digest_auth(true))
# throws feature_unavailable naming "digest_auth" and "HAVE_DAUTH", and that a
# default-constructed webserver does NOT throw (digest_auth_default() returns false
# on HAVE_DAUTH-off). On HAVE_DAUTH-on builds the suite is empty.
webserver_dauth_unavailable_SOURCES = unit/webserver_dauth_unavailable_test.cpp

# consumer_fixture: TASK-034 / AC #2 + TASK-037 CI matrix gate
# (PRD-FLG-REQ-001 / arch §9 testing item 2). Compile-and-link gate
# proving that a single consumer source file referencing every
# previously HAVE_*-gated public symbol compiles unchanged in every
# build configuration (HAVE_BAUTH/DAUTH/GNUTLS/WEBSOCKET on or off).
# main() returns 0; the test is "pass" purely by virtue of the binary
# existing. TASK-037 wires two GitHub Actions matrix entries
# (flag-invariance-on / flag-invariance-off in
# .github/workflows/verify-build.yml) that exercise this fixture in
# both the all-features-on and all-features-off lanes; an intentional
# regression that re-introduces #ifdef HAVE_* around a public method
# breaks the off lane (verified locally during TASK-037 cycle R).
consumer_fixture_SOURCES = consumer_fixture.cpp

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
