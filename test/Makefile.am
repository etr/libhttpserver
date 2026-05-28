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
check_PROGRAMS = basic file_upload http_utils threaded nodelay string_utilities http_endpoint ban_system ws_start_stop authentication deferred http_resource http_response create_webserver create_webserver_explicit new_response_types daemon_info uri_log feature_unavailable header_hygiene_iovec header_hygiene iovec_entry http_method constants body http_response_sbo http_response_factories http_response_move_sanitizer webserver_pimpl http_request_pimpl create_test_request http_request_arena http_request_const_getters http_request_tls_accessors webserver_register_smartptr webserver_register_path_prefix webserver_on_methods webserver_route route_table lookup_pipeline route_table_concurrency routing_regression threadsafety_stress webserver_features webserver_ws_unavailable webserver_register_ws_smartptr webserver_dauth_unavailable consumer_fixture header_hygiene_hooks hook_api_shape hooks_no_firing hooks_accept_ctx_shape hooks_connection_lifecycle hooks_accept_decision_banned hooks_accept_decision_throwing hooks_body_chunk_ctx_shape hooks_request_received_short_circuit hooks_body_chunk_observes_progress hooks_body_chunk_short_circuit_no_leak hooks_before_handler_ctx_shape hooks_route_resolved_miss_and_hit hooks_before_handler_short_circuit hooks_alias_count hooks_alias_functional hooks_handler_exception_chain hooks_handler_exception_user_handler_throws_continues_chain hooks_handler_exception_fallback_to_hardcoded_500 hooks_handler_exception_slot hooks_response_sent_ctx_shape hooks_request_completed_ctx_shape hooks_after_handler_replaces_response hooks_after_handler_mutates_response_in_place hooks_response_sent_carries_status_bytes_timing hooks_request_completed_fires_on_early_failure hooks_log_access_alias_slot hooks_per_route_invalid_phase_throws hooks_per_route_order hooks_per_route_early_413_per_endpoint hooks_per_route_resource_destroyed_first hooks_per_route_concurrent_registration

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

# http_response_move_sanitizer: TASK-038 unit test that pins
# sanitizer-clean (ASan + UBSan) http_response move semantics. Covers the
# four move-assign cases and both move-ctor cases via factory-built
# responses, drives the heap-fallback branch with a synthetic >64 B body
# subclass (fat_body) reached through the same friend hook the SBO test
# uses, and pins the moved-from invariant contract (destructible,
# accessor-safe, re-assignable). Picked up by the asan / ubsan matrix
# entries in .github/workflows/verify-build.yml via $(check_PROGRAMS).
# Needs -lmicrohttpd because the TU subclasses detail::body whose
# materialize() returns MHD_Response*.
http_response_move_sanitizer_SOURCES = unit/http_response_move_sanitizer_test.cpp
http_response_move_sanitizer_LDADD = $(LDADD) -lmicrohttpd

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

# TASK-045 -- Hook bus skeleton (hook_phase, hook_action, hook_handle,
# hook_context, webserver::add_hook). Three tests:
#
# - header_hygiene_hooks: per-header preprocessor sentinel that the
#   four new hook public headers do not transitively pull <microhttpd.h>,
#   <gnutls/gnutls.h>, <sys/socket.h>, or <sys/uio.h>. Mirrors
#   header_hygiene_iovec_test.cpp.
# - hook_api_shape: compile-time + runtime contract for hook_phase /
#   hook_action / hook_handle / webserver::add_hook. Reaches into
#   webserver_impl via the HTTPSERVER_COMPILATION-gated friend bridge,
#   so it needs -lmicrohttpd transitively (default LDADD covers it).
# - hooks_no_firing: integration sentinel registering one hook on every
#   phase and asserting zero invocations across a real HTTP round-trip.
header_hygiene_hooks_SOURCES = unit/header_hygiene_hooks_test.cpp
hook_api_shape_SOURCES = unit/hook_api_shape_test.cpp
hooks_no_firing_SOURCES = integ/hooks_no_firing.cpp

# TASK-046 -- Lifecycle hook bus firing sites (connection_opened,
# accept_decision, connection_closed). Adds:
#
# - hooks_accept_ctx_shape: compile-time pin for accept_ctx's extended
#   {peer, accepted, reason} shape (TASK-045 landed peer only).
# - hooks_connection_lifecycle: integ test driving one curl round-trip
#   and asserting all three lifecycle hooks fired with a valid peer.
# - hooks_accept_decision_banned: blocks 127.0.0.1, asserts the
#   accept_decision hook observes accepted=false reason="banned".
# - hooks_accept_decision_throwing: a throwing accept_decision hook
#   must not flip the accept/reject decision.
hooks_accept_ctx_shape_SOURCES = unit/hooks_accept_ctx_shape_test.cpp
hooks_connection_lifecycle_SOURCES = integ/hooks_connection_lifecycle.cpp
hooks_accept_decision_banned_SOURCES = integ/hooks_accept_decision_banned.cpp
hooks_accept_decision_throwing_SOURCES = integ/hooks_accept_decision_throwing.cpp

# TASK-047 -- Pre-handler short-circuit hooks (request_received, body_chunk).
# Adds:
# - hooks_body_chunk_ctx_shape: compile-time pin for the request_received_ctx /
#   body_chunk_ctx shapes (mutable request*, chunk span, offset, is_final).
# - hooks_request_received_short_circuit: hook returns 413 on Content-Length > 1 MB,
#   downstream body_chunk hook must NOT fire (closes #273).
# - hooks_body_chunk_observes_progress: hook accumulates bytes; total equals body size.
# - hooks_body_chunk_short_circuit_no_leak: ASan-verified that short-circuit drops
#   the in-flight MHD_PostProcessor without leaking its buffer.
hooks_body_chunk_ctx_shape_SOURCES = unit/hooks_body_chunk_ctx_shape_test.cpp
hooks_request_received_short_circuit_SOURCES = integ/hooks_request_received_short_circuit.cpp
hooks_body_chunk_observes_progress_SOURCES = integ/hooks_body_chunk_observes_progress.cpp
hooks_body_chunk_short_circuit_no_leak_SOURCES = integ/hooks_body_chunk_short_circuit_no_leak.cpp

# TASK-048 -- route_resolved + before_handler firing sites and the three
# v1-derived single-slot setters (auth_handler, not_found_handler,
# method_not_allowed_handler) re-described as hook-bus aliases. Adds:
# - hooks_before_handler_ctx_shape: compile-time pin for
#   before_handler_ctx (method + resource fields) and route_resolved_ctx.
# - hooks_route_resolved_miss_and_hit: two route_resolved hooks
#   observe both hit (matched.has_value()) and miss (matched==nullopt).
# - hooks_before_handler_short_circuit: before_handler hook returning
#   respond_with(403) skips both is_allowed check and resource handler.
# - hooks_alias_count: registering auth_handler(fn) installs +1 hook on
#   before_handler; not_found_handler installs +1 on route_resolved.
hooks_before_handler_ctx_shape_SOURCES = unit/hooks_before_handler_ctx_shape_test.cpp
hooks_route_resolved_miss_and_hit_SOURCES = integ/hooks_route_resolved_miss_and_hit.cpp
hooks_before_handler_short_circuit_SOURCES = integ/hooks_before_handler_short_circuit.cpp
hooks_alias_count_SOURCES = unit/hooks_alias_count_test.cpp
# hooks_alias_functional: TASK-048 finding fix. Pins that the
# method_not_allowed_handler alias is a functional before_handler hook
# that short-circuits the chain (not a no-op stub): a user hook registered
# after the alias does NOT fire for method-not-allowed requests.
hooks_alias_functional_SOURCES = integ/hooks_alias_functional_test.cpp

# TASK-049 -- handler_exception firing site and internal_error_handler
# alias slot. Adds:
# - hooks_handler_exception_chain: A=pass, B=respond_with(418); alias C is
#   never invoked because B wins.
# - hooks_handler_exception_user_handler_throws_continues_chain: A throws;
#   the chain continues to B; B's 418 is queued.
# - hooks_handler_exception_fallback_to_hardcoded_500: A=pass, B=throws,
#   alias C=throws -> dispatcher emits empty-body 500; alias C is called
#   exactly once (no re-entry).
# - hooks_handler_exception_slot: unit pin that internal_error_handler
#   wires the dedicated last-position slot (not a vector entry), distinct
#   from the TASK-048 aliases.
hooks_handler_exception_chain_SOURCES = integ/hooks_handler_exception_chain.cpp
hooks_handler_exception_user_handler_throws_continues_chain_SOURCES = integ/hooks_handler_exception_user_handler_throws_continues_chain.cpp
hooks_handler_exception_fallback_to_hardcoded_500_SOURCES = integ/hooks_handler_exception_fallback_to_hardcoded_500.cpp
hooks_handler_exception_slot_SOURCES = unit/hooks_handler_exception_slot_test.cpp

# TASK-050 -- after_handler / response_sent / request_completed firing
# sites + log_access alias. Adds:
# - hooks_response_sent_ctx_shape: compile-time pin for the extended
#   response_sent_ctx fields (status, bytes_queued, elapsed).
# - hooks_request_completed_ctx_shape: compile-time pin for the
#   extended request_completed_ctx (resp pointer + succeeded flag).
# - hooks_after_handler_replaces_response: after_handler returning
#   respond_with(r2) replaces mr->response before MHD_queue_response.
# - hooks_after_handler_mutates_response_in_place: after_handler
#   returning pass() after ctx.response->with_header(...) puts the
#   header on the wire.
# - hooks_response_sent_carries_status_bytes_timing: response_sent
#   hook observes status==200, bytes_queued==body.size(), elapsed>0.
# - hooks_request_completed_fires_on_early_failure: a request_received
#   hook short-circuits to a 413; request_completed still fires with
#   succeeded==true and a non-null resp pointing at the 413.
# - hooks_log_access_alias_slot: unit pin that log_access(fn) wires the
#   dedicated alias slot on webserver_impl (same pattern as the TASK-049
#   internal_error_handler alias slot), and that no entry is pushed
#   into hooks_response_sent_.
hooks_response_sent_ctx_shape_SOURCES = unit/hooks_response_sent_ctx_shape_test.cpp
hooks_request_completed_ctx_shape_SOURCES = unit/hooks_request_completed_ctx_shape_test.cpp
hooks_after_handler_replaces_response_SOURCES = integ/hooks_after_handler_replaces_response.cpp
hooks_after_handler_mutates_response_in_place_SOURCES = integ/hooks_after_handler_mutates_response_in_place.cpp
hooks_response_sent_carries_status_bytes_timing_SOURCES = integ/hooks_response_sent_carries_status_bytes_timing.cpp
hooks_request_completed_fires_on_early_failure_SOURCES = integ/hooks_request_completed_fires_on_early_failure.cpp
hooks_log_access_alias_slot_SOURCES = unit/hooks_log_access_alias_slot_test.cpp
hooks_log_access_alias_slot_LDADD = $(LDADD) -lmicrohttpd

# TASK-051 -- Per-route hooks on http_resource (add_hook overloads). Adds:
# - hooks_per_route_invalid_phase_throws: validation gate -- the five
#   pre-route-resolution phases must throw std::invalid_argument, the
#   five post-route-resolution phases must not throw.
# - hooks_per_route_order: server-wide response_sent hook A fires
#   before per-route hook B on the matched resource; on a different
#   resource only A fires.
# - hooks_per_route_early_413_per_endpoint: two endpoints with
#   different per-route before_handler size caps demonstrate per-route
#   policy isolation.
# - hooks_per_route_resource_destroyed_first: ASan-relevant. A
#   hook_handle whose resource was destroyed must remove() as a no-op.
# - hooks_per_route_concurrent_registration: TSan stress -- registers
#   on resource R from inside a handler on resource Q, exercising the
#   documented route_table_mutex_ -> resource hook_table_mutex_ lock
#   order.
hooks_per_route_invalid_phase_throws_SOURCES = integ/hooks_per_route_invalid_phase_throws.cpp
hooks_per_route_order_SOURCES = integ/hooks_per_route_order.cpp
hooks_per_route_early_413_per_endpoint_SOURCES = integ/hooks_per_route_early_413_per_endpoint.cpp
hooks_per_route_resource_destroyed_first_SOURCES = integ/hooks_per_route_resource_destroyed_first.cpp
hooks_per_route_concurrent_registration_SOURCES = integ/hooks_per_route_concurrent_registration.cpp

noinst_HEADERS = littletest.hpp integ/test_utils.hpp
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
EXTRA_DIST = libhttpserver.supp \
             tsan.supp \
             PERFORMANCE.md \
             bench_harness.hpp \
             v1_baseline/README.md \
             v1_baseline/v1_constants.hpp \
             v1_baseline/measure_v1_sizes.cpp \
             v1_baseline/measure_v1_get_headers.cpp

# TASK-039 -- Performance acceptance gates.
#
# Bench binaries verify the two PRD §3.6 numeric acceptance criteria:
#   * bench_get_headers              -> PRD-REQ-REQ-001 (>=10x speedup on get_headers)
#   * bench_sizeof_http_resource     -> PRD-REQ-REQ-003 (sizeof(http_resource) shrink)
#
# They live in EXTRA_PROGRAMS (not check_PROGRAMS), so:
#   - `make all`   does NOT build them.
#   - `make check` does NOT run them.
#   - `make bench` (the rule below) builds and runs both explicitly.
#
# Rationale (per TASK-039 plan §2 D4 / D6):
#   - Sanitizer runs of `make check` would distort the ratio; bench
#     belongs out of band on a quiet release-mode host.
#   - The bench is noise-sensitive and should not gate per-commit CI.
#     Release-readiness is gated on `make bench` succeeding once on
#     a controlled host (TASK-040+ runbook).
#
# How to add a new bench:
#   1. Append the program name to bench_targets below.
#   2. Add a `<name>_SOURCES = ...` line.
#   3. Add a `<name>_LDADD = ...` line if it needs more than the
#      default empty LDADD.
#   4. Run `make bench` from the build directory.
bench_targets = bench_sizeof_http_resource bench_get_headers bench_hook_overhead
EXTRA_PROGRAMS = $(bench_targets)

# bench_sizeof_http_resource: pure compile-time static_assert guard.
# Note: this binary's only meaningful check is compilation; the runtime
# exit code carries no signal (main() returns 0 unconditionally). It is
# listed under bench_targets rather than check_PROGRAMS because the
# static_assert algebra references v1 baseline constants (from
# v1_constants.hpp) that assume a release-mode ABI, making it a release-
# validation step rather than a per-commit regression test.
# Empty LDADD: no symbols from libhttpserver are referenced at run time.
bench_sizeof_http_resource_SOURCES = bench_sizeof_http_resource.cpp
bench_sizeof_http_resource_LDADD =

# bench_get_headers: warms the get_headers() cache on an
# http_request built via create_test_request, then times 10x1M
# iterations. Compares the median ns/call to the v1 baseline.
# Needs -lmicrohttpd because create_test_request transitively
# constructs an http_request whose impl includes <microhttpd.h>.
bench_get_headers_SOURCES = bench_get_headers.cpp bench_harness.hpp
bench_get_headers_LDADD = $(LDADD) -lmicrohttpd

# bench_hook_overhead (TASK-052): asserts the dispatch hot path's
# per-phase any_hooks_ atomic-load gate stays within a 50ns ceiling
# when no hooks are registered. The gate cost is what every request
# pays per phase regardless of hook registration, and is the load-
# bearing measurement for PRD-HOOK-REQ-009 / §4.10's "zero-cost-when-
# unused" claim. Defines HTTPSERVER_COMPILATION so the bench can
# reach webserver_test_access; this is the same friend pattern used
# by test/unit/hook_api_shape_test.cpp.
bench_hook_overhead_SOURCES = bench_hook_overhead.cpp
bench_hook_overhead_LDADD = $(LDADD) -lmicrohttpd

bench: $(bench_targets)
	@for p in $(bench_targets); do \
	    echo "=== Running bench: $$p ==="; \
	    ./$$p || exit $$?; \
	done

.PHONY: bench
